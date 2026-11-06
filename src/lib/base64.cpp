/*
   base64.cpp and base64.h

   Originally Copyright (C) 2004-2008 René Nyffenegger (public-domain style
   license, see below). Rewritten for Calaos (T3.4 hardening): strict RFC 4648
   validation on decode (invalid characters, misplaced or broken '=' padding,
   impossible lengths and non-canonical trailing bits are rejected instead of
   silently truncating the output), constexpr reverse lookup table (no
   locale-dependent isalnum()), size_t length handling and overflow-checked
   size math on encode. This altered version must not be misrepresented as
   the original source code.

   Original license:

   This source code is provided 'as-is', without any express or implied
   warranty. In no event will the author be held liable for any damages
   arising from the use of this software.

   Permission is granted to anyone to use this software for any purpose,
   including commercial applications, and to alter it and redistribute it
   freely, subject to the following restrictions:

   1. The origin of this source code must not be misrepresented; you must not
      claim that you wrote the original source code. If you use this source code
      in a product, an acknowledgment in the product documentation would be
      appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
      misrepresented as being the original source code.

   3. This notice may not be removed or altered from any source distribution.

   René Nyffenegger rene.nyffenegger@adp-gmbh.ch
*/

#include "base64.h"

#include <array>
#include <cstdint>
#include <limits>

namespace
{

constexpr char base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

constexpr int8_t INVALID_SYMBOL = -1;

//256-entry reverse lookup table: symbol value for alphabet chars,
//INVALID_SYMBOL for everything else (including '=', handled separately).
constexpr std::array<int8_t, 256> makeDecodeTable()
{
    std::array<int8_t, 256> t{};
    for (auto &v: t)
        v = INVALID_SYMBOL;
    for (int i = 0; i < 64; i++)
        t[static_cast<unsigned char>(base64_chars[i])] = static_cast<int8_t>(i);
    return t;
}

constexpr std::array<int8_t, 256> decode_table = makeDecodeTable();

} // namespace

std::string base64_encode(unsigned char const *bytes, size_t len)
{
    if (len == 0)
        return {};
    if (!bytes)
        return {};

    //4 output chars per 3 input bytes; guard ((len + 2) / 3) * 4 overflow
    if (len > (std::numeric_limits<size_t>::max() / 4) * 3 - 2)
        return {};

    std::string ret;
    ret.reserve(((len + 2) / 3) * 4);

    size_t i = 0;
    for (; i + 3 <= len; i += 3)
    {
        uint32_t triple = (static_cast<uint32_t>(bytes[i]) << 16) |
                          (static_cast<uint32_t>(bytes[i + 1]) << 8) |
                          static_cast<uint32_t>(bytes[i + 2]);
        ret += base64_chars[(triple >> 18) & 0x3f];
        ret += base64_chars[(triple >> 12) & 0x3f];
        ret += base64_chars[(triple >> 6) & 0x3f];
        ret += base64_chars[triple & 0x3f];
    }

    size_t rest = len - i;
    if (rest == 1)
    {
        uint32_t v = static_cast<uint32_t>(bytes[i]) << 16;
        ret += base64_chars[(v >> 18) & 0x3f];
        ret += base64_chars[(v >> 12) & 0x3f];
        ret += '=';
        ret += '=';
    }
    else if (rest == 2)
    {
        uint32_t v = (static_cast<uint32_t>(bytes[i]) << 16) |
                     (static_cast<uint32_t>(bytes[i + 1]) << 8);
        ret += base64_chars[(v >> 18) & 0x3f];
        ret += base64_chars[(v >> 12) & 0x3f];
        ret += base64_chars[(v >> 6) & 0x3f];
        ret += '=';
    }

    return ret;
}

std::optional<std::string> base64_decode_checked(std::string const &encoded_string)
{
    const size_t n = encoded_string.size();
    if (n == 0)
        return std::string();

    //Trailing '=' padding: at most 2, only at the very end, and only on
    //4-aligned input (padding exists precisely to reach that alignment).
    size_t pad = 0;
    while (pad < n && encoded_string[n - 1 - pad] == '=')
        pad++;
    if (pad > 2)
        return std::nullopt;
    if (pad > 0 && n % 4 != 0)
        return std::nullopt;

    const size_t symbols = n - pad;
    //4k+1 symbols can never be produced by a base64 encoder (a leftover
    //group carries 2 or 3 symbols, never 1).
    if (symbols % 4 == 1)
        return std::nullopt;

    std::string ret;
    ret.reserve((symbols / 4) * 3 + 2);

    uint32_t acc = 0;
    unsigned acc_bits = 0;
    for (size_t i = 0; i < symbols; i++)
    {
        const int8_t v = decode_table[static_cast<unsigned char>(encoded_string[i])];
        if (v == INVALID_SYMBOL) //covers '=' before the trailing run too
            return std::nullopt;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        acc_bits += 6;
        if (acc_bits >= 8)
        {
            acc_bits -= 8;
            ret += static_cast<char>((acc >> acc_bits) & 0xff);
        }
    }

    //Canonical form (RFC 4648 §3.5): bits left over in the accumulator must
    //be zero, otherwise the input does not round-trip.
    if (acc_bits > 0 && (acc & ((1u << acc_bits) - 1)) != 0)
        return std::nullopt;

    return ret;
}

std::string base64_decode(std::string const &encoded_string)
{
    auto decoded = base64_decode_checked(encoded_string);
    return decoded ? std::move(*decoded) : std::string();
}
