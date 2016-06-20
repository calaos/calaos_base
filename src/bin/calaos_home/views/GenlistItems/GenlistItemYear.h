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

#ifndef GenlistItemYear_H
#define GenlistItemYear_H

#include <Utils.h>
#include <GenlistItemBase.h>


class GenlistItemYear: public GenlistItemBase
{
private:
    AudioPlayer *player;
    int item_id;
    Params item_infos;
    bool in_query;

    void yearItemGet_cb(Params &infos);

public:
    GenlistItemYear(Evas *evas, Evas_Object *parent, AudioPlayer *player, int item_id, void *data = NULL);
    virtual ~GenlistItemYear();

    virtual Evas_Object *getPartItem(Evas_Object *obj, std::string part);
    virtual std::string getLabelItem(Evas_Object *obj, std::string part);

    void buttonClickPlay();
    void buttonClickAdd();

    int getItemId() { return item_id; }
    Params getItemInfos() { return item_infos; }
};

#endif // GenlistItemYear_H
