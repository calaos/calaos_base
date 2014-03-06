/******************************************************************************
 **  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
 **
 **  This file is part of Calaos Home.
 **
 **  Calaos Home is free software; you can redistribute it and/or modify
 **  it under the terms of the GNU General Public License as published by
 **  the Free Software Foundation; either version 3 of the License, or
 **  (at your option) any later version.
 **
 **  Calaos Home is distributed in the hope that it will be useful,
 **  but WITHOUT ANY WARRANTY; without even the implied warranty of
 **  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 **  GNU General Public License for more details.
 **
 **  You should have received a copy of the GNU General Public License
 **  along with Foobar; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/
//-----------------------------------------------------------------------------
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

//This is for logging
#include <syslog.h>
#include <EinaLog.h>

using namespace efl::eina::log;

#include <log4cpp/Category.hh>
#include <log4cpp/Appender.hh>
#include <log4cpp/OstreamAppender.hh>
#include <log4cpp/FileAppender.hh>
#include <log4cpp/Layout.hh>
#include <log4cpp/BasicLayout.hh>
#include <log4cpp/Priority.hh>
#include <log4cpp/NDC.hh>
#include <log4cpp/PatternLayout.hh>
#include <log4cpp/PropertyConfigurator.hh>

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
# define _(x) gettext(x)
#else
# define _(x) (x)
#endif

//-----------------------------------------------------------------------------
using namespace std;

#ifndef uint
typedef unsigned int uint;
#endif

using namespace log4cpp;
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

#define DEFAULT_THEME           PACKAGE_DATA_DIR"/calaos/themes/default.edj"

#define DEFAULT_URL             "http://update.calaos.fr/fwupdate.xml"
#define CALAOS_NETWORK_URL      "https://www.calaos.fr/calaos_network"
#define CALAOS_WEBSITE_URL      "http://www.calaos.fr"
#define CALAOS_CONTACT_EMAIL    "contact@calaos.fr"
#define CALAOS_COPYRIGHT_TEXT   "Copyright 2006-2014 Calaos"
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
#define TCP_LISTEN_PORT         4456
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

//Curl callback
int CURL_write_callback(void *buffer, size_t size, size_t nmemb, void *stream);
int CURL_writebuf_callback(void *buffer, size_t size, size_t nmemb, void *stream);
//-----------------------------------------------------------------------------
namespace Utils
{

        void InitLoggingSystem(std::string conf);
        log4cpp::Category &logger(std::string category);

        /*
                This is how to log something:

                Utils::logger("category") << Priority::DEBUG << "my log message" << log4cpp::eol;

                or

                Utils::logger("category").debug("my debug message %s", str);
                Utils::logger("category").error("my error message");
                Utils::logger("category").info("my info message");
                Utils::logger("category").warn("my warning message");
                Utils::logger("category").notice("my notice message");
                Utils::logger("category").crit("my critical message");
                Utils::logger("category").alert("my alert message");
                Utils::logger("category").emerg("my emergency message");
                Utils::logger("category").fatal("my fatal message");

                The category "root" is the main category for logging
                Any other string will create a subcategory

                This is all priority available:

                EMERG
                FATAL
                ALERT
                CRIT
                ERROR
                WARN
                NOTICE
                INFO
                DEBUG
                NOTSET --> this one does not print anything

        */

        bool file_copy(std::string source, std::string dest);

        bool fileExists(std::string filename);

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

        enum CaseSensitivity { CaseInsensitive, CaseSensitive };
        bool strStartsWith(const string &str, const string &needle, Utils::CaseSensitivity cs = Utils::CaseSensitive);

        //Return a value rounded to 2 decimal after the dot
        double roundValue(double value);

        //Parse a result string into an array of Params.
        void parseParamsItemList(string l, vector<Params> &res, int start_at = 0);

        void initConfigOptions(char *configdir = NULL, char *cachedir = NULL, bool quiet = false);

        string getConfigFile(const char *configFile);
        string getCacheFile(const char *cacheFile);

        string get_config_option(string key);
        bool set_config_option(string key, string value);
        bool del_config_option(string key);
        bool get_config_options(Params &options);
        void Watchdog(std::string fname);
        string getHardwareID();

        //Parse command line options
        bool argvOptionCheck(char **begin, char **end, const std::string &option);
        char *argvOptionParam(char **begin, char **end, const std::string &option);

        //!decode a BASE64 string
        std::string Base64_decode(std::string &str);
        void *Base64_decode_data(std::string &str);
        //!encode a BASE64 string
        std::string Base64_encode(std::string &str);
        std::string Base64_encode(void *data, int size);

        string getFileContent(const char *filename);
        string getFileContentBase64(const char *filename);

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

        class DeletorBase
        {
                public:
                virtual ~DeletorBase() {}
                virtual void operator() (void *b) const
                {
                        logger("root") << Priority::CRIT << "DeletorBase() called, this is an error. It should never happen"
                                       << ", because it means the application leaks memory!" << log4cpp::eol;
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
        enum { PLAY, PAUSE, STOP, ERROR, SONG_CHANGE, PLAYLIST_CHANGE, VOLUME_CHANGE };
        typedef enum { UNKNOWN, SLIMSERVER, IRTRANS, CALAOS } SOCKET_TYPE;
        //-----------------------------------------------------------------------------
        enum { VUP, VDOWN, VSTOP, VNONE };
        //-----------------------------------------------------------------------------
        typedef unsigned short UWord;
        //-----------------------------------------------------------------------------
};
//-----------------------------------------------------------------------------
#endif
