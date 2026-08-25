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

/* T2.17: mask credentials embedded in a URL before it reaches a log.
 * Handles the userinfo password (scheme://user:secret@host/ -> user kept,
 * secret masked) and the values of credential-bearing query parameters
 * (usr/pwd/user/username/password/passwd/account/loginuse/loginpas/_sid,
 * case-insensitive). Moved from IPCam (which now delegates here) so
 * UrlDownloader can mask every URL it logs. */
std::string maskUrlCredentials(const std::string &url);
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
/* T3.25. THE PARSING CONTRACT, in one place, because 319 call sites in src/
 * depend on it and 312 of them do so WITHOUT LOOKING AT THE RETURN VALUE.
 *
 *   input      returns   dest after the call
 *   ""         false     T{}     - nothing readable
 *   "  \t "    false     T{}     - idem, a blank string is not a number
 *   "abc"      false     T{}     - num_get ran and failed
 *   "-", "+"   false     T{}     - consumed whole, still not a number
 *   "12abc"    false     12      - a PARTIAL read is kept, not thrown away
 *   "12 "      false     12      - idem
 *   "12"       true      12
 *
 * WHAT CHANGED, AND IT IS EXACTLY ONE INPUT: the blank string. Until T3.25 the
 * return value was `iss.eof()` alone, and on "" (or on any whitespace-only
 * string) the SENTRY of operator>> fails while skipping whitespace, so num_get
 * NEVER RUNS: eofbit is set, failbit is set, `dest` is left untouched and
 * eof() answered "success". Since Params::operator[] returns "" for an ABSENT
 * key, "the parameter is missing" meant "use whatever was on the stack" at 173
 * call sites whose destination is an uninitialised local. That reached an
 * OutputShutter through the JSON API and armed an impulse on an arbitrary
 * duration (T3.25 section 2).
 *
 * `iss.fail()` is what tells the two regimes apart: the sentry sets failbit,
 * a merely unconsumed trailing character does not.
 *
 * ⛔ WHAT IS DELIBERATELY *NOT* DONE: "if it did not fully succeed, write T{}".
 * That would turn "12abc" and "12 " into 0 and silently change every config
 * value carrying a unit or a stray character. The partial read is kept, and
 * tests/StringUtilsFromString_test pins it.
 */
template<typename T>
bool is_of_type(const std::string &str)
{
    std::istringstream iss(str);
    /* Same locale as from_string() below. std::locale::global() is never
     * called anywhere in src/, so today this is a no-op - it is here so that
     * the guard and the parse it guards can never disagree by construction.
     */
    iss.imbue(std::locale("C"));
    T tmp{};
    iss >> tmp;
    return !iss.fail() && iss.eof();
}
template<typename T>
bool from_string(const std::string &str, T &dest)
{
    std::istringstream iss(str);
    iss.imbue(std::locale("C")); //use the C locale when parsing
    /* Parse into a DEFINED temporary and always publish it: `dest` is written
     * on every path, so a caller that ignores the return value can no longer
     * end up reading its own uninitialised variable.
     */
    T tmp{};
    iss >> tmp;
    dest = tmp;
    return !iss.fail() && iss.eof();
}
/* T3.25. The contract the 45 call sites carrying an INITIALISED destination
 * were relying on, written down instead of inferred: parse into `dest`, and
 * leave `dest` ALONE when there is nothing at all to parse. It is what
 * from_string() used to do BY ACCIDENT on a blank string, and the only reason
 * those sites kept their default when a parameter was absent. A partial read
 * still lands ("12abc" -> 12), exactly as before.
 *
 * Use it where a MEANINGFUL non-zero default must survive a missing value.
 */
template<typename T>
bool from_string_or_keep(const std::string &str, T &dest)
{
    if (str.find_first_not_of(" \t\n\v\f\r") == std::string::npos)
        return false;
    return from_string(str, dest);
}
/* T3.25. The stricter form, recommended for NEW code: any failure - blank,
 * unreadable, or only partially readable - yields `def`.
 *
 * It is deliberately NOT retrofitted onto the existing call sites: unlike
 * from_string_or_keep() it also discards a PARTIAL read, and at least one
 * documented behaviour depends on keeping it (JsonApi.cpp, per_page="1,5"
 * reads as 1 - T3.19). Retrofitting it would have changed behaviours this
 * ticket promised not to change.
 */
template<typename T>
T from_string_or(const std::string &str, T def)
{
    T tmp{};
    return from_string(str, tmp)? tmp: def;
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
