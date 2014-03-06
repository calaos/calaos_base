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

        ~LogStream()
        {
                eina_log_print(logData->domain,
                               logData->level,
                               logData->file.c_str(),
                               logData->function.c_str(),
                               logData->line,
                               "%s",
                               logData->stream.str().c_str());

                delete logData;
        }
};

class EinaLog
{
private:
        int domain;
        string domainName;

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
        ~EinaLog()
        {
                if(domain != EINA_LOG_DOMAIN_GLOBAL)
                        eina_log_domain_unregister(domain);
        }

        LogStream EinaDebug(string file, string function, int line) const
        {
                return LogStream(domain, file, function, line, EINA_LOG_LEVEL_DBG);
        }
        LogStream EinaInfo(string file, string function, int line) const
        {
                return LogStream(domain, file, function, line, EINA_LOG_LEVEL_INFO);
        }
        LogStream EinaCritical(string file, string function, int line) const
        {
                return LogStream(domain, file, function, line, EINA_LOG_LEVEL_CRITICAL);
        }
        LogStream EinaError(string file, string function, int line) const
        {
                return LogStream(domain, file, function, line, EINA_LOG_LEVEL_ERR);
        }
        LogStream EinaWarning(string file, string function, int line) const
        {
                return LogStream(domain, file, function, line, EINA_LOG_LEVEL_WARN);
        }
};

}
}
}


#endif
