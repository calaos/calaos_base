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
#include "KNXExternProc_main.h"
#include <time.h>

class EibNetMuxObj
{
public:
    EibNetMuxObj();
    ~EibNetMuxObj() { if (sock != 0) enmx_close(sock); }

    bool open(const string &server);

    ENMX_HANDLE sock = 0;
};

EibNetMuxObj::EibNetMuxObj()
{
    int enmx_version;
    if ((enmx_version = enmx_init()) != ENMX_VERSION_API)
    {
        cError() << "Incompatible eibnetmux API version (" << enmx_version << ", expected " << ENMX_VERSION_API << ")";
        throw std::runtime_error("Incompatible eibnetmux API version");
    }
}

bool EibNetMuxObj::open(const string &server)
{
    if (server.empty())
        sock = enmx_open(nullptr, (char *)"calaos");
    else
        sock = enmx_open((char *)server.c_str(), (char *)"calaos");
    if (sock < 0)
    {
        cError() << "Connect to eibnetmux failed: " << enmx_errormessage(sock);
        return false;
    }

    return true;
}

void KNXProcess::doRead(int argc, char **argv)
{
    std::unique_ptr<EibNetMuxObj> eibobj (new EibNetMuxObj());

    if (!eibobj->open(eibserver))
        return;

    cout << "Connected to eibnetmux server on " << enmx_gethost(eibobj->sock) << endl;

    string sgroup_addr;
    uint16_t eis = 0;
    uint16_t knx_addr = 0;

    //Get parameters
    int cpt = 0;
    for (int i = 1;i < argc;i++)
    {
        string p = argv[i];
        if (Utils::strStartsWith(p, "--")) continue;
        if (cpt == 0)
            sgroup_addr = p;
        else if (cpt == 1)
            Utils::from_string(p, eis);

        cpt++;
    }

    if (Utils::strContains(sgroup_addr, "/"))
        knx_addr = enmx_getaddress(sgroup_addr.c_str());
    else
        knx_addr = knx_addr & 0xffff;

    //do the reading
    uint16_t len;
    unsigned char *data = enmx_read(eibobj->sock, knx_addr, &len);

    if (!data)
    {
        cError() << "Failed to read data";
        return;
    }

    KNXValue v;
    if (v.setValue(eis, data, len))
        cout << "address " << sgroup_addr << "  value: " << v.toString();
}

void KNXProcess::doWrite(int argc, char **argv)
{
    std::unique_ptr<EibNetMuxObj> eibobj (new EibNetMuxObj());

    if (!eibobj->open(eibserver))
        return;

    cout << "Connected to eibnetmux server on " << enmx_gethost(eibobj->sock) << endl;

    string sgroup_addr;
    uint16_t eis = 0;
    uint16_t knx_addr = 0;
    string value;

    //Get parameters
    int cpt = 0;
    for (int i = 1;i < argc;i++)
    {
        string p = argv[i];
        if (Utils::strStartsWith(p, "--")) continue;
        if (cpt == 0)
            sgroup_addr = p;
        else if (cpt == 1)
            Utils::from_string(p, eis);
        else if (cpt == 2)
            value = p;

        cpt++;
    }

    if (eis < 1 || eis > 15)
    {
        cError() << "Wrong EIS: " << eis;
        return;
    }

    if (Utils::strContains(sgroup_addr, "/"))
        knx_addr = enmx_getaddress(sgroup_addr.c_str());

    unsigned char *p_val = nullptr;
    int val_int;
    uint32_t val_int32;
    float val_float;
    unsigned char val_char;

    switch (eis)
    {
    default: Utils::from_string(value, val_int); p_val = (unsigned char *)&val_int; break;
    case 11: Utils::from_string(value, val_int32); p_val = (unsigned char *)&val_int32; break;
    case 5:
    case 9: Utils::from_string(value, val_float); p_val = (unsigned char *)&val_float; break;
    case 13: Utils::from_string(value, val_char); p_val = (unsigned char *)&val_char; break;
    case 15: p_val = (unsigned char *)value.c_str(); break;
    case 12: break;
    }

    unsigned char *data = (unsigned char *)malloc(enmx_EISsizeKNX[eis]);
    if (enmx_value2eis(eis, (void *)p_val, data) != 0)
    {
        cError() << "Error in value conversion";
        free(data);
        return;
    }

    int len = (eis != 15)? enmx_EISsizeKNX[eis] : value.length();
    if (enmx_write(eibobj->sock, knx_addr, len, data) != 0)
    {
        cError() << "Unable to send command: " << enmx_errormessage(eibobj->sock);
        free(data);
        return;
    }

    free(data);
}

void KNXProcess::doMonitorBus(int argc, char **argv)
{
    std::unique_ptr<EibNetMuxObj> eibobj (new EibNetMuxObj());

    if (!eibobj->open(eibserver))
        return;

    cout << "Connected to eibnetmux server on " << enmx_gethost(eibobj->sock) << endl;

    for (;;)
    {
        unsigned char *data = (unsigned char *)malloc(10);
        uint16_t datalen = 10;
        uint16_t value_size;
        data = enmx_monitor(eibobj->sock, 0xffff, data, &datalen, &value_size);

        if (!data)
        {
            cError() << "Monitor error, exit.";
            return;
        }

        CEMIFRAME *cemiframe = (CEMIFRAME *)data;

        cout << "Received: " << knxPhysicalAddr(cemiframe->saddr);
        cout << "  " << ((cemiframe->ntwrk & EIB_DAF_GROUP)? knxGroupAddr(cemiframe->daddr):knxPhysicalAddr(cemiframe->daddr));

        if (cemiframe->apci & (A_WRITE_VALUE_REQ | A_RESPONSE_VALUE_REQ))
        {
            KNXValue v;
            if (v.setValue(0, cemiframe, cemiframe->length, true))
                cout << " : " << v.toString();
        }

        cout << endl;
    }
}

bool KNXValue::setValue(int _eis, void *data, int datalen, bool cemiframe)
{
    eis = _eis;

    if (eis == 0)
    {
        //try to detect eis based on data length
        switch (datalen)
        {
        case 1: eis = 2; break;
        case 2: eis = 6; break;
        case 3: eis = 5; break;
        case 4: eis = 3; break;
        case 5: eis = 11; break;
        break;
        }
    }

    unsigned char value[20];
    uint32_t *p_int = (uint32_t *)value;
    double *p_real = (double *)value;

    if (cemiframe)
        type = enmx_frame2value(eis, data, value);
    else
        type = enmx_eis2value(eis, (unsigned char *)data, datalen, value);
    switch (type)
    {
    default:
    case enmx_KNXerror:
        cError() << "Unable to convert data for eis " << eis;
        eis = -1;
        return false;
    case enmx_KNXinteger: value_int = *p_int; break;
    case enmx_KNXfloat: value_float = *p_real; break;
    case enmx_KNXchar: value_char = *value; break;
    case enmx_KNXstring: value_string = string((const char *)value, datalen); break;
    }

    return true;
}

string KNXValue::toString()
{
    switch (type)
    {
    case enmx_KNXerror:
        cError() << "No data";
        return string();
    case enmx_KNXinteger:
    {
        switch (eis)
        {
        case 1: return value_int == 0?"false":"true";
        case 3:
        {
            struct tm *gtime = gmtime((time_t *)&value_int);
            return Utils::time2string_digit(gtime->tm_hour * 3600 + gtime->tm_min * 60 + gtime->tm_sec);
        }
        case 4:
        {
            struct tm *ltime = localtime( (time_t *)&value_int);
            stringstream s;
            s << ltime->tm_year + 1900 << "/" << ltime->tm_mon + 1 << "/" << ltime->tm_mday;
            return s.str();
        }
        default: return Utils::to_string(value_int);
            break;
        }
        break;
    }
    case enmx_KNXfloat: return Utils::to_string(value_float);
    case enmx_KNXchar: return Utils::to_string((char)value_char);
    case enmx_KNXstring: return value_string;
    }

    return string();
}

json_t *KNXValue::toJson() const
{
    Params p = {{"type", Utils::to_string(type) },
                {"eis", Utils::to_string(eis)},
                {"value_int", Utils::to_string(value_int)},
                {"value_float", Utils::to_string(value_float)},
                {"value_char", Utils::to_string(value_char)},
                {"value_string", value_string}};
    return p.toJson();
}

KNXValue KNXValue::fromJson(json_t *jval)
{
    Params p;
    jansson_decode_object(jval, p);

    KNXValue v;
    Utils::from_string(p["type"], v.type);
    Utils::from_string(p["eis"], v.eis);
    Utils::from_string(p["value_int"], v.value_int);
    Utils::from_string(p["value_float"], v.value_float);
    Utils::from_string(p["value_char"], v.value_char);
    v.value_string = p["value_string"];

    return v;
}
