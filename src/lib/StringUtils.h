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
 *   input                    returns   dest after the call
 *   ""                       false     T{}       - nothing readable
 *   "  \t "                  false     T{}       - idem, a blank string is not a number
 *   "abc"                    false     T{}       - num_get ran and failed
 *   "-", "+"                 false     T{}       - consumed whole, still not a number
 *   "12abc"                  false     12        - a PARTIAL read is kept, not thrown away
 *   "12 "                    false     12        - idem
 *   "12"                     true      12
 *   ⚠️ "2147483648"  <int>    false     INT_MAX   - OVERFLOW, and it is the ONE failure
 *   ⚠️ "-2147483649" <int>    false     INT_MIN     mode where `dest` is NEITHER T{} NOR a
 *   ⚠️ "1e400"    <double>    false     DBL_MAX     partial read: num_get SATURATES to the
 *   ⚠️ "-1e400"   <double>    false     -DBL_MAX    limit of T, sets failbit, and T3.25
 *                                                  PUBLISHES that saturated value.
 *
 * WHAT CHANGED IS THE RETURN VALUE, ON THREE FAMILIES OF INPUT — measured with
 * g++ -std=c++17, not inferred. Until T3.25 the return was `iss.eof()` alone,
 * which answers "the stream was consumed to the end", NOT "the parse
 * succeeded". Three families are consumed to the end while failing:
 *
 *   1. THE BLANK STRING ("" and any whitespace-only string). The SENTRY of
 *      operator>> fails while skipping whitespace, so num_get NEVER RUNS:
 *      eofbit and failbit are both set, `dest` was left untouched, and eof()
 *      answered "success". Since Params::operator[] returns "" for an ABSENT
 *      key, "the parameter is missing" meant "use whatever was on the stack"
 *      at every call site whose destination is an uninitialised local. That
 *      reached an OutputShutter through the JSON API and armed an impulse on
 *      an arbitrary duration (T3.25 section 2). ⭐ This is the family the
 *      ticket was opened for, and the only one where `dest` was NOT written.
 *   2. THE LONE SIGN ("-", "+"). num_get runs, consumes the sign, finds no
 *      digit, sets failbit and writes 0. eof() said "success" for 0.
 *   3. ⭐ OVERFLOW ("2147483648" for int, "1e400" for double). num_get runs,
 *      consumes every character, saturates to INT_MAX/INT_MIN/±DBL_MAX and
 *      sets failbit. eof() said "success" for the saturated value. THE VALUE
 *      IS UNCHANGED by T3.25 — saturation is what the standard mandates and
 *      what the tree already relied on (core/JsonApiSession_test.cpp and
 *      core/JsonApiInputGuards_test.cpp pin INT_MAX). What T3.25 changes, and
 *      all it changes, is that the caller is now TOLD it was a failure.
 *
 * The conclusion of the T3.25 audit is not affected by families 2 and 3: only
 * the 7 sites in src/ that READ the return value can see the difference, and
 * the two that are affected (WagoConfigParse.h:57, GpioCtrl.h:78) GAIN from it
 * — they reject a value they used to accept.
 *
 * `iss.fail()` is what tells the two regimes apart: the sentry, an unreadable
 * token and an overflow all set failbit; a merely unconsumed trailing
 * character does not.
 *
 * ⛔ WHAT IS DELIBERATELY *NOT* DONE: "if it did not fully succeed, write T{}".
 * That would turn "12abc" and "12 " into 0 and silently change every config
 * value carrying a unit or a stray character. The partial read is kept, and
 * tests/StringUtilsFromString_test pins it — overflow rows included.
 */
/* ⭐ T3.25 (review). WHAT "BLANK" MEANS, in ONE place.
 *
 * The six characters std::isspace() answers true for in the "C" locale, which
 * is exactly the set the istringstream sentry of Detail::parse() skips - and
 * the sentry is the reason a blank string never reaches num_get at all. The
 * set is spelled out rather than obtained from the locale so that "blank" can
 * never start depending on a global the tree never sets.
 *
 * Two callers, and they ask DIFFERENT questions of the same set:
 *   - from_string_unless_blank() below asks "is the WHOLE string blank";
 *   - JsonApi.cpp's setStateValueLostItsArgument() asks "does it END on a
 *     blank" - a set_state value that ends on its separator is a command that
 *     lost its argument.
 * They were two hardcoded copies of " \t\n\v\f\r" until the review pointed
 * out that nothing kept them in step; the QUESTIONS stay separate, the SET is
 * now shared.
 */
inline constexpr const char BLANK_CHARS[] = " \t\n\v\f\r";

namespace Detail
{
/* The ONE parse that from_string(), from_string_or_keep() and is_of_type() all
 * run, so that the guard, the two writers and the predicate can never disagree
 * about what "readable" means. Each caller applies its own WRITE POLICY on top.
 *
 *   readOk == !iss.fail(): num_get produced a value from the input, whole or
 *      partial. FALSE for a blank string, unreadable text, a lone sign, and
 *      ⚠️ for an OVERFLOW — where num_get *does* leave a saturated value in
 *      `out`, which is precisely why the write policy has to be a decision and
 *      not an accident.
 *   whole == iss.eof(): every character of the input was consumed.
 *
 * `out` must already hold a defined value: on the paths where num_get never
 * runs (blank string) it is not written at all.
 */
template<typename T>
void parse(const std::string &str, T &out, bool &readOk, bool &whole)
{
    std::istringstream iss(str);
    /* std::locale::global() is never called anywhere in src/, so imbuing the
     * "C" locale is a no-op today - it is here so that this can never change
     * under us, and so that every caller shares the same one. */
    iss.imbue(std::locale("C"));
    iss >> out;
    readOk = !iss.fail();
    whole = iss.eof();
}
}
template<typename T>
bool is_of_type(const std::string &str)
{
    T tmp{};
    bool readOk = false, whole = false;
    Detail::parse(str, tmp, readOk, whole);
    return readOk && whole;
}
template<typename T>
bool from_string(const std::string &str, T &dest)
{
    /* Parse into a DEFINED temporary and always publish it: `dest` is written
     * on every path, so a caller that ignores the return value can no longer
     * end up reading its own uninitialised variable. ⚠️ On overflow the value
     * published is the SATURATED one, not T{} — see the table above.
     */
    T tmp{};
    bool readOk = false, whole = false;
    Detail::parse(str, tmp, readOk, whole);
    dest = tmp;
    return readOk && whole;
}
/* T3.25. The contract the call sites carrying an INITIALISED destination were
 * relying on, written down instead of inferred: parse into `dest`, and leave
 * `dest` ALONE whenever the input does not yield a value. It is what
 * from_string() used to do BY ACCIDENT on a blank string, and the only reason
 * those sites kept their default when a parameter was absent.
 *
 * Use it where a MEANINGFUL non-zero default must survive a missing value.
 *
 *   ""  "  " "abc" "-" "2147483648" "1e400"  ->  false, `dest` UNTOUCHED
 *   "12abc" "12 "                            ->  false, `dest` = 12 (partial read lands)
 *   "12"                                     ->  true,  `dest` = 12
 *
 * ⭐ THE OVERFLOW ROW IS THE T3.25 REVIEW'S CORRECTION, and it is not cosmetic.
 * The first cut of this helper guarded the BLANK STRING ONLY and then delegated
 * to from_string(), so from_string_or_keep("99999999999999999999", port) with
 * port = 1883 returned false and left port = 2147483647 — destroying the very
 * default the function exists to protect, at all 20 call sites (mqtt port and
 * keepalive, RemoteUI brightness, KNX eis, ColorUtils alpha, the two JSON-API
 * ports, ...). Testing `readOk` instead of testing the string covers the blank
 * string, the unreadable token, the lone sign AND the overflow with one
 * condition, and cannot drift away from what from_string() considers a failure.
 *
 * The rule, stated once: from_string_or_keep() writes `dest` if and only if
 * num_get produced a value FROM THE INPUT. A saturated overflow is a value
 * num_get produced from its own limits, and the default wins over it.
 */
template<typename T>
bool from_string_or_keep(const std::string &str, T &dest)
{
    T tmp{};
    bool readOk = false, whole = false;
    Detail::parse(str, tmp, readOk, whole);
    if (!readOk)
        return false;
    dest = tmp;
    return whole;
}
/* T3.25 (review). The OTHER half of the old from_string_or_keep(): parse
 * exactly as from_string() does, INCLUDING publishing a saturated overflow,
 * unless there is literally nothing to parse.
 *
 *   ""  "  "                                 ->  false, `dest` UNTOUCHED
 *   "abc"                                    ->  false, `dest` = T{}
 *   "12abc" "12 "                            ->  false, `dest` = 12
 *   ⚠️ "99999999999" <int>                    ->  false, `dest` = INT_MAX
 *   "12"                                     ->  true,  `dest` = 12
 *
 * ⛔ It exists for ONE requirement and has TWO call sites: JsonApi.cpp's
 * eventlog `page`/`per_page`. T3.19 documents, and JsonApiInputGuards_test /
 * JsonApiSession_test PIN, all three of these at once:
 *   - an absent or blank per_page answers a page of 100 (the default survives);
 *   - per_page:"abc" reads as 0 and is REFUSED ("per_page is out of range",
 *     with a golden);
 *   - per_page:"99999999999" is ANSWERED, echoing the saturated 2147483647.
 * from_string_or_keep() satisfies the first and breaks the other two, which is
 * why this site does not use it. Do not "simplify" one into the other.
 */
template<typename T>
bool from_string_unless_blank(const std::string &str, T &dest)
{
    if (str.find_first_not_of(BLANK_CHARS) == std::string::npos)
        return false;
    return from_string(str, dest);
}
/* T3.25. The stricter form, recommended for NEW code: any failure - blank,
 * unreadable, only partially readable, or overflowing - yields `def`.
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
