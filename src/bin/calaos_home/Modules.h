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
 **  along with Foobar; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/
#ifndef CALAOS_MODULES
#define CALAOS_MODULES

#include <Utils.h>
#include <dlfcn.h>
#include <Ecore_Evas.h>
#include <EvasSmart.h>
#include <Ecore_File.h>
#include <CalaosModule.h>

using namespace std;

class ModuleDef
{
public:
    ModuleDef(): mod_name("\0"), mod_author("\0"),
        mod_desc("\0"), mod_version("\0"),
        mod_icon("\0"), mod_fname("\0"),
        api(NULL), inst(NULL), handle(NULL)
    {}

    string mod_name; // module name as returned by module->getName()
    string mod_author; // module name as returned by module->getName()
    string mod_desc; // module description as returned by module->getDescription()
    string mod_version; // module version as returned by module->getVersion()
    string mod_icon; // module icon, this is the filename + path of an edje file containing the module icon
    // The group "icon" is loaded from the edje file. If nothing is found, it take the
    // default icon from the main theme.
    string mod_fname; //module filename

    CalaosModuleApi *api;

    CalaosModuleBase *inst; //module instance

    void *handle; //shared object handle
};

class ModuleManager
{
private:
    ModuleManager();
    ~ModuleManager();

    //search paths
    vector<string> search_paths;

    //all running modules are stored here
    vector<ModuleDef> mods_inst;

    //all loadable modules are listed here
    vector<ModuleDef> modules;

public:
    static ModuleManager &Instance()
    {
        static ModuleManager mmanager;

        return mmanager;
    }

    void addPath(string path) { search_paths.push_back(path); }

    // Search for loadable modules in paths
    void SearchModules();

    // Get all available modules
    vector<ModuleDef> getAvailableModules() { return modules; }

    // Create a new instance of mod_fname
    bool createModuleInstance(Evas *evas, ModuleDef &type, ModuleDef &mdef, string id);

    // Delete a module instance
    void DeleteInstance(ModuleDef &mod);

    // Get C++ instance of i module
    CalaosModuleBase *getModuleInstance(ModuleDef &mod);

    // Get all instance for a specific module type
    vector<ModuleDef> getModuleInstances(string mod_fname);

    //total number of modules instance
    int instanceSize() { return mods_inst.size(); }
    //total modules available
    int availableSize() { return modules.size(); }
};

#endif
