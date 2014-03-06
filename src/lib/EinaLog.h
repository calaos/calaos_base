#ifndef _EINA_LOG_H_
#define _EINA_LOG_H_

#include <string>
#include <sstream>
#include <Eina.h>

using namespace std;

#define Debug()		EinaDebug(__FILE__, __PRETTY_FUNCTION__, __LINE__)		
#define Info()		EinaInfo(__FILE__, __PRETTY_FUNCTION__, __LINE__)
#define Warning()	EinaWarning(__FILE__, __PRETTY_FUNCTION__, __LINE__)
#define Error()		EinaError(__FILE__, __PRETTY_FUNCTION__, __LINE__)
#define Critical()	EinaCritical(__FILE__, __PRETTY_FUNCTION__, __LINE__)

namespace efl
{
namespace eina
{
namespace log
{

class LogStream
{
private:
	int domain;
	string message, file, function;
	int line;
	Eina_Log_Level level;

public:
	template<typename T>
	LogStream& operator << (const T& obj)
	{
		ostringstream stream;
		stream << obj;
		message += stream.str();
		return *this;
	}
	LogStream(int d, string f, string fn, int ln, Eina_Log_Level l):
		domain(d),
		file(f),
		function(fn),
		line(ln),
		level(l)
		{}

	~LogStream()
	{
		eina_log_print(domain, level, file.c_str(), function.c_str(), line, "%s", message.c_str());
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
