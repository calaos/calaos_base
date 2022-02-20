/******************************************************************************
 **  Copyright (c) 2006-2018, Calaos. All Rights Reserved.
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

#include <Prefix.h>

Prefix::Prefix(int argc, char **argv)
{
    cInfo() << "Binaries directory is: " << binDirectoryGet();
    cInfo() << "Libraries directory is: " << libDirectoryGet();
    cInfo() << "Data directory is: " << dataDirectoryGet();
}

string Prefix::binDirectoryGet()
{
    char *str = getenv("CALAOS_BIN_PREFIX");
    if (str != NULL)
        return str;
    else
        return PACKAGE_BIN_DIR;
}

string Prefix::libDirectoryGet()
{
    char *str = getenv("CALAOS_LIB_PREFIX");
    if (str != NULL)
        return str;
    else
        return PACKAGE_LIB_DIR;
}

string Prefix::dataDirectoryGet()
{
    char *str = getenv("CALAOS_DATA_PREFIX");
    if (str != NULL)
        return str;
    else
        return PACKAGE_DATA_DIR;
}
