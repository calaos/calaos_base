#!/usr/bin/env python3

from calaos_extern_proc import cDebugDom, cInfoDom, cErrorDom, cCriticalDom, cWarningDom, configure_logger, ExternProcClient
from reolink_aio.api import Host
import asyncio
import argparse
import json
import threading
from datetime import datetime
import time

import logging

class ReolinkClient(ExternProcClient):
    def __init__(self):
        super().__init__()
        self.registered_cameras = {}
        self.camera_configs = {}  # Store camera connection parameters
        self.connection_status = {}  # Track connection status
        self.last_reconnect_attempt = {}  # Track last reconnection attempt
        self.reconnect_tasks = {}  # Track reconnection tasks
        self.event_loop = None
        self.event_loop_thread = None

    def stop(self):
        # Cancel all reconnection tasks
        for task in self.reconnect_tasks.values():
            if task and not task.done():
                task.cancel()
        
        if self.event_loop:
            # Stop the event loop
            self.event_loop.call_soon_threadsafe(self.event_loop.stop)

            # Wait for the thread to finish
            if self.event_loop_thread and self.event_loop_thread.is_alive():
                self.event_loop_thread.join(timeout=5.0)

        return super().stop()

    def parse_arguments(self):
        parser = argparse.ArgumentParser(description='Calaos Reolink Extension')

        # Define all possible arguments
        parser.add_argument('--socket', help='Socket path')
        parser.add_argument('--namespace', help='Namespace')

        args = parser.parse_args()

        # Check that socket and namespace are used together
        if not args.socket or not args.namespace:
            parser.error('Must specify both --socket and --namespace')

        if bool(args.socket) != bool(args.namespace):
            parser.error('--socket and --namespace must be used together')

        self.sockpath = args.socket
        self.name = args.namespace

    def setup(self):
        self.parse_arguments()

        if not self.connect_socket():
            cCriticalDom("reolink")("Failed to connect to socket")
            return False

        # Set up asyncio event loop for handling camera connections
        try:
            self.event_loop = asyncio.new_event_loop()

            # Start the event loop in a separate thread
            def run_event_loop():
                asyncio.set_event_loop(self.event_loop)
                cDebugDom("reolink")("Starting event loop thread")
                self.event_loop.run_forever()
                cDebugDom("reolink")("Event loop thread stopped")

            self.event_loop_thread = threading.Thread(target=run_event_loop, daemon=True)
            self.event_loop_thread.start()

        except Exception as e:
            cErrorDom("reolink")("Failed to create event loop: %s", str(e))
            return False

        # Send status connected message
        status_msg = {
            "status": "connected",
            "message": "Reolink client ready"
        }
        self.send_message(json.dumps(status_msg))

        # Start periodic event checking as fallback
        if self.event_loop:
            asyncio.run_coroutine_threadsafe(self.check_events_periodically(), self.event_loop)
            # Start connection monitoring
            asyncio.run_coroutine_threadsafe(self.monitor_connections(), self.event_loop)

        return True

    def run(self, timeout_ms):
        return super().run(timeout_ms)

    async def monitor_connections(self):
        """Monitor camera connections and trigger reconnections when needed"""
        cDebugDom("reolink")("Starting connection monitoring")
        
        while True:
            try:
                await asyncio.sleep(10)  # Check every 10 seconds
                
                for camera_key, host in list(self.registered_cameras.items()):
                    hostname = camera_key.split("_", 1)[0]
                    
                    # Check if Baichuan TCP push is active
                    if not host.baichuan.events_active:
                        current_time = time.time()
                        
                        # Mark as disconnected if not already
                        if self.connection_status.get(hostname, True):
                            cWarningDom("reolink")(f"Camera {hostname} TCP push inactive, marking as disconnected")
                            self.connection_status[hostname] = False
                        
                        # Try to reconnect if enough time has passed since last attempt
                        last_attempt = self.last_reconnect_attempt.get(hostname, 0)
                        if current_time - last_attempt > 30:  # Wait 30 seconds between reconnection attempts
                            if hostname not in self.reconnect_tasks or self.reconnect_tasks[hostname].done():
                                cInfoDom("reolink")(f"Starting reconnection task for camera {hostname}")
                                self.reconnect_tasks[hostname] = asyncio.create_task(
                                    self.reconnect_camera(hostname)
                                )
                    else:
                        # Mark as connected if it was previously disconnected
                        if not self.connection_status.get(hostname, True):
                            cInfoDom("reolink")(f"Camera {hostname} TCP push active again, marking as connected")
                            self.connection_status[hostname] = True
                            
            except Exception as e:
                cErrorDom("reolink")(f"Error in connection monitoring: {str(e)}")
                await asyncio.sleep(10)

    async def reconnect_camera(self, hostname):
        """Attempt to reconnect to a specific camera"""
        try:
            self.last_reconnect_attempt[hostname] = time.time()
            cInfoDom("reolink")(f"Attempting to reconnect to camera {hostname}")
            
            # Get camera configuration
            config = self.camera_configs.get(hostname)
            if not config:
                cErrorDom("reolink")(f"No configuration found for camera {hostname}")
                return
            
            # Remove old camera instance
            old_keys = [key for key in self.registered_cameras.keys() if key.startswith(hostname)]
            for key in old_keys:
                old_host = self.registered_cameras.pop(key, None)
                if old_host:
                    try:
                        # Unsubscribe from events
                        await old_host.baichuan.unsubscribe_events()
                        cDebugDom("reolink")(f"Unsubscribed from old events for {hostname}")
                    except Exception as e:
                        cDebugDom("reolink")(f"Error unsubscribing from old events for {hostname}: {str(e)}")
            
            # Wait a bit before reconnecting
            await asyncio.sleep(5)
            
            # Try to reconnect
            await self.connect_camera(
                config['hostname'],
                config['username'], 
                config['password'],
                config['event_type']
            )
            
            cInfoDom("reolink")(f"Successfully reconnected to camera {hostname}")
            
        except Exception as e:
            cErrorDom("reolink")(f"Failed to reconnect to camera {hostname}: {str(e)}")
            # Schedule another attempt later
            await asyncio.sleep(60)  # Wait 1 minute before next attempt

    def create_event_callback(self, host, hostname, channels):
        """Create an event callback function that checks all supported event types"""
        def event_callback():
            cDebugDom("reolink")(f"Baichuan event callback triggered for {hostname}")

            # Check all channels for events
            for channel in channels:
                detected_events = []

                try:
                    # Check for motion detection
                    if host.motion_detected(channel):
                        detected_events.append("motion")

                    # Check for AI detections
                    ai_types = ["face", "person", "vehicle", "pet", "package", "cry"]
                    for ai_type in ai_types:
                        if host.ai_detected(channel, ai_type):
                            detected_events.append(ai_type)

                    # Check for visitor detection (doorbell)
                    if host.visitor_detected(channel):
                        detected_events.append("visitor")

                except Exception as e:
                    cWarningDom("reolink")(f"Error checking event states for {hostname} channel {channel}: {str(e)}")
                    continue

                # Send detailed event information to Calaos
                if detected_events:
                    # Mark camera as connected since we're receiving events
                    if not self.connection_status.get(hostname, True):
                        cInfoDom("reolink")(f"Camera {hostname} appears to be back online (receiving events)")
                        self.connection_status[hostname] = True
                    
                    for detected_event in detected_events:
                        event_msg = {
                            "event": "detection",
                            "hostname": hostname,
                            "event_type": detected_event,
                            "channel": channel,
                            "timestamp": datetime.now().isoformat(),
                            "camera_name": host.camera_name(channel) if hasattr(host, 'camera_name') else f"Camera_{channel}",
                            "tcp_push_active": host.baichuan.events_active
                        }
                        self.send_message(json.dumps(event_msg))
                        cInfoDom("reolink")(f"Event detected on {hostname} channel {channel}: {detected_event}")

        return event_callback

    async def connect_camera(self, hostname, username, password, event_type):
        """Connect to a Reolink camera and set up Baichuan TCP event subscription"""
        try:
            cInfoDom("reolink")(f"Connecting to camera {hostname}")

            # Store camera configuration for reconnection
            self.camera_configs[hostname] = {
                'hostname': hostname,
                'username': username,
                'password': password,
                'event_type': event_type
            }

            host = Host(hostname, username, password)
            await host.get_host_data()

            camera_key = f"{hostname}_{event_type}"
            self.registered_cameras[camera_key] = host
            self.connection_status[hostname] = True

            cInfoDom("reolink")(f"Connected to camera {hostname}, setting up Baichuan TCP subscription for {event_type} events")

            # Set up Baichuan TCP push notification subscription
            await host.baichuan.subscribe_events()

            # Get available channels
            channels = getattr(host, 'channels', [0])  # Default to channel 0 if not available
            cInfoDom("reolink")(f"Camera {hostname} has channels: {channels}")

            # Create event callback for all channels
            event_callback = self.create_event_callback(host, hostname, channels)
            
            # Create disconnect callback to detect when connection is lost
            def disconnect_callback():
                cWarningDom("reolink")(f"Connection lost callback triggered for {hostname}")
                self.connection_status[hostname] = False
            
            # Register disconnect callback with Baichuan
            try:
                if hasattr(host.baichuan, '_close_callback'):
                    original_close_callback = host.baichuan._close_callback
                    
                    def enhanced_close_callback():
                        disconnect_callback()
                        if original_close_callback:
                            original_close_callback()
                    
                    host.baichuan._close_callback = enhanced_close_callback
            except Exception as e:
                cDebugDom("reolink")(f"Could not register disconnect callback for {hostname}: {str(e)}")

            # Register the callback with a unique ID for specific event types
            callback_id = f"{hostname}_{event_type}"

            # Register for motion and AI detection events (cmd_id=33) on all channels
            for channel in channels:
                host.baichuan.register_callback(f"{callback_id}_ch{channel}", event_callback, cmd_id=33, channel=channel)

            # Also register for battery events if supported (cmd_id=252)
            try:
                if hasattr(host, 'battery_percent') and host.battery_percent(0) is not None:
                    battery_callback_id = f"{hostname}_battery"
                    host.baichuan.register_callback(battery_callback_id, event_callback, cmd_id=252, channel=0)
                    cDebugDom("reolink")(f"Registered battery events for {hostname}")
            except Exception as e:
                cDebugDom("reolink")(f"Battery events not supported for {hostname}: {str(e)}")

            # Register for privacy mode changes (cmd_id=623)
            privacy_callback_id = f"{hostname}_privacy"
            host.baichuan.register_callback(privacy_callback_id, event_callback, cmd_id=623, channel=None)

            # Check if TCP push is active
            if host.baichuan.events_active:
                cInfoDom("reolink")(f"Baichuan TCP push active for camera {hostname}")
            else:
                cWarningDom("reolink")(f"Baichuan TCP push not active for camera {hostname}")

            cInfoDom("reolink")(f"Successfully registered camera {hostname} for Baichuan {event_type} events")

        except Exception as e:
            cErrorDom("reolink")(f"Failed to connect to camera {hostname}: {str(e)}")
            error_msg = {
                "status": "error",
                "message": f"Failed to connect to camera {hostname}: {str(e)}"
            }
            self.send_message(json.dumps(error_msg))

    async def check_events_periodically(self):
        """Periodically check for Baichuan events as fallback when callbacks don't work"""
        cDebugDom("reolink")("Starting periodic event checking as fallback")

        while True:
            try:
                # Sleep for a longer interval to reduce spam - callbacks should handle most events
                await asyncio.sleep(30)  # Increased from 5 to 30 seconds

                # Only check if we have registered cameras and no active TCP push
                if not self.registered_cameras:
                    continue

                for camera_key, host in list(self.registered_cameras.items()):
                    hostname, event_type = camera_key.split("_", 1)

                    # Only use polling if TCP push is not active and camera is marked as disconnected
                    if not host.baichuan.events_active and not self.connection_status.get(hostname, True):
                        # Only log once every 5 minutes to avoid spam
                        current_time = time.time()
                        last_log_key = f"fallback_log_{hostname}"
                        last_log_time = getattr(self, last_log_key, 0)
                        
                        if current_time - last_log_time > 300:  # 5 minutes
                            cDebugDom("reolink")(f"Fallback: checking events for {hostname} (TCP push not active)")
                            setattr(self, last_log_key, current_time)

                        # Simple motion check as fallback - but only for motion events
                        if event_type == "motion":
                            try:
                                for channel in range(getattr(host, 'num_channels', 1)):
                                    if hasattr(host, 'motion_detected') and host.motion_detected(channel):
                                        cInfoDom("reolink")(f"Fallback: Motion detected on {hostname} channel {channel}")
                                        event_msg = {
                                            "event": "detection",
                                            "hostname": hostname,
                                            "event_type": "motion",
                                            "channel": channel,
                                            "timestamp": datetime.now().isoformat(),
                                            "fallback": True
                                        }
                                        self.send_message(json.dumps(event_msg))
                                        break
                            except Exception as e:
                                cDebugDom("reolink")(f"Error in fallback motion check for {hostname}: {str(e)}")

            except Exception as e:
                cErrorDom("reolink")(f"Error in periodic event check: {str(e)}")
                await asyncio.sleep(30)

    def message_received(self, message):
        cDebugDom("reolink")(f"Received message: {message}")

        # Parse the JSON message
        try:
            msg_data = json.loads(message)
        except Exception as e:
            cErrorDom("reolink")("Failed to parse JSON message: %s", str(e))
            return

        if msg_data.get("action") == "register":
            hostname = msg_data.get("hostname")
            username = msg_data.get("username")
            password = msg_data.get("password")
            event_type = msg_data.get("event_type", "motion")

            if not hostname:
                cErrorDom("reolink")("No hostname provided")
                return

            if not username or not password:
                cErrorDom("reolink")("No username or password provided")
                return

            # Schedule the camera connection in the event loop
            if self.event_loop:
                cDebugDom("reolink")(f"Scheduling camera connection for {hostname}")
                try:
                    asyncio.run_coroutine_threadsafe(
                        self.connect_camera(hostname, username, password, event_type),
                        self.event_loop
                    )
                    cDebugDom("reolink")(f"Camera connection scheduled successfully for {hostname}")
                except Exception as e:
                    cErrorDom("reolink")(f"Failed to schedule camera connection: {str(e)}")
            else:
                cErrorDom("reolink")("Event loop not initialized")
        else:
            cWarningDom("reolink")(f"Unknown action: {msg_data.get('action')}")

if __name__ == "__main__":
    configure_logger()
    cInfoDom("reolink")("Starting Reolink control client")

    client = ReolinkClient()
    if client.setup():
        client.run(200)
    else:
        cCriticalDom("reolink")("Failed to setup ReolinkClient")
        exit(1)