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

    WagoWire::Request req;
    const WagoWire::Decoded decoded = WagoWire::decodeRequest(jsonData, values, req);

    if (decoded == WagoWire::Decoded::NoSuchAction)
        return;

    /* A field we could not read is not a zero. calaos_server is told the
     * request failed and the PLC is never contacted; answering nothing would
     * leave the pending command of WagoMap hanging forever. */
    if (decoded == WagoWire::Decoded::Refused)
    {
        cError() << "Refusing " << jsonData["action"] << " request " << jsonData["id"]
                 << ": address, count or value is missing or unreadable";

        sendMessage(WagoWire::commandExpectsReadReply(req.command)?
                        WagoWire::buildReadReply(jsonData, false, vector<string>()):
                        WagoWire::buildStatusReply(jsonData, false));
        return;
    }

    string res;
    bool status = true;

    switch (req.command)
    {
    case WagoWire::Request::ReadBits:
    {
        vector<bool> values_bits;

        cDebug() << "Reading address " << req.address << " (PLC: " << wago_host << ")";

        if (!wago->read_bits(WagoTypes::Address(req.address), WagoTypes::Count(req.count), values_bits))
        {
            cWarning() << "Wago MBUS, Reconnecting to host " << wago_host;
            wago->Connect();
            if (!wago->read_bits(WagoTypes::Address(req.address), WagoTypes::Count(req.count), values_bits))
            {
                cError() << "Wago MBUS, failed to send request";
                status = false;
            }
        }

        vector<string> jvalues;
        for (size_t i = 0;i < values_bits.size();i++)
            jvalues.push_back(values_bits[i]?"true":"false");

        res = WagoWire::buildReadReply(jsonData, status, jvalues);
        break;
    }

    case WagoWire::Request::WriteBit:
        cDebug() << "Writing " << req.bitValue << " to address " << req.address
                 << " (PLC: " << wago_host << ")";

        if (!wago->write_single_bit(WagoTypes::Address(req.address), WagoTypes::BitValue(req.bitValue)))
        {
            cWarning() << "Wago MBUS, Reconnecting to host " << wago_host;
            wago->Connect();
            if (!wago->write_single_bit(WagoTypes::Address(req.address), WagoTypes::BitValue(req.bitValue)))
            {
                cError() << "Wago MBUS, failed to send request";
                status = false;
            }
        }

        res = WagoWire::buildStatusReply(jsonData, status);
        break;

    case WagoWire::Request::WriteBits:
        cDebug() << "Writing multiple values to address " << req.address
                 << " (PLC: " << wago_host << ")";

        if (!wago->write_multiple_bits(WagoTypes::Address(req.address), WagoTypes::Count(req.count), req.bits))
        {
            cWarning() << "Wago MBUS, Reconnecting to host " << wago_host;
            wago->Connect();
            if (!wago->write_multiple_bits(WagoTypes::Address(req.address), WagoTypes::Count(req.count), req.bits))
            {
                cError() << "Wago MBUS, failed to send request";
                status = false;
            }
        }

        res = WagoWire::buildStatusReply(jsonData, status);
        break;

    case WagoWire::Request::ReadWords:
    {
        vector<UWord> values_words;

        cDebug() << "Reading address " << req.address << " (PLC: " << wago_host << ")";

        if (!wago->read_words(WagoTypes::Address(req.address), WagoTypes::Count(req.count), values_words))
        {
            cWarning() << "Wago MBUS, Reconnecting to host " << wago_host;
            wago->Connect();
            if (!wago->read_words(WagoTypes::Address(req.address), WagoTypes::Count(req.count), values_words))
            {
                cError() << "Wago MBUS, failed to send request";
                status = false;
            }
        }

        vector<string> jvalues;
        for (size_t i = 0;i < values_words.size();i++)
            jvalues.push_back(Utils::to_string(values_words[i]));

        res = WagoWire::buildReadReply(jsonData, status, jvalues);
        break;
    }

    case WagoWire::Request::WriteWord:
        cDebug() << "Writing " << req.wordValue << " to address " << req.address
                 << " (PLC: " << wago_host << ")";

        /* The pair of F-WAGO-7: two UWord side by side on a WRITE.
         * Permuting them used to compile in silence and preset an arbitrary
         * register of the PLC. The wrapping below is the one place left where
         * a human names which is which. */
        if (!wago->write_single_word(WagoTypes::Address(req.address), WagoTypes::WordValue(req.wordValue)))
        {
            cWarning() << "Wago MBUS, Reconnecting to host " << wago_host;
            wago->Connect();
            if (!wago->write_single_word(WagoTypes::Address(req.address), WagoTypes::WordValue(req.wordValue)))
            {
                cError() << "Wago MBUS, failed to send request";
                status = false;
            }
        }

        res = WagoWire::buildStatusReply(jsonData, status);
        break;

    case WagoWire::Request::WriteWords:
        cDebug() << "Writing multiple values to address " << req.address
                 << " (PLC: " << wago_host << ")";

        if (!wago->write_multiple_words(WagoTypes::Address(req.address), WagoTypes::Count(req.count), req.words))
        {
            cWarning() << "Wago MBUS, Reconnecting to host " << wago_host;
            wago->Connect();
            if (!wago->write_multiple_words(WagoTypes::Address(req.address), WagoTypes::Count(req.count), req.words))
            {
                cError() << "Wago MBUS, failed to send request";
                status = false;
            }
        }

        res = WagoWire::buildStatusReply(jsonData, status);
        break;

    case WagoWire::Request::None:
        break;
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
