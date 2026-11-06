# T1.8 — ExternProcClient quality fixes:
#  * stop() must actually interrupt the run() loop (was dead code)
#  * send_message() must use sendall() (send() may write partially)
#  * framing must count UTF-8 bytes, not str characters
import socket
import threading
import time

import pytest

pytest.importorskip("colorama")

from calaos_extern_proc.extern_proc import ExternProcClient
from calaos_extern_proc.message import ExternProcMessage


class FakePartialSocket:
    """A socket whose send() only ever writes half the buffer."""

    def __init__(self):
        self.data = b""

    def send(self, b):
        n = max(1, len(b) // 2)
        self.data += bytes(b[:n])
        return n

    def sendall(self, b):
        self.data += bytes(b)
        return None


def test_stop_interrupts_run_loop():
    a, b = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        client = ExternProcClient()
        client.sockfd = a
        t = threading.Thread(target=client.run, args=(50,), daemon=True)
        t.start()
        time.sleep(0.15)
        assert t.is_alive()
        client.stop()
        t.join(timeout=2.0)
        assert not t.is_alive(), "stop() did not interrupt the run() loop"
    finally:
        a.close()
        b.close()


def test_send_message_uses_sendall_full_frame():
    client = ExternProcClient()
    fake = FakePartialSocket()
    client.sockfd = fake
    payload = "x" * 1000
    client.send_message(payload)
    expected = ExternProcMessage(payload).get_raw_data()
    assert fake.data == expected, "frame truncated: send() used instead of sendall()"


def test_framing_roundtrip_over_socketpair():
    a, b = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        sender = ExternProcClient()
        sender.sockfd = a
        receiver = ExternProcClient()
        receiver.sockfd = b
        got = []
        receiver.message_received = got.append

        sender.send_message('{"msg": "hello"}')
        sender.send_message("second")
        deadline = time.monotonic() + 2.0
        while len(got) < 2 and time.monotonic() < deadline:
            assert receiver.process_socket_recv()
        assert got == ['{"msg": "hello"}', "second"]
    finally:
        a.close()
        b.close()


def test_framing_counts_utf8_bytes_not_characters():
    payload = "température: 21°C"  # len(str) < len(utf-8 bytes)
    msg = ExternProcMessage(payload)
    raw = msg.get_raw_data()
    encoded = payload.encode("utf-8")
    declared_len = (raw[1] << 24) | (raw[2] << 16) | (raw[3] << 8) | raw[4]
    assert declared_len == len(encoded)
    assert raw[5:] == encoded

    # And it must parse back cleanly
    parsed = ExternProcMessage()
    assert parsed.process_frame_data(bytearray(raw))
    assert parsed.isvalid
    assert parsed.payload == payload
