/******************************************************************************
**  Copyright (c) 2006-2011, Calaos. All Rights Reserved.
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
**  along with Calaos; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/

#ifndef GENLISTITEMALBUM_H
#define GENLISTITEMALBUM_H

#include <Utils.h>
#include <GenlistItemBase.h>

using namespace Utils;

class GenlistItemAlbum: public GenlistItemBase
{
        private:
                string label_album, label_artist, label_year;
                AudioPlayer *player;
                int item_id;
                Params item_infos;
                bool in_query;
                string cover_fname;
                bool cover_downloaded;
                int reqtype;
                int command_id;

                void albumItemGet_cb(Params &infos);
                void albumItemCoverGet_cb(Params &res);

        public:
                enum { ALBUM_LIST, ALBUM_ARTIST, ALBUM_YEAR, ALBUM_GENRE };

                GenlistItemAlbum(Evas *evas, Evas_Object *parent, AudioPlayer *player, int item_id, int request_type, int _command_id = 0, void *data = NULL);
                virtual ~GenlistItemAlbum();

                virtual Evas_Object *getPartItem(Evas_Object *obj, string part);
                virtual string getLabelItem(Evas_Object *obj, string part);

                void buttonClickPlay();
                void buttonClickAdd();

                int getItemId() { return item_id; }
                Params getItemInfos() { return item_infos; }
};

#endif // GENLISTITEMALBUM_H
