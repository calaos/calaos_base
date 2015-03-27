/******************************************************************************
 **  Copyright (c) 2007-2015, Calaos. All Rights Reserved.
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
#include "ExternProc.h"

#ifdef HAVE_OWCAPI_H
#include <owcapi.h>
#endif

shared_ptr<ExternProcClient> calaosClient;

static string getValue(const string &path, const string &param)
{
    string value;
#ifdef HAVE_OWCAPI_H
    string p = path + "/" + param;
    char *res;
    size_t len;

    if (OW_get(p.c_str(), &res, &len) >= 0)
    {
        value = res;
        free(res);
    }
    else
    {
        cError() << "Error reading OW path: " << p << " " << strerror(errno);
    }
#endif
    return value;
}

static list<string> scanDevices()
{
    list<string> listDevices;
#ifdef HAVE_OWCAPI_H
    ssize_t ret;
    size_t len;
    char *dir_buffer = NULL;

    ret = OW_get("/", &dir_buffer, &len);

    if (ret < 0)
    {
        OW_finish();

        return listDevices;
    }

    vector<string> tok;
    Utils::split(string(dir_buffer), tok, ",");
    free(dir_buffer);

    for (const string &s: tok)
    {
        if ((s[0] > '0' && s[0] < '9') ||
            (s[0] > 'A' && s[0] < 'F'))
        {
            if (s[s.length() - 1] == '/') //remove trailing /
                listDevices.push_back(s.substr(0, s.length() - 1));
            else
                listDevices.push_back(s);
        }
    }
#else
    cError() << "owcapi support is not built";
#endif

    return listDevices;
}

int main(int argc, char **argv)
{
    bool scan_devices = false;
    if (argvOptionCheck(argv, argv + argc, "--scan"))
    {
        //this option is for direct use only and only scans all devices
        //and display a list with detected 1wire devices
        scan_devices = true;
        argc--; argv++;
    }
    else
    {
        calaosClient.reset(new ExternProcClient(argc, argv));

        if (!calaosClient->connectSocket())
        {
            cError() << "process cannot connect to calaos_server";
            return 1;
        }
    }

    string owargs;
    for (int i = 1;i < argc;i++)
        owargs += string(argv[i]) + " ";

#ifdef HAVE_OWCAPI_H
    if (OW_init(owargs.c_str()) != 0)
    {
        cError() << "Unable to initialize OW library : " << strerror(errno);
        return 1;
    }
    cInfo() << "OW Library initialization ok";
#endif

    if (scan_devices)
    {
        cout << "Scanning for 1wire devices... ";
        list<string> l = scanDevices();
        cout << "found " << l.size() << " devices." << endl;
        for (const string &dev: l)
        {
            cout << "Device: " << dev
                 << " (" << getValue(dev, "type") << ")"
                 << " --> value: " << getValue(dev, "temperature") << endl;
        }
        return 0;
    }

    calaosClient->messageReceived.connect([=](const string &msg)
    {
        //actually we don't need to do anything here for OW IO.
        //calaos_server will not send us any data
        //for debug print
        cout << "Message received: " << msg << endl;
    });

    auto sendValues = [=]()
    {
        //read all devices and send a json with all data
        list<string> l = scanDevices();
        json_t *jdata = json_array();
        for (const string &dev: l)
        {
            json_t *jdev = json_object();
            json_object_set_new(jdev, "id", json_string(dev.c_str()));
            json_object_set_new(jdev, "value", json_string(getValue(dev, "temperature").c_str()));
            json_object_set_new(jdev, "device_type", json_string(getValue(dev, "type").c_str()));
            json_array_append_new(jdata, jdev);
        }

        char *d = json_dumps(jdata, JSON_COMPACT | JSON_ENSURE_ASCII /*| JSON_ESCAPE_SLASH*/);
        if (!d)
        {
            cError() << "json_dumps failed!";
            json_decref(jdata);
            return;
        }

        json_decref(jdata);
        string res(d);
        free(d);

        calaosClient->sendMessage(res);
    };

    calaosClient->readTimeout.connect(sendValues);
    sendValues(); //first time send
    calaosClient->run();

#ifdef HAVE_OWCAPI_H
    OW_finish();
#endif

    return 0;
}
