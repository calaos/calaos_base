/******************************************************************************
 **  Copyright (c) 2007-2015, Calaos. All Rights Reserved.
 **
 **  This file is part of Calaos.
 **
 **  Calaos is free software; you can redistribute it and/or modify
 **  it under the terms of the GNU General Public License as published by
 **  the Free Software Foundation; either version 3 of the License, or
 **  (at your option) any later version.
 **
 **  Calaos is distributed in the hope that it will be useful,
 **  but WITHOUT ANY WARRANTY; without even the implied warranty of
 **  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 **  GNU General Public License for more details.
 **
 **  You should have received a copy of the GNU General Public License
 **  along with Foobar; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/
#ifndef S_WebSocketFrame_H
#define S_WebSocketFrame_H

#include "Calaos.h"

using namespace Calaos;

class WebSocketFrame
{
public:
    WebSocketFrame();

    int getCloseCode() const { return closeCode; }
    string getCloseReason() const { return closeReason; }
    bool isFinalFrame() const { return finalFrame; }
    bool isControlFrame() const { return (opcode & 0x08) == 0x08; }
    bool isDataFrame() const { return !isControlFrame(); }
    bool isContinuationFrame() const { return isDataFrame() && (opcode == OpCodeContinue); }
    bool hasMask() const { return mask != 0; }
    uint32_t getMask() const { return mask; }
    int getRsv1() const { return rsv1; }
    int getRsv2() const { return rsv2; }
    int getRsv3() const { return rsv3; }
    int getOpcode() const { return opcode; }
    string getPayload() const { return payload; }
    bool isOpCodeReserved() { return ((opcode > OpCodeBinary) && (opcode < OpCodeClose)) || (opcode > OpCodePong); }

    void clear();

    bool isValid() const { return isvalid; }

    bool processFrameData(string &data);

private:
    enum OpCode
    {
        OpCodeContinue    = 0x0,
        OpCodeText        = 0x1,
        OpCodeBinary      = 0x2,
        OpCodeReserved3   = 0x3,
        OpCodeReserved4   = 0x4,
        OpCodeReserved5   = 0x5,
        OpCodeReserved6   = 0x6,
        OpCodeReserved7   = 0x7,
        OpCodeClose       = 0x8,
        OpCodePing        = 0x9,
        OpCodePong        = 0xA,
        OpCodeReservedB   = 0xB,
        OpCodeReservedC   = 0xC,
        OpCodeReservedD   = 0xD,
        OpCodeReservedE   = 0xE,
        OpCodeReservedF   = 0xF
    };

    enum
    {
        StateReadHeader,
        StateReadPayloadLength,
        StateReadBigPayloadLength,
        StateReadMask,
        StateReadPayload,
        StateNeedMoreData,
    };
    int state;

    int closeCode;
    string closeReason;
    bool finalFrame;
    uint32_t mask;
    int rsv1;
    int rsv2;
    int rsv3;
    int opcode;
    bool maskbit;

    uint64_t payload_length;
    string payload;

    bool isvalid;

    void checkValid();
    void processMask();
};

#endif
