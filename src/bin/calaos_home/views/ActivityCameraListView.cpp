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
#include "ActivityCameraListView.h"

ActivityCameraListView::ActivityCameraListView(Evas *_e, Evas_Object *_parent):
    ActivityView(_e, _parent, "calaos/page/media/camera_list")
{
    ActivityCameraObject ac;
    cameras.reserve(4);
    for (int i = 0;i < 4;i++)
        cameras.push_back(ac);

    addCallback("camera", "*", sigc::mem_fun(*this, &ActivityCameraListView::EdjeCallback));

    page_view = new PagingView(evas, parent);
    Swallow(page_view, "home.pager");

    page_view->drag_start.connect(sigc::mem_fun(*this, &ActivityCameraListView::pagerDragStart));
    page_view->drag_stop.connect(sigc::mem_fun(*this, &ActivityCameraListView::pagerDragStop));
}

ActivityCameraListView::~ActivityCameraListView()
{
    for (int i = 0;i < 4;i++)
        DELETE_NULL_FUNC(evas_object_del, cameras[i].video);

    for_each(scenarios.begin(), scenarios.end(), Delete());
    DELETE_NULL(page_view);
}

void ActivityCameraListView::resetView()
{
}

static void _smart_cam_cb(void *data, Evas_Object *obj, void *event_info)
{
    ActivityCameraObject *ac = reinterpret_cast<ActivityCameraObject *>(data);
    if (!ac) return;

    ac->view->EmitSignal("show,picture," + Utils::to_string(ac->position + 1), "calaos");
    evas_object_smart_callback_del(obj, "frame_decode", _smart_cam_cb);
}

static void _smart_cam_stop_cb(void *data, Evas_Object *obj, void *event_info)
{
    ActivityCameraObject *ac = reinterpret_cast<ActivityCameraObject *>(data);
    if (!ac) return;

    //Restart the playing of stream if it stops
    elm_video_play(ac->video);
}

void ActivityCameraListView::setCamera(Camera *camera, int position)
{
    if (cameras[position].video)
        evas_object_del(cameras[position].video);

    Evas_Object *video = elm_video_add(parent);

    Swallow(video, "camera.swallow." + Utils::to_string(position + 1));
    elm_video_file_set(video, camera->params["mjpeg_url"].c_str());
    elm_video_play(video);
    evas_object_show(video);

    setPartText("camera.title." + Utils::to_string(position + 1), camera->params["name"]);

    cameras[position].video = video;
    cameras[position].camera = camera;
    cameras[position].view = this; //For the C callback
    cameras[position].position = position; //For the C callback

    evas_object_smart_callback_add(elm_video_emotion_get(video), "frame_decode", _smart_cam_cb, &cameras[position]);
    evas_object_smart_callback_add(elm_video_emotion_get(video), "decode_stop", _smart_cam_stop_cb, &cameras[position]);
    evas_object_smart_callback_add(elm_video_emotion_get(video), "playback_finished", _smart_cam_stop_cb, &cameras[position]);
}

void ActivityCameraListView::disableCamera(int position)
{
    setPartText("camera.title." + Utils::to_string(position + 1), "Aucune caméra");
    EmitSignal("hide,picture," + Utils::to_string(position + 1), "calaos");

    if (cameras[position].video)
        evas_object_del(cameras[position].video);

    ActivityCameraObject ac;
    cameras[position] = ac;
}

void ActivityCameraListView::EnableLeftButton()
{
    EmitSignal("enable,left", "calaos");
}

void ActivityCameraListView::DisableLeftButton()
{
    EmitSignal("disable,left", "calaos");
}

void ActivityCameraListView::EnableRightButton()
{
    EmitSignal("enable,right", "calaos");
}

void ActivityCameraListView::DisableRightButton()
{
    EmitSignal("disable,right", "calaos");
}

void ActivityCameraListView::ShowLoading()
{
    EmitSignal("show,loading", "calaos");
}

void ActivityCameraListView::HideLoading()
{
    EmitSignal("hide,loading", "calaos");
}

void ActivityCameraListView::EdjeCallback(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (emission == "page,right")
    {
        button_right_click.emit();
    }
    else if (emission == "page,left")
    {
        button_left_click.emit();
    }
}

void ActivityCameraListView::addScenarioPage(list<IOBase *> &scenarios_io)
{
    EdjeObject *container = new EdjeObject(theme, evas);
    container->LoadEdje("calaos/page/home/scenario");
    container->setAutoDelete(true);

    list<IOBase *>::iterator it = scenarios_io.begin();
    for (int i = 0;it != scenarios_io.end() && i < 6;it++, i++)
    {
        IOView *ioView = IOViewFactory::CreateIOView(evas, getEvasObject(), IOView::IO_SCENARIO_HOME);
        ioView->setIO(*it);
        ioView->Show();
        ioView->initView();

        scenarios.push_back(ioView);

        string _t = "element." + Utils::to_string(i + 1);
        container->Swallow(ioView, _t);
    }

    page_view->addPage(container->getEvasObject());
}

void ActivityCameraListView::EnableView()
{
    for (unsigned int i = 0;i < cameras.size();i++)
    {
        if (cameras[i].video)
            elm_video_play(cameras[i].video);
    }
}

void ActivityCameraListView::DisableView()
{
    for (unsigned int i = 0;i < cameras.size();i++)
    {
        if (cameras[i].video)
            elm_video_pause(cameras[i].video);
    }
}

void ActivityCameraListView::pagerDragStart()
{
    EmitSignal("show,edge", "calaos");
}

void ActivityCameraListView::pagerDragStop()
{
    EmitSignal("hide,edge", "calaos");
}
