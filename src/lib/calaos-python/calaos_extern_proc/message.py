# calaos_extern_proc/message.py
import struct
from enum import Enum

class MessageType(Enum):
    TypeUnknown = 0x00
    TypeMessage = 0x21  # Changed to match C++ version (0x21)

class MessageState(Enum):
    StateReadHeader = 0
    StateReadPayload = 1

class ExternProcMessage:
    def __init__(self, data=None):
        self.clear()
        if data is not None:
            self.payload = data
            self.payload_length = len(data)
            self.isvalid = True
            self.opcode = MessageType.TypeMessage

    def clear(self):
        self.payload = ""
        self.payload_length = 0
        self.isvalid = False
        self.opcode = MessageType.TypeUnknown
        self.state = MessageState.StateReadHeader

    def process_frame_data(self, data):
        finished = False

        while data and not finished:
            if self.state == MessageState.StateReadHeader:
                if len(data) >= 5:  # Header is 5 bytes (1 byte type + 4 bytes size)
                    # Read header type (1 byte)
                    self.opcode = data[0]
                    # Read length (4 bytes, big endian)
                    self.payload_length = (
                        (data[1] << 24) |
                        (data[2] << 16) |
                        (data[3] << 8) |
                        data[4]
                    )
                    data = data[5:]

                    if self.opcode == MessageType.TypeMessage.value:
                        self.isvalid = True
                        self.state = MessageState.StateReadPayload
                    else:
                        self.isvalid = False
                        finished = False
                        self.state = MessageState.StateReadHeader
                else:
                    return False

            elif self.state == MessageState.StateReadPayload:
                if not self.payload_length:
                    finished = True
                    self.state = MessageState.StateReadHeader
                else:
                    if len(data) >= self.payload_length:
                        self.payload = data[:self.payload_length].decode('utf-8')
                        data = data[self.payload_length:]
                        finished = True
                        self.state = MessageState.StateReadHeader
                    else:
                        return False

        return finished

    def get_raw_data(self):
        frame = bytearray()
        # Add type byte
        frame.append(self.opcode.value if isinstance(self.opcode, MessageType) else self.opcode)
        # Add length as 4 bytes, big endian
        frame.append((self.payload_length >> 24) & 0xFF)
        frame.append((self.payload_length >> 16) & 0xFF)
        frame.append((self.payload_length >> 8) & 0xFF)
        frame.append(self.payload_length & 0xFF)
        # Add payload data
        frame.extend(self.payload.encode('utf-8'))
        return bytes(frame)
