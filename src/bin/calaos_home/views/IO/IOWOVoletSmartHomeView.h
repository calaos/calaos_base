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
#ifndef IOWOVOLETSMARTHOMEVIEW_H
#define IOWOVOLETSMARTHOMEVIEW_H

#include <Utils.h>
#include <GenlistItemBase.h>

using namespace Utils;

class IOWOVoletSmartHomeView: public GenlistItemBase, public IOBaseElement
{
        private:
                virtual void ioDeleted();

                EdjeObject *window_slider;

                void sliderObjectDeleted();

        public:
                IOWOVoletSmartHomeView(Evas *evas, Evas_Object *parent, IOBase *io, string style_addition, Elm_Genlist_Item_Type flags = ELM_GENLIST_ITEM_NONE);
                virtual ~IOWOVoletSmartHomeView();

                virtual Evas_Object *getPartItem(Evas_Object *obj, string part);
                virtual string getLabelItem(Evas_Object *obj, string part);

                //Called when the real IO changed
                virtual void initView();
                virtual void updateView();

                void buttonClickUp();
                void buttonClickDown();
                void buttonClickStop();
                void buttonClickImpulseUp();
                void buttonClickImpulseDown();
                void buttonClickSet25();
                void buttonClickSet50();
                void buttonClickSet75();
};

#endif // IOWOVOLETSMARTHOMEVIEW_H
