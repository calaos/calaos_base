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
#include <Modules.h>
#include "Prefix.h"

ModuleManager::ModuleManager()
{
    ecore_file_init();
}

struct DlCloseHandle
{
    template <class T> void operator ()(T &p) const
    {
        if (p.inst) delete p.inst;
        p.inst = NULL;
        dlclose(p.handle);
        p.handle = NULL;
        p.api = NULL;
    }
};
ModuleManager::~ModuleManager()
{
    for_each(modules.begin(), modules.end(), DlCloseHandle());
    for_each(mods_inst.begin(), mods_inst.end(), DlCloseHandle());
}

void ModuleManager::SearchModules()
{
    for (uint i = 0;i < search_paths.size();i++)
    {
        cInfoDom("module") << "ModuleManager: searching modules in: " << search_paths[i];
        char *fname = NULL;
        void *data = NULL;
        Eina_List *subdir = ecore_file_ls(search_paths[i].c_str());
        Eina_List *l = NULL;

        EINA_LIST_FOREACH(subdir, l, data)
        {
            fname = (char *)data;
            string p;

            p = search_paths[i];

            if (fname)
            {
                p += "/";
                p += fname;
            }
            if (p[p.length() - 1] != '/') p += "/";

            if (!ecore_file_is_dir(p.c_str()))
                continue;

            p += "module.so";

            if (!ecore_file_exists(p.c_str()))
                continue;

            bool alreadyin = false;
            for (uint im = 0;im < modules.size() && !alreadyin;im++)
            {
                ModuleDef mdef = modules[im];
                if (p == mdef.mod_fname)
                    alreadyin = true;
            }

            if (alreadyin) continue;

            //try to load the module
            void *handle = dlopen(p.c_str(), RTLD_LAZY);

            if (handle)
            {
                //object can be loaded, check version and
                CalaosModuleApi *api = (CalaosModuleApi *)dlsym(handle, "calaos_modapi");
                if (!api)
                {
                    cErrorDom("module") << "ModuleManager: module " << p << ". calaos_modapi export not found: " << dlerror();
                    continue;
                }

                if (api->api_version != CALAOS_MODULE_API_VERSION)
                {
                    dlclose(handle);

                    cErrorDom("module") << "ModuleManager: module " << p << ". The API version doesn't match";

                    continue;
                }

                string module_name;
                vector<string> tok;
                Utils::split(p, tok, "/");
                if (tok.size() > 2)
                    module_name = tok[tok.size() - 2];

                string themepath = Prefix::Instance().dataDirectoryGet();
                themepath += "/widgets/" + module_name;

                ModuleDef mdef;
                mdef.mod_name = api->name;
                mdef.mod_desc = api->desc;
                mdef.mod_version = api->version;
                mdef.mod_author = api->author;
                mdef.mod_icon = themepath + "/icon.edj";
                mdef.mod_fname = p;
                mdef.handle = handle;
                mdef.inst = NULL;
                mdef.api = api;

                cInfoDom("module") << "ModuleManager: found module: " << mdef.mod_name;

                modules.push_back(mdef);
            }
            else
            {
                cWarningDom("module") << "ModuleManager: file " << p << " : failed to dlopen: " << dlerror();
            }

        }

        EINA_LIST_FREE(subdir, data)
                free(data);
    }
}

bool ModuleManager::createModuleInstance(Evas *evas, ModuleDef &type, ModuleDef &mdef, string id)
{
    if (!type.handle || !type.api) return false;

    string module_name;
    vector<string> tok;
    Utils::split(type.mod_fname, tok, "/");
    if (tok.size() > 2)
        module_name = tok[tok.size() - 2];

    string themepath = Prefix::Instance().dataDirectoryGet();
    themepath += "/widgets/" + module_name;

    CalaosModuleBase *cmod = type.api->create_object(evas, id.c_str(), themepath.c_str());

    if (cmod)
    {
        mdef.mod_name = type.mod_name;
        mdef.mod_desc = type.mod_desc;
        mdef.mod_version = type.mod_version;
        mdef.mod_author = type.mod_author;
        mdef.mod_icon = themepath + "/icon.edj";
        mdef.mod_fname = type.mod_fname;
        mdef.inst = cmod;
        mdef.handle = type.handle;
        mdef.api = type.api;

        cInfoDom("module") << "ModuleManager: New module instance: " << mdef.mod_name;
        cInfoDom("module") << "ModuleManager: module icon: " << mdef.mod_icon;

        mods_inst.push_back(mdef);

        return true;
    }

    mdef.inst = NULL;
    mdef.handle = NULL;
    mdef.api = NULL;

    return false;
}

void ModuleManager::DeleteInstance(ModuleDef &mod)
{
    //safety check

    if (!mod.inst) return;

    vector<ModuleDef>::iterator it = mods_inst.begin();
    for (int i = 0;i < instanceSize();i++, it++)
    {
        if (mod.inst == mods_inst[i].inst)
        {
            CalaosModuleBase *cmod = getModuleInstance(mod);
            if (cmod) delete cmod;
            mod.inst = NULL;

            mods_inst.erase(it);

            break;
        }
    }
}

CalaosModuleBase *ModuleManager::getModuleInstance(ModuleDef &mod)
{
    //safety check

    if (!mod.inst) return NULL;

    bool found = false;
    for (int i = 0;i < instanceSize();i++)
    {
        if (mod.inst == mods_inst[i].inst) found = true;
    }

    if (found) return mod.inst;

    return NULL;
}

vector<ModuleDef> ModuleManager::getModuleInstances(string mod_fname)
{
    vector<ModuleDef> mods;

    for (int i = 0;i < instanceSize();i++)
    {
        if (mods_inst[i].mod_fname == mod_fname)
            mods.push_back(mods_inst[i]);
    }

    return mods;
}
