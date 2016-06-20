/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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
#include "ActivityCameraSelectView.h"
#include "GenlistItems/GenlistItemSimple.h"
#include "GenlistItems/GenlistItemSimpleHeader.h"
#include "CalaosCameraView.h"

ActivityCameraSelectView::ActivityCameraSelectView(Evas *_e, Evas_Object *_parent):
    ActivityView(_e, _parent, "calaos/page/media/camera_select"),
    camera_video(NULL),
    camera(NULL)
{
    addCallback("camera", "*", sigc::mem_fun(*this, &ActivityCameraSelectView::EdjeCallback));

    list_item = elm_genlist_add(parent);
    Swallow(list_item, "list.swallow");
    elm_object_style_set(list_item, "calaos");
    elm_genlist_select_mode_set(list_item, ELM_OBJECT_SELECT_MODE_ALWAYS);
    evas_object_show(list_item);

    addCallback("button.*", "pressed", sigc::mem_fun(*this, &ActivityCameraSelectView::ButtonCallback));
}

ActivityCameraSelectView::~ActivityCameraSelectView()
{
    elm_genlist_clear(list_item);

    DELETE_NULL(camera_video);
    DELETE_NULL_FUNC(evas_object_del, list_item);
}

void ActivityCameraSelectView::resetView()
{
}

std::string ActivityCameraSelectView::getTitle()
{
    return std::string("Caméra: ") + camera->params["name"];
}

void ActivityCameraSelectView::ShowLoading()
{
    EmitSignal("show,loading", "calaos");
}

void ActivityCameraSelectView::HideLoading()
{
    EmitSignal("hide,loading", "calaos");
}

void ActivityCameraSelectView::EdjeCallback(void *data, Evas_Object *_edje, std::string emission, std::string source)
{

}

static void _smart_cam_cb(void *data, Evas_Object *obj, void *event_info)
{
    ActivityCameraSelectView *view = reinterpret_cast<ActivityCameraSelectView *>(data);
    if (!view) return;

    view->EmitSignal("show,picture", "calaos");
    evas_object_smart_callback_del(obj, "frame_decode", _smart_cam_cb);
}

void ActivityCameraSelectView::setCamera(Camera *cam)
{
    if (!cam) return;
    camera = cam;

    DELETE_NULL(camera_video);

    camera_video = new CalaosCameraView(evas);

    Swallow(camera_video->getSmartObject(), "camera.swallow");

    std::string u, p;
    CalaosConnection::getCredentials(u, p);

    std::string url = "http://";
    url += CalaosConnection::getCalaosServerIp() + ":5454/api";
    url += "?cn_user=" + u;
    url += "&cn_pass=" + p;
    url += "&action=camera";
    url += "&type=get_video";
    url += "&id=" + camera->params["id"];

    camera_video->setCameraUrl(url);
    camera_video->play();
    evas_object_show(camera_video->getSmartObject());

    //TODO: this should be done in a signal from CalaosCameraView
    EmitSignal("show,picture", "calaos");

    if (camera->params["ptz"] == "true")
        EmitSignal("ptz,true", "calaos");
    else
        EmitSignal("ptz,false", "calaos");

    if (camera->params["zoom"] == "true")
        EmitSignal("zoom,true", "calaos");
    else
        EmitSignal("zoom,false", "calaos");

    Room *room = cam->getRoom();
    elm_genlist_clear(list_item);

    if (!room) return;

    setPartText("room_title.text", room->name);

    std::list<IOBase *>::iterator it = room->visible_ios.begin();
    for (;it != room->visible_ios.end();it++)
    {
        IOViewFactory::CreateIOBaseElement(evas, list_item, *it, list_item, "left"/*, group_item*/);
    }

    elm_genlist_realized_items_update(list_item);
}

void ActivityCameraSelectView::ButtonCallback(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (source == "button.position.save")
    {
        buttonSavePositionClick();
    }
    else if (source == "button.ptz.left")
    {
        camera->MoveLeft();
    }
    else if (source == "button.ptz.right")
    {
        camera->MoveRight();
    }
    else if (source == "button.ptz.up")
    {
        camera->MoveUp();
    }
    else if (source == "button.ptz.down")
    {
        camera->MoveDown();
    }
    else if (source == "button.ptz.center")
    {
        camera->MoveCenter();
    }
    else if (source == "button.position.1")
    {
        camera->Recall(1);
    }
    else if (source == "button.position.2")
    {
        camera->Recall(2);
    }
    else if (source == "button.position.3")
    {
        camera->Recall(3);
    }
    else if (source == "button.position.4")
    {
        camera->Recall(4);
    }
    else if (source == "button.position.5")
    {
        camera->Recall(5);
    }
    else if (source == "button.position.6")
    {
        camera->Recall(6);
    }
    else if (source == "button.position.7")
    {
        camera->Recall(7);
    }
    else if (source == "button.position.8")
    {
        camera->Recall(8);
    }
    else if (source == "button.ptz.zoom_in")
    {
        camera->ZoomIn();
    }
    else if (source == "button.ptz.zoom_out")
    {
        camera->ZoomOut();
    }
}

void ActivityCameraSelectView::buttonSavePositionClick()
{
    Evas_Object *table = createPaddingTable(evas, parent, 260, 200);

    pager_position = elm_naviframe_add(parent);
    evas_object_show(pager_position);

    Evas_Object *glist = elm_genlist_add(parent);
    elm_object_style_set(glist, "calaos");
    elm_genlist_select_mode_set(glist, ELM_OBJECT_SELECT_MODE_ALWAYS);
    evas_object_size_hint_fill_set(glist, EVAS_HINT_FILL, EVAS_HINT_FILL);
    evas_object_size_hint_weight_set(glist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_show(glist);

    GenlistItemBase *header = new GenlistItemSimpleHeader(evas, glist, _("Save the <light_blue>actual position</light_blue><br><small>Choose a position.</small>"));
    header->Append(glist);
    for (int i = 0;i < 8;i++)
    {
        std::string label = _("Position %1");
        Utils::replace_str(label, "%1", Utils::to_string(i + 1));
        int *user_data = new int(i + 1);
        GenlistItemSimple *item  = new GenlistItemSimple(evas, glist, label, true, false, user_data);
        item->Append(glist, header);
        item->setAutoDeleteUserData(new DeletorT<int *>);
        item->item_selected.connect(sigc::mem_fun(*this, &ActivityCameraSelectView::positionSelected));
    }

    elm_genlist_realized_items_update(glist);

    elm_table_pack(table, glist, 1, 1, 1, 1);

    popup_position = elm_ctxpopup_add(parent);
    elm_object_content_set(popup_position, pager_position);
    elm_object_style_set(popup_position, "calaos");
    evas_object_size_hint_min_set(popup_position, 300, 240);

    Evas_Coord x,y;
    evas_pointer_canvas_xy_get(evas, &x, &y);
    evas_object_move(popup_position, x, y);
    evas_object_show(popup_position);

    elm_naviframe_item_push(pager_position, NULL, NULL, NULL, table, "calaos");
}

void ActivityCameraSelectView::positionSelected(void *data)
{
    int *user_data = reinterpret_cast<int *>(data);
    int position = *user_data;
    std::string text = _("<center>Save to <light_blue>position #%1</light_blue></center>");
    Utils::replace_str(text, "%1", Utils::to_string(position));

    camera->Save(position);

    EdjeObject *page = new EdjeObject(theme, evas);
    page->LoadEdje("calaos/page/media/camera_select/position_save");
    page->setAutoDelete(true);
    page->setPartText("text", text);

    elm_naviframe_item_push(pager_position, NULL, NULL, NULL, page->getEvasObject(), "calaos");
    EcoreTimer::singleShot(2.0, sigc::mem_fun(*this, &ActivityCameraSelectView::positionSaved));
}

void ActivityCameraSelectView::positionSaved()
{
    evas_object_hide(popup_position);
}
