#!/usr/bin/env python3

from calaos_extern_proc import cDebugDom, cInfoDom, cErrorDom, cCriticalDom, cWarningDom, configure_logger, ExternProcClient
from reolink_aio.api import Host
import asyncio
import argparse
import json
import threading
from datetime import datetime
import time
import contextlib
import signal
import functools
import sys
import errno
import concurrent.futures
import os

import logging

class CircuitBreaker:
    """Circuit breaker to prevent infinite reconnection attempts"""
    def __init__(self, failure_threshold=5, recovery_timeout=300, half_open_max_calls=3):
        self.failure_count = 0
        self.failure_threshold = failure_threshold
        self.recovery_timeout = recovery_timeout
        self.half_open_max_calls = half_open_max_calls
        self.last_failure_time = 0
        self.state = "closed"  # closed, open, half-open
        self.half_open_calls = 0
        self.lock = threading.Lock()

    def can_attempt(self):
        """Check if an attempt is allowed"""
        with self.lock:
            current_time = time.time()

            if self.state == "closed":
                return True
            elif self.state == "open":
                if current_time - self.last_failure_time > self.recovery_timeout:
                    self.state = "half-open"
                    self.half_open_calls = 0
                    return True
                return False
            elif self.state == "half-open":
                return self.half_open_calls < self.half_open_max_calls

            return False

    def record_success(self):
        """Record a success"""
        with self.lock:
            self.failure_count = 0
            self.state = "closed"
            self.half_open_calls = 0

    def record_failure(self):
        """Record a failure"""
        with self.lock:
            self.failure_count += 1
            self.last_failure_time = time.time()

            if self.state == "half-open":
                self.state = "open"
            elif self.failure_count >= self.failure_threshold:
                self.state = "open"

            if self.state == "half-open":
                self.half_open_calls += 1

    def get_status(self):
        """Returns the current circuit breaker status"""
        with self.lock:
            return {
                "state": self.state,
                "failure_count": self.failure_count,
                "time_until_retry": max(0, self.recovery_timeout - (time.time() - self.last_failure_time)) if self.state == "open" else 0
            }

class ReolinkClient(ExternProcClient):
    def __init__(self):
        super().__init__()
        # Thread-safe dictionaries with locks
        self._cameras_lock = threading.RLock()
        self._status_lock = threading.RLock()
        self._config_lock = threading.RLock()

        self.registered_cameras = {}
        self.camera_configs = {}  # Store camera connection parameters
        self.connection_status = {}  # Track connection status
        self.last_reconnect_attempt = {}  # Track last reconnection attempt
        self.reconnect_tasks = {}  # Track reconnection tasks
        self.background_tasks = []  # Track background tasks
        self._callback_tasks_lock = threading.Lock()  # Lock for callback_tasks
        self.callback_tasks = {}  # Track callback tasks
        self.camera_performance = {}  # Track performance per camera
        self._perf_timestamps = {}  # Track when performance metrics were last updated
        self.circuit_breakers = {}  # Circuit breakers for each camera
        self.event_loop = None
        self.event_loop_thread = None
        self.shutdown_requested = False
        self.last_health_check = time.time()
        self.callback_timeouts = {}  # Track callback performance
        self._dynamic_timers = {}  # Centralized timer storage to prevent memory leaks
        self.restart_cooldown = 0  # Prevent too frequent restarts
        self.min_restart_interval = 900  # 15 minutes minimum between restarts
        self._executor = None  # Reuse executor
        self._camera_semaphores = {}  # Per-camera semaphore to limit concurrent operations

    @property
    def executor(self):
        """Reusable executor for blocking operations"""
        if self._executor is None:
            self._executor = concurrent.futures.ThreadPoolExecutor(
                max_workers=8,
                thread_name_prefix="reolink_callback"
            )
        return self._executor

    def get_camera_semaphore(self, hostname):
        """Get or create a per-camera semaphore to limit concurrent executor operations"""
        if hostname not in self._camera_semaphores:
            self._camera_semaphores[hostname] = asyncio.Semaphore(3)
        return self._camera_semaphores[hostname]

    async def run_in_executor_safe(self, hostname, func, timeout_seconds=5.0):
        """Run a blocking function in executor with per-camera semaphore protection and timeout.
        Returns the result or None if the semaphore is full or timeout occurs."""
        semaphore = self.get_camera_semaphore(hostname)

        try:
            async with asyncio.timeout(timeout_seconds):
                async with semaphore:
                    return await asyncio.get_event_loop().run_in_executor(self.executor, func)
        except asyncio.TimeoutError:
            cDebugDom("reolink")(f"Executor call timeout for {hostname} after {timeout_seconds}s")
            return None
        except Exception as e:
            cDebugDom("reolink")(f"Executor call failed for {hostname}: {e}")
            return None

    def get_adaptive_timeout(self, hostname, operation_type, base_timeout):
        """Adaptive timeout based on past performance"""
        perf_key = f"{hostname}_{operation_type}"
        avg_time = self.camera_performance.get(perf_key, base_timeout / 2)

        # Timeout = 3x average time, minimum = base_timeout
        adaptive_timeout = max(base_timeout, avg_time * 3)
        return min(adaptive_timeout, base_timeout * 3)  # Maximum 3x the base timeout

    def update_performance(self, hostname, operation_type, duration):
        """Update performance statistics with intelligent limitation"""
        perf_key = f"{hostname}_{operation_type}"
        current_time = time.time()

        # Limit the number of entries per camera to avoid infinite growth
        camera_perf_keys = [k for k in self.camera_performance.keys() if k.startswith(f"{hostname}_")]

        if len(camera_perf_keys) > 20:  # Max 20 metrics per camera
            # Remove the oldest metric
            oldest_key = min(camera_perf_keys,
                           key=lambda k: self._perf_timestamps.get(k, 0))
            if oldest_key in self.camera_performance:
                del self.camera_performance[oldest_key]
            if oldest_key in self._perf_timestamps:
                del self._perf_timestamps[oldest_key]

        # Update statistics
        current_avg = self.camera_performance.get(perf_key, duration)
        self.camera_performance[perf_key] = current_avg * 0.7 + duration * 0.3
        self._perf_timestamps[perf_key] = current_time

    async def cleanup_camera_sessions(self):
        """Properly close all camera sessions"""
        cInfoDom("reolink")("Cleaning up camera sessions...")

        with self._cameras_lock:
            for camera_key, host in self.registered_cameras.items():
                try:
                    hostname = camera_key.split("_")[0]
                    cDebugDom("reolink")(f"Cleaning up session for {hostname}")

                    # Logout from camera
                    try:
                        await host.logout()
                    except Exception as e:
                        cDebugDom("reolink")(f"Error during logout for {hostname}: {e}")

                    # Close aiohttp session if it exists
                    if hasattr(host, '_aiohttp_session') and host._aiohttp_session:
                        try:
                            await host._aiohttp_session.close()
                        except Exception as e:
                            cDebugDom("reolink")(f"Error closing aiohttp session for {hostname}: {e}")

                except Exception as e:
                    cDebugDom("reolink")(f"Error cleaning up camera {camera_key}: {e}")

    def stop(self):
        cInfoDom("reolink")("Shutting down Reolink client...")
        self.shutdown_requested = True

        # Cancel callback tasks
        with self._callback_tasks_lock:
            for task in self.callback_tasks.values():
                if not task.done():
                    try:
                        task.cancel()
                    except Exception as e:
                        cDebugDom("reolink")(f"Error canceling callback task: {e}")

        # Cancel all background tasks
        for task in self.background_tasks:
            if task and not task.done():
                try:
                    task.cancel()
                except Exception as e:
                    cDebugDom("reolink")(f"Error canceling task: {e}")

        # Cancel all reconnection tasks
        with self._cameras_lock:
            for task in self.reconnect_tasks.values():
                if task and not task.done():
                    try:
                        task.cancel()
                    except Exception as e:
                        cDebugDom("reolink")(f"Error canceling reconnect task: {e}")

        # Shutdown executor with graceful handling
        if self._executor:
            try:
                self._executor.shutdown(wait=False, cancel_futures=True)
            except Exception as e:
                cDebugDom("reolink")(f"Error during executor shutdown: {e}")
            finally:
                self._executor = None

        if self.event_loop and not self.event_loop.is_closed():
            try:
                # Schedule shutdown with timeout
                def shutdown_loop():
                    try:
                        # Give pending tasks time to cleanup
                        self.event_loop.call_later(2.0, self.event_loop.stop)
                    except Exception as e:
                        cDebugDom("reolink")(f"Error scheduling loop shutdown: {e}")

                self.event_loop.call_soon_threadsafe(shutdown_loop)

                # Wait for the thread to finish with timeout
                if self.event_loop_thread and self.event_loop_thread.is_alive():
                    self.event_loop_thread.join(timeout=5.0)
                    if self.event_loop_thread.is_alive():
                        cWarningDom("reolink")("Event loop thread did not shut down gracefully")

            except Exception as e:
                cErrorDom("reolink")(f"Error during shutdown: {e}")

        return super().stop()

    @contextlib.asynccontextmanager
    async def timeout_context(self, timeout_seconds, operation_name="operation"):
        """Robust timeout context using asyncio.timeout (Python 3.11+)
        or asyncio.wait_for pattern for older versions"""
        start_time = time.time()

        try:
            async with asyncio.timeout(timeout_seconds):
                yield
        except asyncio.TimeoutError:
            elapsed = time.time() - start_time
            cWarningDom("reolink")(f"Timeout ({timeout_seconds}s, elapsed: {elapsed:.1f}s) during {operation_name}")
            raise

    def safe_camera_access(self, func, *args, **kwargs):
        """Thread-safe access to camera data"""
        with self._cameras_lock:
            return func(*args, **kwargs)

    def safe_status_access(self, func, *args, **kwargs):
        """Thread-safe access to status data"""
        with self._status_lock:
            return func(*args, **kwargs)

    def safe_config_access(self, func, *args, **kwargs):
        """Thread-safe access to config data"""
        with self._config_lock:
            return func(*args, **kwargs)

    def cleanup_completed_tasks(self):
        """Version with performance stats cleanup"""
        # Cleanup existing background tasks
        original_count = len(self.background_tasks)
        self.background_tasks = [task for task in self.background_tasks if not task.done()]
        cleaned_bg = original_count - len(self.background_tasks)

        # Cleanup callback tasks
        with self._callback_tasks_lock:
            original_cb_count = len(self.callback_tasks)
            self.callback_tasks = {k: v for k, v in self.callback_tasks.items() if not v.done()}
            cleaned_cb = original_cb_count - len(self.callback_tasks)

        # Cleanup dynamic timers with intelligent logic and thread-safe protection
        current_time = time.time()
        old_timer_count = len(self._dynamic_timers)

        timers_to_remove = []

        # Thread-safe access to timers
        with self._config_lock:
            for key, value in self._dynamic_timers.items():
                # failed_reconnect timers: compare with value (expiration timestamp)
                if key.startswith("failed_reconnect_"):
                    if current_time > value:  # Expired
                        timers_to_remove.append(key)
                        cDebugDom("reolink")(f"Expired failed_reconnect timer for {key}")
                # Other timers: compare with age (keep only last hour)
                else:
                    cutoff_time = current_time - 3600  # 1 hour
                    if value < cutoff_time:
                        timers_to_remove.append(key)

            # Remove expired timers within the same lock
            for key in timers_to_remove:
                del self._dynamic_timers[key]

        cleaned_timers = len(timers_to_remove)

        # Cleanup performance stats for disconnected cameras
        current_cameras = set()
        try:
            with self._cameras_lock:
                current_cameras = {
                    key.split('_')[0] for key in self.registered_cameras.keys()
                }
        except Exception:
            pass  # In case of error, don't clean stats

        perf_cleaned = 0
        if current_cameras:  # Only if we could retrieve the list
            perf_keys_to_remove = []
            timestamp_keys_to_remove = []

            for perf_key in list(self.camera_performance.keys()):
                hostname = perf_key.split('_')[0]
                if hostname not in current_cameras:
                    perf_keys_to_remove.append(perf_key)

            # Also clean corresponding timestamps
            for timestamp_key in list(self._perf_timestamps.keys()):
                hostname = timestamp_key.split('_')[0]
                if hostname not in current_cameras:
                    timestamp_keys_to_remove.append(timestamp_key)

            # Remove obsolete entries
            for key in perf_keys_to_remove:
                del self.camera_performance[key]

            for key in timestamp_keys_to_remove:
                del self._perf_timestamps[key]

            perf_cleaned = len(perf_keys_to_remove)

        if cleaned_bg > 0 or cleaned_cb > 0 or cleaned_timers > 0 or perf_cleaned > 0:
            cDebugDom("reolink")(
                f"Cleanup: {cleaned_bg} bg tasks, {cleaned_cb} callback tasks, {cleaned_timers} timers, {perf_cleaned} perf stats"
            )

    def set_timer(self, key, value):
        """Centralized timer management to avoid memory leaks - Thread-safe without race condition"""
        needs_cleanup = False

        with self._config_lock:
            self._dynamic_timers[key] = value
            # Check atomically if cleanup is needed
            needs_cleanup = len(self._dynamic_timers) > 100

        # Cleanup outside the lock to avoid nested locks
        if needs_cleanup:
            self.cleanup_completed_tasks()

    def get_timer(self, key, default=0):
        """Thread-safe timer retrieval"""
        with self._config_lock:
            return self._dynamic_timers.get(key, default)

    async def cleanup_executor_tasks(self):
        """Periodically clean executor state to avoid resource leaks"""
        cDebugDom("reolink")("Starting executor cleanup monitor")

        while not self.shutdown_requested:
            try:
                await asyncio.sleep(300)  # Every 5 minutes

                if self._executor:
                    # Check executor state
                    try:
                        # Force a small cleanup if possible
                        if hasattr(self._executor, '_threads'):
                            active_threads = len([t for t in self._executor._threads if t.is_alive()])
                            total_threads = len(self._executor._threads)

                            if total_threads > active_threads * 2:  # Too many dead threads
                                cDebugDom("reolink")(f"Executor cleanup: {active_threads} active / {total_threads} total threads")

                        # Log statistics
                        current_time = time.time()
                        executor_log_key = "executor_status_log"
                        last_executor_log = self.get_timer(executor_log_key, 0)

                        if current_time - last_executor_log > 1800:  # Log every 30 minutes
                            cInfoDom("reolink")(
                                f"Executor status: active threads={active_threads if 'active_threads' in locals() else 'unknown'}, "
                                f"total threads={total_threads if 'total_threads' in locals() else 'unknown'}"
                            )
                            self.set_timer(executor_log_key, current_time)

                    except Exception as e:
                        cDebugDom("reolink")(f"Error checking executor status: {e}")

            except Exception as e:
                cErrorDom("reolink")(f"Error in executor cleanup: {e}")
                await asyncio.sleep(300)  # Continue even in case of error

    def classify_connection_error(self, error):
        """Classify errors to adapt retry strategy"""

        if isinstance(error, (OSError, ConnectionError)):
            error_code = getattr(error, 'errno', None)
            if error_code in [errno.ENETUNREACH, errno.EHOSTUNREACH]:
                return "network_unreachable"
            elif error_code in [errno.ECONNREFUSED]:
                return "connection_refused"
            elif error_code in [errno.ETIMEDOUT]:
                return "connection_timeout"
            elif error_code in [errno.ECONNRESET]:
                return "connection_reset"
        elif isinstance(error, asyncio.TimeoutError):
            return "timeout"
        elif isinstance(error, (ValueError, TypeError)):
            return "invalid_credentials"

        return "unknown"

    def get_retry_strategy(self, error_type):
        """Return retry strategy based on error type"""
        strategies = {
            "network_unreachable": {"delay": 60, "max_retries": 3},  # Network problem, wait longer
            "connection_refused": {"delay": 30, "max_retries": 5},  # Service down, moderate retry
            "connection_timeout": {"delay": 20, "max_retries": 4},  # Network latency
            "connection_reset": {"delay": 10, "max_retries": 6},    # Temporary problem
            "timeout": {"delay": 15, "max_retries": 4},             # General timeout
            "invalid_credentials": {"delay": 300, "max_retries": 1}, # Credentials, wait long
            "unknown": {"delay": 30, "max_retries": 3}              # Default
        }
        return strategies.get(error_type, strategies["unknown"])

    async def detect_deadlock(self):
        """Advanced deadlock detection with responsiveness testing"""
        try:
            response_times = []

            # Perform multiple responsiveness tests
            for i in range(3):
                test_event = asyncio.Event()  # Fresh event for each test
                start_time = time.time()

                async def test_task(evt=test_event):
                    await asyncio.sleep(0.001)  # Micro-delay
                    evt.set()

                test_task_handle = asyncio.create_task(test_task())

                try:
                    await asyncio.wait_for(test_event.wait(), timeout=2.0)
                    response_time = time.time() - start_time
                    response_times.append(response_time)

                except asyncio.TimeoutError:
                    cWarningDom("reolink")(f"Deadlock test {i+1}/3 timed out")
                    return True  # Deadlock detected
                finally:
                    if not test_task_handle.done():
                        test_task_handle.cancel()

                # Small delay between tests
                await asyncio.sleep(0.1)

            # Analyze response times
            if response_times:
                avg_response = sum(response_times) / len(response_times)
                max_response = max(response_times)

                # Smart detection thresholds
                if avg_response > 0.5 or max_response > 1.0:
                    cWarningDom("reolink")(
                        f"Event loop performance degraded: avg={avg_response:.3f}s, max={max_response:.3f}s"
                    )
                    return True

                cDebugDom("reolink")(f"Event loop responsive: avg={avg_response:.3f}s, max={max_response:.3f}s")
                return False
            else:
                cWarningDom("reolink")("No successful deadlock tests completed")
                return True

        except Exception as e:
            cErrorDom("reolink")(f"Error in deadlock detection: {e}")
            return True  # In case of error, assume a problem

    async def health_check(self):
        """Periodic system health check - AGGRESSIVE VERSION with deadlock detection"""
        cDebugDom("reolink")("Starting aggressive health check monitor with deadlock detection")

        consecutive_failures = 0
        max_consecutive_failures = 3

        while not self.shutdown_requested:
            try:
                await asyncio.sleep(20)  # More frequent checks (20s instead of 30s)

                current_time = time.time()

                # Clean completed tasks periodically
                self.cleanup_completed_tasks()

                # Advanced deadlock detection test
                deadlock_detected = await self.detect_deadlock()
                if deadlock_detected:
                    cWarningDom("reolink")("Advanced deadlock detection triggered")
                    consecutive_failures += 1
                else:
                    # Partial reset of failures if deadlock test passes
                    consecutive_failures = max(0, consecutive_failures - 1)

                # Check callbacks that take too long (more aggressive)
                with self._cameras_lock:
                    stuck_callbacks = []
                    for camera_key, last_time in list(self.callback_timeouts.items()):
                        callback_duration = current_time - last_time
                        if callback_duration > 30:  # Callback stuck for more than 30s (instead of 60s)
                            stuck_callbacks.append((camera_key, callback_duration))
                            hostname = camera_key.split("_")[0]
                            with self._status_lock:
                                self.connection_status[hostname] = False

                    if stuck_callbacks:
                        cWarningDom("reolink")(f"Detected {len(stuck_callbacks)} stuck callbacks")
                        consecutive_failures += 1

                # Check camera availability
                cameras_count = len(self.safe_camera_access(lambda: self.registered_cameras))
                connected_count = len([status for status in self.safe_status_access(lambda: self.connection_status.values()) if status])

                if cameras_count > 0 and connected_count == 0:
                    cWarningDom("reolink")(f"All {cameras_count} cameras are disconnected!")
                    consecutive_failures += 1

                # Check background task performance
                active_tasks = len([task for task in self.background_tasks if not task.done()])
                if active_tasks < 2 and cameras_count > 0:  # At least monitor and health check should be running
                    cWarningDom("reolink")(f"Too few background tasks running: {active_tasks}")
                    consecutive_failures += 1

                # Monitor performance stats growth
                perf_stats_count = len(self.camera_performance)
                if perf_stats_count > 200:  # Alert threshold
                    cWarningDom("reolink")(f"Performance stats growing large: {perf_stats_count} entries")
                    consecutive_failures += 1

                # More aggressive emergency restart decision
                time_since_last_check = current_time - self.last_health_check

                if (consecutive_failures >= max_consecutive_failures or
                    time_since_last_check > 120):  # More aggressive: 2 minutes instead of 5

                    cCriticalDom("reolink")(
                        f"Health check critical failure detected "
                        f"(consecutive failures: {consecutive_failures}, "
                        f"time since last check: {time_since_last_check:.1f}s)"
                    )
                    self.force_restart()
                    consecutive_failures = 0  # Reset after restart

                self.last_health_check = current_time

                # Periodic health status log with performance stats (every 5 minutes)
                health_log_key = "health_status_log"
                last_health_log = self.get_timer(health_log_key, 0)
                if current_time - last_health_log > 300:
                    # Calculate some interesting stats
                    avg_callback_time = 0
                    callback_count = 0
                    for key, value in self.camera_performance.items():
                        if key.endswith('_callback'):
                            avg_callback_time += value
                            callback_count += 1

                    avg_callback_time = avg_callback_time / max(callback_count, 1)

                    # Monitor file descriptor count to detect resource leaks
                    try:
                        fd_count = len(os.listdir('/proc/self/fd'))
                    except Exception:
                        fd_count = -1

                    cInfoDom("reolink")(
                        f"Health status: {cameras_count} cameras, "
                        f"{connected_count} connected, "
                        f"{active_tasks} active tasks, "
                        f"avg callback time: {avg_callback_time:.2f}s, "
                        f"perf stats: {perf_stats_count}, "
                        f"open fds: {fd_count}, "
                        f"consecutive failures: {consecutive_failures}"
                    )
                    self.set_timer(health_log_key, current_time)

            except Exception as e:
                cErrorDom("reolink")(f"Health check failed: {e}")
                consecutive_failures += 1

                # In case of critical error in health check itself
                if consecutive_failures >= max_consecutive_failures:
                    cCriticalDom("reolink")(f"Health check system failure ({consecutive_failures} consecutive errors)")
                    self.force_restart()

    def force_restart(self):
        """Force an emergency restart of the system with cooldown and concurrency protection"""
        current_time = time.time()

        # Protection against simultaneous calls with atomic flag
        restart_key = "restart_in_progress"
        with self._config_lock:
            if self.get_timer(restart_key, 0) > current_time - 5:  # 5 seconds protection
                cDebugDom("reolink")("Restart already in progress, ignoring duplicate request")
                return

            # Mark restart as in progress
            self.set_timer(restart_key, current_time)

        # Check cooldown to avoid too frequent restarts
        if current_time - self.restart_cooldown < self.min_restart_interval:
            cWarningDom("reolink")(
                f"Restart requested but in cooldown period "
                f"({int(self.min_restart_interval - (current_time - self.restart_cooldown))}s remaining)"
            )
            return

        self.restart_cooldown = current_time
        cCriticalDom("reolink")("Forcing emergency restart of event system")

        try:
            # Mark all cameras as disconnected
            with self._status_lock:
                for hostname in self.connection_status:
                    self.connection_status[hostname] = False

            # Cancel all running callback tasks
            with self._callback_tasks_lock:
                for task_key, task in self.callback_tasks.items():
                    if task and not task.done():
                        task.cancel()
                self.callback_tasks.clear()

            # Properly close camera sessions before clearing
            with self._cameras_lock:
                for camera_key, host in self.registered_cameras.items():
                    try:
                        hostname = camera_key.split("_")[0]
                        # Schedule logout in event loop if possible
                        if self.event_loop and not self.event_loop.is_closed():
                            asyncio.run_coroutine_threadsafe(
                                self._safe_logout(host, hostname), self.event_loop
                            )
                    except Exception as e:
                        cDebugDom("reolink")(f"Error scheduling cleanup for {camera_key}: {e}")

                self.registered_cameras.clear()

                # Cancel reconnection tasks
                for task in self.reconnect_tasks.values():
                    if task and not task.done():
                        task.cancel()
                self.reconnect_tasks.clear()

            # Clean tasks and timers
            self.cleanup_completed_tasks()

            # Signal to main system that a restart is needed
            restart_count = len([t for t in self._dynamic_timers.keys() if t.startswith("restart_")]) + 1
            error_msg = {
                "status": "critical_error",
                "message": "System deadlock detected, restart required",
                "restart_count": restart_count,
                "timestamp": current_time
            }
            self.send_message(json.dumps(error_msg))

        except Exception as e:
            cCriticalDom("reolink")(f"Error during force restart: {e}")
        finally:
            # Release restart flag after a safety delay
            with self._config_lock:
                self.set_timer(restart_key, current_time - 10)  # Allow new restarts after 10s

    async def _safe_logout(self, host, hostname):
        """Safely logout and close a camera session to prevent resource leaks"""
        try:
            async with self.timeout_context(5, f"logout {hostname}"):
                await host.logout()
                cDebugDom("reolink")(f"Logged out from {hostname}")
        except Exception as e:
            cDebugDom("reolink")(f"Error during logout for {hostname}: {e}")

        # Close aiohttp session if it exists
        try:
            if hasattr(host, '_aiohttp_session') and host._aiohttp_session:
                await host._aiohttp_session.close()
                cDebugDom("reolink")(f"Closed aiohttp session for {hostname}")
        except Exception as e:
            cDebugDom("reolink")(f"Error closing aiohttp session for {hostname}: {e}")

    async def verify_connection_health(self, host, hostname):
        """Active connection health verification"""
        try:
            async with self.timeout_context(5, f"health check {hostname}"):
                # Test with a simple request
                await host.get_host_data()
                return True
        except Exception as e:
            cDebugDom("reolink")(f"Connection health check failed for {hostname}: {e}")
            return False

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
        self._start_time = time.time()  # Track startup time for uptime calculation
        self.parse_arguments()

        if not self.connect_socket():
            cCriticalDom("reolink")("Failed to connect to socket")
            return False

        # Set up asyncio event loop for handling camera connections
        try:
            self.event_loop = asyncio.new_event_loop()

            # Add a global exception handler for improved robustness
            def exception_handler(loop, context):
                exception = context.get('exception')
                if isinstance(exception, asyncio.CancelledError):
                    return  # Cancelled tasks are normal

                cErrorDom("reolink")(f"Unhandled exception in event loop: {context}")

                # If the exception is critical, trigger an emergency health check
                if isinstance(exception, (MemoryError, SystemError)):
                    cCriticalDom("reolink")("Critical system exception detected, forcing restart")
                    if hasattr(self, 'force_restart'):
                        try:
                            self.force_restart()
                        except Exception as e:
                            cCriticalDom("reolink")(f"Failed to trigger emergency restart: {e}")

            self.event_loop.set_exception_handler(exception_handler)

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

        # Start periodic event checking as fallback and monitoring tasks
        if self.event_loop:
            # Create and keep references to tasks
            task1 = asyncio.run_coroutine_threadsafe(self.check_events_periodically(), self.event_loop)
            task2 = asyncio.run_coroutine_threadsafe(self.monitor_connections(), self.event_loop)
            task3 = asyncio.run_coroutine_threadsafe(self.health_check(), self.event_loop)
            task4 = asyncio.run_coroutine_threadsafe(self.cleanup_executor_tasks(), self.event_loop)

            self.background_tasks.extend([task1, task2, task3, task4])

            cDebugDom("reolink")("All background tasks started successfully (including executor cleanup)")

        return True

    def run(self, timeout_ms):
        return super().run(timeout_ms)

    async def heartbeat_monitor(self):
        """Monitor event loop health and force restart if needed"""
        cDebugDom("reolink")("Starting heartbeat monitoring")

        while not self.shutdown_requested:
            try:
                await asyncio.sleep(30)  # Heartbeat interval
                current_time = time.time()

                # Update heartbeat
                self.last_health_check = current_time

                # Check if we need to clean up completed reconnection tasks
                completed_tasks = [hostname for hostname, task in self.reconnect_tasks.items()
                                 if task and task.done()]
                for hostname in completed_tasks:
                    del self.reconnect_tasks[hostname]
                    cDebugDom("reolink")(f"Cleaned up completed reconnection task for {hostname}")

            except Exception as e:
                cErrorDom("reolink")(f"Error in heartbeat monitor: {str(e)}")
                await asyncio.sleep(30)

    async def monitor_connections(self):
        """Monitor camera connections and trigger reconnections when needed"""
        cDebugDom("reolink")("Starting connection monitoring")

        while not self.shutdown_requested:
            try:
                await asyncio.sleep(10)  # Check every 10 seconds

                # Thread-safe access to cameras
                cameras_to_check = self.safe_camera_access(lambda: list(self.registered_cameras.items()))

                for camera_key, host in cameras_to_check:
                    hostname = camera_key.split("_", 1)[0]

                    try:
                        # Check if Baichuan TCP push is active with timeout
                        async with self.timeout_context(3, f"connection check {hostname}"):
                            tcp_active = host.baichuan.events_active

                            # Active health check periodically
                            current_time = time.time()
                            last_health_key = f"health_{hostname}"
                            last_health_time = self.get_timer(last_health_key, 0)

                            # Complete check every 5 minutes
                            if current_time - last_health_time > 300:
                                is_healthy = await self.verify_connection_health(host, hostname)
                                self.set_timer(last_health_key, current_time)

                                if not is_healthy:
                                    tcp_active = False
                                    cWarningDom("reolink")(f"Health check failed for {hostname}, marking as inactive")

                            if not tcp_active:
                                current_time = time.time()

                                # Mark as disconnected if not already
                                status_changed = self.safe_status_access(
                                    lambda: self._mark_disconnected_if_needed(hostname)
                                )

                                if status_changed:
                                    cWarningDom("reolink")(f"Camera {hostname} TCP push inactive, marking as disconnected")

                                # Try to reconnect if enough time has passed since last attempt
                                last_attempt = self.last_reconnect_attempt.get(hostname, 0)

                                # Check if the camera is temporarily "banned" after repeated failures
                                failed_key = f"failed_reconnect_{hostname}"
                                failed_until = self.get_timer(failed_key, 0)

                                if current_time < failed_until:
                                    # Camera temporarily banned from reconnection
                                    remaining = int(failed_until - current_time)
                                    if remaining % 300 == 0:  # Log every 5 minutes only
                                        cDebugDom("reolink")(f"Camera {hostname} reconnection banned for {remaining}s more")
                                    continue

                                if current_time - last_attempt > 30:  # Wait 30 seconds between reconnection attempts
                                    task_exists = self.safe_camera_access(
                                        lambda: hostname in self.reconnect_tasks and not self.reconnect_tasks[hostname].done()
                                    )

                                    if not task_exists:
                                        cInfoDom("reolink")(f"Starting reconnection task for camera {hostname}")
                                        reconnect_task = asyncio.create_task(
                                            self.reconnect_camera(hostname)
                                        )
                                        with self._cameras_lock:
                                            self.reconnect_tasks[hostname] = reconnect_task
                            else:
                                # Mark as connected if it was previously disconnected
                                status_changed = self.safe_status_access(
                                    lambda: self._mark_connected_if_needed(hostname)
                                )

                                if status_changed:
                                    cInfoDom("reolink")(f"Camera {hostname} TCP push active again, marking as connected")

                    except asyncio.TimeoutError:
                        cWarningDom("reolink")(f"Timeout checking connection status for {hostname}")
                        self.safe_status_access(lambda: self._mark_disconnected_if_needed(hostname))
                    except Exception as e:
                        cWarningDom("reolink")(f"Error checking connection for {hostname}: {e}")

            except Exception as e:
                cErrorDom("reolink")(f"Error in connection monitoring: {str(e)}")
                await asyncio.sleep(10)

    def _mark_disconnected_if_needed(self, hostname):
        """Helper method to mark camera as disconnected if it was connected"""
        current_status = self.connection_status.get(hostname, True)
        if current_status:
            self.connection_status[hostname] = False
            return True
        return False

    def _mark_connected_if_needed(self, hostname):
        """Helper method to mark camera as connected if it was disconnected"""
        current_status = self.connection_status.get(hostname, True)
        if not current_status:
            self.connection_status[hostname] = True
            return True
        return False

    async def reconnect_camera(self, hostname, max_retries=5):
        """Attempt to reconnect to a specific camera with circuit breaker protection"""

        # Get or create the circuit breaker for this camera
        if hostname not in self.circuit_breakers:
            self.circuit_breakers[hostname] = CircuitBreaker(
                failure_threshold=3,
                recovery_timeout=600,  # 10 minutes
                half_open_max_calls=2
            )

        circuit_breaker = self.circuit_breakers[hostname]

        # Check if the circuit breaker allows the attempt
        if not circuit_breaker.can_attempt():
            status = circuit_breaker.get_status()
            cWarningDom("reolink")(
                f"Circuit breaker prevents reconnection to {hostname} "
                f"(state: {status['state']}, retry in: {status['time_until_retry']:.0f}s)"
            )
            return False

        for retry_count in range(max_retries):
            try:
                self.last_reconnect_attempt[hostname] = time.time()
                cInfoDom("reolink")(f"Attempting to reconnect to camera {hostname} (attempt {retry_count + 1}/{max_retries})")

                # Get camera configuration with thread-safe access
                config = self.safe_config_access(lambda: self.camera_configs.get(hostname))
                if not config:
                    cErrorDom("reolink")(f"No configuration found for camera {hostname}")
                    circuit_breaker.record_failure()
                    return False

                # Remove old camera instance with timeout and proper session cleanup
                async with self.timeout_context(15, f"cleanup old connection {hostname}"):
                    old_keys = self.safe_camera_access(
                        lambda: [key for key in self.registered_cameras.keys() if key.startswith(hostname)]
                    )

                    for key in old_keys:
                        old_host = self.safe_camera_access(lambda: self.registered_cameras.pop(key, None))
                        if old_host:
                            try:
                                # Unsubscribe from events with timeout
                                async with self.timeout_context(5, f"unsubscribe events {hostname}"):
                                    await old_host.baichuan.unsubscribe_events()
                                    cDebugDom("reolink")(f"Unsubscribed from old events for {hostname}")
                            except Exception as e:
                                cDebugDom("reolink")(f"Error unsubscribing from old events for {hostname}: {str(e)}")

                            # Properly close the session to avoid resource leaks
                            await self._safe_logout(old_host, hostname)

                # Wait with exponential backoff (except for first attempt)
                if retry_count > 0:
                    backoff_time = min(5 * (2 ** (retry_count - 1)), 60)  # Max 60 seconds
                    cDebugDom("reolink")(f"Waiting {backoff_time}s before retry {retry_count + 1}")
                    await asyncio.sleep(backoff_time)

                # Try to reconnect with timeout
                async with self.timeout_context(30, f"reconnect {hostname}"):
                    await self.connect_camera(
                        config['hostname'],
                        config['username'],
                        config['password'],
                        config['event_type']
                    )

                cInfoDom("reolink")(f"Successfully reconnected to camera {hostname} after {retry_count + 1} attempts")

                # Record success in the circuit breaker
                circuit_breaker.record_success()

                # Clean the failure counter
                failed_key = f"failed_reconnect_{hostname}"
                with self._config_lock:
                    if failed_key in self._dynamic_timers:
                        del self._dynamic_timers[failed_key]

                return True

            except asyncio.TimeoutError:
                error_type = self.classify_connection_error(asyncio.TimeoutError())
                strategy = self.get_retry_strategy(error_type)
                cErrorDom("reolink")(f"Timeout during reconnection to camera {hostname} (attempt {retry_count + 1}) - Type: {error_type}")
                circuit_breaker.record_failure()
                if retry_count == max_retries - 1:
                    break  # Last attempt, exit loop
                # Continue with next iteration

            except Exception as e:
                error_type = self.classify_connection_error(e)
                strategy = self.get_retry_strategy(error_type)
                cErrorDom("reolink")(f"Failed to reconnect to camera {hostname} (attempt {retry_count + 1}): {str(e)} - Type: {error_type}")
                circuit_breaker.record_failure()

                # Adapt retry strategy based on error type
                if error_type == "invalid_credentials":
                    cCriticalDom("reolink")(f"Invalid credentials for {hostname}, stopping reconnection attempts")
                    break  # Stop immediately for credential errors

                if retry_count == max_retries - 1:
                    break  # Last attempt, exit loop
                # Continue with next iteration

        # All attempts failed
        cErrorDom("reolink")(f"Max retries ({max_retries}) reached for {hostname}, circuit breaker activated")

        # The circuit breaker now manages waiting delays
        status = circuit_breaker.get_status()

        # Send failure notification with circuit breaker information
        error_msg = {
            "status": "reconnect_failed",
            "hostname": hostname,
            "message": f"Failed to reconnect after {max_retries} attempts",
            "circuit_breaker_state": status['state'],
            "next_retry_in": status['time_until_retry'],
            "failure_count": status['failure_count']
        }
        self.send_message(json.dumps(error_msg))
        return False

    def create_event_callback(self, host, hostname, channels):
        """Improved version with task tracking and robust error handling"""
        def event_callback():
            """Synchronous callback that immediately delegates to the asynchronous version"""
            try:
                if self.event_loop and not self.event_loop.is_closed():
                    # Create and track the task
                    def create_and_track_task():
                        task = asyncio.create_task(
                            self._async_event_callback(host, hostname, channels)
                        )
                        # Track the task with automatic cleanup
                        camera_key = f"{hostname}_callback"

                        # Clean old task if it exists (thread-safe)
                        with self._callback_tasks_lock:
                            old_task = self.callback_tasks.get(camera_key)
                            if old_task and not old_task.done():
                                old_task.cancel()
                            self.callback_tasks[camera_key] = task

                        # Automatic cleanup when task finishes with improved error handling
                        def cleanup_task(fut):
                            with self._callback_tasks_lock:
                                self.callback_tasks.pop(camera_key, None)
                            if fut.exception():
                                exception = fut.exception()
                                cWarningDom("reolink")(f"Callback task failed for {hostname}: {exception}")

                                # If the error is critical, mark the camera as disconnected
                                if isinstance(exception, (asyncio.TimeoutError, ConnectionError, OSError)):
                                    try:
                                        with self._status_lock:
                                            self.connection_status[hostname] = False
                                        cWarningDom("reolink")(f"Marking {hostname} as disconnected due to callback error")
                                    except Exception as e:
                                        cDebugDom("reolink")(f"Error updating connection status for {hostname}: {e}")

                        task.add_done_callback(cleanup_task)
                        return task

                    self.event_loop.call_soon_threadsafe(create_and_track_task)
                else:
                    cWarningDom("reolink")(f"Event loop unavailable for callback {hostname}")
            except Exception as e:
                cErrorDom("reolink")(f"Failed to schedule async callback for {hostname}: {e}")
                # In case of critical error, mark the camera as disconnected
                try:
                    with self._status_lock:
                        self.connection_status[hostname] = False
                except Exception:
                    pass

        return event_callback

    async def _async_event_callback(self, host, hostname, channels):
        """Version with adaptive timeouts and initialization filtering"""
        callback_start_time = time.time()
        camera_key = f"{hostname}_{getattr(host, 'event_type', 'unknown')}"

        try:
            # Ignore events during the first 3 seconds after connection
            init_time = self.get_timer(f"init_time_{hostname}", 0)
            if init_time == 0:  # First time, mark initialization time
                self.set_timer(f"init_time_{hostname}", callback_start_time)
                cDebugDom("reolink")(f"First callback for {hostname}, ignoring potential startup burst")
                return
            elif callback_start_time - init_time < 3:
                cDebugDom("reolink")(f"Ignoring event during initialization period for {hostname}")
                return

            # Mark the callback start for monitoring
            with self._cameras_lock:
                self.callback_timeouts[camera_key] = callback_start_time

            cDebugDom("reolink")(f"Async Baichuan event callback triggered for {hostname}")

            # Adaptive global timeout
            global_timeout = self.get_adaptive_timeout(hostname, "callback", 10)
            async with self.timeout_context(global_timeout, f"complete event callback {hostname}"):
                # Check all channels for events with individual timeout protection
                for channel in channels:
                    detected_events = []

                    try:
                        # Check if this is a doorbell device for optimized processing
                        is_doorbell = self.get_timer(f"is_doorbell_{hostname}", 0) > 0
                        event_type = getattr(host, 'event_type', 'unknown')

                        # For doorbell devices, prioritize visitor detection
                        if is_doorbell or event_type == 'visitor':
                            # Check visitor detection first (highest priority for doorbells)
                            visitor_timeout = self.get_adaptive_timeout(hostname, "visitor", 1.0)  # Faster for doorbells
                            visitor_start = time.time()

                            try:
                                if hasattr(host, 'visitor_detected'):
                                    visitor_detector = functools.partial(host.visitor_detected, channel)
                                    visitor_result = await self.run_in_executor_safe(hostname, visitor_detector, visitor_timeout)

                                    if visitor_result:
                                        detected_events.append("visitor")
                                        cInfoDom("reolink")(f"Visitor detected at doorbell {hostname}!")

                                visitor_duration = time.time() - visitor_start
                                self.update_performance(hostname, "visitor", visitor_duration)

                                # For doorbell, if visitor is detected, we can prioritize sending that event first
                                if detected_events and is_doorbell:
                                    # Send visitor event immediately for doorbells
                                    event_msg = {
                                        "event": "detection",
                                        "hostname": hostname,
                                        "event_type": "visitor",
                                        "channel": channel,
                                        "timestamp": datetime.now().isoformat(),
                                        "camera_name": getattr(host, 'camera_name', lambda x: f"Camera_{x}")(channel) if hasattr(host, 'camera_name') else f"Camera_{channel}",
                                        "tcp_push_active": host.baichuan.events_active,
                                        "callback_duration": time.time() - callback_start_time,
                                        "async_callback": True,
                                        "adaptive_timeout": global_timeout,
                                        "doorbell_optimized": True
                                    }
                                    self.send_message(json.dumps(event_msg))

                                    # Mark camera as connected since we're receiving events
                                    self.safe_status_access(lambda: self._mark_connected_if_received_events(hostname))

                                    # For doorbells, we can continue to check other events but visitor takes priority
                                    continue

                            except asyncio.TimeoutError:
                                cWarningDom("reolink")(f"Visitor check timeout for {hostname} (timeout: {visitor_timeout:.1f}s)")
                            except Exception as e:
                                cDebugDom("reolink")(f"Visitor check failed for {hostname}: {e}")

                        # Motion check with adaptive timeout
                        motion_timeout = self.get_adaptive_timeout(hostname, "motion", 1.5)
                        motion_start = time.time()

                        try:
                            if hasattr(host, 'motion_detected'):
                                motion_detector = functools.partial(host.motion_detected, channel)
                                motion_result = await self.run_in_executor_safe(hostname, motion_detector, motion_timeout)

                                if motion_result:
                                    detected_events.append("motion")

                            # Update performance stats
                            motion_duration = time.time() - motion_start
                            self.update_performance(hostname, "motion", motion_duration)

                        except Exception as e:
                            cDebugDom("reolink")(f"Motion check failed for {hostname}: {e}")

                        # Check for AI detections with adaptive timeouts (lower priority for doorbells)
                        if not is_doorbell or not detected_events:  # Skip AI for doorbells if we already have events
                            ai_types = ["face", "person", "vehicle", "pet", "package", "cry"]
                            for ai_type in ai_types:
                                ai_timeout = self.get_adaptive_timeout(hostname, f"ai_{ai_type}", 1.0)
                                ai_start = time.time()

                                try:
                                    if hasattr(host, 'ai_detected'):
                                        ai_detector = functools.partial(host.ai_detected, channel, ai_type)
                                        ai_result = await self.run_in_executor_safe(hostname, ai_detector, ai_timeout)

                                        if ai_result:
                                            detected_events.append(ai_type)

                                    # Update stats
                                    ai_duration = time.time() - ai_start
                                    self.update_performance(hostname, f"ai_{ai_type}", ai_duration)

                                except Exception as e:
                                    cDebugDom("reolink")(f"AI {ai_type} check failed for {hostname}: {e}")
                                    continue

                        # For non-doorbell devices, also check visitor detection
                        if not is_doorbell and event_type != 'visitor':
                            visitor_timeout = self.get_adaptive_timeout(hostname, "visitor", 1.5)
                            visitor_start = time.time()

                            try:
                                if hasattr(host, 'visitor_detected'):
                                    visitor_detector = functools.partial(host.visitor_detected, channel)
                                    visitor_result = await self.run_in_executor_safe(hostname, visitor_detector, visitor_timeout)

                                    if visitor_result:
                                        detected_events.append("visitor")

                                visitor_duration = time.time() - visitor_start
                                self.update_performance(hostname, "visitor", visitor_duration)

                            except Exception as e:
                                cDebugDom("reolink")(f"Visitor check failed for {hostname}: {e}")

                    except Exception as e:
                        cWarningDom("reolink")(f"Error processing channel {channel} for {hostname}: {str(e)}")
                        continue

                    # Send detailed event information to Calaos
                    if detected_events:
                        # Mark camera as connected since we're receiving events
                        status_updated = self.safe_status_access(
                            lambda: self._mark_connected_if_received_events(hostname)
                        )

                        if status_updated:
                            cInfoDom("reolink")(f"Camera {hostname} appears to be back online (receiving events)")

                        for detected_event in detected_events:
                            try:
                                event_msg = {
                                    "event": "detection",
                                    "hostname": hostname,
                                    "event_type": detected_event,
                                    "channel": channel,
                                    "timestamp": datetime.now().isoformat(),
                                    "camera_name": getattr(host, 'camera_name', lambda x: f"Camera_{x}")(channel) if hasattr(host, 'camera_name') else f"Camera_{channel}",
                                    "tcp_push_active": host.baichuan.events_active,
                                    "callback_duration": time.time() - callback_start_time,
                                    "async_callback": True,
                                    "adaptive_timeout": global_timeout
                                }
                                self.send_message(json.dumps(event_msg))
                                cInfoDom("reolink")(f"Event detected on {hostname} channel {channel}: {detected_event}")
                            except Exception as e:
                                cErrorDom("reolink")(f"Error sending event message for {hostname}: {e}")

        except asyncio.TimeoutError:
            cWarningDom("reolink")(f"Complete callback timeout for {hostname} (timeout: {global_timeout:.1f}s)")
        except Exception as e:
            cErrorDom("reolink")(f"Critical error in async event callback for {hostname}: {str(e)}")
        finally:
            # Clean callback monitoring
            callback_duration = time.time() - callback_start_time
            with self._cameras_lock:
                self.callback_timeouts.pop(camera_key, None)

            # Update global callback stats
            self.update_performance(hostname, "callback", callback_duration)

            # Alert if callback took too long
            if callback_duration > 8.0:
                cWarningDom("reolink")(f"Async event callback for {hostname} took {callback_duration:.2f}s (very long)")
            elif callback_duration > 5.0:
                cDebugDom("reolink")(f"Async event callback for {hostname} took {callback_duration:.2f}s (long)")

    def _mark_connected_if_received_events(self, hostname):
        """Helper method to mark camera as connected when receiving events"""
        current_status = self.connection_status.get(hostname, True)
        if not current_status:
            self.connection_status[hostname] = True
            return True
        return False

    async def connect_camera(self, hostname, username, password, event_type):
        """Connect to a Reolink camera and set up Baichuan TCP event subscription"""
        try:
            cInfoDom("reolink")(f"Connecting to camera {hostname}")

            # Store camera configuration for reconnection with thread-safe access
            self.safe_config_access(lambda: self._store_camera_config(hostname, username, password, event_type))

            # Create host connection with timeout
            async with self.timeout_context(20, f"host connection {hostname}"):
                host = Host(hostname, username, password)
                await host.get_host_data()

            camera_key = f"{hostname}_{event_type}"

            # Store camera with thread-safe access
            self.safe_camera_access(lambda: self.registered_cameras.update({camera_key: host}))
            self.safe_status_access(lambda: self.connection_status.update({hostname: True}))

            cInfoDom("reolink")(f"Connected to camera {hostname}, setting up Baichuan TCP subscription for {event_type} events")

            # Set up Baichuan TCP push notification subscription with timeout
            tcp_push_supported = False
            try:
                async with self.timeout_context(15, f"event subscription {hostname}"):
                    await host.baichuan.subscribe_events()

                # IMPORTANT: Wait for TCP connection to establish
                await asyncio.sleep(2)  # Wait 2 seconds

                # Check now if TCP push is active
                if host.baichuan.events_active:
                    cInfoDom("reolink")(f"Baichuan TCP push active for camera {hostname}")
                    tcp_push_supported = True
                else:
                    cWarningDom("reolink")(f"Baichuan TCP push not active for camera {hostname}, retrying...")

                    # Attempt a reconnection
                    await host.baichuan.unsubscribe_events()
                    await asyncio.sleep(1)
                    await host.baichuan.subscribe_events()
                    await asyncio.sleep(2)

                    if host.baichuan.events_active:
                        cInfoDom("reolink")(f"Baichuan TCP push now active for {hostname} after retry")
                        tcp_push_supported = True
                    else:
                        cWarningDom("reolink")(f"Baichuan TCP push still not active for {hostname}")
                        # Mark for using polling
                        self.set_timer(f"force_polling_{hostname}", 1)
                        tcp_push_supported = False

            except Exception as e:
                cWarningDom("reolink")(f"Failed to setup TCP push for {hostname}: {e}")
                tcp_push_supported = False

            # Wait for connection to establish
            await asyncio.sleep(2)

            # Get available channels
            channels = getattr(host, 'channels', [0])  # Default to channel 0 if not available
            cInfoDom("reolink")(f"Camera {hostname} has channels: {channels}")

            # Optimize for doorbell devices
            model = getattr(host, 'model', '').lower()
            if "doorbell" in model or event_type == "visitor":
                cInfoDom("reolink")(f"Detected doorbell device: {model}")

                # Mark as doorbell for optimization
                self.set_timer(f"is_doorbell_{hostname}", 1)

                # Adjust timeouts for doorbell (responsiveness is important)
                self.update_performance(hostname, "visitor", 0.5)  # Fast baseline
                self.update_performance(hostname, "motion", 0.8)   # Motion secondary

            # Create event callback for all channels
            event_callback = self.create_event_callback(host, hostname, channels)

            # Create disconnect callback to detect when connection is lost - Secure version with circuit breaker
            def disconnect_callback():
                # Safety check to avoid calls after destruction
                if self.shutdown_requested:
                    return

                try:
                    cWarningDom("reolink")(f"Connection lost callback triggered for {hostname}")
                    with self._status_lock:
                        self.connection_status[hostname] = False

                    # Use circuit breaker to decide on automatic reconnection
                    circuit_breaker = self.circuit_breakers.get(hostname)
                    if circuit_breaker and circuit_breaker.can_attempt():
                        # Schedule a reconnection attempt after an intelligent delay
                        if self.event_loop and not self.event_loop.is_closed():
                            def schedule_reconnect():
                                if not self.shutdown_requested:
                                    cInfoDom("reolink")(f"Auto-scheduling reconnection for {hostname} via disconnect callback")
                                    asyncio.create_task(self.reconnect_camera(hostname))

                            # Delay based on circuit breaker state
                            delay = 5.0 if circuit_breaker.state == "closed" else 15.0
                            self.event_loop.call_later(delay, schedule_reconnect)
                    else:
                        cDebugDom("reolink")(f"Circuit breaker prevents auto-reconnection for {hostname}")

                except Exception as e:
                    cDebugDom("reolink")(f"Error in disconnect callback for {hostname}: {e}")

            # Register disconnect callback with Baichuan - Robust version
            try:
                if hasattr(host.baichuan, '_close_callback'):
                    original_close_callback = host.baichuan._close_callback

                    def enhanced_close_callback():
                        try:
                            disconnect_callback()
                            if original_close_callback and callable(original_close_callback):
                                original_close_callback()
                        except Exception as e:
                            cDebugDom("reolink")(f"Error in enhanced disconnect callback for {hostname}: {e}")

                    host.baichuan._close_callback = enhanced_close_callback
                    cDebugDom("reolink")(f"Enhanced disconnect callback registered for {hostname}")
            except Exception as e:
                cDebugDom("reolink")(f"Could not register disconnect callback for {hostname}: {str(e)}")

            # Register the callback with a unique ID for specific event types
            callback_id = f"{hostname}_{event_type}"

            # Register for motion and AI detection events (cmd_id=33) on all channels
            for channel in channels:
                try:
                    host.baichuan.register_callback(f"{callback_id}_ch{channel}", event_callback, cmd_id=33, channel=channel)
                except Exception as e:
                    cWarningDom("reolink")(f"Failed to register callback for {hostname} channel {channel}: {e}")

            # Also register for battery events if supported (cmd_id=252)
            try:
                if hasattr(host, 'battery_percent') and host.battery_percent(0) is not None:
                    battery_callback_id = f"{hostname}_battery"
                    host.baichuan.register_callback(battery_callback_id, event_callback, cmd_id=252, channel=0)
                    cDebugDom("reolink")(f"Registered battery events for {hostname}")
            except Exception as e:
                cDebugDom("reolink")(f"Battery events not supported for {hostname}: {str(e)}")

            # Register for privacy mode changes (cmd_id=623)
            try:
                privacy_callback_id = f"{hostname}_privacy"
                host.baichuan.register_callback(privacy_callback_id, event_callback, cmd_id=623, channel=None)
            except Exception as e:
                cDebugDom("reolink")(f"Privacy events registration failed for {hostname}: {str(e)}")

            # Check if TCP push is active
            if host.baichuan.events_active:
                cInfoDom("reolink")(f"Baichuan TCP push active for camera {hostname}")
            else:
                cWarningDom("reolink")(f"Baichuan TCP push not active for camera {hostname}")

            cInfoDom("reolink")(f"Successfully registered camera {hostname} for Baichuan {event_type} events")

        except asyncio.TimeoutError:
            cErrorDom("reolink")(f"Timeout connecting to camera {hostname}")
            error_msg = {
                "status": "error",
                "message": f"Timeout connecting to camera {hostname}"
            }
            self.send_message(json.dumps(error_msg))
        except Exception as e:
            cErrorDom("reolink")(f"Failed to connect to camera {hostname}: {str(e)}")
            error_msg = {
                "status": "error",
                "message": f"Failed to connect to camera {hostname}: {str(e)}"
            }
            self.send_message(json.dumps(error_msg))

    def _store_camera_config(self, hostname, username, password, event_type):
        """Helper method to store camera configuration"""
        self.camera_configs[hostname] = {
            'hostname': hostname,
            'username': username,
            'password': password,
            'event_type': event_type
        }

    async def check_events_periodically(self):
        """Periodically check for Baichuan events as fallback when callbacks don't work"""
        cDebugDom("reolink")("Starting periodic event checking as fallback")
        fallback_error_count = 0
        max_fallback_errors = 3

        while not self.shutdown_requested:
            try:
                # Sleep for a longer interval to reduce spam - callbacks should handle most events
                await asyncio.sleep(30)  # Increased from 5 to 30 seconds

                # Thread-safe access to cameras
                cameras_to_check = self.safe_camera_access(lambda: list(self.registered_cameras.items()))

                # Only check if we have registered cameras
                if not cameras_to_check:
                    continue

                for camera_key, host in cameras_to_check:
                    try:
                        hostname, event_type = camera_key.split("_", 1)

                        # Only use polling if TCP push is not active and camera is marked as disconnected
                        tcp_active = getattr(host.baichuan, 'events_active', False)
                        is_connected = self.safe_status_access(lambda: self.connection_status.get(hostname, True))

                        if not tcp_active and not is_connected:
                            # Only log once every 5 minutes to avoid spam
                            current_time = time.time()
                            last_log_key = f"fallback_log_{hostname}"
                            last_log_time = self.get_timer(last_log_key, 0)

                            if current_time - last_log_time > 300:  # 5 minutes
                                cDebugDom("reolink")(f"Fallback: checking events for {hostname} (TCP push not active)")
                                self.set_timer(last_log_key, current_time)

                            # Simple motion check as fallback
                            if event_type == "motion":
                                try:
                                    num_channels = getattr(host, 'num_channels', 1)
                                    for channel in range(num_channels):
                                        motion_detected = False
                                        if hasattr(host, 'motion_detected'):
                                            motion_detector = functools.partial(host.motion_detected, channel)
                                            motion_detected = await self.run_in_executor_safe(hostname, motion_detector, 5.0)

                                        if motion_detected:
                                            cInfoDom("reolink")(f"Fallback: Motion detected on {hostname} channel {channel}")
                                            event_msg = {
                                                "event": "detection",
                                                "hostname": hostname,
                                                "event_type": "motion",
                                                "channel": channel,
                                                "timestamp": datetime.now().isoformat(),
                                                "fallback": True,
                                                "tcp_push_active": tcp_active
                                            }
                                            self.send_message(json.dumps(event_msg))
                                            break
                                except Exception as e:
                                    cDebugDom("reolink")(f"Error in fallback motion check for {hostname}: {str(e)}")

                    except Exception as e:
                        cWarningDom("reolink")(f"Error processing camera {camera_key} in periodic check: {e}")

            except Exception as e:
                fallback_error_count += 1
                cErrorDom("reolink")(f"Error in periodic event check ({fallback_error_count}/{max_fallback_errors}): {str(e)}")

                if fallback_error_count >= max_fallback_errors:
                    cCriticalDom("reolink")("Too many errors in fallback event checking, forcing restart")
                    self.force_restart()
                    break

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
            if self.event_loop and not self.event_loop.is_closed():
                cDebugDom("reolink")(f"Scheduling camera connection for {hostname}")
                try:
                    future = asyncio.run_coroutine_threadsafe(
                        self.connect_camera(hostname, username, password, event_type),
                        self.event_loop
                    )
                    # Add to background tasks for monitoring
                    self.background_tasks.append(future)
                    cDebugDom("reolink")(f"Camera connection scheduled successfully for {hostname}")

                    # Clean tasks if we have too many
                    if len(self.background_tasks) > 50:
                        self.cleanup_completed_tasks()

                except Exception as e:
                    cErrorDom("reolink")(f"Failed to schedule camera connection: {str(e)}")
            else:
                cErrorDom("reolink")("Event loop not initialized or closed")
        elif msg_data.get("action") == "health_check":
            # Respond to health check requests
            current_time = time.time()
            active_callback_tasks = len([task for task in self.callback_tasks.values() if not task.done()])
            health_msg = {
                "status": "healthy" if current_time - self.last_health_check < 60 else "unhealthy",
                "message": "Reolink client health status",
                "cameras_count": len(self.safe_camera_access(lambda: self.registered_cameras)),
                "connected_cameras": len([status for status in self.safe_status_access(lambda: self.connection_status.values()) if status]),
                "background_tasks": len(self.background_tasks),
                "callback_tasks": active_callback_tasks,
                "active_timers": len(self._dynamic_timers),
                "performance_stats": len(self.camera_performance),
                "performance_timestamps": len(self._perf_timestamps),
                "circuit_breakers": {hostname: cb.get_status() for hostname, cb in self.circuit_breakers.items()},
                "last_health_check": current_time - self.last_health_check,
                "uptime": current_time - getattr(self, '_start_time', current_time),
                "restart_cooldown_remaining": max(0, self.min_restart_interval - (current_time - self.restart_cooldown)),
                "executor_active": self._executor is not None,
                "memory_optimization": {
                    "functools_partial_used": True,
                    "thread_safe_timers": True,
                    "bounded_performance_metrics": True,
                    "error_classification": True
                }
            }
            self.send_message(json.dumps(health_msg))
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