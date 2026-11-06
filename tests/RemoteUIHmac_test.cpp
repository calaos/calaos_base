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

//T1.6: RemoteUI HMAC constant-time comparison + dedup'd helpers.
//
//The helpers under test (HMACAuthenticator::findHeader, hexDecode,
//constantTimeHexEquals) are header-inline on purpose so this test compiles
//them directly and needs no server object files.
//
//Why the old code was wrong (RED rationale, the old code cannot be linked
//here since the helper did not exist): RemoteUI::validateHMAC ended with
//"return oss.str() == hmac;". std::string operator== compares byte by byte
//and returns at the first difference, so the time taken leaked how many
//leading characters of an attacker-supplied MAC were correct — a classic
//byte-at-a-time forgery side channel on a device authentication gate.
//constantTimeHexEquals guards the length first (CRYPTO_memcmp is only
//constant-time over equal-length buffers) and then compares the raw HMAC
//bytes with CRYPTO_memcmp.

#include <gtest/gtest.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <map>
#include <string>
#include <vector>

#include "RemoteUI/HMACAuthenticator.h"

using namespace Calaos;

namespace
{

//Compute HMAC-SHA256 like RemoteUI::validateHMAC does
std::vector<unsigned char> computeHmac(const std::string &secret, const std::string &message)
{
    unsigned char result[EVP_MAX_MD_SIZE];
    unsigned int result_len = 0;

    HMAC(EVP_sha256(),
         secret.c_str(), secret.length(),
         reinterpret_cast<const unsigned char *>(message.c_str()), message.length(),
         result, &result_len);

    return std::vector<unsigned char>(result, result + result_len);
}

std::string hexEncode(const std::vector<unsigned char> &raw, bool uppercase = false)
{
    static const char *lc = "0123456789abcdef";
    static const char *uc = "0123456789ABCDEF";
    const char *digits = uppercase ? uc : lc;

    std::string out;
    for (unsigned char b : raw)
    {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 0x0f]);
    }
    return out;
}

} // namespace

TEST(ConstantTimeHexEquals, MatchingMacAccepted)
{
    auto mac = computeHmac("device-secret", "token:1700000000:nonce");
    ASSERT_EQ(mac.size(), 32u); // SHA256

    EXPECT_TRUE(HMACAuthenticator::constantTimeHexEquals(hexEncode(mac), mac.data(), mac.size()));
}

TEST(ConstantTimeHexEquals, UppercaseHexOfCorrectMacAccepted)
{
    auto mac = computeHmac("device-secret", "token:1700000000:nonce");

    EXPECT_TRUE(HMACAuthenticator::constantTimeHexEquals(hexEncode(mac, true), mac.data(), mac.size()));
}

TEST(ConstantTimeHexEquals, MismatchingMacRejected)
{
    auto mac = computeHmac("device-secret", "token:1700000000:nonce");
    auto other = computeHmac("other-secret", "token:1700000000:nonce");

    EXPECT_FALSE(HMACAuthenticator::constantTimeHexEquals(hexEncode(other), mac.data(), mac.size()));
}

TEST(ConstantTimeHexEquals, SingleBitFlipRejected)
{
    auto mac = computeHmac("device-secret", "token:1700000000:nonce");

    //flip first byte
    std::string first = hexEncode(mac);
    first[0] = (first[0] == 'f') ? '0' : 'f';
    EXPECT_FALSE(HMACAuthenticator::constantTimeHexEquals(first, mac.data(), mac.size()));

    //flip last byte
    std::string last = hexEncode(mac);
    last.back() = (last.back() == 'f') ? '0' : 'f';
    EXPECT_FALSE(HMACAuthenticator::constantTimeHexEquals(last, mac.data(), mac.size()));
}

TEST(ConstantTimeHexEquals, WrongLengthRejected)
{
    auto mac = computeHmac("device-secret", "token:1700000000:nonce");
    std::string hex = hexEncode(mac);

    //truncated (still even length, decodes fine, but too short)
    EXPECT_FALSE(HMACAuthenticator::constantTimeHexEquals(hex.substr(0, hex.size() - 2),
                                                          mac.data(), mac.size()));
    //too long (correct prefix + extra byte)
    EXPECT_FALSE(HMACAuthenticator::constantTimeHexEquals(hex + "00", mac.data(), mac.size()));
    //empty supplied MAC
    EXPECT_FALSE(HMACAuthenticator::constantTimeHexEquals("", mac.data(), mac.size()));
    //empty computed MAC never validates
    EXPECT_FALSE(HMACAuthenticator::constantTimeHexEquals("", mac.data(), 0));
}

TEST(ConstantTimeHexEquals, MalformedHexRejected)
{
    auto mac = computeHmac("device-secret", "token:1700000000:nonce");
    std::string hex = hexEncode(mac);

    //odd length
    EXPECT_FALSE(HMACAuthenticator::constantTimeHexEquals(hex.substr(0, hex.size() - 1),
                                                          mac.data(), mac.size()));
    //non-hex character, same length
    std::string bad = hex;
    bad[5] = 'g';
    EXPECT_FALSE(HMACAuthenticator::constantTimeHexEquals(bad, mac.data(), mac.size()));
}

TEST(HexDecode, RoundTripAndErrors)
{
    std::vector<unsigned char> out;

    EXPECT_TRUE(HMACAuthenticator::hexDecode("00ff10Ab", out));
    ASSERT_EQ(out.size(), 4u);
    EXPECT_EQ(out[0], 0x00);
    EXPECT_EQ(out[1], 0xff);
    EXPECT_EQ(out[2], 0x10);
    EXPECT_EQ(out[3], 0xab);

    //empty string decodes to empty buffer
    EXPECT_TRUE(HMACAuthenticator::hexDecode("", out));
    EXPECT_TRUE(out.empty());

    //odd length
    EXPECT_FALSE(HMACAuthenticator::hexDecode("abc", out));
    //non-hex characters
    EXPECT_FALSE(HMACAuthenticator::hexDecode("zz", out));
    EXPECT_FALSE(HMACAuthenticator::hexDecode("0 ", out));
}

TEST(FindHeader, ExactAndLowercaseAndMissing)
{
    std::map<std::string, std::string> headers = {
        { "Authorization", "Bearer tok" },
        { "x-auth-nonce", "abcd" },
    };

    //exact case match
    EXPECT_EQ(HMACAuthenticator::findHeader(headers, "Authorization"), "Bearer tok");
    //lowercase fallback (header stored lowercase, looked up canonical)
    EXPECT_EQ(HMACAuthenticator::findHeader(headers, "X-Auth-Nonce"), "abcd");
    //missing header
    EXPECT_EQ(HMACAuthenticator::findHeader(headers, "X-Auth-HMAC"), "");
    //empty map
    std::map<std::string, std::string> empty;
    EXPECT_EQ(HMACAuthenticator::findHeader(empty, "Authorization"), "");
}
//Note: WebSocketHeaders::parse itself is not exercised here on purpose —
//it lives in HMACAuthenticator.cpp whose object drags the whole server
//core at link time. It is a plain field-assignment wrapper around the
//findHeader tested above.
