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

//E4.4cd: the config reader/writer (CalaosConfig, Room, Rule, the IOs and the
//rule conditions/actions) runs on pugixml. TinyXML is still included here for
//src/lib/ConfigStore.cpp (local_config.xml), the last consumer left in the
//tree; E4.4d-bis ports it and E4.4e drops src/lib/TinyXML entirely.
#include <TinyXML/tinyxml.h>
#include "XmlUtils.h"
#include <sigc++/sigc++.h>

#include "ColorUtils.h"
#include "FileUtils.h"

//This is for logging
#include <Logger.h>

#include "json.hpp"
using Json = nlohmann::json;

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

#ifndef uint
typedef unsigned int uint;
#endif

#include "Constants.h"
#include "MemMacros.h"
#include "LogSetup.h"
#include "StringUtils.h"
#include "ConfigStore.h"
#include "SystemInfo.h"

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
namespace Utils
{

// Return a value rounded to precision decimal after the dot
double roundValue(double value, int precision);

//Parse a result string into an array of Params.
void parseParamsItemList(std::string l, std::vector<Params> &res, int start_at = 0);

//Parse command line options
bool argvOptionCheck(char **begin, char **end, const std::string &option);
char *argvOptionParam(char **begin, char **end, const std::string &option);

std::string getFileContent(const char *filename);
std::string getFileContentBase64(const char *filename);

std::string getTmpFilename(const std::string &ext = "tmp", const std::string &prefix = "_tmp");

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
