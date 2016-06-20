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

#include "WagoCtrl.h"

namespace Calaos {
class WagoProcess: public ExternProcClient
{
public:

    //needs to be reimplemented
    virtual bool setup(int &argc, char **&argv);
    virtual int procMain();

    EXTERN_PROC_CLIENT_CTOR(WagoProcess)

protected:

    WagoCtrl *wago = nullptr;
    std::string wago_host;
    int wago_port = 502;

    //needs to be reimplemented
    virtual void readTimeout();
    virtual void messageReceived(const std::string &msg);
};

void WagoProcess::readTimeout()
{
    if (!wago->is_connected())
    {
        cInfo() << "Connecting to " << wago_host;
        wago->Connect();
    }
}

void WagoProcess::messageReceived(const std::string &msg)
{
    json_error_t jerr;
    json_t *jroot = json_loads(msg.c_str(), 0, &jerr);

    if (!jroot || !json_is_object(jroot))
    {
        cWarningDom("wago") << "Error parsing json from sub process: " << jerr.text;
        if (jroot)
            json_decref(jroot);
        return;
    }

    std::string res;

    Params jsonData;
    jansson_decode_object(jroot, jsonData);

    if (jsonData["action"] == "read_bits" ||
        jsonData["action"] == "read_output_bits")
    {
        Utils::UWord address;
        int count;
        std::vector<bool> values_bits;
        bool status = true;

        Utils::from_string(jsonData["address"], address);
        Utils::from_string(jsonData["count"], count);

        Utils::UWord offset = 0;
        if (jsonData["action"] == "read_output_bits")
            offset = 0x200;

        cInfo() << "Reading address " << (address + offset) << " (PLC: " << wago_host << ")";

        if (!wago->read_bits(address + offset, count, values_bits))
        {
            cWarning() << "Wago MBUS, Reconnecting to host " << wago_host;
            wago->Connect();
            if (!wago->read_bits(address, count, values_bits))
            {
                cError() << "Wago MBUS, failed to send request";
                status = false;
            }
        }

        json_t *jret = json_object();
        json_object_set_new(jret, "id", json_string(jsonData["id"].c_str()));
        json_object_set_new(jret, "action", json_string(jsonData["action"].c_str()));
        json_object_set_new(jret, "address", json_string(jsonData["address"].c_str()));
        json_object_set_new(jret, "count", json_string(jsonData["count"].c_str()));
        json_object_set_new(jret, "status", json_string(status?"true":"false"));
        json_t *jarr = json_array();
        for (uint i = 0;i < values_bits.size();i++)
            json_array_append_new(jarr, json_string(values_bits[i]?"true":"false"));
        json_object_set_new(jret, "values", jarr);

        res = jansson_to_string(jret);
    }
    else if (jsonData["action"] == "write_bit")
    {
        Utils::UWord address;
        bool value = jsonData["value"] == "true";
        bool status = true;

        Utils::from_string(jsonData["address"], address);

        cInfo() << "Writing " << value << " to address " << address << " (PLC: " << wago_host << ")";

        if (!wago->write_single_bit(address, value))
        {
            cWarning() << "Wago MBUS, Reconnecting to host " << wago_host;
            wago->Connect();
            if (!wago->write_single_bit(address, value))
            {
                cError() << "Wago MBUS, failed to send request";
                status = false;
            }
        }

        json_t *jret = json_object();
        json_object_set_new(jret, "id", json_string(jsonData["id"].c_str()));
        json_object_set_new(jret, "status", json_string(status?"true":"false"));
        res = jansson_to_string(jret);
    }
    else if (jsonData["action"] == "write_bits")
    {
        Utils::UWord address;
        int count;
        std::vector<bool> values_bits;
        bool status = true;

        Utils::from_string(jsonData["address"], address);
        Utils::from_string(jsonData["count"], count);

        uint idx;
        json_t *value;

        cInfo() << "Writing multiple values to address " << address << " (PLC: " << wago_host << ")";

        json_array_foreach(json_object_get(jroot, "values"), idx, value)
        {
            std::string v = json_string_value(value);
            values_bits.push_back(v == "true");
        }

        if (!wago->write_multiple_bits(address, count, values_bits))
        {
            cWarning() << "Wago MBUS, Reconnecting to host " << wago_host;
            wago->Connect();
            if (!wago->write_multiple_bits(address, count, values_bits))
            {
                cError() << "Wago MBUS, failed to send request";
                status = false;
            }
        }

        json_t *jret = json_object();
        json_object_set_new(jret, "id", json_string(jsonData["id"].c_str()));
        json_object_set_new(jret, "status", json_string(status?"true":"false"));
        res = jansson_to_string(jret);
    }
    else if (jsonData["action"] == "read_words" ||
             jsonData["action"] == "read_output_words")
    {
        Utils::UWord address;
        int count;
        std::vector<Utils::UWord> values_words;
        bool status = true;

        Utils::from_string(jsonData["address"], address);
        Utils::from_string(jsonData["count"], count);

        Utils::UWord offset = 0;
        if (jsonData["action"] == "read_output_words")
            offset = 0x200;

        cInfo() << "Reading address " << (address + offset) << " (PLC: " << wago_host << ")";

        if (!wago->read_words(address + offset, count, values_words))
        {
            cWarning() << "Wago MBUS, Reconnecting to host " << wago_host;
            wago->Connect();
            if (!wago->read_words(address, count, values_words))
            {
                cError() << "Wago MBUS, failed to send request";
                status = false;
            }
        }

        json_t *jret = json_object();
        json_object_set_new(jret, "id", json_string(jsonData["id"].c_str()));
        json_object_set_new(jret, "action", json_string(jsonData["action"].c_str()));
        json_object_set_new(jret, "address", json_string(jsonData["address"].c_str()));
        json_object_set_new(jret, "count", json_string(jsonData["count"].c_str()));
        json_object_set_new(jret, "status", json_string(status?"true":"false"));
        json_t *jarr = json_array();
        for (uint i = 0;i < values_words.size();i++)
            json_array_append_new(jarr, json_string(Utils::to_string(values_words[i]).c_str()));
        json_object_set_new(jret, "values", jarr);

        res = jansson_to_string(jret);
    }
    else if (jsonData["action"] == "write_word")
    {
        Utils::UWord address;
        Utils::UWord value;
        bool status = true;

        Utils::from_string(jsonData["address"], address);
        Utils::from_string(jsonData["value"], value);

        cInfo() << "Writing " << value << " to address " << address << " (PLC: " << wago_host << ")";

        if (!wago->write_single_word(address, value))
        {
            cWarning() << "Wago MBUS, Reconnecting to host " << wago_host;
            wago->Connect();
            if (!wago->write_single_word(address, value))
            {
                cError() << "Wago MBUS, failed to send request";
                status = false;
            }
        }

        json_t *jret = json_object();
        json_object_set_new(jret, "id", json_string(jsonData["id"].c_str()));
        json_object_set_new(jret, "status", json_string(status?"true":"false"));
        res = jansson_to_string(jret);
    }
    else if (jsonData["action"] == "write_words")
    {
        Utils::UWord address;
        int count;
        std::vector<Utils::UWord> values_words;
        bool status = true;

        Utils::from_string(jsonData["address"], address);
        Utils::from_string(jsonData["count"], count);

        uint idx;
        json_t *value;

        cInfo() << "Writing multiple values to address " << address << " (PLC: " << wago_host << ")";

        json_array_foreach(json_object_get(jroot, "values"), idx, value)
        {
            std::string v = json_string_value(value);
            Utils::UWord vv;
            Utils::from_string(v, vv);
            values_words.push_back(vv);
        }

        if (!wago->write_multiple_words(address, count, values_words))
        {
            cWarning() << "Wago MBUS, Reconnecting to host " << wago_host;
            wago->Connect();
            if (!wago->write_multiple_words(address, count, values_words))
            {
                cError() << "Wago MBUS, failed to send request";
                status = false;
            }
        }

        json_t *jret = json_object();
        json_object_set_new(jret, "id", json_string(jsonData["id"].c_str()));
        json_object_set_new(jret, "status", json_string(status?"true":"false"));
        res = jansson_to_string(jret);
    }

    json_decref(jroot);

    if (!res.empty())
        sendMessage(res);
}

bool WagoProcess::setup(int &argc, char **&argv)
{
    if (!connectSocket())
    {
        cError() << "process cannot connect to calaos_server";
        return false;
    }

    if (argc >= 1)
        wago_host = argv[1];

    if (argc >= 2)
        Utils::from_string(argv[2], wago_port);

    cDebug() << "Wago host: " << wago_host << ":" << wago_port;

    wago = new WagoCtrl(wago_host, wago_port);

    return true;
}

int WagoProcess::procMain()
{
    run(1000);

    return 0;
}


}

EXTERN_PROC_CLIENT_MAIN(Calaos::WagoProcess)
