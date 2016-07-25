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
#include <Eina.h>

using namespace std;

#define EinaLogDebug(a)         a->EinaDebug(__FILE__, __PRETTY_FUNCTION__, __LINE__)
#define EinaLogInfo(a)          a->EinaInfo(__FILE__, __PRETTY_FUNCTION__, __LINE__)
#define EinaLogWarning(a)       a->EinaWarning(__FILE__, __PRETTY_FUNCTION__, __LINE__)
#define EinaLogError(a)         a->EinaError(__FILE__, __PRETTY_FUNCTION__, __LINE__)
#define EinaLogCritical(a)      a->EinaCritical(__FILE__, __PRETTY_FUNCTION__, __LINE__)

namespace efl
{
namespace eina
{
namespace log
{

class LogStream
{
private:
    struct LogData
    {
        LogData(int d, string f, string fn, int ln, Eina_Log_Level l):
            domain(d),
            file(f),
            function(fn),
            line(ln),
            level(l)
        {}
        int domain;
        string file, function;
        int line;
        Eina_Log_Level level;
        ostringstream stream;
    } *logData;

public:
    template<typename T>
    LogStream& operator << (const T& obj)
    {
        logData->stream << obj;
        return *this;
    }
    LogStream(int d, string f, string fn, int ln, Eina_Log_Level l):
        logData(new LogData(d, f, fn, ln, l))
    {}

    ~LogStream();
};

class EinaLog
{
private:
    int domain;
    string domainName;
    bool coutOutput = false;

public:
    EinaLog(string dname="EinaLog"):
        domain(-1),
        domainName(dname)
    {
        eina_init();
        if(domain<0)
        {
            domain = eina_log_domain_register(domainName.c_str(), EINA_COLOR_CYAN);
            if(domain<0) domain = EINA_LOG_DOMAIN_GLOBAL;
        }
    }
    EinaLog(bool cout_out):
        domain(-1),
        coutOutput(cout_out)
    { }
    ~EinaLog()
    {
        if (!coutOutput)
        {
            if(domain != EINA_LOG_DOMAIN_GLOBAL)
                eina_log_domain_unregister(domain);
            eina_shutdown();
        }
    }

    LogStream EinaDebug(string file, string function, int line) const
    {
        if (coutOutput) return LogStream(-1, file, function, line, EINA_LOG_LEVEL_UNKNOWN);
        return LogStream(domain, file, function, line, EINA_LOG_LEVEL_DBG);
    }
    LogStream EinaInfo(string file, string function, int line) const
    {
        if (coutOutput) return LogStream(-1, file, function, line, EINA_LOG_LEVEL_UNKNOWN);
        return LogStream(domain, file, function, line, EINA_LOG_LEVEL_INFO);
    }
    LogStream EinaCritical(string file, string function, int line) const
    {
        if (coutOutput) return LogStream(-1, file, function, line, EINA_LOG_LEVEL_UNKNOWN);
        return LogStream(domain, file, function, line, EINA_LOG_LEVEL_CRITICAL);
    }
    LogStream EinaError(string file, string function, int line) const
    {
        if (coutOutput) return LogStream(-1, file, function, line, EINA_LOG_LEVEL_UNKNOWN);
        return LogStream(domain, file, function, line, EINA_LOG_LEVEL_ERR);
    }
    LogStream EinaWarning(string file, string function, int line) const
    {
        if (coutOutput) return LogStream(-1, file, function, line, EINA_LOG_LEVEL_UNKNOWN);
        return LogStream(domain, file, function, line, EINA_LOG_LEVEL_WARN);
    }
};

}
}
}


#endif
