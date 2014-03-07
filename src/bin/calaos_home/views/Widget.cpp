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
**  along with Foobar; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#include <Widget.h>
#include <GenlistItems/GenlistItemSimple.h>
#include <GenlistItems/GenlistItemSimpleHeader.h>
#include <ActivityWidgetsView.h>
#include <ActivityIntl.h>

static void _edje_widget_cb(void *data, Evas_Object *obj, const char *emission, const char *source)
{
        Widget *w = reinterpret_cast<Widget *>(data);
        if (w) w->Callback(obj, emission, source);
}
static void _evas_move_start_cb(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
        Widget *w = reinterpret_cast<Widget *>(data);
        if (w) w->Callback(NULL, "start,move", "widget");
}
static void _evas_move_stop_cb(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
        Widget *w = reinterpret_cast<Widget *>(data);
        if (w) w->Callback(NULL, "stop,move", "widget");
}
static void _evas_move_move_cb(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
        Widget *w = reinterpret_cast<Widget *>(data);
        if (w) w->Callback(NULL, "moving", "widget");
}
static void _evas_resize_start_cb(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
        Widget *w = reinterpret_cast<Widget *>(data);
        if (w) w->Callback(NULL, "start,resize", "widget");
}
static void _evas_resize_stop_cb(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
        Widget *w = reinterpret_cast<Widget *>(data);
        if (w) w->Callback(NULL, "stop,resize", "widget");
}
static void _evas_resize_move_cb(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
        Widget *w = reinterpret_cast<Widget *>(data);
        if (w) w->Callback(NULL, "resizing", "widget");
}

Widget::Widget(string &_theme, Evas *_evas, ModuleDef &_mtype, string _id, Evas_Object *_parent, ActivityWidgetsView *_view):
        EdjeObject(_theme, _evas),
        parent(_parent),
        view(_view),
        id(_id),
        mtype(_mtype.mod_fname),
        timer(NULL),
        popup(NULL),
        moving(false),
        resizing(false)
{
        if (mtype == "xmas" || id == "xmas")
                return;

        if (!LoadEdje("calaos/widget/base"))
        {
                cCriticalDom("root") << "FATAL ERROR ! Exiting ...."; 
                exit(1);
        }

        edje_object_signal_callback_add(edje, "*", "*", _edje_widget_cb, this);

        if (!ModuleManager::Instance().createModuleInstance(evas, _mtype, mdef, id))
        {
                string msg = "Error, createModuleInstance(" + mtype + ") failed !";
                throw line_exception(msg.c_str(), __LINE__);
        }

        CalaosModuleBase *module = ModuleManager::Instance().getModuleInstance(mdef);

        if (!module)
        {
                string msg = "Error, getModuleInstance(" + mtype + ") failed !";
                throw line_exception(msg.c_str(), __LINE__);
        }

        Evas_Object *o = module->getEvasObject();
        edje_object_part_swallow(edje, "widget", o);

        evas_object_show(o);

        //Box for moving widget
        move_box = evas_object_rectangle_add(evas);
        evas_object_color_set(move_box, 0, 0, 0, 0);
        evas_object_event_callback_add(move_box, EVAS_CALLBACK_MOUSE_DOWN,
                                _evas_move_start_cb, this);
        evas_object_event_callback_add(move_box, EVAS_CALLBACK_MOUSE_MOVE,
                                _evas_move_move_cb, this);
        evas_object_event_callback_add(move_box, EVAS_CALLBACK_MOUSE_UP,
                                _evas_move_stop_cb, this);

        //Box for resizing widget
        resize_box = evas_object_rectangle_add(evas);
        evas_object_color_set(resize_box, 0, 0, 0, 0);
        evas_object_event_callback_add(resize_box, EVAS_CALLBACK_MOUSE_DOWN,
                                _evas_resize_start_cb, this);
        evas_object_event_callback_add(resize_box, EVAS_CALLBACK_MOUSE_MOVE,
                                _evas_resize_move_cb, this);
        evas_object_event_callback_add(resize_box, EVAS_CALLBACK_MOUSE_UP,
                                _evas_resize_stop_cb, this);

        edje_object_part_swallow(edje, "widget.resize.bloc", resize_box);

        int w, h;
        module->getSizeMin(w, h);
        Resize(w, h);

//        set_layer(-5);
}

Widget::~Widget()
{
        if (mtype == "xmas" || id == "xmas")
                return;

        ModuleManager::Instance().DeleteInstance(mdef);

        evas_object_del(move_box);
        evas_object_del(resize_box);

        if (timer)
        {
                delete timer;
                timer = NULL;
        }
}

void Widget::Callback(Evas_Object *edj, std::string emission, std::string source)
{
        int wx, wy, mx, my, w, h, rx, ry;

        if (source == "widget" && emission == "start,move")
        {
                getGeometry(&wx, &wy, NULL, NULL);
                evas_pointer_output_xy_get(evas, &mx, &my);
                offset_x = mx - wx;
                offset_y = my - wy;

                moving = true;
        }
        else if (source == "widget" && emission == "moving" && moving)
        {
                evas_pointer_output_xy_get(evas, &mx, &my);
                Move(mx - offset_x, my - offset_y);
        }
        else if (source == "widget" && emission == "stop,move")
        {
                moving = false;
        }
        else if (source == "widget" && emission == "start,resize")
        {
                evas_object_geometry_get(resize_box, &wx, &wy, NULL, NULL);
                evas_pointer_output_xy_get(evas, &mx, &my);
                offset_x = mx - wx;
                offset_y = my - wy;

                resizing = true;
        }
        else if (source == "widget" && emission == "resizing" && resizing)
        {
                getGeometry(&wx, &wy, &w, &h);
                evas_pointer_output_xy_get(evas, &mx, &my);

                evas_object_geometry_get(resize_box, &rx, &ry, NULL, NULL);

                Resize(mx - offset_x + (wx + w - rx) - wx, my - offset_y + (wy + h - ry) - wy);
        }
        else if (source == "widget" && emission == "stop,resize")
        {
                resizing = false;
        }
        else if (source == "button.delete" && emission == "pressed")
        {
                Evas_Object *table = createPaddingTable(evas, parent, 280, 260);

                Evas_Object *glist = elm_genlist_add(table);
                elm_object_style_set(glist, "calaos");
                elm_genlist_select_mode_set(glist, ELM_OBJECT_SELECT_MODE_ALWAYS);
                evas_object_size_hint_fill_set(glist, EVAS_HINT_FILL, EVAS_HINT_FILL);
                evas_object_size_hint_weight_set(glist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
                evas_object_show(glist);

                string title_label = _("Confirmation<br><small><light_blue>Are you sure to want delete this widget?</light_blue></small>");
                GenlistItemBase *header = new GenlistItemSimpleHeader(evas, glist, title_label);
                header->Append(glist);

                GenlistItemSimple *item = new GenlistItemSimple(evas, parent, _("Yes, del this widget"), true);
                item->Append(glist, header);
                item->item_selected.connect(sigc::mem_fun(*this, &Widget::deleteValid));

                item = new GenlistItemSimple(evas, parent, "Non", true);
                item->Append(glist, header);
                item->item_selected.connect(sigc::mem_fun(*this, &Widget::deleteCancel));

                elm_table_pack(table, glist, 1, 1, 1, 1);

                popup = elm_ctxpopup_add(parent);
                elm_object_content_set(popup, table);
                elm_object_style_set(popup, "calaos");
                elm_ctxpopup_direction_priority_set(popup,
                                                    ELM_CTXPOPUP_DIRECTION_LEFT,
                                                    ELM_CTXPOPUP_DIRECTION_RIGHT,
                                                    ELM_CTXPOPUP_DIRECTION_DOWN,
                                                    ELM_CTXPOPUP_DIRECTION_UP);

                evas_pointer_canvas_xy_get(evas, &wx, &wy);
                evas_object_move(popup, wx, wy);
                evas_object_show(popup);
        }
}

void Widget::deleteValid(void *data)
{
        elm_ctxpopup_dismiss(popup);
        view->DeleteWidget(this);
}

void Widget::deleteCancel(void *data)
{
        elm_ctxpopup_dismiss(popup);
}

void Widget::Show()
{
        edje_object_signal_emit(edje, "show", "calaos");
        EdjeObject::Show();
}

void Widget::Hide()
{
        edje_object_signal_emit(edje, "hide", "calaos");
}

void Widget::Move(int wx, int wy)
{
        //Clamp values
        if (wx < 0) wx = 0;
        if (wy < 0) wy = 0;
        int w, h;
        getGeometry(NULL, NULL, &w, &h);
        if (wx + w > WIDTH) wx = WIDTH - w;
        if (wy + h + 100 > HEIGHT) wy = HEIGHT - h - 100;

        posx = wx;
        posy = wy;

        EdjeObject::Move(wx, wy);
        evas_object_move(move_box, wx, wy);
}

void Widget::Resize(int w, int h)
{
        CalaosModuleBase *module = ModuleManager::Instance().getModuleInstance(mdef);

        int minw, minh, maxw, maxh;
        module->getSizeMin(minw, minh);
        module->getSizeMax(maxw, maxh);

        if (minw > 0 && w < minw) w = minw;
        if (minh > 0 && h < minh) h = minh;
        if (maxw > 0 && w > maxw) w = maxw;
        if (maxh > 0 && h > maxh) h = maxh;

        width = w;
        height = h;

        EdjeObject::Resize(w, h);
        evas_object_resize(move_box, w, h);
}

void Widget::EditMode()
{
        evas_object_show(move_box);
        edje_object_signal_emit(edje, "widget,edit", "calaos");

        CalaosModuleBase *module = ModuleManager::Instance().getModuleInstance(mdef);
        int minw, minh, maxw, maxh;
        module->getSizeMin(minw, minh);
        module->getSizeMax(maxw, maxh);

        if (minw != maxw || minh != maxh)
                edje_object_signal_emit(edje, "widget,resizable", "calaos");

        getGeometry(&old_x, &old_y, &old_w, &old_h);
}

void Widget::NormalMode()
{
        evas_object_hide(move_box);
        edje_object_signal_emit(edje, "widget,normal", "calaos");
}

void Widget::Reset()
{
        start_time = ecore_time_get();

        getGeometry(&start_x, &start_y, &start_w, &start_h);

        //create a timer to do the animation
        if (timer) delete timer;
        timer = new EcoreTimer(1.0 / 30., (sigc::slot<void>)sigc::mem_fun(*this, &Widget::_ResetAnim) );
}

void Widget::_ResetAnim()
{
        int new_x, new_y, new_w, new_h;

        //anim has to be done in 1s.
        double time = ecore_time_get() - start_time;
        if (time >= 1.0) time = 1.0;

        //calc a DECELERATE value
        double time_val = sin(time * PI / 2.0);

        int xoffset, yoffset, woffset, hoffset;

        if (old_x > start_x) xoffset = old_x - start_x; else xoffset = start_x - old_x;
        if (old_y > start_y) yoffset = old_y - start_y; else yoffset = start_y - old_y;
        if (old_w > start_w) woffset = old_w - start_w; else woffset = start_w - old_w;
        if (old_h > start_h) hoffset = old_h - start_h; else hoffset = start_h - old_h;

        if (old_x > start_x)
                new_x = start_x + (xoffset * time_val);
        else
                new_x = start_x - (xoffset * time_val);

        if (old_y > start_y)
                new_y = start_y + (yoffset * time_val);
        else
                new_y = start_y - (yoffset * time_val);

        if (old_w > start_w)
                new_w = start_w + (woffset * time_val);
        else
                new_w = start_w - (woffset * time_val);

        if (old_h > start_h)
                new_h = start_h + (hoffset * time_val);
        else
                new_h = start_h - (hoffset * time_val);

        Move(new_x, new_y);
        Resize(new_w, new_h);

        //animation is done, exit timer
        if (time_val >= 1.0 && timer)
        {
                delete timer;
                timer = NULL;
        }
}

void Widget::Save(TiXmlElement *pnode)
{
        if (!pnode) return;

        TiXmlElement *node = new TiXmlElement("calaos:widget");
        pnode->LinkEndChild(node);

        node->SetAttribute("id", id);
        node->SetAttribute("type", mtype);
        node->SetAttribute("posx", Utils::to_string(posx));
        node->SetAttribute("posy", Utils::to_string(posy));
        node->SetAttribute("width", Utils::to_string(width));
        node->SetAttribute("height", Utils::to_string(height));
}

string Widget::getStringInfo()
{
        CalaosModuleBase *module = ModuleManager::Instance().getModuleInstance(mdef);

        if (!module)
        {
                string msg = "Error, getModuleInstance(" + mtype + ") failed !";
                throw line_exception(msg.c_str(), __LINE__);

                return "Error !";
        }

        return module->getStringInfo();
}
