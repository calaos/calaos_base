/******************************************************************************
**  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
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
#ifndef CXWidget_H
#define CXWidget_H

#include "EdjeObject.h"
#include "EcoreTimer.h"
#include "Widget.h"

class Flake
{
        private:
                EdjeObject *object;
                int x, y;
                int width, height;
                int speed;
                double start_time;

        public:
                Flake(EdjeObject *o): x(0), y(0)
                        { setObject(o); }
                Flake(): object(NULL), x(0), y(0), width(0), height(0) { }
                ~Flake() { delete object; }

                void setObject(EdjeObject *o)
                        { object = o; edje_object_size_min_get(object->getEvasObject(), &width, &height); }
                EdjeObject *getObject()
                        { return object; }
                void Move(int _x, int _y)
                        { x = _x; y = _y; if (object) object->Move(x, y); }
                int getX() { return x; }
                int getY() { return y; }
                void Resize() { if (object) object->Resize(width, height); }
                void Show() { object->Show(); }
                void Hide() { object->Hide(); }
                int getWidth() { return width; }
                void setWidth(int w) { width = w; }
                int getHeight() { return height; }
                void setHeight(int h) { height = h; }
                int getSpeed() { return speed; }
                void setSpeed(int s) { speed = s; }
                double getStart() { return start_time; }
                void setStart(double s) { start_time = s; }
};

class XmasWidget: public Widget
{
        private:
                Ecore_Animator *animator;
                Evas_Object *clip;
                vector<Flake *> flakes;
                int clipx, clipy, clipw, cliph;

                bool LoadWidget(string type, double x, double y, string _id);

        public:
                XmasWidget(string &_theme, Evas *_evas, ModuleDef &_mdef, string _id, Evas_Object *parent, ActivityWidgetsView *view);
                ~XmasWidget();

                virtual void Show();
                virtual void Hide();
                virtual void Move(int x, int y);
                virtual void Resize(int w, int h);

                virtual void EditMode();
                virtual void NormalMode();

                //Reset position of widget
                virtual void Reset();

                //Save config & position to xml
                virtual void Save(TiXmlElement *node);

                virtual string getId() { return id; }
                virtual string getType() { return mtype; }
                virtual string getStringInfo();

                void _Animator();
};

#endif
