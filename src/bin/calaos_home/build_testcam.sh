#!/bin/bash

PKG=$(pkg-config --cflags --libs elementary evas ecore-con sigc++-2.0)
SRC="test_cam.cpp \
CalaosCameraView.cpp \
EvasSmart.cpp \
../../lib/Utils.cpp ../../lib/Params.cpp \
../../lib/TinyXML/tinyxmlparser.cpp \
../../lib/TinyXML/tinyxmlerror.cpp \
../../lib/TinyXML/tinyxml.cpp \
../../lib/TinyXML/tinystr.cpp \
../../lib/tcpsocket.cpp \
../../lib/EcoreTimer.cpp \
b64.o
"
gcc -g -c -o b64.o ../../lib/base64.c -I../../lib
g++ -g -std=c++11 -o test_cam $SRC $PKG -I../../lib  -I.  -DETC_DIR=""