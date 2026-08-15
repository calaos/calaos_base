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

//T3.4: hardening tests for the vendored base64 codec (src/lib/base64.*) and
//the Utils::Base64_* wrappers (StringUtils.cpp). Before the rewrite,
//base64_decode() silently truncated at the first out-of-alphabet character
//(including whitespace and embedded NULs) and accepted broken padding and
//non-canonical trailing bits; Utils::Base64_encode(void*, int) converted a
//negative size to a huge unsigned length (out-of-bounds read). The strict
//decoder rejects all of that; the legacy-shaped entry points map rejection
//to an empty string so every caller signature stays source-compatible.

#include <gtest/gtest.h>
#include <string>

#include "Utils.h"
#include "base64.h"

//---------------------------------------------------------------------------
//RFC 4648 §10 known-answer vectors, encode and strict decode
//---------------------------------------------------------------------------

TEST(Base64Encode, Rfc4648Vectors)
{
    auto enc = [](const std::string &s) {
        return base64_encode(reinterpret_cast<const unsigned char *>(s.data()), s.size());
    };
    EXPECT_EQ(enc(""), "");
    EXPECT_EQ(enc("f"), "Zg==");
    EXPECT_EQ(enc("fo"), "Zm8=");
    EXPECT_EQ(enc("foo"), "Zm9v");
    EXPECT_EQ(enc("foob"), "Zm9vYg==");
    EXPECT_EQ(enc("fooba"), "Zm9vYmE=");
    EXPECT_EQ(enc("foobar"), "Zm9vYmFy");
}

TEST(Base64Decode, Rfc4648Vectors)
{
    auto dec = [](const std::string &s) { return base64_decode_checked(s); };
    ASSERT_TRUE(dec("").has_value());
    EXPECT_EQ(*dec(""), "");
    EXPECT_EQ(dec("Zg=="), "f");
    EXPECT_EQ(dec("Zm8="), "fo");
    EXPECT_EQ(dec("Zm9v"), "foo");
    EXPECT_EQ(dec("Zm9vYg=="), "foob");
    EXPECT_EQ(dec("Zm9vYmE="), "fooba");
    EXPECT_EQ(dec("Zm9vYmFy"), "foobar");
}

TEST(Base64Decode, UnpaddedInputStillAccepted)
{
    //historic decoder tolerated missing '=' padding; keep that lenience
    EXPECT_EQ(base64_decode_checked("Zg"), "f");
    EXPECT_EQ(base64_decode_checked("Zm8"), "fo");
    EXPECT_EQ(base64_decode_checked("TWE"), "Ma");
}

TEST(Base64RoundTrip, AllByteValuesAndEmbeddedNuls)
{
    std::string all;
    for (int i = 0; i < 256; i++)
        all.push_back(static_cast<char>(i));
    //also covers embedded NULs in the *binary* payload (must survive)
    std::string encoded =
        base64_encode(reinterpret_cast<const unsigned char *>(all.data()), all.size());
    auto decoded = base64_decode_checked(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, all);
}

TEST(Base64RoundTrip, LargeBuffer)
{
    //3 MB pseudo-random payload: exercises the size math on a real buffer
    std::string big;
    big.resize(3 * 1024 * 1024 + 1); //+1 → padded leftover group
    uint32_t x = 0x12345678;
    for (auto &c: big)
    {
        x = x * 1664525u + 1013904223u;
        c = static_cast<char>(x >> 24);
    }
    std::string encoded =
        base64_encode(reinterpret_cast<const unsigned char *>(big.data()), big.size());
    EXPECT_EQ(encoded.size(), ((big.size() + 2) / 3) * 4);
    auto decoded = base64_decode_checked(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, big);
}

//---------------------------------------------------------------------------
//Malformed input: strict rejection (red before the hardening — the old
//decoder returned silently truncated data for every one of these)
//---------------------------------------------------------------------------

TEST(Base64Decode, RejectsInvalidCharacters)
{
    EXPECT_FALSE(base64_decode_checked("TWFu!").has_value());
    EXPECT_FALSE(base64_decode_checked("TW Fu").has_value());
    EXPECT_FALSE(base64_decode_checked("TWFu\nTWFu").has_value());
    EXPECT_FALSE(base64_decode_checked("TWFu\r\n").has_value());
    EXPECT_FALSE(base64_decode_checked("TWF*").has_value());
    EXPECT_FALSE(base64_decode_checked("\xffZm9v").has_value());
}

TEST(Base64Decode, RejectsEmbeddedNulInEncodedText)
{
    //NUL inside the *encoded* string is an invalid symbol, not a terminator
    EXPECT_FALSE(base64_decode_checked(std::string("TQ\0=", 4)).has_value());
    EXPECT_FALSE(base64_decode_checked(std::string("\0\0\0\0", 4)).has_value());
    EXPECT_FALSE(base64_decode_checked(std::string("Zm9v\0Zm9v", 9)).has_value());
}

TEST(Base64Decode, RejectsMisplacedOrBrokenPadding)
{
    EXPECT_FALSE(base64_decode_checked("=").has_value());
    EXPECT_FALSE(base64_decode_checked("==").has_value());
    EXPECT_FALSE(base64_decode_checked("====").has_value());
    EXPECT_FALSE(base64_decode_checked("T===").has_value());   //3 pad chars
    EXPECT_FALSE(base64_decode_checked("TQ=").has_value());    //not 4-aligned
    EXPECT_FALSE(base64_decode_checked("T=Q=").has_value());   //'=' mid-group
    EXPECT_FALSE(base64_decode_checked("TQ==AAAA").has_value()); //data after pad
    EXPECT_FALSE(base64_decode_checked("Zm9vYg==Zm8=").has_value());
}

TEST(Base64Decode, RejectsImpossibleLength)
{
    //4k+1 symbols cannot be produced by any encoder
    EXPECT_FALSE(base64_decode_checked("A").has_value());
    EXPECT_FALSE(base64_decode_checked("AAAAA").has_value());
    EXPECT_FALSE(base64_decode_checked("Zm9vY").has_value());
}

TEST(Base64Decode, RejectsNonCanonicalTrailingBits)
{
    //RFC 4648 §3.5: unused bits in the final group must be zero
    EXPECT_FALSE(base64_decode_checked("TR==").has_value()); //canonical: TQ==
    EXPECT_FALSE(base64_decode_checked("TWF=").has_value()); //canonical: TWE=
    EXPECT_FALSE(base64_decode_checked("TR").has_value());   //unpadded variant
    EXPECT_FALSE(base64_decode_checked("TWF").has_value());
}

TEST(Base64Decode, LegacyShimMapsRejectionToEmpty)
{
    //old behavior: "TWFu!" -> "Man" (silent truncation). Now: "".
    EXPECT_EQ(base64_decode("TWFu!"), "");
    EXPECT_EQ(base64_decode("TQ="), "");
    EXPECT_EQ(base64_decode("TR=="), "");
    EXPECT_EQ(base64_decode("TWFu"), "Man");
}

//---------------------------------------------------------------------------
//Encoder edge cases
//---------------------------------------------------------------------------

TEST(Base64Encode, NullOrEmptyInput)
{
    EXPECT_EQ(base64_encode(nullptr, 0), "");
    //null pointer with a non-zero length must not be dereferenced
    EXPECT_EQ(base64_encode(nullptr, 42), "");
}

//---------------------------------------------------------------------------
//Utils:: wrappers (StringUtils.cpp) — legacy signatures kept, hardened inside
//---------------------------------------------------------------------------

TEST(UtilsBase64, WrapperRoundTrip)
{
    std::string payload("hello\0world\xff", 12); //embedded NUL + high byte
    std::string encoded = Utils::Base64_encode(payload);
    auto decoded = Utils::Base64_decode(encoded);
    EXPECT_EQ(decoded, payload);
    EXPECT_EQ(Utils::Base64_decode_data(encoded), payload);
}

TEST(UtilsBase64, WrapperRejectsInvalidInputAsEmpty)
{
    std::string bad1 = "TWFu!";
    std::string bad2 = "TQ=";
    std::string bad3 = "TR==";
    EXPECT_EQ(Utils::Base64_decode(bad1), "");
    EXPECT_EQ(Utils::Base64_decode(bad2), "");
    EXPECT_EQ(Utils::Base64_decode_data(bad3), "");
}

TEST(UtilsBase64, WrapperEncodeGuardsSize)
{
    char buf[4] = {'a', 'b', 'c', 'd'};
    //negative size used to become a ~4 GB unsigned length → OOB read
    EXPECT_EQ(Utils::Base64_encode(buf, -1), "");
    EXPECT_EQ(Utils::Base64_encode(buf, 0), "");
    EXPECT_EQ(Utils::Base64_encode(nullptr, 8), "");
    EXPECT_EQ(Utils::Base64_encode(buf, 4), "YWJjZA==");
}

TEST(UtilsBase64, WebSocketKeyShapePreserved)
{
    //the WebSocket handshake relies on decode("...16-byte key...") == 16 bytes
    std::string key = "dGhlIHNhbXBsZSBub25jZQ=="; //RFC 6455 sample nonce
    EXPECT_EQ(Utils::Base64_decode(key).size(), 16u);
    std::string garbage = "not!a!valid!key!!!";
    EXPECT_EQ(Utils::Base64_decode(garbage).size(), 0u);
}
