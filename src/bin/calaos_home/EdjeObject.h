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
#ifndef EDJEOBJECT_H
#define EDJEOBJECT_H


#include <Evas.h>
#include <Edje.h>
#include <Utils.h>
#include <CommonUtils.h>

using namespace Utils;

/* Edje signal callback
   Prototype is callback(void *data, Evas_Object *edje_object, string emission, string source)
*/
typedef sigc::slot<void, void *, Evas_Object *, std::string, std::string> EdjeCallBack;
typedef sigc::signal<void, void *, Evas_Object *, std::string, std::string> EdjeCallBackSignal;

typedef struct _EdjeCallbackData: public sigc::trackable
{
        EdjeCallBackSignal signal_cb;
        sigc::connection connection;
        void *user_data;
        string signal;
        string source;
} EdjeCallbackData;

#define CHECK_EDJE_RETURN(...) \
        if (!edje) \
        { \
                cCritical() <<  "EdjeObject: Edje object NULL ! (" << collection  << ")"; \
                return __VA_ARGS__; \
        }

class EAPI EdjeObject: public sigc::trackable
{
        public:
                void _evasObjectDeleted();
                void _evasObjectShow();
                void _evasObjectHide();

        protected:
                string theme; //Edje theme filename
                string collection; //Edje collection group

                Evas *evas;
                Evas_Object *edje; //The edje object

                vector<EdjeCallbackData *> callbacks;

                bool autodelete; //autodelete EdjeObject if Evas_Object is deleted. Be carefull with this

                list<Evas_Object *> swallow_objs;
                list<EdjeObject *> swallow_eobjs;

                virtual void objectDeleted() { }
                virtual void objectShown() { }
                virtual void objectHidden() { }

        public:
                EdjeObject(string &_theme, Evas *_evas);
                EdjeObject(const char *_theme, Evas *_evas);
                virtual ~EdjeObject();

                //load the edje file
                bool LoadEdje(string collection);

                virtual void Show() { CHECK_EDJE_RETURN() evas_object_show(edje); }
                virtual void Hide() { CHECK_EDJE_RETURN() evas_object_hide(edje); }
                void Move(int x, int y) { CHECK_EDJE_RETURN() evas_object_move(edje, x, y); }
                void Resize(int w, int h) { CHECK_EDJE_RETURN() evas_object_resize(edje, w, h); }

                void getGeometry(int *x, int *y, int *w, int *h);
                void getSizeMin(int *w, int *h) { CHECK_EDJE_RETURN() edje_object_size_min_get(edje, w, h); }
                void getSizeMax(int *w, int *h) { CHECK_EDJE_RETURN() edje_object_size_max_get(edje, w, h); }

                void setLayer(int i) { CHECK_EDJE_RETURN() evas_object_layer_set(edje, i); }
                int getLayer() { CHECK_EDJE_RETURN(0) return evas_object_layer_get(edje); }

                void EmitSignal(string signal, string source) { CHECK_EDJE_RETURN() edje_object_signal_emit(edje, signal.c_str(), source.c_str()); }

                void setPartText(string part, string text) { CHECK_EDJE_RETURN() edje_object_part_text_set(edje, part.c_str(), text.c_str()); }
                string getPartText(string part);

                void setDragValue(string part, double x, double y) { CHECK_EDJE_RETURN() edje_object_part_drag_value_set(edje, part.c_str(), x, y); }
                void getDragValue(string part, double *x, double *y) { CHECK_EDJE_RETURN() edje_object_part_drag_value_get(edje, part.c_str(), x, y); }

                void Swallow(EdjeObject *obj, string part, bool delete_on_del = false);
                void Swallow(Evas_Object *obj, string part, bool delete_on_del = false);

                Evas_Object *getEvasObject() { CHECK_EDJE_RETURN(NULL) return edje; }

                void setTheme(string &_theme) { theme = _theme; }

                string getCollection() { return collection; }

                void setAutoDelete(bool autodel) { autodelete = autodel; }
                bool getAutoDelete() { return autodelete; }

                sigc::connection *addCallback(string source, string signal, EdjeCallBack slot_cb, void *user_data = NULL);
                void delCallback(sigc::connection *connection);

                /* Object was deleted */
                sigc::signal<void> object_deleted;

                /* Object was shown/hidden */
                sigc::signal<void> object_shown;
                sigc::signal<void> object_hidden;

                /* Edje signal */
                sigc::signal<void, void *, Evas_Object *, std::string, std::string> object_signal;
};

#endif // EDJEOBJECT_H
