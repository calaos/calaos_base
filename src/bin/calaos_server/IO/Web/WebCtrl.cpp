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
#include <WebCtrl.h>
#include <jansson.h>
#include <Params.h>

#include <TinyXML/tinyxml.h>
#include <TinyXML/xpath_processor.h>


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
    frequency = 0.0;
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

        if (str_file_type == "xml" || str_file_type == "XML")
            file_type = WebCtrl::XML;
        else if (str_file_type == "json" || str_file_type == "JSON")
            file_type = WebCtrl::JSON;
        else if (str_file_type == "text" || str_file_type == "TEXT")
            file_type = WebCtrl::TEXT;
        else
            file_type = WebCtrl::UNKNOWN;

        hash[url] = WebCtrl(p, file_type);
    }

    return hash[url];
}


void WebCtrl::Add(string path,
                  double _frequency,
                  std::function<void()> fileDownloaded_cb)
{

    if (!_frequency)
    {
       cError() << "The frequency parameter is NUL, plesae set a real value.";
       return;
    }

    fileDownloadedCallbacks.push_back(std::make_pair(path, fileDownloaded_cb));
    if (!frequency || frequency > _frequency )
        frequency = _frequency;

    if (!timer)
        timer = new EcoreTimer(frequency, [=]() {
            launchDownload();
        });
    else
        timer->Reset(frequency);

    if (!dlManager)
        dlManager = new DownloadManager();
    launchDownload();
}

void WebCtrl::Del(string path)
{
    for(unsigned int i = 0; i < fileDownloadedCallbacks.size(); i++)
    {
        if (fileDownloadedCallbacks[i].first == path)
            fileDownloadedCallbacks.erase(fileDownloadedCallbacks.begin() + i);
    }
}

void WebCtrl::launchDownload()
{
    string filename = "/tmp/calaos_" + param.get_param("id") + ".part";
    string u = param.get_param("url");

    if (u.find("http://") == 0)
    {
        dlManager->add(param.get_param("url"), filename, [=](string emission, string source, void* data) {
            string dest =  "/tmp/calaos_" + param.get_param("id");
            string src = dest + ".part";
            ecore_file_mv(src.c_str(), dest.c_str());
            for(unsigned int i = 0; i < fileDownloadedCallbacks.size(); i++)
              {
                fileDownloadedCallbacks[i].second();
              }
          }, [=](string url, string destination_file, double dl_now, double dl_total, void *data) {
          }, NULL);
    }
    else
    {
        for(unsigned int i = 0; i < fileDownloadedCallbacks.size(); i++)
        {
            fileDownloadedCallbacks[i].second();
        }
    }
}

string WebCtrl::getValueJson(string path, string filename)
{
    string value;
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
                // Test if the token is an array index
                // if it's the case, it must be something like [x]
                if (val[0] == '[')
                {
                    int idx;
                    // Remove first and last char
                    val.erase(0, 1);
                    val.pop_back();
                    // Read array index
                    from_string(val, idx);
                    var = json_array_get(parent, idx);
                }
                // Toke is a normal object name
                else
                {
                    var = json_object_get(parent, val.c_str());
                }

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
                if (json_is_string(var))
                    value = json_string_value(var);
                else if (json_is_number(var))
                    value = Utils::to_string(json_number_value(var));
                json_decref(var);
                json_decref(root);
            }
            else
            {
                cError() << "Error, " << path << " Can't be read";
            }
        }
    }
    else
    {
        var = json_object_get(root, path.c_str());
        if (var)
        {
            json_decref(var);
            value = json_string_value(var);
            json_decref(root);
        }
    }
    return value;
}

string WebCtrl::getValueXml(string path, string filename)
{
    TiXmlDocument document(filename);
    string value;
    string xpath;

    if (!document.LoadFile())
    {
        cError() << "Error loading file " << filename;
        // Error loading file
        return "";
    }

    xpath = path + "/text()";
    TinyXPath::xpath_processor proc(document.RootElement(), xpath.c_str());
    value = proc.S_compute_xpath();

    return value;
}

/* 
 * Read plain text file and return value find in path Path is of type:
 * line/pos/separator line is the line number in the file. This
 * function read the correspondig line. If sepearator exists, the line
 * is split with separator as delimiter. The value returned is the pos
 * value in the list. In case separator doesn't exist the value
 * returned is the whole line found.
 * Example : the file contains : 
 * 10.0,10.1,10.2,10.3
 * 20.0,20.1,20.2,20.3
 * 
 * If The path is "2/4/,"
 * The value returned will be the 4th token of the second line : 20.3
 * 
 */
string WebCtrl::getValueText(string path, string filename)
{
    string value;
    vector<string> tokens;
    vector<string> items;
    int line_nb;
    unsigned int item_nb;
    ifstream file(filename);
    string line;
    int i;

    if (!file.is_open())
    {
        cError() << "Error reading file " << filename;
        return "";
    }

    Utils::split(path, tokens, "/");
    from_string(tokens[0], line_nb);
    for (i = 0; i < line_nb; i++)
    {
        getline(file, line);
    }

    if (tokens.size() == 3 && tokens[2] != "")
      {
        Utils::split(line, items, tokens[2]);
        from_string(tokens[1], item_nb);
        if (item_nb <= 0)
          item_nb = 1;
        if (items.size() >= item_nb)
            value = items[item_nb - 1];
      }
    else
      {
        value = line;
      }

    return value;
}


double WebCtrl::getValueDouble(string path)
{
    double val;
    string value;

    value = getValue(path);
    from_string(value, val);
    return val;
}

string WebCtrl::getValue(string path)
{
    string filename;
    string url =  param.get_param("url");

    if (url.find("http://") != 0)
    {

        filename = url;
    }
    else
    {
        filename = "/tmp/calaos_" + param.get_param("id");
    }

    cDebug() << "Filename : " << filename << " file type : " << file_type;

    if (file_type == JSON)
        return getValueJson(path, filename);
    else if (file_type == XML)
        return getValueXml(path, filename);
    else if (file_type == TEXT)
        return getValueText(path, filename);

    else
        return "";
}

void WebCtrl::setValue(string value)
{
    string url =  param.get_param("url");
    string data = param.get_param("data");
    string data_type = param.get_param("data_type");

    // Case where there is no data to send, we assume the value is in the url
    if (param.get_param("data") == "")
    {
    string filename;

        replace_str(url, "__##VALUE##__", url_encode(value));
    }
    else
    {
        replace_str(data, "__##VALUE##__", value);
    }

    FileDownloader *fdownloader = new FileDownloader(url, data, data_type, true);
    fdownloader->Start();

    cDebug() << "Set value with param : " 
             << url << " | "
             << param.get_param("request_type") << " | "
             << data << " | "
             << data_type;

}

void WebCtrl::setValue(int r, int g, int b)
{
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%02x%02x%02x", r, g, b);

    setValue(tmp);
}
