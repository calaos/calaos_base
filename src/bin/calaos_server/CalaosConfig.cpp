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
#include "CalaosConfig.h"
#include <Eet.h>

namespace Calaos {

typedef struct _ConfigStateValue
{
public:
    char *id;
    char *value;

    _ConfigStateValue():
        id(NULL),
        value(NULL) {}
    ~_ConfigStateValue()
    {
        free(id);
        free(value);
    }

} ConfigStateValue;

typedef struct
{
    unsigned int version; //versionned cache
    Eina_Hash *states;

} ConfigStateCache;

static Eet_Data_Descriptor *edd_state = NULL;
static Eet_Data_Descriptor *edd_cache = NULL;

static void _eina_hash_free_cb(void *data)
{
    delete (ConfigStateValue *)data;
}

Config::Config()
{
    //Init eet for States file
    eet_init();
    initEetDescriptors();

    //read config hash table
    cache_states = eina_hash_string_superfast_new(_eina_hash_free_cb);
    loadStateCache();

    saveCacheTimer = new EcoreTimer(60.0, [=]() { saveStateCache(); });
}

Config::~Config()
{
    eina_hash_free(cache_states);
    releaseEetDescriptors();
    eet_shutdown();
}

void Config::initEetDescriptors()
{
    Eet_Data_Descriptor_Class edc;

    EET_EINA_FILE_DATA_DESCRIPTOR_CLASS_SET(&edc, ConfigStateValue);
    edd_state = eet_data_descriptor_stream_new(&edc);

    EET_DATA_DESCRIPTOR_ADD_BASIC(edd_state, ConfigStateValue, "id", id, EET_T_STRING);
    EET_DATA_DESCRIPTOR_ADD_BASIC(edd_state, ConfigStateValue, "value", value, EET_T_STRING);

    EET_EINA_FILE_DATA_DESCRIPTOR_CLASS_SET(&edc, ConfigStateCache);
    edd_cache = eet_data_descriptor_stream_new(&edc);

    EET_DATA_DESCRIPTOR_ADD_BASIC(edd_cache, ConfigStateCache, "version", version, EET_T_UINT);
    EET_DATA_DESCRIPTOR_ADD_HASH(edd_cache, ConfigStateCache, "states", states, edd_state);
}

void Config::releaseEetDescriptors()
{
    eet_data_descriptor_free(edd_cache);
    eet_data_descriptor_free(edd_state);
}

void Config::LoadConfigIO()
{
    std::string file = Utils::getConfigFile(IO_CONFIG);

    if (!Utils::fileExists(file))
    {
        std::ofstream conf(file.c_str(), std::ofstream::out);
        conf << "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>" << std::endl;
        conf << "<calaos:ioconfig xmlns:calaos=\"http://www.calaos.fr\">" << std::endl;
        conf << "<calaos:home></calaos:home>" << std::endl;
        conf << "</calaos:ioconfig>" << std::endl;
        conf.close();
    }

    TiXmlDocument document(file);

    if (!document.LoadFile())
    {
        cError() <<  "There was a parse error";
        cError() <<  document.ErrorDesc();
        cError() <<  "In file " << file << " At line " << document.ErrorRow();

        exit(-1);
    }

    TiXmlHandle docHandle(&document);

    TiXmlElement *room_node = docHandle.FirstChildElement("calaos:ioconfig").FirstChildElement("calaos:home").FirstChildElement().ToElement();
    for(; room_node; room_node = room_node->NextSiblingElement())
    {
        if (room_node->ValueStr() == "calaos:room" &&
            room_node->Attribute("name") &&
            room_node->Attribute("type"))
        {
            std::string name, type;
            int hits = 0;

            name = room_node->Attribute("name");
            type = room_node->Attribute("type");
            if (room_node->Attribute("hits"))
                room_node->Attribute("hits", &hits);

            Room *room = new Room(name, type, hits);
            ListeRoom::Instance().Add(room);

            room->LoadFromXml(room_node);
        }
    }

    cInfo() <<  "Done. ";
}

void Config::SaveConfigIO()
{
    std::string file = Utils::getConfigFile(IO_CONFIG);
    std::string tmp = file + "_tmp";

    cInfo() <<  "Saving " << file << "...";

    TiXmlDocument document;
    TiXmlDeclaration *decl = new TiXmlDeclaration("1.0", "UTF-8", "");
    TiXmlElement *ionode = new TiXmlElement("calaos:ioconfig");
    ionode->SetAttribute("xmlns:calaos", "http://www.calaos.fr");
    document.LinkEndChild(decl);
    document.LinkEndChild(ionode);
    TiXmlElement *node = new TiXmlElement("calaos:home");
    ionode->LinkEndChild(node);

    for (int i = 0;i < ListeRoom::Instance().size();i++)
    {
        Room *room = ListeRoom::Instance().get_room(i);
        room->SaveToXml(node);
    }

    if (document.SaveFile(tmp))
    {
        ecore_file_unlink(file.c_str());
        ecore_file_mv(tmp.c_str(), file.c_str());
    }

    cInfo() <<  "Done.";
}

void Config::LoadConfigRule()
{
    std::string file = Utils::getConfigFile(RULES_CONFIG);

    if (!Utils::fileExists(file))
    {
        std::ofstream conf(file.c_str(), std::ofstream::out);
        conf << "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>" << std::endl;
        conf << "<calaos:rules xmlns:calaos=\"http://www.calaos.fr\">" << std::endl;
        conf << "</calaos:rules>" << std::endl;
        conf.close();
    }

    TiXmlDocument document(file);

    if (!document.LoadFile())
    {
        cError() <<  "There was a parse error in " << file;
        cError() <<  document.ErrorDesc();
        cError() <<  "In file " << file << " At line " << document.ErrorRow();

        exit(-1);
    }

    TiXmlHandle docHandle(&document);

    TiXmlElement *rule_node = docHandle.FirstChildElement("calaos:rules").FirstChildElement().ToElement();

    if (!rule_node)
    {
        cError() <<  "Error, <calaos:rules> node not found in file " << file;
    }

    for(; rule_node; rule_node = rule_node->NextSiblingElement())
    {
        if (rule_node->ValueStr() == "calaos:rule" &&
            rule_node->Attribute("name") &&
            rule_node->Attribute("type"))
        {
            std::string name, type;

            name = rule_node->Attribute("name");
            type = rule_node->Attribute("type");

            Rule *rule = new Rule(type, name);
            rule->LoadFromXml(rule_node);

            ListeRule::Instance().Add(rule);
        }
    }

    cInfo() <<  "Done. " << ListeRule::Instance().size() << " rules loaded.";
}

void Config::SaveConfigRule()
{
    std::string file = Utils::getConfigFile(RULES_CONFIG);
    std::string tmp = file + "_tmp";

    cInfo() <<  "Saving " << file << "...";

    TiXmlDocument document;
    TiXmlDeclaration *decl = new TiXmlDeclaration("1.0", "UTF-8", "");
    TiXmlElement *rulesnode = new TiXmlElement("calaos:rules");
    rulesnode->SetAttribute("xmlns:calaos", "http://www.calaos.fr");
    document.LinkEndChild(decl);
    document.LinkEndChild(rulesnode);

    for (int i = 0;i < ListeRule::Instance().size();i++)
    {
        Rule *rule = ListeRule::Instance().get_rule(i);
        rule->SaveToXml(rulesnode);
    }

    if (document.SaveFile(tmp))
    {
        ecore_file_unlink(file.c_str());
        ecore_file_mv(tmp.c_str(), file.c_str());
    }

    cInfo() <<  "Done.";
}

void Config::loadStateCache()
{
    ConfigStateCache *cache;
    std::string file = Utils::getCacheFile("iostates.cache");

    Eet_File *ef = eet_open(file.c_str(), EET_FILE_MODE_READ);
    if (!ef)
    {
        cWarning() <<  "Could not open iostates.cache for read !";
        return;
    }

    cache = (ConfigStateCache *)eet_data_read(ef, edd_cache, "calaos/states/cache");
    if (!cache)
    {
        eet_close(ef);
        cWarning() <<  "Could not read iostates.cache, corrupted file?";
        return;
    }

    if (cache->version < CONFIG_STATES_CACHE_VERSION)
    {
        cWarning() <<  "File version too old, upgrading to new format";
        cache->version = CONFIG_STATES_CACHE_VERSION;
    }

    //read all states and put it in cache_states
    Eina_Iterator *it = eina_hash_iterator_tuple_new(cache->states);
    void *data;
    while (eina_iterator_next(it, &data))
    {
        Eina_Hash_Tuple *t = (Eina_Hash_Tuple *)data;
        ConfigStateValue *state = (ConfigStateValue *)t->data;
        std::string skey = state->id;
        std::string svalue = state->value;
        SaveValueIO(skey, svalue, false);
    }
    eina_iterator_free(it);

    eina_hash_free(cache->states);
    free(cache);
    eet_close(ef);

    cInfo() <<  "States cache read successfully.";
}

void Config::saveStateCache()
{
    Eet_File *ef;
    std::string file = Utils::getCacheFile("iostates.cache");
    std::string tmp = file + "_tmp";
    ConfigStateCache *cache;

    cache = new ConfigStateCache;
    cache->version = CONFIG_STATES_CACHE_VERSION;
    cache->states = cache_states;

    ef = eet_open(tmp.c_str(), EET_FILE_MODE_WRITE);
    if (!ef)
    {
        cWarning() <<  "Could not open iostates.cache for write !";
        delete cache;
        return;
    }

    Eina_Bool ret = eet_data_write(ef, edd_cache, "calaos/states/cache", cache, EINA_TRUE);

    eet_close(ef);
    delete cache;

    if (ret)
    {
        ecore_file_unlink(file.c_str());
        ecore_file_mv(tmp.c_str(), file.c_str());
    }

    cInfo() <<  "State cache file written successfully (" << file << ")";
}

void Config::SaveValueIO(std::string id, std::string value, bool save)
{
    if (eina_hash_find(cache_states, id.c_str()))
    {
        ConfigStateValue *v = new ConfigStateValue;
        v->id = strdup(id.c_str());
        v->value = strdup(value.c_str());
        void *old_data = eina_hash_set(cache_states, id.c_str(), v);
        delete (ConfigStateValue *)old_data;
    }
    else
    {
        ConfigStateValue *v = new ConfigStateValue;
        v->id = strdup(id.c_str());
        v->value = strdup(value.c_str());
        eina_hash_add(cache_states, id.c_str(), v);
    }

    if (save)
        saveStateCache();
}

bool Config::ReadValueIO(std::string id, std::string &value)
{
    ConfigStateValue *v = (ConfigStateValue *)eina_hash_find(cache_states, id.c_str());
    if (!v)
        return false;

    value = v->value;

    return true;
}

}
