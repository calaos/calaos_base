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
#include "CameraModel.h"
#include "CalaosModel.h"

CameraModel::CameraModel(CalaosConnection *con):
    connection(con)
{
}

CameraModel::~CameraModel()
{
    for_each(cameras.begin(), cameras.end(), Delete());
}

void CameraModel::load()
{
    connection->SendCommand("camera ?", sigc::mem_fun(*this, &CameraModel::camera_count_cb));
}

void CameraModel::camera_count_cb(bool success, vector<string> result, void *data)
{
    if (!success) return;

    if (result.size() < 2) return;

    if (is_of_type<int>(result[1]))
    {
        int count;
        from_string(result[1], count);

        load_count = 0;

        if (count == 0)
        {
            //No camera found
            load_done.emit();
        }

        for (int i = 0;i < count;i++)
        {
            Camera *cam = new Camera(connection);
            cameras.push_back(cam);

            cam->params.Add("num", Utils::to_string(cameras.size() - 1));

            load_count++;
            cam->load_done.connect(sigc::mem_fun(*this, &CameraModel::load_camera_done));

            string cmd = "camera get " + Utils::to_string(i);
            connection->SendCommand(cmd, sigc::mem_fun(*cam, &Camera::camera_get_cb));
        }
    }
    else
    {
        //Load of camera failed because of a wrong reply
        load_done.emit();
    }
}

void CameraModel::load_camera_done(Camera *camera)
{
    load_count--;

    cDebug() << "[CAMERA load done]";

    if (load_count <= 0)
    {

        cDebug() << "[CAMERA LOAD DONE sending signal]";
        load_done.emit();
    }
}

void Camera::camera_get_cb(bool success, vector<string> result, void *data)
{
    for (uint b = 2;b < result.size();b++)
    {
        vector<string> tmp;
        Utils::split(result[b], tmp, ":", 2);

        if (tmp.size() < 2) continue;

        params.Add(tmp[0], tmp[1]);
    }

    load_done.emit(this);
}

Room *Camera::getRoom()
{
    if (room) return room;

    map<string, IOBase *>::const_iterator it = CalaosModel::Instance().getHome()->getCacheOutputs().find(params["oid"]);
    if (it == CalaosModel::Instance().getHome()->getCacheOutputs().end())
        return NULL;

    IOBase *output = (*it).second;
    return output->getRoom();
}

void Camera::sendAction_cb(bool success, vector<string> result, void *data)
{
    //do nothing...
}

void Camera::MoveCenter()
{
    string cmd = "camera move " + params["num"] + " home";
    connection->SendCommand(cmd, sigc::mem_fun(*this, &Camera::sendAction_cb));
}

void Camera::MoveUp()
{
    string cmd = "camera move " + params["num"] + " up";
    connection->SendCommand(cmd, sigc::mem_fun(*this, &Camera::sendAction_cb));
}

void Camera::MoveDown()
{
    string cmd = "camera move " + params["num"] + " down";
    connection->SendCommand(cmd, sigc::mem_fun(*this, &Camera::sendAction_cb));
}

void Camera::MoveLeft()
{
    string cmd = "camera move " + params["num"] + " left";
    connection->SendCommand(cmd, sigc::mem_fun(*this, &Camera::sendAction_cb));
}

void Camera::MoveRight()
{
    string cmd = "camera move " + params["num"] + " right";
    connection->SendCommand(cmd, sigc::mem_fun(*this, &Camera::sendAction_cb));
}

void Camera::ZoomIn()
{
    string cmd = "camera move " + params["num"] + " zoomin";
    connection->SendCommand(cmd, sigc::mem_fun(*this, &Camera::sendAction_cb));
}

void Camera::ZoomOut()
{
    string cmd = "camera move " + params["num"] + " zoomout";
    connection->SendCommand(cmd, sigc::mem_fun(*this, &Camera::sendAction_cb));
}

void Camera::Recall(int position)
{
    string cmd = "camera move " + params["num"] + " " + Utils::to_string(position);
    connection->SendCommand(cmd, sigc::mem_fun(*this, &Camera::sendAction_cb));
}

void Camera::Save(int position)
{
    string cmd = "camera save " + params["num"] + " " + Utils::to_string(position);
    connection->SendCommand(cmd, sigc::mem_fun(*this, &Camera::sendAction_cb));
}
