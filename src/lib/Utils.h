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

#include "Constants.h"
#include "MemMacros.h"
#include "LogSetup.h"
#include "StringUtils.h"

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
namespace Utils
{

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
//Batched update: every key of toSet is created or updated and every key of
//toDelete is removed, in a single load/modify/atomic write cycle. The file is
//reloaded at call time under the lock, so the keys that are in neither list
//keep the value another process may have given them in the meantime.
bool set_config_options(const Params &toSet, const std::vector<std::string> &toDelete = {});
void Watchdog(std::string fname);

string createRandomUuid();

//Parse command line options
bool argvOptionCheck(char **begin, char **end, const std::string &option);
char *argvOptionParam(char **begin, char **end, const std::string &option);

string getFileContent(const char *filename);
string getFileContentBase64(const char *filename);
unsigned int getUptime();

string getTmpFilename(const string &ext = "tmp", const string &prefix = "_tmp");

double getMainLoopTime();

inline bool fileExists(const std::string &filename)
{
    std::ifstream file(filename);
    return file.good();
}
//Some usefull fonctors
struct Delete
{
    template <class T> void operator ()(T *&p) const
    {
        DELETE_NULL(p)
    }
};

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
