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
#include "CalaosConfig.h"
#include <Eet.h>

using namespace Calaos;

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
              cErrorDom("root") <<  "Config::LoadConfigIO() There was a parse error";
              cErrorDom("root") <<  document.ErrorDesc();
              cErrorDom("root") <<  "In file " << file << " At line " << document.ErrorRow();

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
                        string name, type;
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

        cInfoDom("root") <<  "Config::LoadConfigIO() Done. ";
}

void Config::SaveConfigIO()
{
        string file = Utils::getConfigFile(IO_CONFIG);
        string tmp = file + "_tmp";

        cInfoDom("root") <<  "Config::SaveConfigIO() Saving " << file << "...";

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

        cInfoDom("root") <<  "Config::SaveConfigIO() Done.";
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
                cErrorDom("root") <<  "Config::LoadConfigRule() There was a parse error in " << file;
                cErrorDom("root") <<  document.ErrorDesc();
                cErrorDom("root") <<  "In file " << file << " At line " << document.ErrorRow();

                exit(-1);
        }

        TiXmlHandle docHandle(&document);

        TiXmlElement *rule_node = docHandle.FirstChildElement("calaos:rules").FirstChildElement().ToElement();

        if (!rule_node)
        {
                cErrorDom("root") <<  "Config::LoadConfigRule() Error, <calaos:rules> node not found in file " << file;
        }

        for(; rule_node; rule_node = rule_node->NextSiblingElement())
        {
                if (rule_node->ValueStr() == "calaos:rule" &&
                    rule_node->Attribute("name") &&
                    rule_node->Attribute("type"))
                {
                        string name, type;

                        name = rule_node->Attribute("name");
                        type = rule_node->Attribute("type");

                        Rule *rule = new Rule(type, name);
                        rule->LoadFromXml(rule_node);

                        ListeRule::Instance().Add(rule);
                }
        }

        cInfoDom("root") <<  "Config::LoadConfigRule() Done. " << ListeRule::Instance().size() << " rules loaded.";
}

void Config::SaveConfigRule()
{
        string file = Utils::getConfigFile(RULES_CONFIG);
        string tmp = file + "_tmp";

        cInfoDom("root") <<  "Config::SaveConfigRule() Saving " << file << "...";

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

        cInfoDom("root") <<  "Config::SaveConfigRule() Done.";
}

void Config::loadStateCache()
{
        ConfigStateCache *cache;
        string file = Utils::getCacheFile("iostates.cache");

        Eet_File *ef = eet_open(file.c_str(), EET_FILE_MODE_READ);
        if (!ef)
        {
                cWarningDom("root") <<  "Config::loadStateCache() could not open iostates.cache for read !";
                return;
        }

        cache = (ConfigStateCache *)eet_data_read(ef, edd_cache, "calaos/states/cache");
        if (!cache)
        {
                eet_close(ef);
                cWarningDom("root") <<  "Config::loadStateCache() could not read iostates.cache, corrupted file?";
                return;
        }

        if (cache->version < CONFIG_STATES_CACHE_VERSION)
        {
                cWarningDom("root") <<  "Config::loadStateCache() file version too old, upgrading to new format";
                cache->version = CONFIG_STATES_CACHE_VERSION;
        }

        //read all states and put it in cache_states
        Eina_Iterator *it = eina_hash_iterator_tuple_new(cache->states);
        void *data;
        while (eina_iterator_next(it, &data))
        {
                Eina_Hash_Tuple *t = (Eina_Hash_Tuple *)data;
                ConfigStateValue *state = (ConfigStateValue *)t->data;
                string skey = state->id;
                string svalue = state->value;
                SaveValueIO(skey, svalue, false);
        }
        eina_iterator_free(it);

        eet_close(ef);

        cInfoDom("root") <<  "Config::loadStateCache(): States cache read successfully.";
}

void Config::saveStateCache()
{
        Eet_File *ef;
        string file = Utils::getCacheFile("iostates.cache");
        string tmp = file + "_tmp";
        ConfigStateCache *cache;

        cache = new ConfigStateCache;
        cache->version = CONFIG_STATES_CACHE_VERSION;
        cache->states = cache_states;

        ef = eet_open(tmp.c_str(), EET_FILE_MODE_WRITE);
        if (!ef)
        {
                cWarningDom("root") <<  "Config::saveStateCache() could not open iostates.cache for write !";
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

        cDebugDom("root") <<  "Config::saveStateCache(): States cache written successfully.";
}

void Config::SaveValueIO(string id, string value, bool save)
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

bool Config::ReadValueIO(string id, string &value)
{
        ConfigStateValue *v = (ConfigStateValue *)eina_hash_find(cache_states, id.c_str());
        if (!v)
                return false;

        value = v->value;

        return true;
}
