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


class Camera: public sigc::trackable
{
private:
    CalaosConnection *connection;
    Room *room;

public:
    Camera(CalaosConnection *c):
        connection(c),
        room(NULL)
    {}

    void load(json_t *data);

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
};

class CameraModel: public sigc::trackable
{
private:
    CalaosConnection *connection;

public:
    CameraModel(CalaosConnection *connection);
    ~CameraModel();

    void load(json_t *data);

    std::list<Camera *> cameras;

    sigc::signal<void> load_done;
};

#endif // CAMERAMODEL_H
