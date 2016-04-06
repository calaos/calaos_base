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
#include "module.h"

CalaosModuleBase * constructor(Evas *evas, const char *id, const char *path)
{
    return new ModuleNote(evas, id, path);
}

EAPI CalaosModuleApi calaos_modapi =
{
    CALAOS_MODULE_API_VERSION,
    CMOD_WIDGET,
    _("Notes"),
    _("Let messages on the screen thanks to the numeric version of the well known yellow paper.") ,
    "2.0",
    "Calaos",
    constructor
};

ModuleNote::ModuleNote(Evas *_e, std::string _id, std::string _path):
    CalaosModuleBase(_e, _id, _path)
{
    std::string theme = module_path + "/default.edj";
    edje = new EdjeObject(theme, evas);
    edje->LoadEdje("widget/note");

    std::string path = MODULE_CONFIG_PATH;
    path += "widget_note/";
    ecore_file_mkpath(path.c_str());

    path += id;

    if (ecore_file_exists(path.c_str()))
    {
        std::ifstream file(path.c_str());

        if (file)
        {
            std::stringstream buf;
            buf << file.rdbuf();
            file.close();
            text = buf.str();

            edje->setPartText("note.text", text);
        }
    }

    edje->addCallback("note", "*", sigc::mem_fun(*this, &ModuleNote::EdjeCallback));
}

ModuleNote::~ModuleNote()
{
    delete edje;
}

void ModuleNote::EdjeCallback(void *data, Evas_Object *edje_object, std::string emission, std::string source)
{
    cDebug() << "Show keyboard";
    ApplicationMain::Instance().ShowKeyboard(_("You can now write the new note to display."),
                                             sigc::mem_fun(*this, &ModuleNote::KeyboardCb),
                                             true,
                                             text,
                                             1);
}

void ModuleNote::KeyboardCb(std::string t)
{
    text = t;
    edje->setPartText("note.text", text);

    std::string path = MODULE_CONFIG_PATH;
    path += "widget_note/";
    path += id;
    std::ofstream file(path.c_str());

    if (file)
    {
        file << text;
        file.close();
    }
}

std::string ModuleNote::getStringInfo()
{
    return text;
}

void ModuleNote::getSizeMin(int &w, int &h)
{
    edje->getSizeMin(&w, &h);
}

void ModuleNote::getSizeMax(int &w, int &h)
{
    edje->getSizeMax(&w, &h);
}

Evas_Object *ModuleNote::getEvasObject()
{
    return edje->getEvasObject();
}
