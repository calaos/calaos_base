#!/bin/env python3

from calaos_extern_proc import cDebugDom, cInfoDom, cErrorDom, cCriticalDom, cWarningDom, configure_logger, ExternProcClient
from roonapi import RoonApi, RoonDiscovery
import time
import argparse
import json

class RoonClient(ExternProcClient):
    def __init__(self):
        super().__init__()
        self.roon_api = None
        self.roon_host = ""
        self.roon_port = 9330
        self.roon_token = ""
        self.roon_core_id = ""
        self.cli_list_zones = False

        # list of zone id to subscribe to
        self.subscribed_zones = []

        self.appinfo = {
            "extension_id": "calaos_roon_extension",
            "display_name": "Calaos Roon Extension",
            "display_version": "1.0.0",
            "publisher": "Calaos",
            "email": "team@calaos.fr",
        }

    def stop(self):
        if self.roon_api:
            self.roon_api.stop()
        return super().stop()

    def parse_arguments(self):
        parser = argparse.ArgumentParser(description='Calaos Roon Extension')

        # Define all possible arguments
        parser.add_argument('--socket', help='Socket path')
        parser.add_argument('--namespace', help='Namespace')
        parser.add_argument('--list', action='store_true', help='List all zones')
        parser.add_argument('--host', help='Roon host')
        parser.add_argument('--port', help='Roon port', default=9330)

        args = parser.parse_args()

        # Check if at least one mode is selected
        if not (args.list or args.socket or args.namespace):
            parser.error('Must specify either --list or (--socket and --namespace)')

        # Check for invalid combinations
        if args.list and (args.socket or args.namespace):
            parser.error('--list cannot be combined with --socket or --namespace')

        # Check that socket and namespace are used together
        if bool(args.socket) != bool(args.namespace):
            parser.error('--socket and --namespace must be used together')

        if not args.list:
            self.sockpath = args.socket
            self.name = args.namespace

        self.cli_list_zones = args.list
        self.roon_host = args.host
        self.roon_port = args.port

    def setup(self):
        self.parse_arguments()

        if not self.cli_list_zones:
            if not self.connect_socket():
                cCriticalDom("roon")("Failed to connect to socket")
                return False

        def get_roon_host():
            if self.roon_host:
                return (self.roon_host, self.roon_port)

            discover = RoonDiscovery(None)
            servers = discover.first()
            discover.stop()

            return (servers[0], servers[1]) if servers else (None, None)

        host, port = get_roon_host()
        if not host:
            cCriticalDom("roon")("No Roon server found")
            return False

        cInfoDom("roon")("Using Roon server: %s:%s", host, port)

        # read the core_id and token from the file from self.cachePath
        try:
            with open(f"{self.cachePath}/roon_core_id", "r") as f:
                self.roon_core_id = f.read()
            with open(f"{self.cachePath}/roon_token", "r") as f:
                self.roon_token = f.read()

            self.roon_api = RoonApi(self.appinfo, self.roon_token, host, port, True)
            self.roon_core_id = self.roon_api.core_id
            self.roon_token = self.roon_api.token

        except Exception as e:
            cWarningDom("roon")("No core_id or token found in cache, please authorize the extension in Roon to enable Calaos Roon Extension")

            self.roon_api = RoonApi(self.appinfo, None, host, port, False)

            auth_api = []
            while len(auth_api) == 0:
                time.sleep(1)
                auth_api = [api for api in [self.roon_api] if api.token is not None]
            api = auth_api[0]

            cDebugDom("roon")("Got authorisation : %s %s %s", api.host, api.core_name, api.core_id)

            self.roon_core_id = api.core_id
            self.roon_token = api.token

            #wait until the extension is connected
            while not self.roon_api.ready:
                time.sleep(1)

        # save the core_id and token to the file for later reconnect
        with open(f"{self.cachePath}/roon_core_id", "w") as f:
            f.write(self.roon_core_id)
        with open(f"{self.cachePath}/roon_token", "w") as f:
            f.write(self.roon_token)

        cInfoDom("roon")("Connected to Roon server %s", self.roon_api.core_name)
        zones = self.roon_api.zones
        cInfoDom("roon")("Zones available:")
        for zone_id, zone in zones.items():
            cInfoDom("roon")(f"Zone: {zone_id} [{zone.get('display_name', 'Unknown')}]")

        return True

    def run(self, timeout_ms):
        self.roon_api.register_state_callback(self.roon_state_received)
        return super().run(timeout_ms)

    def roon_state_received(self, event, changed_ids):
        """ Callback for Roon state changes """
        print("Event: %s" % event)
        for zone_id in changed_ids:
            if zone_id not in self.roon_api.zones:
                # Zone has been removed?
                continue

            zone = self.roon_api.zones[zone_id]

            print(zone["now_playing"].get("seek_position", 0))

            if zone_id in self.subscribed_zones:
                # Send the zone state to the extern process using json
                z = self.parse_cover_url(zone)
                self.send_message(json.dumps(z))

    def message_received(self, message):
        cDebugDom("roon")(f"Received message: {message}")

        # Parse the JSON message
        try:
            message = json.loads(message)
        except Exception as e:
            cErrorDom("roon")("Failed to parse JSON message: %s", str(e))
            return

        if message.get("action") == "subscribe":
            zid = message.get("zone_id")
            if zid and zid in self.roon_api.zones:
                self.subscribed_zones.append(zid)

                # Send the initial zone state to the extern process using json
                zone = self.parse_cover_url(self.roon_api.zones[zid])
                self.send_message(json.dumps(zone))
            elif zid:
                cErrorDom("roon")(f"Zone ID {zid} not found in Roon")
            else:
                cErrorDom("roon")("No zone_id in message")

        elif message.get("action") == "play" or message.get("action") == "pause" or message.get("action") == "stop" or message.get("action") == "next" or message.get("action") == "previous" or message.get("action") == "play_pause" or message.get("action") == "next":
            zid = message.get("zone_id")
            if zid and zid in self.roon_api.zones:
                self.roon_api.playback_control(zid, message.get("action"))
            elif zid:
                cErrorDom("roon")(f"Zone ID {zid} not found in Roon")
            else:
                cErrorDom("roon")("No zone_id in message")

        elif message.get("action") == "set_volume":
            zid = message.get("zone_id")
            volume = message.get("volume")

            # get output_id from first output of zone
            outputs = self.roon_api.zones[zid].get("outputs")
            output_id = None
            if outputs:
                output_id = outputs[0].get("output_id")

            if zid and output_id and zid in self.roon_api.zones:
                self.roon_api.set_volume_percent(output_id, volume)
            elif zid:
                cErrorDom("roon")(f"Zone ID {zid} not found in Roon")
            else:
                cErrorDom("roon")("No zone_id in message")

    def parse_cover_url(self, zone):
        try:
            now_playing_data = zone["now_playing"]
            image_id = now_playing_data.get("image_key")
            cover_url = self.roon_api.get_image(image_id)
        except KeyError:
            pass
        else:
            zone["cover_url"] = cover_url

        return zone

if __name__ == "__main__":
    configure_logger()
    cInfoDom("roon")("Starting Roon control client")

    client = RoonClient()
    if client.setup():
        if client.cli_list_zones:
            exit(0)

        client.run(200)
    else:
        cCriticalDom("roon")("Failed to setup RoonClient")
        exit(1)
