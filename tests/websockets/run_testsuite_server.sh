#!/bin/sh
# test server websocket implementation
#
# fuzzingclient.json excludes 9.1.5, 9.1.6, 9.2.5 and 9.2.6: those send 8 MiB
# and 16 MiB messages, and the server refuses anything over 4 MiB with a 1009
# close on purpose (WebSocketFrame::MAX_FRAME_SIZE_IN_BYTES and
# TransportLimits::MaxWebsocketMessageSize). Every other 9.x case stays at
# 4 MiB or below and is expected to pass.

mkdir -p reports
wstest -m fuzzingclient -s fuzzingclient.json
