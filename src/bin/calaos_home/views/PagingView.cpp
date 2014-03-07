/******************************************************************************
**  Copyright (c) 2006-2012, Calaos. All Rights Reserved.
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
#include "PagingView.h"

static void _cb_object_resize(void *data , Evas *e, Evas_Object *obj, void *event_info)
{
        PagingView *p = reinterpret_cast<PagingView *>(data);
        if (p) p->_objectResized();
}

static void _scroller_anim_stop_cb(void *data , Evas_Object *obj, void *event_info)
{
        PagingView *p = reinterpret_cast<PagingView *>(data);
        if (p) p->updateSelector();
}

static void _scroller_start_cb(void *data , Evas_Object *obj, void *event_info)
{
        PagingView *p = reinterpret_cast<PagingView *>(data);
        if (p) p->dragStart();
}

static void _scroller_stop_cb(void *data , Evas_Object *obj, void *event_info)
{
        PagingView *p = reinterpret_cast<PagingView *>(data);
        if (p) p->dragStop();
}

PagingView::PagingView(Evas *_e, Evas_Object *_parent):
        BaseView(_e, _parent)
{
        try
        {
                LoadEdje("calaos/paging_view");
        }
        catch(exception const& e)
        {
                cCriticalDom("root") <<  "PagingView: Can't load edje";
                throw;
        }

        pager_scroller = elm_scroller_add(parent);
        elm_scroller_bounce_set(pager_scroller, EINA_TRUE, EINA_FALSE);
        elm_scroller_policy_set(pager_scroller, ELM_SCROLLER_POLICY_OFF, ELM_SCROLLER_POLICY_OFF);
        elm_scroller_page_relative_set(pager_scroller, 1.0, 1.0);

        box_content = elm_box_add(pager_scroller);
        evas_object_size_hint_weight_set(box_content, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
        evas_object_size_hint_align_set(box_content, EVAS_HINT_FILL, EVAS_HINT_FILL);
        elm_box_homogeneous_set(box_content, EINA_TRUE);
        elm_box_horizontal_set(box_content, EINA_TRUE);

        elm_object_content_set(pager_scroller, box_content);
        evas_object_show(box_content);
        elm_object_style_set(pager_scroller, "calaos/pager");

        evas_object_smart_callback_add(pager_scroller, "scroll,drag,start", _scroller_start_cb, this);
        evas_object_smart_callback_add(pager_scroller, "scroll,anim,start", _scroller_start_cb, this);
        evas_object_smart_callback_add(pager_scroller, "scroll,anim,stop", _scroller_anim_stop_cb, this);
//        evas_object_smart_callback_add(pager_scroller, "scroll,drag,stop", _scroller_stop_cb, this);
        evas_object_smart_callback_add(pager_scroller, "scroll,anim,stop", _scroller_stop_cb, this);

        Swallow(pager_scroller, "pager.swallow");

        // Selector
        box_selector = elm_box_add(parent);
        elm_box_homogeneous_set(box_selector, EINA_TRUE);
        elm_box_horizontal_set(box_selector, EINA_TRUE);
        evas_object_show(box_selector);

        Swallow(box_selector, "selector.swallow");

        evas_object_event_callback_add(pager_scroller, EVAS_CALLBACK_RESIZE, _cb_object_resize, this);
}

PagingView::~PagingView()
{
        elm_box_clear(box_content);
        elm_box_clear(box_selector);
        elm_object_content_unset(pager_scroller);

        for (unsigned int i = 0;i < pages.size(); i++)
                evas_object_del(pages[i]);
        pages.clear();

        for_each(selectors.begin(), selectors.end(), Delete());
        selectors.clear();

        DELETE_NULL_FUNC(evas_object_del, box_content)
        DELETE_NULL_FUNC(evas_object_del, box_selector)
        DELETE_NULL_FUNC(evas_object_del, pager_scroller)
}

int PagingView::addPage(Evas_Object *content)
{
        evas_object_show(content);
        elm_box_pack_end(box_content, content);
        pages.push_back(content);

        EdjeObject *o = new EdjeObject(theme, evas);
        try
        {
                o->LoadEdje("calaos/paging_view/selector");
        }
        catch(exception const& e)
        {
                cCriticalDom("root") <<  "PagingView: Can't load edje calaos/paging_view/selector";
                throw;
        }

        int w, h;
        edje_object_size_min_get(o->getEvasObject(), &w, &h);
        evas_object_size_hint_min_set(o->getEvasObject(), w, h);

        o->Show();
        elm_box_pack_end(box_selector, o->getEvasObject());
        selectors.push_back(o);

        _objectResized();

        bringPage(pages.size() - 1);

        updateSelector();

        return pages.size();
}

void PagingView::delPage(int page)
{
        if (page < 0 || page >= (int)pages.size())
        {
                cCriticalDom("root") <<  "PagingView: delPage(" << page << ") out of bound [size: " << pages.size() << "] !";
                return;
        }

        elm_box_unpack(box_content, pages[page]);
        evas_object_del(pages[page]);
        pages.erase(pages.begin() + page);

        elm_box_unpack(box_content, selectors[page]->getEvasObject());
        delete selectors[page];
        selectors.erase(selectors.begin() + page);

        bringPage(0);
}

void PagingView::updateSelector()
{
        int current = getCurrentPage();

        for (unsigned int i = 0;i < selectors.size();i++)
        {
                if ((int)i == current)
                        selectors[i]->EmitSignal("select", "calaos");
                else
                        selectors[i]->EmitSignal("unselect", "calaos");
        }
}

void PagingView::_objectResized()
{
        Evas_Coord w, h;
        evas_object_geometry_get(pager_scroller, NULL, NULL, &w, &h);

        for (unsigned int i = 0;i < pages.size();i++)
                evas_object_size_hint_min_set(pages[i], w, h);
}

void PagingView::dragStart()
{
        drag_start.emit();
}

void PagingView::dragStop()
{
        drag_stop.emit();
}
