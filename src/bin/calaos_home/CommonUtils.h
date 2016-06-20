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
#ifndef COMMONUTILS_H
#define COMMONUTILS_H

#include <Utils.h>
#include <Evas.h>

namespace Utils
{

struct DeleteEvasObject
{
    template <class T> void operator ()(T *&p) const
    {
        DELETE_NULL_FUNC(evas_object_del, p);
    }
};

Evas_Object *createPaddingTable(Evas *evas, Evas_Object *parent, int w, int h, int padding_top_bottom = 1, int padding_side = 1);

}

#endif // COMMONUTILS_H
