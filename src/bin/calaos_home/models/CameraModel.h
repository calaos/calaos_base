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
#ifndef CAMERAMODEL_H
#define CAMERAMODEL_H

#include <Utils.h>

#include "CalaosConnection.h"
#include "RoomModel.h"

using namespace Utils;

class Camera: public sigc::trackable
{
private:
    CalaosConnection *connection;

    Room *room;

    void sendAction_cb(bool success, vector<string> result, void *data);

public:
    Camera(CalaosConnection *c):
        connection(c),
        room(NULL)
    {}

    void camera_get_cb(bool success, vector<string> result, void *data);

    Params params;

    //Return the room where the camera is
    Room *getRoom();

    void MoveCenter();
    void MoveUp();
    void MoveDown();
    void MoveLeft();
    void MoveRight();
    void ZoomIn();
    void ZoomOut();
    void Recall(int position);
    void Save(int position);

    sigc::signal<void, Camera *> load_done;
};

class CameraModel: public sigc::trackable
{
private:
    CalaosConnection *connection;

    int load_count;
    void load_camera_done(Camera *camera);

    void camera_count_cb(bool success, vector<string> result, void *data);

public:
    CameraModel(CalaosConnection *connection);
    ~CameraModel();

    void load();

    list<Camera *> cameras;

    sigc::signal<void> load_done;
};

#endif // CAMERAMODEL_H
