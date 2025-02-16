# calaos_extern_proc/message.py
import struct
from enum import Enum

class MessageType(Enum):
    TypeUnknown = 0
    TypeMessage = 1

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
                if len(data) >= 5:
                    # Read header
                    self.opcode = data[0]
                    # Read length
                    self.payload_length = struct.unpack('!I', data[1:5])[0]
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
        frame.append(self.opcode.value if isinstance(self.opcode, MessageType) else self.opcode)
        frame.extend(struct.pack('!I', self.payload_length))
        frame.extend(self.payload.encode('utf-8'))
        return frame
