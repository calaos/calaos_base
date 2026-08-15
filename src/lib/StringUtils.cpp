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
#include "StringUtils.h"

#include <base64.h>

#include <algorithm>
#include <iomanip>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

using namespace Utils;
using namespace std;

string Utils::url_encode(string str)
{
    string ret = "";
    char tmp[10];

    for (uint i = 0;i < str.length();i++)
    {
        if ((str[i] >= 'a' && str[i] <= 'z') ||
            (str[i] >= 'A' && str[i] <= 'Z') ||
            (str[i] >= '0' && str[i] <= '9') ||
            str[i] == '_')
            ret += str[i];
        else
        {
            memset(tmp, '\0', 5);
            sprintf(tmp, "%%%02X", (unsigned char)str[i]);
            ret += tmp;
        }
    }

    return ret;
}

string Utils::url_decode(string str)
{
    string ret = "";

    for (uint i = 0;i < str.length();i++)
    {
        if (str[i] == '%' && isxdigit((int)str[i + 1]) && isxdigit((int)str[i + 2]))
        {
            ret += (char) htoi((char *)str.c_str() + i + 1);
            i += 2;
        }
        else
            ret += str[i];
    }

    return ret;
}

std::string Utils::url_decode2(std::string str)
{
    return url_decode(url_decode(str));
}

//T3.4: invalid base64 (bad chars, broken padding, non-canonical input) now
//yields an empty string instead of a silently truncated one. Signatures kept
//source-compatible; a signature-breaking std::optional API would require
//touching every caller (WebSocket.cpp handshake, Utils.cpp file encode).
std::string Utils::Base64_decode(std::string &str)
{
    return base64_decode_checked(str).value_or(std::string());
}

std::string Utils::Base64_decode_data(std::string &str)
{
    return base64_decode_checked(str).value_or(std::string());
}

std::string Utils::Base64_encode(std::string &str)
{
    return base64_encode(reinterpret_cast<const unsigned char *>(str.data()), str.size());
}

std::string Utils::Base64_encode(void *data, int size)
{
    //negative size used to be converted to a huge unsigned length → OOB read
    if (!data || size <= 0)
        return {};

    return base64_encode(reinterpret_cast<const unsigned char *>(data),
                         static_cast<size_t>(size));
}

int Utils::htoi(char *s)
{
    int value;
    int c;

    c = ((unsigned char *)s)[0];
    if (isupper(c))
        c = tolower(c);
    value = (c >= '0' && c <= '9' ? c - '0' : c - 'a' + 10) * 16;

    c = ((unsigned char *)s)[1];
    if (isupper(c))
        c = tolower(c);
    value += c >= '0' && c <= '9' ? c - '0' : c - 'a' + 10;

    return (value);
}

string Utils::time2string(long s, long ms)
{
    double sec = s;
    int hours = (int)(sec / 3600.0);
    sec -= hours * 3600;
    int min = (int)(sec / 60.0);
    sec -= min * 60;

    stringstream str;

    if (hours == 1)
        str << hours << " " << "heure" << " ";
    if (hours > 1)
        str << hours << " " << "heures" << " ";
    if (min == 1)
        str << min << " " << "minute" << " ";
    if (min > 1)
        str << min << " " << "minutes" << " ";
    if (sec == 1)
    {
        str << sec << " " << "seconde";
        if (ms > 0) str << " ";
    }
    if (sec > 1)
    {
        str << sec << " " << "secondes";
        if (ms > 0) str << " ";
    }
    if (ms > 0)
        str << ms << " " << "ms";

    return str.str();
}

string Utils::time2string_digit(long s, long ms)
{
    double sec = s;
    int hours = (int)(sec / 3600.0);
    sec -= hours * 3600;
    int min = (int)(sec / 60.0);
    sec -= min * 60;

    stringstream str;

    if (hours > 0)
        str << setw(2) << setfill('0') << hours << ":";

    str << setw(2) << setfill('0') << min << ":";
    str << setw(2) << setfill('0') << sec;

    if (ms > 0)
        str << "." << setw(4) << setfill('0') << ms;

    return str.str();
}

void Utils::split(const string &str, vector<string> &tokens, const string &delimiters, int max /* = 0 */)
{
    // Skip delimiters at beginning.
    string::size_type lastPos = str.find_first_not_of(delimiters, 0);
    // Find first "non-delimiter".
    string::size_type pos     = str.find_first_of(delimiters, lastPos);

    int counter = 0;

    while (string::npos != pos || string::npos != lastPos)
    {
        if (counter + 1 >= max && max > 0)
        {
            tokens.push_back(str.substr(lastPos, string::npos));
            break;
        }

        // Found a token, add it to the vector.
        tokens.push_back(str.substr(lastPos, pos - lastPos));
        // Skip delimiters.  Note the "not_of"
        lastPos = str.find_first_not_of(delimiters, pos);
        // Find next "non-delimiter"
        pos = str.find_first_of(delimiters, lastPos);

        counter++;
    }

    while (tokens.size() < (uint)max) tokens.push_back("");
}

void Utils::remove_tag(string &html, const string begin_tag, const string end_tag)
{
    string::size_type start_pos = html.find(begin_tag, 0);

    while (start_pos != string::npos)
    {
        string::size_type end_pos = html.find(end_tag, start_pos);
        if (end_pos == string::npos)
        {
            break;
        }
        else
        {
            end_pos += end_tag.length();
            html.erase(start_pos, end_pos - start_pos);
            start_pos = html.find(begin_tag, 0);
        }
    }
}

void Utils::replace_str(string &source, const string searchstr, const string replacestr)
{
    string::size_type pos = 0;
    while((pos = source.find(searchstr, pos)) != string::npos)
    {
        source.erase(pos, searchstr.length());
        source.insert(pos, replacestr);
        pos += replacestr.length();
    }
}

void Utils::trim_right(std::string &source, const std::string &t)
{
    source.erase(source.find_last_not_of(t) + 1);
}

void Utils::trim_left(std::string &source, const std::string &t)
{
    source.erase(0, source.find_first_not_of(t));
}

bool Utils::strContains(const string &str, const string &needle, CaseSensitivity cs)
{
    if (needle.empty())
        return true;

    if (needle.length() > str.length())
        return false;

    if (cs == Utils::CaseSensitive)
        return str.find(needle) != std::string::npos;

    string s = str;
    return str_to_lower(s).find(str_to_lower(needle)) != std::string::npos;
}

bool Utils::strStartsWith(const string &str, const string &needle, Utils::CaseSensitivity cs)
{
    if (needle.empty())
        return true;

    if (needle.length() > str.length())
        return false;

    if (cs == Utils::CaseSensitive)
        return memcmp(str.c_str(), needle.c_str(), needle.length()) == 0;

    for (uint i = 0;i < needle.length();i++)
    {
        if (tolower(str[i]) != tolower(needle[i]))
            return false;
    }

    return true;
}

string Utils::str_to_lower(std::string s)
{
     std::transform(s.begin(), s.end(), s.begin(), Utils::to_lower());
     return s;
}

string Utils::str_to_upper(std::string s)
{
     std::transform(s.begin(), s.end(), s.begin(), Utils::to_upper());
     return s;
}

string Utils::trim(const string &str)
{
    if (str.size() == 0)
        return str;
    if (!::isspace(str[0]) && !::isspace(str[str.length() - 1]))
        return str;

    uint start = 0;
    uint end = str.length() - 1;
    uint len;

    while (start <= end && ::isspace(str[start]))
        start++;

    if (start <= end)
    {
        while (end && ::isspace(str[end]))
            end--;
    }

    len = end - start + 1;
    if (len <= 0)
        return string();

    return string(str, start, len);
}

string Utils::escape_quotes(const string &s)
{
    string after;
    after.reserve(s.length() + 4);

    for (string::size_type i = 0; i < s.length(); i++)
    {
        switch (s[i])
        {
            case '"':
            case '\\':
                after += '\\';
                // Fall through.

            default:
                after += s[i];
        }
    }

    return after;
}

string Utils::escape_space(const string &s)
{

    int count = 0;

    for (string::size_type i = 0; i < s.length(); i++)
    {
        if (isspace(s[i]))
            count ++;
    }

    string ret;
    ret.reserve(s.length() + count);

    for (string::size_type i = 0; i < s.length(); i++)
    {
        switch (s[i])
        {
            case ' ':
                ret += '\\';
                // Fall through.
            default:
                ret += s[i];
        }
    }

    return ret;
}

CStrArray::CStrArray(const string &str_split)
{
    Utils::split(str_split, m_strings, " ");
    updateNative();
}

CStrArray::CStrArray(const vector<string> &lst):
    m_strings(lst)
{
    updateNative();
}

CStrArray::~CStrArray()
{
    delete [] m_data;
}

void CStrArray::updateNative()
{
    delete [] m_data;
    m_data = new const char*[m_strings.size() + 1];
    stringstream sstr;

    unsigned index = 0;
    for (auto it = m_strings.begin();it != m_strings.end();it++)
    {
        sstr << *it << " ";
        m_data[index] = it->c_str();
        index++;
    }
    m_data[index] = NULL;
    m_tostring = sstr.str();
}

std::string CStrArray::toString()
{
    return m_tostring;
}

/* T2.17: moved from IPCam::maskUrlCredentials (which now delegates here) so
 * UrlDownloader can mask every URL it logs. Contract pinned by IPCamUrl_test
 * and UrlDownloader_test. */
std::string Utils::maskUrlCredentials(const std::string &url)
{
    static const string mask = "*****";
    string masked = url;

    //userinfo password: scheme://user:secret@host/... → keep user, mask secret
    auto schemeEnd = masked.find("://");
    if (schemeEnd != string::npos)
    {
        auto authStart = schemeEnd + 3;
        auto authEnd = masked.find('/', authStart);
        auto at = masked.find('@', authStart);
        if (at != string::npos && (authEnd == string::npos || at < authEnd))
        {
            auto colon = masked.find(':', authStart);
            if (colon != string::npos && colon < at)
                masked.replace(colon + 1, at - colon - 1, mask);
        }
    }

    //values of credential-bearing query parameters
    static const char *const credKeys[] =
    {
        "usr", "pwd", "user", "username", "password",
        "passwd", "account", "loginuse", "loginpas", "_sid",
    };

    auto q = masked.find('?');
    if (q == string::npos)
        return masked;

    string out = masked.substr(0, q + 1);
    const string query = masked.substr(q + 1);

    size_t pos = 0;
    for (;;)
    {
        auto amp = query.find('&', pos);
        string tok = (amp == string::npos)?
                     query.substr(pos):
                     query.substr(pos, amp - pos);

        auto eq = tok.find('=');
        if (eq != string::npos)
        {
            const string key = tok.substr(0, eq);
            const string lkey = Utils::str_to_lower(key);
            for (const char *ck: credKeys)
            {
                if (lkey == ck)
                {
                    tok = key + '=' + mask;
                    break;
                }
            }
        }

        out += tok;
        if (amp == string::npos)
            break;
        out += '&';
        pos = amp + 1;
    }

    return out;
}
