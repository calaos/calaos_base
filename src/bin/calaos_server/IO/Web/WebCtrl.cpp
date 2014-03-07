/******************************************************************************
**  Copyright (c) 2007-2013, Calaos. All Rights Reserved.
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
#include <WebCtrl.h>
#include <jansson.h>
#include <Params.h>

using namespace Calaos;

unordered_map<string, WebCtrl> WebCtrl::hash;

WebCtrl::WebCtrl()
{

}

WebCtrl::WebCtrl(Params &p, int _file_type)
{
    dlManager = NULL;
    timer = NULL;
    param = p;
    frequency = 60.0;
    file_type = _file_type;
}

WebCtrl::~WebCtrl()
{
    if (timer)
        delete timer;
    if (dlManager)
        delete dlManager;
}


WebCtrl &WebCtrl::Instance(Params &p)
{
    string url = p.get_param("url");

    if (hash.find(url) == hash.end())
    {
        string str_file_type = p.get_param("file_type");
        int file_type;

        if (str_file_type == "xml")
            file_type = WebCtrl::XML;
        else if (str_file_type == "json")
            file_type = WebCtrl::JSON;
        else
            file_type = WebCtrl::UNKNOWN;

        hash[url] = WebCtrl(p, file_type);
    }

    return hash[url];
}


void WebCtrl::Add(double _frequency = 60.0)
{
    if (frequency > _frequency )
        frequency = _frequency;

    if (!timer)
        timer = new EcoreTimer(frequency, [=]() {
            string filename = "/tmp/calaos_" + param.get_param("id") + ".part";
            dlManager->add(param.get_param("url"), filename, [=](string emission, string source, void* data) {
                string dest =  "/tmp/calaos_" + param.get_param("id");
                string src = dest + ".part";
                ecore_file_mv(src.c_str(), dest.c_str());
            }, [=](string url, string destination_file, double dl_now, double dl_total, void *data) {
            }, NULL);
        });
    else
        timer->Reset(frequency);

    if (!dlManager)
        dlManager = new DownloadManager();
}

double WebCtrl::getValueJson(string path, string filename)
{
    double value = 0.0;
    json_t *root, *parent, *var;
    json_error_t err;

    vector<string> tokens;
    vector<string>::iterator it;

    Utils::split(path, tokens, "/");

    root = json_load_file(filename.c_str(), 0, &err);

    if (tokens.size())
    {
        if(root)
        {
            parent = root;
            for (it = tokens.begin();it != tokens.end();it++)
            {
                string val = *it;
                var = json_object_get(parent, val.c_str());
                if (!var)
                {
                    var = parent;
                    break;
                }
                else
                {
                    parent = var;
                }
            }
            if (var)
            {
                value = json_number_value(var);
                json_decref(var);
                json_decref(root);
            }

        }
    }
    else
    {
        var = json_object_get(root, path.c_str());
        if (var)
        {
            json_decref(var);
            value = json_number_value(var);
            json_decref(root);
        }
    }
    return value;
}

double WebCtrl::getValueXml(string path, string filename)
{
    double value = 0.0;
    TiXmlDocument document(filename);
    if (!document.LoadFile())
    {
        // Error loading file
        return 0.0;
    }

    vector<string> tokens;

    Utils::split(path, tokens, "/");
    TiXmlHandle docHandle(&document);


    //TiXmlElement *root = docHandle.FirstChildElement(*it).ToElement();
    /* Do something */
    return value;
}

double WebCtrl::getValue(string path)
{
    string filename;

    filename = "/tmp/calaos_" + param.get_param("id");

    if (file_type == JSON)
        return getValueJson(path, filename);
    else if (file_type == XML)
        return getValueXml(path, filename);
    else
        return 0.0;
}

