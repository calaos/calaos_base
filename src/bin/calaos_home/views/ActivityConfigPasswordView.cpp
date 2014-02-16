/******************************************************************************
**  Copyright (c) 2006-2013, Calaos. All Rights Reserved.
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
#include "ActivityConfigPasswordView.h"
#include "GengridItemConfig.h"
#include <GenlistItemSimpleHeader.h>
#include <GenlistItemSimple.h>
#include <Calendar.h>
ActivityConfigPasswordView::ActivityConfigPasswordView(Evas *_e, Evas_Object *_parent):
        ActivityView(_e, _parent, "calaos/page/config/password")
{
        setPartText("tab1.text", _("Change your password"));
        setPartText("tab1.text.detail", _("Resume : <light_blue>Password</light_blue><br><small>Change your password !</small>"));
        setPartText("tab2.text", _("About"));
        setPartText("tab2.text.detail", _("About : <light_blue>Calaos products</light_blue><br><small>Touchscreen solutions.</small>"));

}

ActivityConfigPasswordView::~ActivityConfigPasswordView()
{
        evas_object_del(tzList);
}

void ActivityConfigPasswordView::resetView()
{
}

