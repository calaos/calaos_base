/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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
    pfx = eina_prefix_new(argv[0], NULL, "CALAOS", "calaos", NULL, 
                             PACKAGE_BIN_DIR, PACKAGE_LIB_DIR, 
                             PACKAGE_DATA_DIR, PACKAGE_LOCALE_DIR);
    if (!pfx)
        cError() << "Error in finding preffix";
    else
    {
        cInfo() << "Install preffix is " << eina_prefix_get(pfx);
        cInfo() << "Binaries directory is  " << eina_prefix_bin_get(pfx);
        cInfo() << "Libraries directory is " << eina_prefix_lib_get(pfx);
        cInfo() << "Data directory is " << eina_prefix_data_get(pfx);
        cInfo() << "Locale directory is " << eina_prefix_locale_get(pfx);
    }
}

Prefix::~Prefix()
{
    if (pfx)
        eina_prefix_free(pfx);
}

std::string Prefix::binDirectoryGet()
{ 
    const char *str = eina_prefix_bin_get(pfx); 
    // test returned string to avoir exception
    if (str) 
        return str; 
    else return "";
}

std::string Prefix::libDirectoryGet()
{
    const char *str = eina_prefix_lib_get(pfx); 
    // test returned string to avoir exception
    if (str) 
        return str; 
    else return "";
}

std::string Prefix::dataDirectoryGet()
{
    const char *str = eina_prefix_data_get(pfx); 
    // test returned string to avoir exception
    if (str) 
        return str; 
    else return "";
}

std::string Prefix::localeDirectoryGet()
{
    const char *str = eina_prefix_locale_get(pfx); 
    // test returned string to avoir exception
    if (str) 
        return str; 
    else return "";
}


