#!/bin/sh
# test server websocket implementation

mkdir -p reports
wstest -m fuzzingclient -s fuzzingclient.json
