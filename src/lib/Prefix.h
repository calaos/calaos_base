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

    std::string binDirectoryGet();
    std::string libDirectoryGet();
    std::string dataDirectoryGet();
    std::string localeDirectoryGet();

private:
    Eina_Prefix *pfx;
    Prefix(int argc, char **argv);
};

#endif // CPREFIX_H
