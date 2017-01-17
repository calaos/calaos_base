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
#ifndef __DATA_LOGGER_H
#define __DATA_LOGGER_H

#include <IOBase.h>

/* DISABLED, need to be rewritten once */

namespace Calaos
{

class DataLogger
{
private:
    DataLogger();

    void initEetDescriptors();
    void releaseEetDescriptors();
//    Eet_File *ef;
//    Eina_Hash *hash_values;
public:
    static DataLogger &Instance()
    {
        static DataLogger inst;
        return inst;
    }
    ~DataLogger();

    void log(IOBase *io);
};

}
#endif /* __DATA_LOGGER_H */
