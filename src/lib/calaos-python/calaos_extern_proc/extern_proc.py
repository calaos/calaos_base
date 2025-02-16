import socket
import select
import os
import argparse
from .message import ExternProcMessage

class ExternProcClient:
    def __init__(self):
        self.sockfd = -1
        self.recv_buffer = bytearray()
        self.current_frame = ExternProcMessage()
        self.user_fds = []
        self.name = "extern_process"
        self.sockpath = ""

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
            print(f"Socket path {self.sockpath} not found")
            return False

        try:
            self.sockfd = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.sockfd.connect(self.sockpath)
            return True
        except Exception as e:
            print(f"Connect failed: {str(e)}")
            return False

    def process_socket_recv(self):
        try:
            data = self.sockfd.recv(4096)
            if not data:
                return False

            self.recv_buffer.extend(data)
            while self.current_frame.process_frame_data(self.recv_buffer):
                if self.current_frame.isvalid:
                    self.message_received(self.current_frame.payload)
                    self.current_frame.clear()

            return True
        except Exception as e:
            print(f"Error reading socket: {str(e)}")
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
                print(f"Error in run loop: {str(e)}")
                quit_loop = True

    def stop(self):
        self._running = False

    def send_message(self, data):
        msg = ExternProcMessage(data)
        frame = msg.get_raw_data()
        try:
            self.sockfd.send(frame)
        except Exception as e:
            print(f"Error writing to socket: {str(e)}")

    # Methods to be implemented by child classes
    def setup(self):
        return True

    def read_timeout(self):
        pass

    def message_received(self, msg):
        pass

    def handle_fd_set(self, fd):
        return True