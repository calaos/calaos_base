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
#include "CommonUtils.h"
#include <Elementary.h>


Evas_Object *Utils::createPaddingTable(Evas *evas, Evas_Object *parent, int w, int h, int padding_top_bottom, int padding_side)
{
    Evas_Object *table = elm_table_add(parent);
    evas_object_size_hint_min_set(table, w, h);
    evas_object_show(table);

    Evas_Object *pad = evas_object_rectangle_add(evas);
    evas_object_size_hint_min_set(pad, w-10, padding_top_bottom);
    elm_table_pack(table, pad, 1, 0, 1, 1);

    pad = evas_object_rectangle_add(evas);
    evas_object_size_hint_min_set(pad, w-10, padding_top_bottom);
    elm_table_pack(table, pad, 1, 2, 1, 1);

    pad = evas_object_rectangle_add(evas);
    evas_object_size_hint_min_set(pad, padding_side, h);
    elm_table_pack(table, pad, 0, 1, 1, 1);

    pad = evas_object_rectangle_add(evas);
    evas_object_size_hint_min_set(pad, padding_side, h);
    elm_table_pack(table, pad, 2, 1, 1, 1);

    return table;
}
