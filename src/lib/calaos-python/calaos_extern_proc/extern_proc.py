import socket
import select
import os
import argparse
from .message import ExternProcMessage
from .logger import (
    cDebugDom,
    cInfoDom,
    cWarningDom,
    cErrorDom,
    cCriticalDom
)

class ExternProcClient:
    def __init__(self):
        self.sockfd = -1
        self.recv_buffer = bytearray()
        self.current_frame = ExternProcMessage()
        self.user_fds = []
        self.name = "extern_process"
        self.sockpath = ""

        #Calaos config/cache paths passed to process by environment variables
        self.cachePath = os.environ.get('CALAOS_CACHE_PATH', ".")
        self.configPath = os.environ.get('CALAOS_CONFIG_PATH', ".")

    def parse_arguments(self):
        parser = argparse.ArgumentParser()
        parser.add_argument('--socket', required=True, help='Socket path')
        parser.add_argument('--namespace', help='Namespace')
        args, unknown = parser.parse_known_args()

        self.sockpath = args.socket
        if args.namespace:
            self.name = args.namespace

    def connect_socket(self):
        if not os.path.exists(self.sockpath):
            cCriticalDom("ExternProcClient")(f"Socket path {self.sockpath} not found")
            return False

        try:
            self.sockfd = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.sockfd.connect(self.sockpath)
            return True
        except Exception as e:
            cCriticalDom("ExternProcClient")(f"Connect failed: {str(e)}")
            return False

    def process_socket_recv(self):
        try:
            data = self.sockfd.recv(4096)
            if not data:
                return False

            cDebugDom("ExternProcClient")(f"Received {len(data)} bytes")
            self.recv_buffer.extend(data)

            while True:
                # Process one frame
                frame_processed = self.current_frame.process_frame_data(self.recv_buffer)

                if not frame_processed:
                    # Not enough data for a complete frame
                    break

                if self.current_frame.isvalid:
                    self.message_received(self.current_frame.payload)

                # Calculate how many bytes were consumed
                frame_size = 5 + self.current_frame.payload_length  # header(5) + payload
                self.recv_buffer = self.recv_buffer[frame_size:]

                # Clear frame for next message
                self.current_frame.clear()

                # If no more data in buffer, stop processing
                if len(self.recv_buffer) < 5:  # Minimum frame size (header)
                    break

            return True

        except Exception as e:
            cErrorDom("ExternProcClient")(f"Error reading socket: {str(e)}")
            return False

    def run(self, timeout_ms):
        quit_loop = False

        while not quit_loop:
            read_list = [self.sockfd] + self.user_fds
            try:
                readable, _, _ = select.select(read_list, [], [], timeout_ms/1000.0)

                if not readable:
                    self.read_timeout()
                    continue

                for fd in readable:
                    if fd == self.sockfd:
                        if not self.process_socket_recv():
                            quit_loop = True
                            break
                    else:
                        if not self.handle_fd_set(fd):
                            quit_loop = True
                            break

            except Exception as e:
                cCriticalDom("ExternProcClient")(f"Error in run loop: {str(e)}")
                quit_loop = True

    def stop(self):
        self._running = False

    def send_message(self, data):
        msg = ExternProcMessage(data)
        frame = msg.get_raw_data()
        try:
            self.sockfd.send(frame)
        except Exception as e:
            cErrorDom("ExternProcClient")(f"Error writing to socket: {str(e)}")

    # Methods to be implemented by child classes
    def setup(self):
        return True

    def read_timeout(self):
        pass

    def message_received(self, msg):
        cDebugDom("ExternProcClient")(f"Received message (unhandeld): {msg}")
        pass

    def handle_fd_set(self, fd):
        return True