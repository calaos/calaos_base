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

#ifndef GenlistItemAlbumHeader_H
#define GenlistItemAlbumHeader_H

#include <Utils.h>
#include <GenlistItemBase.h>

using namespace Utils;

class GenlistItemAlbumHeader: public GenlistItemBase
{
        private:
                AudioPlayer *player;
                Params album_infos;
                int album_id;
                bool in_query;
                string cover_fname;
                bool cover_downloaded;

                EcoreTimer *dltimer;

                void albumItemGet_cb(Params &infos);
                void albumItemCoverGet_cb(Params &res);

        public:
                GenlistItemAlbumHeader(Evas *evas, Evas_Object *parent, AudioPlayer *player, Params &album_infos, int album_id, void *data = NULL);
                virtual ~GenlistItemAlbumHeader();

                virtual Evas_Object *getPartItem(Evas_Object *obj, string part);
                virtual string getLabelItem(Evas_Object *obj, string part);

                void buttonClickPlay();
                void buttonClickAdd();
};

#endif // GenlistItemAlbumHeader_H
