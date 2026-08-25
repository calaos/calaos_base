/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
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
#include "WagoWire.h"

class WagoProcess: public ExternProcClient
{
public:

    //needs to be reimplemented
    virtual bool setup(int &argc, char **&argv);
    virtual int procMain();

    EXTERN_PROC_CLIENT_CTOR(WagoProcess)

protected:

    WagoCtrl *wago = nullptr;
    string wago_host;
    int wago_port = 502;

    //needs to be reimplemented
    virtual void readTimeout();
    virtual void messageReceived(const string &msg);
};

void WagoProcess::readTimeout()
{
    if (!wago->is_connected())
    {
        cInfo() << "Connecting to " << wago_host;
        wago->Connect();
    }
}

void WagoProcess::messageReceived(const string &msg)
{
    string res;

    Params jsonData;
    vector<string> values;

    //E4.1h: ONE non-throwing parse for both the flattened Params and the
    //"values" array. The array used to be read straight off the raw parsed
    //root, separately from the flattening.
    if (!WagoWire::decodeMessage(msg, jsonData, &values))
    {
        cWarningDom("wago") << "Error parsing json from calaos_server";
        return;
    }

    if (jsonData["action"] == "read_bits" ||
        jsonData["action"] == "read_output_bits")
    {
        UWord address;
        int count;
        vector<bool> values_bits;
        bool status = true;

        Utils::from_string(jsonData["address"], address);
        Utils::from_string(jsonData["count"], count);

        UWord offset = 0;
        if (jsonData["action"] == "read_output_bits")
            offset = 0x200;

        cDebug() << "Reading address " << (address + offset) << " (PLC: " << wago_host << ")";

        if (!wago->read_bits(address + offset, count, values_bits))
        {
            cWarning() << "Wago MBUS, Reconnecting to host " << wago_host;
            wago->Connect();
            if (!wago->read_bits(address + offset, count, values_bits))
            {
                cError() << "Wago MBUS, failed to send request";
                status = false;
            }
        }

        vector<string> jvalues;
        for (size_t i = 0;i < values_bits.size();i++)
            jvalues.push_back(values_bits[i]?"true":"false");

        res = WagoWire::buildReadReply(jsonData, status, jvalues);
    }
    else if (jsonData["action"] == "write_bit")
    {
        UWord address;
        bool value = jsonData["value"] == "true";
        bool status = true;

        Utils::from_string(jsonData["address"], address);

        cDebug() << "Writing " << value << " to address " << address << " (PLC: " << wago_host << ")";

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

        res = WagoWire::buildStatusReply(jsonData, status);
    }
    else if (jsonData["action"] == "write_bits")
    {
        UWord address;
        int count;
        vector<bool> values_bits;
        bool status = true;

        Utils::from_string(jsonData["address"], address);
        Utils::from_string(jsonData["count"], count);

        cDebug() << "Writing multiple values to address " << address << " (PLC: " << wago_host << ")";

        for (const string &v: values)
            values_bits.push_back(v == "true");

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

        res = WagoWire::buildStatusReply(jsonData, status);
    }
    else if (jsonData["action"] == "read_words" ||
             jsonData["action"] == "read_output_words")
    {
        UWord address;
        int count;
        vector<UWord> values_words;
        bool status = true;

        Utils::from_string(jsonData["address"], address);
        Utils::from_string(jsonData["count"], count);

        UWord offset = 0;
        if (jsonData["action"] == "read_output_words")
            offset = 0x200;

        cDebug() << "Reading address " << (address + offset) << " (PLC: " << wago_host << ")";

        if (!wago->read_words(address + offset, count, values_words))
        {
            cWarning() << "Wago MBUS, Reconnecting to host " << wago_host;
            wago->Connect();
            if (!wago->read_words(address + offset, count, values_words))
            {
                cError() << "Wago MBUS, failed to send request";
                status = false;
            }
        }

        vector<string> jvalues;
        for (size_t i = 0;i < values_words.size();i++)
            jvalues.push_back(Utils::to_string(values_words[i]));

        res = WagoWire::buildReadReply(jsonData, status, jvalues);
    }
    else if (jsonData["action"] == "write_word")
    {
        UWord address;
        UWord value;
        bool status = true;

        Utils::from_string(jsonData["address"], address);
        Utils::from_string(jsonData["value"], value);

        cDebug() << "Writing " << value << " to address " << address << " (PLC: " << wago_host << ")";

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

        res = WagoWire::buildStatusReply(jsonData, status);
    }
    else if (jsonData["action"] == "write_words")
    {
        UWord address;
        int count;
        vector<UWord> values_words;
        bool status = true;

        Utils::from_string(jsonData["address"], address);
        Utils::from_string(jsonData["count"], count);

        cDebug() << "Writing multiple values to address " << address << " (PLC: " << wago_host << ")";

        for (const string &v: values)
        {
            UWord vv;
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

        res = WagoWire::buildStatusReply(jsonData, status);
    }

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

    //argv[N] is only valid when argc > N, otherwise we would dereference
    //the NULL terminator of argv (or read past it)
    if (argc > 1)
        wago_host = argv[1];

    if (argc > 2)
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

EXTERN_PROC_CLIENT_MAIN(WagoProcess)
