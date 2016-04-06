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
#ifndef CalaosModule_H
#define CalaosModule_H

#include <Utils.h>
#include <Evas.h>
#include <Ecore.h>
#include <Edje.h>

#define CALAOS_MODULE_API_VERSION       2

enum CalaosModuleType { CMOD_NONE=0, CMOD_WIDGET /* more to come */ };

#define MODULE_CONFIG_PATH     "/mnt/ext3/calaos/.modules/"

class CalaosModuleBase
{
protected:
    Evas *evas;
    std::string id;
    std::string module_path;

public:
    CalaosModuleBase(Evas *_evas, std::string _id, std::string _module_path):
        evas(_evas), id(_id), module_path(_module_path)
    { }

    /* These functions need to be implemented in modules */
    virtual ~CalaosModuleBase() { }

    /* Return a string that represent the current module instance
                   It's used to display a list of active module
                */
    virtual std::string getStringInfo() { return ""; }

    virtual void getSizeMin(int &w, int &h) { }
    virtual void getSizeMax(int &w, int &h) { }

    virtual Evas_Object *getEvasObject() { return NULL; }
};


/* Calaos Module API, all modules should define this */
typedef struct _CalaosModuleApi
{
    int                     api_version;            // API version, must be CALAOS_MODULE_API_VERSION
    CalaosModuleType        type;                   // Module type
    const char *            name;                   // Module name
    const char *            desc;                   // Module description
    const char *            version;                // Module Revision
    const char *            author;                 // Module author's name

    // Return a new instance for the derived CalaosModuleBase class
    CalaosModuleBase * (*create_object)(Evas *evas, const char *id, const char *path);

} CalaosModuleApi;

#endif
