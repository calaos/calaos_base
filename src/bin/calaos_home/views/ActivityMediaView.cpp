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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef ENABLE_NLS
# include <libintl.h>
# define _(x) gettext(x)
#else
# define _(x) (x)
#endif

#include "ActivityMediaView.h"

ActivityMediaView::ActivityMediaView(Evas *_e, Evas_Object *_parent):
        ActivityView(_e, _parent, "calaos/page/media")
{
        setPartText("header.label", _("Multimedia"));

        contentView = new MainContentView(evas, parent);
        Swallow(contentView->getSmartObject(), "calaos.main.content");

        addCallback("button.*", "pressed", sigc::mem_fun(*this, &ActivityMediaView::buttonPressed));
}

ActivityMediaView::~ActivityMediaView()
{
        DELETE_NULL(contentView)
}

void ActivityMediaView::resetView()
{
}

void ActivityMediaView::buttonPressed(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
        button_clicked.emit(source);
}
