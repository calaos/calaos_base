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
#include <EdjeObject.h>

static void _edje_object_signal_cb(void *data, Evas_Object *obj, const char *emission, const char *source);
static void _edje_del(void *data, Evas *e , Evas_Object *obj, void *event_info);
static void _edje_show(void *data, Evas *e , Evas_Object *obj, void *event_info);
static void _edje_hide(void *data, Evas *e , Evas_Object *obj, void *event_info);

EdjeObject::EdjeObject(string &_theme, Evas *_evas):
        theme(_theme),
        evas(_evas),
        autodelete(false)
{
        //create the edje object
        edje = edje_object_add(evas);

        evas_object_event_callback_add(edje, EVAS_CALLBACK_DEL, _edje_del, this);
        evas_object_event_callback_add(edje, EVAS_CALLBACK_SHOW, _edje_show, this);
        evas_object_event_callback_add(edje, EVAS_CALLBACK_HIDE, _edje_hide, this);

        evas_object_data_set(edje, "EdjeObject", this);

        // Add an automatic callback that triggers the object_signal for all signals (*, *)
        addCallback("*", "*", sigc::mem_fun(object_signal, &sigc::signal<void, void *, Evas_Object *, std::string, std::string>::emit));
}

EdjeObject::EdjeObject(const char *_theme, Evas *_evas):
        theme(_theme),
        evas(_evas),
        autodelete(false)
{
        //create the edje object
        edje = edje_object_add(evas);

        evas_object_event_callback_add(edje, EVAS_CALLBACK_DEL, _edje_del, this);
        evas_object_event_callback_add(edje, EVAS_CALLBACK_SHOW, _edje_show, this);
        evas_object_event_callback_add(edje, EVAS_CALLBACK_HIDE, _edje_hide, this);

        evas_object_data_set(edje, "EdjeObject", this);

        // Add an automatic callback that triggers the object_signal for all signals (*, *)
        addCallback("*", "*", sigc::mem_fun(object_signal, &sigc::signal<void, void *, Evas_Object *, std::string, std::string>::emit));
}

EdjeObject::~EdjeObject()
{
        //deletes all swallowed objs
        for_each(swallow_eobjs.begin(), swallow_eobjs.end(), Delete());
        for_each(swallow_objs.begin(), swallow_objs.end(), DeleteEvasObject());

        vector<EdjeCallbackData *>::iterator iter = callbacks.begin();

        for (;callbacks.size() > 0 && iter != callbacks.end();iter++)
        {
                EdjeCallbackData *data = *iter;
                if (edje)
                        edje_object_signal_callback_del(edje, data->signal.c_str(), data->source.c_str(), _edje_object_signal_cb);

                delete data;
        }
        callbacks.clear();

        if (edje)
        {
                void *data = evas_object_event_callback_del_full(edje, EVAS_CALLBACK_DEL, _edje_del, this);
                if (!data)
                        cCritical() <<  "EdjeObject::~EdjeObject : Something went wrong with callback deletion !";

                //Emit deleted signal only if the Evas_Object is still alive
                objectDeleted();
                object_deleted.emit();
        }
        DELETE_NULL_FUNC(evas_object_del, edje)
}

bool EdjeObject::LoadEdje(string c)
{
        CHECK_EDJE_RETURN(false)

        collection = c;
        if (edje_object_file_set(edje, theme.c_str(), collection.c_str()) == 0)
        {
                int err = edje_object_load_error_get(edje);
                string serr = "EdjeObject::LoadEdje(" + theme + ", " + collection + ") - ";
                switch (err)
                {
                        case EDJE_LOAD_ERROR_NONE: serr += "No Error."; break;
                        default:
                        case EDJE_LOAD_ERROR_GENERIC: serr += "Unknown Error"; break;
                        case EDJE_LOAD_ERROR_DOES_NOT_EXIST: serr += "File doesn't exist"; break;
                        case EDJE_LOAD_ERROR_PERMISSION_DENIED: serr += "Permission denied"; break;
                        case EDJE_LOAD_ERROR_RESOURCE_ALLOCATION_FAILED: serr += "Allocation failed"; break;
                        case EDJE_LOAD_ERROR_CORRUPT_FILE: serr += "File is corrupted"; break;
                        case EDJE_LOAD_ERROR_UNKNOWN_FORMAT: serr += "Unknown file format"; break;
                        case EDJE_LOAD_ERROR_INCOMPATIBLE_FILE: serr += "Incompatible file"; break;
                        case EDJE_LOAD_ERROR_UNKNOWN_COLLECTION: serr += "Unknown collection (" + collection + ")"; break;
                        case EDJE_LOAD_ERROR_RECURSIVE_REFERENCE: serr += "Recursive reference"; break;
                }

                throw (runtime_error(serr));

                return false;
        }

        return true;
}

void EdjeObject::getGeometry(int *x, int *y, int *w, int *h)
{
        CHECK_EDJE_RETURN()

        evas_object_geometry_get(edje, x, y, w, h);
}

string EdjeObject::getPartText(string part)
{
        CHECK_EDJE_RETURN("")

        const char *txt = edje_object_part_text_get(edje, part.c_str());
        if (txt)
                return string(txt);

        return "";
}

void EdjeObject::Swallow(EdjeObject *obj, string part, bool delete_on_del)
{
        CHECK_EDJE_RETURN();
        edje_object_part_swallow(edje, part.c_str(), obj->getEvasObject());

        if (delete_on_del)
                swallow_eobjs.push_back(obj);
}

void EdjeObject::Swallow(Evas_Object *obj, string part, bool delete_on_del)
{
        CHECK_EDJE_RETURN();
        edje_object_part_swallow(edje, part.c_str(), obj);

        if (delete_on_del)
                swallow_objs.push_back(obj);
}

sigc::connection *EdjeObject::addCallback(string source, string signal, EdjeCallBack slot_cb, void *user_data)
{
        CHECK_EDJE_RETURN(NULL)

        EdjeCallbackData *data = new EdjeCallbackData;

        data->connection = data->signal_cb.connect(slot_cb);
        data->user_data = user_data;
        data->signal = signal;
        data->source = source;

        callbacks.push_back(data);

        edje_object_signal_callback_add(edje, signal.c_str(), source.c_str(), _edje_object_signal_cb, data);

        return &data->connection;
}

void EdjeObject::delCallback(sigc::connection *connection)
{
        CHECK_EDJE_RETURN()

        vector<EdjeCallbackData *>::iterator iter = callbacks.begin();

        for (;iter != callbacks.end();iter++)
        {
                EdjeCallbackData *data = *iter;
                if (&data->connection == connection)
                {
                        edje_object_signal_callback_del(edje, data->signal.c_str(), data->source.c_str(), _edje_object_signal_cb);

                        delete data;
                        callbacks.erase(iter);

                        break;
                }
        }
}

void EdjeObject::_evasObjectDeleted()
{
        for_each(callbacks.begin(), callbacks.end(), Delete());

        edje = NULL;

        objectDeleted();
        object_deleted.emit();

        if (autodelete)
        {
                //Warning ! We do suicide here.
                //Don't do anything here after that call,
                //it will mess up the stack
                delete this;
        }
}

static void _edje_del(void *data, Evas *e , Evas_Object *obj, void *event_info)
{
        EdjeObject *o = reinterpret_cast<EdjeObject *>(data);
        if (!o) return;

        o->_evasObjectDeleted();
}

void EdjeObject::_evasObjectShow()
{
        object_shown.emit();
        objectShown();
}

static void _edje_show(void *data, Evas *e , Evas_Object *obj, void *event_info)
{
        EdjeObject *o = reinterpret_cast<EdjeObject *>(data);
        if (!o) return;

        o->_evasObjectShow();
}

void EdjeObject::_evasObjectHide()
{
        object_hidden.emit();
        objectHidden();
}

static void _edje_hide(void *data, Evas *e , Evas_Object *obj, void *event_info)
{
        EdjeObject *o = reinterpret_cast<EdjeObject *>(data);
        if (!o) return;

        o->_evasObjectHide();
}

static void _edje_object_signal_cb(void *data, Evas_Object *obj, const char *emission, const char *source)
{
        EdjeCallbackData *d = reinterpret_cast<EdjeCallbackData *>(data);
        if (!d) return;

        d->signal_cb.emit(d->user_data, obj, emission, source);
}

