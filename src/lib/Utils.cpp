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
#include "Utils.h"

#include <tcpsocket.h>
#include "libuvw.h"

#include "sole.hpp"

using namespace Utils;

double Utils::roundValue(double value, int precision)
{
    double tmp = pow(10, precision);
    return (double)round(value * tmp) / tmp;
}

void Utils::parseParamsItemList(string l, vector<Params> &res, int start_at)
{
    vector<string> tokens;
    split(l, tokens);
    Params item;

    for (unsigned int i = start_at;i < tokens.size();i++)
    {
        string tmp = tokens[i];
        vector<string> tk;
        split(tmp, tk, ":", 2);

        if (tk.size() != 2) continue;

        if (item.Exists(tk[0]))
        {
            res.push_back(item);
            item.clear();
        }

        item.Add(tk[0], tk[1]);
    }

    if (item.size() > 0)
        res.push_back(item);
}

void Utils::Watchdog(std::string fname)
{
    std::string file = "/tmp/wd_" + fname;

    std::ifstream f(file.c_str());

    if (f.fail())
    {
        std::ofstream of(file.c_str());

        of << "wd_" << fname;
        of.close();
    }

    f.close();
}

bool Utils::argvOptionCheck(char **begin, char **end, const std::string &option)
{
    return std::find(begin, end, option) != end;
}

char *Utils::argvOptionParam(char **begin, char **end, const std::string &option)
{
    char ** itr = std::find(begin, end, option);
    if (itr != end && ++itr != end)
        return *itr;
    return NULL;
}

string Utils::getFileContent(const char *filename)
{
    ifstream ifs(filename, ios::in | ios::binary | ios::ate);
    if (!ifs) return "";

    ifstream::pos_type filesize = ifs.tellg();
    ifs.seekg(0, ios::beg);

    //filesize can be 0 (empty file) or -1 (tellg() failure); &buff[0] on an
    //empty vector is undefined behaviour, so bail out before indexing.
    if (filesize <= 0)
        return "";

    vector<char> buff(filesize);
    ifs.read(&buff[0], filesize);

    return string(&buff[0], filesize);
}

string Utils::getFileContentBase64(const char *filename)
{
    ifstream ifs(filename, ios::in | ios::binary | ios::ate);
    if (!ifs) return "";

    ifstream::pos_type filesize = ifs.tellg();
    ifs.seekg(0, ios::beg);

    //Same empty/negative-size guard as getFileContent() above.
    if (filesize <= 0)
        return "";

    vector<char> buff(filesize);
    ifs.read(&buff[0], filesize);

    return Utils::Base64_encode(&buff[0], filesize);
}

unsigned int Utils::getUptime()
{
#if defined(__linux__) || defined(__linux) || defined(linux)
    struct sysinfo info;
    if (sysinfo(&info) != 0)
        return -1;
    return info.uptime;
#elif defined(macintosh) || defined(__APPLE__) || defined(__APPLE_CC__)
    struct timeval boottime;
    size_t len = sizeof(boottime);
    int mib[2] = { CTL_KERN, KERN_BOOTTIME };
    if (sysctl(mib, 2, &boottime, &len, NULL, 0) < 0)
        return -1;
    return time(NULL) - boottime.tv_sec;
#elif (defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)) && defined(CLOCK_UPTIME)
    struct timespec ts;
    if (clock_gettime(CLOCK_UPTIME, &ts) != 0)
        return -1;
    return ts.tv_sec;
#else
    return 0;
#endif
}

string Utils::createRandomUuid()
{
    //sole is already vendored in the tree and used for the very same need
    //elsewhere (ActionPush). It draws its 122 random bits from
    //std::random_device, the OS CSPRNG on linux, and unlike the generator that
    //used to live here it returns a real RFC 4122 v4 uuid: version and variant
    //nibbles are set, so the value is not mistaken for another uuid flavour.
    return sole::uuid4().str();
}

string Utils::getTmpFilename(const string &ext, const string &prefix)
{
    //The previous rand()-guessed name + exists()-then-open loop left a
    //TOCTOU/symlink window between the check and the caller's open().
    //mkstemp()'s O_EXCL creation is atomic: the name is reserved for us by
    //the time it returns, and it refuses to follow a pre-planted symlink.
    string tmpl = "/tmp/calaos" + prefix + "_XXXXXX." + ext;
    vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');

    int suffixLen = static_cast<int>(ext.size()) + 1; //+1 for the '.'
    int fd = mkstemps(buf.data(), suffixLen);
    if (fd < 0)
    {
        //Callers have to cope with the empty string: /tmp being full, read only
        //or missing is not something they can guess otherwise
        cErrorDom("system") << "getTmpFilename: mkstemps(" << tmpl << ") failed: "
                            << strerror(errno) << " (errno " << errno << ")";
        return "";
    }
    close(fd);

    return string(buf.data());
}

double Utils::getMainLoopTime()
{
    return uvw::Loop::getDefault()->now().count() / 1000.0;
}
