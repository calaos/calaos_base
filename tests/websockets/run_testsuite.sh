#!/bin/sh

mkdir -p reports
wstest -m fuzzingclient -s fuzzingclient.json
