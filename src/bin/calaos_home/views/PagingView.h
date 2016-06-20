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
#ifndef PAGINGVIEW_H
#define PAGINGVIEW_H

#include <BaseView.h>

#define CHECK_SCROLL_RETURN(...) \
    if (!edje) \
{ \
    cCritical() <<  "PagingView: Scroller object NULL !"; \
    return __VA_ARGS__; \
    }

class PagingView: public BaseView
{
protected:
    Evas_Object *pager_scroller;

    Evas_Object *box_content;
    Evas_Object *box_selector;

    std::vector<Evas_Object *> pages;
    std::vector<EdjeObject *> selectors;

public:
    PagingView(Evas *evas, Evas_Object *parent);
    ~PagingView();

    int addPage(Evas_Object *content);
    void delPage(int page);

    void bringPage(int page) { CHECK_SCROLL_RETURN(); elm_scroller_page_bring_in(pager_scroller, page, 0); }
    int getPageCount() { CHECK_SCROLL_RETURN(0); return pages.size(); }
    int getCurrentPage() { CHECK_SCROLL_RETURN(0); int n; elm_scroller_current_page_get(pager_scroller, &n, NULL); return n; }

    sigc::signal<void> drag_start;
    sigc::signal<void> drag_stop;


    //private stuff
    void updateSelector();
    void dragStart();
    void dragStop();
    void _objectResized();
};

#endif // PAGINGVIEW_H
