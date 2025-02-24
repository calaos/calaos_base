/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
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

const uint64_t MAX_FRAME_SIZE_IN_BYTES = INT_MAX - 1;

WebSocketFrame::WebSocketFrame()
{
    clear();
}

void WebSocketFrame::clear()
{
    state = StateReadHeader;
    closeCode = CloseCodeNormal;
    closeReason.clear();
    finalFrame = true;
    mask = 0;
    rsv1 = rsv2 = rsv3 = 0;
    opcode = OpCodeClose;
    payload_length = 0;
    payload.clear();
    isvalid = false;
    maskbit = false;
    haserror = false;
}

void WebSocketFrame::checkValid()
{
    if (rsv1 || rsv2 || rsv3)
    {
        closeCode = CloseCodeProtocolError;
        closeReason = "RSV fields are non zero";
        isvalid = false;
        haserror = true;
    }
    else if (isOpCodeReserved())
    {
        closeCode = CloseCodeProtocolError;
        closeReason = "Use of reserved opcode";
        isvalid = false;
        haserror = true;
    }
    else if (isControlFrame())
    {
        if (payload_length > 125)
        {
            closeCode = CloseCodeProtocolError;
            closeReason = "Control frame is too big";
            isvalid = false;
            haserror = true;
        }
        else if (!isFinalFrame())
        {
            closeCode = CloseCodeProtocolError;
            closeReason = "Control frame cannot be fragmented";
            isvalid = false;
            haserror = true;
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

                //Handle 0 length payload here for client
                if (state == StateReadPayload && payload_length == 0)
                {
                    cDebugDom("websocket") << "0 payload length";
                    haserror = true;
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
                    closeCode = CloseCodeProtocolError;
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
                    closeCode = CloseCodeProtocolError;
                    closeReason = "Highest bit of payload length is not 0";
                    isvalid = false;
                    finished = true;
                    state = StateReadHeader;
                }
                else if (payload_length <= 0xFFFFu)
                {
                    closeCode = CloseCodeProtocolError;
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
                mask = (uint8_t(data[0]) << 24) |
                       (uint8_t(data[1]) << 16) |
                       (uint8_t(data[2]) << 8) |
                       (uint8_t(data[3]));
                data.erase(0, 4);

                state = StateReadPayload;

                //Handle 0 length payload here
                if (payload_length == 0)
                {
                    haserror = true;
                    cDebugDom("websocket") << "0 payload length";
                    finished = true;
                    state = StateReadHeader;
                }
            }
            else
                return false;
            break;
        }
        case StateReadPayload:
        {
            //Handle 0 length payload here
            if (payload_length == 0)
            {
                cDebugDom("websocket") << "0 payload length";
                haserror = true;
                finished = true;
                state = StateReadHeader;
            }
            else if (payload_length > MAX_FRAME_SIZE_IN_BYTES)
            {
                closeCode = CloseCodeTooMuchData;
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
                        processMask((char *)payload.c_str(), payload.size(), mask);

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

void WebSocketFrame::processMask(char *p, uint64_t size, uint32_t maskingKey)
{
    //unmask payload
    const uint8_t m[] = { uint8_t((maskingKey & 0xFF000000u) >> 24),
                          uint8_t((maskingKey & 0x00FF0000u) >> 16),
                          uint8_t((maskingKey & 0x0000FF00u) >> 8),
                          uint8_t((maskingKey & 0x000000FFu))
                        };

    int i = 0;
    std::stringstream stream;
    stream << std::hex << maskingKey;
    cDebugDom("websocket") << "payload size: " << size << " mask: " << stream.str();
    while (size-- > 0)
        *p++ ^= m[i++ % 4];
}

string WebSocketFrame::toString()
{
    if (!isValid()) return "Invalid frame";

    stringstream s;
    s << "Final:" << (isFinalFrame()?'1':'0')
      << " Control:" << (isControlFrame()?'1':'0')
      << " Continue:" << (isContinuationFrame()?'1':'0')
      << " isText:" << (getOpcode() == OpCodeText?'1':'0')
      << " payloadSize:" << payload_length;
    if (isTextFrame())
        s << " " << (payload.size() > 40?payload.substr(0, 40):payload);

    return s.str();
}

void WebSocketFrame::parseCloseCodeReason(uint16_t &code, string &reason)
{
    if (payload.size() == 1)
    {
        code = CloseCodeProtocolError;
        reason = "Payload of CloseFrame is too small";
    }
    if (payload.size() >= 2)
        code = (uint8_t(payload[0]) << 8) | uint8_t(payload[1]);
    if (payload.size() > 2)
        reason = payload.substr(2);
}

string WebSocketFrame::makeFrame(int _opcode, const string &_payload, bool _lastframe, uint32_t maskingKey)
{
    string frame;

    if (_payload.size() > 0x7FFFFFFFFFFFFFFFULL)
    {
        cErrorDom("websocket") << "frame payload too big: " << _payload.size();
        return frame;
    }

    uint8_t b = static_cast<uint8_t>((_opcode & 0x0F) | (_lastframe ? 0x80 : 0x00));
    frame.push_back(static_cast<char>(b));

    b = 0;

    if (maskingKey != 0)
        b |= 0x80;
    if (_payload.size() <= 125)
    {
        b |= static_cast<uint8_t>(_payload.size());
        frame.push_back(static_cast<char>(b));
    }
    else if (_payload.size() <= 0xFFFFU)
    {
        b |= 126;
        frame.push_back(static_cast<char>(b));
        frame.push_back(static_cast<char>(_payload.size() >> 8));
        frame.push_back(static_cast<char>(_payload.size()));
    }
    else if (_payload.size() <= 0x7FFFFFFFFFFFFFFFULL)
    {
        b |= 127;
        frame.push_back(static_cast<char>(b));
        uint64_t s = _payload.size();
        frame.push_back(static_cast<char>((s >> 56) & 0x7F));
        frame.push_back(static_cast<char>(s >> 48));
        frame.push_back(static_cast<char>(s >> 40));
        frame.push_back(static_cast<char>(s >> 32));
        frame.push_back(static_cast<char>(s >> 24));
        frame.push_back(static_cast<char>(s >> 16));
        frame.push_back(static_cast<char>(s >> 8));
        frame.push_back(static_cast<char>(s));
    }

    if (maskingKey != 0)
    {
        frame.push_back(static_cast<char>(maskingKey >> 24));
        frame.push_back(static_cast<char>(maskingKey >> 16));
        frame.push_back(static_cast<char>(maskingKey >> 8));
        frame.push_back(static_cast<char>(maskingKey));
    }

    if (maskingKey != 0)
    {
        string maskedPayload = _payload;
        processMask((char *)maskedPayload.c_str(), maskedPayload.size(), maskingKey);
        frame.append(maskedPayload);
    }
    else
        frame.append(_payload);

    return frame;
}
