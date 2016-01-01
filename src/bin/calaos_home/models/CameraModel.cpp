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

void CameraModel::load(json_t *data)
{
    if (!data || !json_is_object(data)) return;

    json_t *jcam = json_object_get(data, "cameras");
    if (!json_is_array(jcam))
    {
        load_done.emit();
        return;
    }

    size_t idx;
    json_t *value;

    json_array_foreach(jcam, idx, value)
    {
        Camera *cam = new Camera(connection);
        cam->load(value);
        cameras.push_back(cam);
    }

    load_done.emit();
}

void Camera::load(json_t *data)
{
    jansson_decode_object(data, params);
}

Room *Camera::getRoom()
{
    if (room) return room;

    map<string, IOBase *>::const_iterator it = CalaosModel::Instance().getHome()->getCacheIO().find(params["id"]);
    if (it == CalaosModel::Instance().getHome()->getCacheIO().end())
        return NULL;

    IOBase *output = (*it).second;
    room = output->getRoom();
    return room;
}

void Camera::MoveCenter()
{
    Params p = {{ "id", params["id"] },
                { "value", "move home" }};
    connection->sendCommand("set_state", p);
}

void Camera::MoveUp()
{
    Params p = {{ "id", params["id"] },
                { "value", "move up" }};
    connection->sendCommand("set_state", p);
}

void Camera::MoveDown()
{
    Params p = {{ "id", params["id"] },
                { "value", "move down" }};
    connection->sendCommand("set_state", p);
}

void Camera::MoveLeft()
{
    Params p = {{ "id", params["id"] },
                { "value", "move left" }};
    connection->sendCommand("set_state", p);
}

void Camera::MoveRight()
{
    Params p = {{ "id", params["id"] },
                { "value", "move right" }};
    connection->sendCommand("set_state", p);
}

void Camera::ZoomIn()
{
    Params p = {{ "id", params["id"] },
                { "value", "move zoomin" }};
    connection->sendCommand("set_state", p);
}

void Camera::ZoomOut()
{
    Params p = {{ "id", params["id"] },
                { "value", "move zoomout" }};
    connection->sendCommand("set_state", p);
}

void Camera::Recall(int position)
{
    Params p = {{ "id", params["id"] },
                { "value", string("recall ") + Utils::to_string(position) }};
    connection->sendCommand("set_state", p);
}

void Camera::Save(int position)
{
    Params p = {{ "id", params["id"] },
                { "value", string("save ") + Utils::to_string(position) }};
    connection->sendCommand("set_state", p);
}
