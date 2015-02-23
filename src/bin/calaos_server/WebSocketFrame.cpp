/******************************************************************************
 **  Copyright (c) 2006-2015, Calaos. All Rights Reserved.
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
#include "WebSocketFrame.h"
#include "WebSocket.h"

const uint64_t MAX_FRAME_SIZE_IN_BYTES = INT_MAX - 1;

WebSocketFrame::WebSocketFrame()
{
    clear();
}

void WebSocketFrame::clear()
{
    state = StateReadHeader;
    closeCode = WebSocket::CloseCodeNormal;
    finalFrame = true;
    mask = 0;
    rsv1 = rsv2 = rsv3 = 0;
    opcode = OpCodeClose;
    payload_length = 0;
    isvalid = false;
    maskbit = false;
}

void WebSocketFrame::checkValid()
{
    if (rsv1 || rsv2 || rsv3)
    {
        closeCode = WebSocket::CloseCodeProtocolError;
        closeReason = "RSV fields are non zero";
        isvalid = false;
    }
    else if (isOpCodeReserved())
    {
        closeCode = WebSocket::CloseCodeProtocolError;
        closeReason = "Use of reserved opcode";
        isvalid = false;
    }
    else if (isControlFrame())
    {
        if (payload_length > 125)
        {
            closeCode = WebSocket::CloseCodeProtocolError;
            closeReason = "Control frame is too big";
            isvalid = false;
        }
        else if (!isFinalFrame())
        {
            closeCode = WebSocket::CloseCodeProtocolError;
            closeReason = "Control frame cannot be fragmented";
            isvalid = false;
        }
        else
            isvalid = true;
    }
    else
        isvalid = true;
}

bool WebSocketFrame::processFrameData(string &data)
{
    bool finished = false;

    while (!data.empty() && !finished)
    {
        switch(state)
        {
        case StateReadHeader:
        {
            if (data.size() >= 2)
            {
                //first byte read FIN, RSV*, opcode
                finalFrame = (uint8_t(data[0]) & 0x80) != 0;
                rsv1 = (uint8_t(data[0]) & 0x40);
                rsv2 = (uint8_t(data[0]) & 0x20);
                rsv3 = (uint8_t(data[0]) & 0x10);
                opcode = (uint8_t(data[0]) & 0x0F);

                //second byte, read mask payload length
                maskbit = (uint8_t(data[1]) & 0x80) != 0;
                payload_length = (uint8_t(data[1]) & 0x7F);

                data.erase(0, 2);

                if (payload_length == 126)
                    state = StateReadPayloadLength;
                else if (payload_length == 127)
                    state = StateReadBigPayloadLength;
                else
                    state = maskbit? StateReadMask:StateReadPayload;

                checkValid();
                if (closeCode != WebSocket::CloseCodeNormal)
                {
                    finished = true;
                    state = StateReadHeader;
                }
            }
            else
                return false;
            break;
        }
        case StateReadPayloadLength:
        {
            if (data.size() >= 2)
            {
                payload_length = (uint8_t(data[0]) << 8) | uint8_t(data[1]);
                data.erase(0, 2);

                if (payload_length < 126)
                {
                    closeCode = WebSocket::CloseCodeProtocolError;
                    closeReason = "Frame smaller than 126 must be expressed as one byte";
                    isvalid = false;
                    finished = true;
                    state = StateReadHeader;
                }
                else
                    state = maskbit? StateReadMask:StateReadPayload;
            }
            else
                return false;
            break;
        }
        case StateReadBigPayloadLength:
        {
            if (data.size() >= 8)
            {
                payload_length =
                        (uint64_t(uint8_t(data[0])) << 56) |
                        (uint64_t(uint8_t(data[1])) << 48) |
                        (uint64_t(uint8_t(data[2])) << 40) |
                        (uint64_t(uint8_t(data[3])) << 32) |
                        (uint8_t(data[4]) << 24) |
                        (uint8_t(data[5]) << 16) |
                        (uint8_t(data[6]) << 8) |
                        uint8_t(data[7]);
                data.erase(0, 8);

                uint64_t v = 1;
                if (payload_length & (v << 63))
                {
                    closeCode = WebSocket::CloseCodeProtocolError;
                    closeReason = "Highest bit of payload length is not 0";
                    isvalid = false;
                    finished = true;
                    state = StateReadHeader;
                }
                else if (payload_length <= 0xFFFFu)
                {
                    closeCode = WebSocket::CloseCodeProtocolError;
                    closeReason = "Frame smaller than 65536 (2^16) must be expressed as 2 bytes";
                    isvalid = false;
                    finished = true;
                    state = StateReadHeader;
                }
                else
                    state = maskbit? StateReadMask:StateReadPayload;
            }
            else
                return false;
            break;
        }
        case StateReadMask:
        {
            if (data.size() >= 4)
            {
                std::stringstream stream;
                stream << std::hex << uint32_t(data[0]) << "-" << uint32_t(data[1]) << "-" << uint32_t(data[2]) << "-" << uint32_t(data[3]);
                cDebugDom("websocket") << "Reading mask: " << stream.str();

                mask = (uint8_t(data[0]) << 24) |
                       (uint8_t(data[1]) << 16) |
                       (uint8_t(data[2]) << 8) |
                       (uint8_t(data[3]));
                data.erase(0, 4);

                state = StateReadPayload;
            }
            else
                return false;
            break;
        }
        case StateReadPayload:
        {
            if (!payload_length)
            {
                finished = true;
                state = StateReadHeader;
                break;
            }
            else if (payload_length > MAX_FRAME_SIZE_IN_BYTES)
            {
                closeCode = WebSocket::CloseCodeTooMuchData;
                closeReason = "Maximum framesize exceeded";
                isvalid = false;
                finished = true;
                state = StateReadHeader;
            }
            else
            {
                if (data.size() >= payload_length)
                {
                    payload = data.substr(0, payload_length);
                    data.erase(0, payload_length);

                    if (maskbit)
                        processMask();

                    finished = true;
                    state = StateReadHeader;
                }
                else
                    return false;
            }

            break;
        }
        default:
            break;
        }
    }

    return finished;
}

void WebSocketFrame::processMask()
{
    //unmask payload
    const uint8_t m[] = { uint8_t((mask & 0xFF000000u) >> 24),
                          uint8_t((mask & 0x00FF0000u) >> 16),
                          uint8_t((mask & 0x0000FF00u) >> 8),
                          uint8_t((mask & 0x000000FFu))
                        };

    char *p = (char *)payload.c_str();
    int i = 0;
    uint64_t size = payload.size();
    std::stringstream stream;
    stream << std::hex << mask;
    cDebugDom("websocket") << "payload size: " << size << " mask: " << stream.str();
    while (size-- > 0)
        *p++ ^= m[i++ % 4];
}
