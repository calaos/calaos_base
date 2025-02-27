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
#ifndef CUTILS_H
#define CUTILS_H
//-----------------------------------------------------------------------------
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string>
#include <vector>
#include <list>
#include <queue>
#include <map>
#include <stack>
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <iomanip>
#include <bitset>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <unordered_map>
#include <stdexcept>
#include <memory>
#include <functional>
#include <ctime>
#include <locale>
#ifndef _WIN32
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <pthread.h>
#include <pwd.h>
#endif
#include <Params.h>
#include <base64.h>

#include <TinyXML/tinyxml.h>
#include <sigc++/sigc++.h>

#include "ColorUtils.h"
#include "FileUtils.h"

//This is for logging
#include <Logger.h>

#include "json.hpp"
using Json = nlohmann::json;

#if defined(__linux__) || defined(__linux) || defined(linux)
#include <sys/sysinfo.h>
#elif defined(macintosh) || defined(__APPLE__) || defined(__APPLE_CC__)
#include <time.h>
#include <errno.h>
#include <sys/sysctl.h>
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
#include <time.h>
#endif

#ifdef EAPI
# undef EAPI
#endif /* ifdef EAPI */

#ifdef _WIN32
# ifdef EFL_EET_BUILD
#  ifdef DLL_EXPORT
#   define EAPI __declspec(dllexport)
#  else /* ifdef DLL_EXPORT */
#   define EAPI
#  endif /* ! DLL_EXPORT */
# else /* ifdef EFL_EET_BUILD */
#  define EAPI __declspec(dllimport)
# endif /* ! EFL_EET_BUILD */
#else /* ifdef _WIN32 */
# ifdef __GNUC__
#  if __GNUC__ >= 4
#   define EAPI __attribute__ ((visibility("default")))
#  else /* if __GNUC__ >= 4 */
#   define EAPI
#  endif /* if __GNUC__ >= 4 */
# else /* ifdef __GNUC__ */
#  define EAPI
# endif /* ifdef __GNUC__ */
#endif /* ! _WIN32 */

#ifdef HAVE_GETTEXT
#include <libintl.h>
# define _(x) gettext(x)
# define gettext_noop(String) String
# define N_(String) gettext_noop (String)
#else
# define _(x) (x)
# define N_(x) (x)
#endif

//-----------------------------------------------------------------------------
using namespace std;

#ifndef uint
typedef unsigned int uint;
#endif

//-----------------------------------------------------------------------------
// Some common defines
//-----------------------------------------------------------------------------
#define PREFIX_CONFIG_PATH      ETC_DIR"/calaos"
#define ETC_CONFIG_PATH         "/etc/calaos"
#define HOME_CONFIG_PATH        ".config/calaos"
#define HOME_CACHE_PATH         ".cache/calaos"

#define LOCAL_CONFIG            "local_config.xml"
#define IO_CONFIG               "io.xml"
#define RULES_CONFIG            "rules.xml"
#define WIDGET_CONFIG           "widgets.xml"

#define DEFAULT_URL             "http://update.calaos.fr/fwupdate.xml"
#define CALAOS_NETWORK_URL      "https://www.calaos.fr/calaos_network"
#define CALAOS_WEBSITE_URL      "http://www.calaos.fr"
#define CALAOS_CONTACT_EMAIL    "contact@calaos.fr"
#define CALAOS_COPYRIGHT_TEXT   "Copyright (c) 2006-2025, Calaos. All Rights Reserved."
#define ZONETAB                 "/usr/share/zoneinfo/zone.tab"
#define CURRENT_ZONE            "/etc/timezone"
#define LOCALTIME               "/etc/localtime"
#define ZONEPATH                "/usr/share/zoneinfo/"

// The size of the window. For now The Calaos touchscreen gui is only designed
// to fit a screen of 1024x768 pixels.
#define WIDTH   1024
#define HEIGHT   768
//-----------------------------------------------------------------------------
#define PI 3.14159265358979323846
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
#define RED "\x1b[31;01m"
#define DARKRED "\x1b[31;06m"
#define RESET "\x1b[0m"
#define GREEN "\x1b[32;06m"
#define YELLOW "\x1b[33;06m"

#define WAGO_LISTEN_PORT        4646
#define BCAST_UDP_PORT          4545
#define JSONAPI_PORT            5454

#define WAGO_KNX_START_ADDRESS          6144
#define WAGO_841_START_ADDRESS          4096

/* These macros are usefull to delete/free/... an object and set it to NULL */
#define DELETE_NULL(p) \
    if (p) { delete p; p = NULL; }

#define FREE_NULL(p) \
    if (p) { free(p); p = NULL; }

#define DELETE_NULL_FUNC(fn, p) \
    if (p) { fn(p); p = NULL; }

#define VAR_UNUSED(x) (void)x;

//-----------------------------------------------------------------------------

//Log macros
#define cDebug() LoggerDebug(Utils::calaosLogger())
#define cInfo() LoggerInfo(Utils::calaosLogger())
#define cWarning() LoggerWarning(Utils::calaosLogger())
#define cError() LoggerError(Utils::calaosLogger())
#define cCritical() LoggerCritical(Utils::calaosLogger())

#define cDebugDom(domain) LoggerDebug(Utils::calaosLogger(domain))
#define cInfoDom(domain) LoggerInfo(Utils::calaosLogger(domain))
#define cWarningDom(domain) LoggerWarning(Utils::calaosLogger(domain))
#define cErrorDom(domain) LoggerError(Utils::calaosLogger(domain))
#define cCriticalDom(domain) LoggerCritical(Utils::calaosLogger(domain))

//-----------------------------------------------------------------------------
namespace Utils
{
void initLogger(const char *default_domain);
void freeLoggers();
Logger *calaosLogger(const char *domain = nullptr);

string url_encode(string str);
string url_decode(string str);
std::string url_decode2(std::string str); //decode 2 times
int htoi(char *s);
string time2string(long s, long ms = 0);
string time2string_digit(long s, long ms = 0);

/* usefull string utilities */
void split(const string &str, vector<string> &tokens, const string &delimiters = " ", int max = 0);
void remove_tag(string &source, const string begin_tag, const string end_tag);
void replace_str(string &source, const string searchstr, const string replacestr);
void trim_right(std::string &source, const std::string &t);
void trim_left(std::string &source, const std::string &t);
string trim(const string &str);
string escape_quotes(const string &s);
string escape_space(const string &s);

enum CaseSensitivity { CaseInsensitive, CaseSensitive };
bool strContains(const string &str, const string &needle, Utils::CaseSensitivity cs = Utils::CaseSensitive);
bool strStartsWith(const string &str, const string &needle, Utils::CaseSensitivity cs = Utils::CaseSensitive);

// Return a value rounded to precision decimal after the dot
double roundValue(double value, int precision);

//Parse a result string into an array of Params.
void parseParamsItemList(string l, vector<Params> &res, int start_at = 0);

void initConfigOptions(char *configdir = NULL, char *cachedir = NULL, bool quiet = false);

string getConfigPath();
string getCachePath();
string getConfigFile(const char *configFile);
string getCacheFile(const char *cacheFile);

string get_config_option(string key, bool no_logger_out = false);
bool set_config_option(string key, string value);
bool del_config_option(string key);
bool get_config_options(Params &options);
void Watchdog(std::string fname);
string getHardwareID();

string createRandomUuid();

//Parse command line options
bool argvOptionCheck(char **begin, char **end, const std::string &option);
char *argvOptionParam(char **begin, char **end, const std::string &option);

//!decode a BASE64 string
std::string Base64_decode(std::string &str);
std::string Base64_decode_data(std::string &str);
//!encode a BASE64 string
std::string Base64_encode(std::string &str);
std::string Base64_encode(void *data, int size);

string getFileContent(const char *filename);
string getFileContentBase64(const char *filename);
unsigned int getUptime();

string getTmpFilename(const string &ext = "tmp", const string &prefix = "_tmp");

double getMainLoopTime();

class CStrArray
{
public:
    CStrArray() {}
    CStrArray(const string &str_split);
    CStrArray(const vector<string> &lst);
    ~CStrArray();

    const char *at(std::size_t pos) { return m_strings.at(pos).c_str(); }
    void set(const vector<string> &lst);
    std::size_t count() const { return m_strings.size(); }
    const char **constData() const { return m_data; }
    char **data() { return (char **)m_data; }

    std::string toString();

private:
    vector<string> m_strings;
    string m_tostring;
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
struct Delete
{
    template <class T> void operator ()(T *&p) const
    {
        DELETE_NULL(p)
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

std::string str_to_lower(std::string s);
std::string str_to_upper(std::string s);

class DeletorBase
{
public:
    virtual ~DeletorBase() {}
    virtual void operator() (void *b) const
    {
        cCritical() << "DeletorBase() called, this is an error. It should never happen"
                    << ", because it means the application leaks memory!";
    }
};

//Fonctor to delete a void * with a specified type
template<typename T>
class DeletorT: public DeletorBase
{
public:
    virtual void operator() (void *b) const
    {
        T base = reinterpret_cast<T>(b);
        if (base) delete base;
    }
};
//-----------------------------------------------------------------------------
//Used by the CURL callback
typedef struct file_curl
{
    char *fname;
    FILE *fp;
} File_CURL;
typedef struct buffer_curl
{
    void *buffer;
    unsigned int bufsize;
} Buffer_CURL;
//-----------------------------------------------------------------------------
class line_exception : public std::exception
{
private:
    std::string msg;

public:
    line_exception( const char * Msg, int Line )
    {
        std::ostringstream oss;
        oss << "Error line " << Line << " : " << Msg;
        msg = oss.str();
    }

    virtual ~line_exception() throw()
    { }

    virtual const char * what() const throw()
    {
        return msg.c_str();
    }
};


//-----------------------------------------------------------------------------
typedef enum { TBOOL, TINT, TSTRING, TUNKNOWN } DATA_TYPE;
enum { AudioPlay, AudioPause, AudioStop, AudioError, AudioSongChange, AudioPlaylistChange, AudioVolumeChange };
typedef enum { UNKNOWN, SLIMSERVER, IRTRANS, CALAOS } SOCKET_TYPE;
//-----------------------------------------------------------------------------
enum { SHUTTER_UP, SHUTTER_DOWN, SHUTTER_STOP, SHUTTER_NONE };
//-----------------------------------------------------------------------------
typedef unsigned short UWord;
//-----------------------------------------------------------------------------
}
//-----------------------------------------------------------------------------
#endif
