/*
 **  Base64 codec (originally from René Nyffenegger's public-domain
 **  implementation, rewritten/hardened for Calaos — T3.4).
 */
#ifndef S_BASE64_H
#define S_BASE64_H

#include <cstddef>
#include <optional>
#include <string>

//Encode len bytes. Returns "" when bytes is null (with len != 0) or when the
//output size would overflow. Output is canonical RFC 4648 base64 ('=' padded,
//no line wrapping).
std::string base64_encode(unsigned char const *bytes, size_t len);

//Strict RFC 4648 decoder. Returns std::nullopt on ANY invalid input:
//characters outside the base64 alphabet (including whitespace and NUL),
//'=' anywhere but as final padding, more than two '=', an impossible
//length (4k+1 symbols), or non-canonical trailing bits. Unpadded input of
//valid length is accepted. Empty input decodes to an empty string.
std::optional<std::string> base64_decode_checked(std::string const &s);

//Legacy-compatible shim over base64_decode_checked(): returns "" on invalid
//input (historically the input was silently truncated instead).
std::string base64_decode(std::string const &s);

#endif
