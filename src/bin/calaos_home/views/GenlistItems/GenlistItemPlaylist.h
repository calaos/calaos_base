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

#ifndef GENLISTITEMPLAYLIST_H
#define GENLISTITEMPLAYLIST_H

#include <Utils.h>
#include <GenlistItemBase.h>


class GenlistItemPlaylist: public GenlistItemBase
{
private:
    std::string label;
    AudioPlayer *player;
    int playlist_item;
    Params item_infos;
    Evas_Object *popup_infos;

    void playlistItemGet_cb(Params &infos);
    void playlistItemDelete_cb(void *data);

public:
    GenlistItemPlaylist(Evas *evas, Evas_Object *parent, AudioPlayer *player, int playlist_item, void *data = NULL);
    virtual ~GenlistItemPlaylist();

    virtual Evas_Object *getPartItem(Evas_Object *obj, std::string part);
    virtual std::string getLabelItem(Evas_Object *obj, std::string part);

    void buttonClickPlay();
    void buttonClickMore();
};

#endif // GENLISTITEMPLAYLIST_H
