/******************************************************************************
 **  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
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
#ifndef _EINA_LOG_H_
#define _EINA_LOG_H_

#include <string>
#include <sstream>

using namespace std;

#define LoggerDebug(a)         a->LogDebug(__FILE__, __PRETTY_FUNCTION__, __LINE__)
#define LoggerInfo(a)          a->LogInfo(__FILE__, __PRETTY_FUNCTION__, __LINE__)
#define LoggerWarning(a)       a->LogWarning(__FILE__, __PRETTY_FUNCTION__, __LINE__)
#define LoggerError(a)         a->LogError(__FILE__, __PRETTY_FUNCTION__, __LINE__)
#define LoggerCritical(a)      a->LogCritical(__FILE__, __PRETTY_FUNCTION__, __LINE__)

class LogStream
{
private:
    struct LogData
    {
        LogData(const string &d, const string &f, const string &fn, int ln, int l):
            domain(d),
            file(f),
            function(fn),
            line(ln),
            level(l)
        {}
        string domain;
        string file, function;
        int line;
        int level;
        ostringstream stream;
    } *logData;

    bool isTerminal();

    // Colors
    const char* colorBlack()      { return isTerminal() ? "\e[30m" : ""; }
    const char* colorRed()        { return isTerminal() ? "\e[31m" : ""; }
    const char* colorGreen()      { return isTerminal() ? "\e[32m" : ""; }
    const char* colorYellow()     { return isTerminal() ? "\e[33m" : ""; }
    const char* colorBlue()       { return isTerminal() ? "\e[34m" : ""; }
    const char* colorPurple()     { return isTerminal() ? "\e[35m" : ""; }
    const char* colorCyan()       { return isTerminal() ? "\e[36m" : ""; }
    const char* colorLightGray() { return isTerminal() ? "\e[37m" : ""; }
    const char* colorWhite()      { return isTerminal() ? "\e[37m" : ""; }
    const char* colorLightRed()  { return isTerminal() ? "\e[91m" : ""; }
    const char* colorDim()        { return isTerminal() ? "\e[2m"  : ""; }

    // Formating
    const char* colorBold()       { return isTerminal() ? "\e[1m" : ""; }
    const char* colorUnderline()  { return isTerminal() ? "\e[4m" : ""; }

    // You should end each line with this!
    const char* colorReset()      { return isTerminal() ? "\e[0m" : ""; }

public:
    template<typename T>
    LogStream& operator << (const T& obj)
    {
        logData->stream << obj;
        return *this;
    }
    LogStream(const string &d, const string &f, const string &fn, int ln, int l):
        logData(new LogData(d, f, fn, ln, l))
    {}

    ~LogStream();
};

class Logger
{
private:
    string domainName;

public:
    Logger(string dname="calaos_server"):
        domainName(dname)
    {}

    enum {
        LOG_LEVEL_UNKNOWN = 0,
        LOG_LEVEL_CRITICAL,
        LOG_LEVEL_ERROR,
        LOG_LEVEL_WARNING,
        LOG_LEVEL_INFO,
        LOG_LEVEL_DEBUG,
    };

    LogStream LogDebug(const string &file, const string & function, int line) const
    {
        return LogStream(domainName, file, function, line, LOG_LEVEL_DEBUG);
    }
    LogStream LogInfo(const string & file, const string & function, int line) const
    {
        return LogStream(domainName, file, function, line, LOG_LEVEL_INFO);
    }
    LogStream LogCritical(const string & file, const string & function, int line) const
    {
        return LogStream(domainName, file, function, line, LOG_LEVEL_CRITICAL);
    }
    LogStream LogError(const string & file, const string & function, int line) const
    {
        return LogStream(domainName, file, function, line, LOG_LEVEL_ERROR);
    }
    LogStream LogWarning(const string & file, const string & function, int line) const
    {
        return LogStream(domainName, file, function, line, LOG_LEVEL_WARNING);
    }
};

#endif
