#ifndef CPREFIX_H
#define CPREFIX_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <Utils.h>
#include <Eina.h>

// This class is an C++ implementarion of eina_prefix
// When the instance is created, eina try to find in whih prefix
// Calaos has been installed. So when you call dataDirectoryGet, 
// it's able to retrieve the path of data directory.
// For example if you install calaos in : /opt/usr dataDirectoryGet will
// returns /opt/usr/share/calaos as for dataDirectoryGet
// returns /opt/usr/bin as for binDirectoryGet
// returns /opt/usr/lib as for libDirectoryGet
// returns /opt/usr/locale as for localeDirectoryGet

class Prefix
{
public:
    static Prefix &Instance(int argc = 0, char **argv = nullptr)
    {
        static Prefix prefix(argc, argv);
        return prefix;
    }

    ~Prefix();

    string binDirectoryGet();
    string libDirectoryGet();
    string dataDirectoryGet();
    string localeDirectoryGet();

private:
    Eina_Prefix *pfx;
    Prefix(int argc, char **argv);
};

#endif // CPREFIX_H
