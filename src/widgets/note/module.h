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
#ifndef CALAOS_MODULE_H
#define CALAOS_MODULE_H

#include <ApplicationMain.h>
#include <CalaosModule.h>
#include <Utils.h>
#include <EdjeObject.h>
#include <Ecore_File.h>

class ModuleNote: public CalaosModuleBase
{
    private:
        EdjeObject *edje;
        std::string text;

        void KeyboardCb(std::string text);

    public:
        ModuleNote(Evas *evas, std::string id, std::string module_path);

        virtual ~ModuleNote();

        virtual std::string getStringInfo();

        virtual void getSizeMin(int &w, int &h);
        virtual void getSizeMax(int &w, int &h);

        virtual Evas_Object *getEvasObject();

        void EdjeCallback(void *data, Evas_Object *edje_object, std::string emission, std::string source);
};

#endif
