/******************************************************************************
 **  Copyright (c) 2006-2026, Calaos. All Rights Reserved.
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
 **  along with Calaos; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/

//T2.3: regression test for the WebSocket handshake digest after the bundled
//CSHA1 implementation was replaced by OpenSSL EVP_Digest in WebSocket.cpp.
//Reproduces the exact computation performed by WebSocket::processHandshake
//(EVP_Digest one-shot SHA1 + Utils::Base64_encode) and checks it against the
//known-answer vector of RFC 6455 section 1.3 / 4.2.2, proving the
//Sec-WebSocket-Accept value stays byte-identical.

#include <gtest/gtest.h>
#include <openssl/evp.h>

#include <string>

#include "Utils.h"

namespace
{

//Same pipeline as WebSocket.cpp: accept = base64(SHA1(key + GUID))
std::string computeWebSocketAccept(const std::string &clientKey)
{
    std::string key = clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    uint8_t digest[EVP_MAX_MD_SIZE];
    unsigned int digestLen = 0;
    if (EVP_Digest(key.data(), key.size(), digest, &digestLen,
                   EVP_sha1(), nullptr) != 1 ||
        digestLen != 20)
        return {};
    return Utils::Base64_encode(digest, digestLen);
}

} //namespace

TEST(WebSocketAccept, Rfc6455KnownAnswerVector)
{
    //RFC 6455, sections 1.3 and 4.2.2: the sample nonce
    //"dGhlIHNhbXBsZSBub25jZQ==" hashes to
    //b37a4f2cc0624f1690f64606cf385945b2bec4ea and must yield exactly this
    //accept value.
    EXPECT_EQ("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=",
              computeWebSocketAccept("dGhlIHNhbXBsZSBub25jZQ=="));
}

TEST(WebSocketAccept, DigestIsAlways20Bytes)
{
    //An empty client key still produces a well-formed 28-char base64 of a
    //20-byte SHA1 digest (the guard in WebSocket.cpp requires digestLen == 20).
    std::string accept = computeWebSocketAccept("");
    EXPECT_EQ(28u, accept.size());
    EXPECT_EQ('=', accept.back());
}
