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
#include "ActivityWebController.h"

ActivityWebController::ActivityWebController(Evas *e, Evas_Object *p, string _url):
        ActivityController(e, p, ActivityViewFactory::ACTIVITY_VIEW_WEB)
{
}

ActivityWebController::~ActivityWebController()
{
}

void ActivityWebController::createView()
{
        if (view) return;

        ActivityController::createView();

        //ActivityWebView *webView = dynamic_cast<ActivityWebView *>(view);
        //keyboardView->validPressed.connect(sigc::mem_fun(*this, &ActivityWebController::validButtonPressed));
}
