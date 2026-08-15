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
#ifndef CALAOS_STRINGUTILS_H
#define CALAOS_STRINGUTILS_H

#include <string>
#include <vector>
#include <sstream>
#include <locale>
#include <ctype.h>

//-----------------------------------------------------------------------------
namespace Utils
{
std::string url_encode(std::string str);
std::string url_decode(std::string str);
std::string url_decode2(std::string str); //decode 2 times
int htoi(char *s);
std::string time2string(long s, long ms = 0);
std::string time2string_digit(long s, long ms = 0);

/* usefull string utilities */
void split(const std::string &str, std::vector<std::string> &tokens, const std::string &delimiters = " ", int max = 0);
void remove_tag(std::string &source, const std::string begin_tag, const std::string end_tag);
void replace_str(std::string &source, const std::string searchstr, const std::string replacestr);
void trim_right(std::string &source, const std::string &t);
void trim_left(std::string &source, const std::string &t);
std::string trim(const std::string &str);
std::string escape_quotes(const std::string &s);
std::string escape_space(const std::string &s);

enum CaseSensitivity { CaseInsensitive, CaseSensitive };
bool strContains(const std::string &str, const std::string &needle, Utils::CaseSensitivity cs = Utils::CaseSensitive);
bool strStartsWith(const std::string &str, const std::string &needle, Utils::CaseSensitivity cs = Utils::CaseSensitive);

//!decode a BASE64 string
std::string Base64_decode(std::string &str);
std::string Base64_decode_data(std::string &str);
//!encode a BASE64 string
std::string Base64_encode(std::string &str);
std::string Base64_encode(void *data, int size);

std::string str_to_lower(std::string s);
std::string str_to_upper(std::string s);

class CStrArray
{
public:
    CStrArray() {}
    CStrArray(const std::string &str_split);
    CStrArray(const std::vector<std::string> &lst);
    ~CStrArray();

    const char *at(std::size_t pos) { return m_strings.at(pos).c_str(); }
    void set(const std::vector<std::string> &lst);
    std::size_t count() const { return m_strings.size(); }
    const char **constData() const { return m_data; }
    char **data() { return (char **)m_data; }

    std::string toString();

private:
    std::vector<std::string> m_strings;
    std::string m_tostring;
    const char **m_data = nullptr;
    void updateNative();
};

//-----------------------------------------------------------------------------
template<typename T>
bool is_of_type(const std::string &str)
{
    std::istringstream iss(str);
    T tmp;
    iss >> tmp;
    return iss.eof();
}
template<typename T>
bool from_string(const std::string &str, T &dest)
{
    std::istringstream iss(str);
    iss.imbue(std::locale("C")); //use the C locale when parsing
    iss >> dest;
    return iss.eof();
}
template<typename T>
std::string to_string( const T & Value )
{
    std::ostringstream oss;
    oss << Value;
    return oss.str();
}

//Some usefull fonctors
struct UrlDecode
{
    template <class T> void operator ()(T &str) const
    {
        str = Utils::url_decode2(str);
    }
};
class to_lower
{
public:
    char operator() (char c) const
    {
        return tolower(c);
    }
};
class to_upper
{
public:
    char operator() (char c) const
    {
        return toupper(c);
    }
};
}

#endif
