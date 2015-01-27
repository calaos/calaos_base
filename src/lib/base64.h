/*
 **  These functions are from the Squid Http Proxy project.
 */
#ifndef S_BASE64_H
#define S_BASE64_H

#include <string>

std::string base64_encode(unsigned char const* , unsigned int len);
std::string base64_decode(std::string const& s);

#endif
