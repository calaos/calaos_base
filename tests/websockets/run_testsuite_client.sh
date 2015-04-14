#!/bin/sh
#test client websocket

mkdir -p reports
wstest -m fuzzingserver
