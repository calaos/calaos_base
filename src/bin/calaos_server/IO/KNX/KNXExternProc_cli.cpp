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

class KnxdObj
{
public:
    KnxdObj() {}
    ~KnxdObj() { if (sock != 0) EIBClose(sock); }

    bool open(const string &server);

    EIBConnection * sock = nullptr;
};

bool KnxdObj::open(const string &server)
{
    if (!server.empty())
        sock = EIBSocketURL(server.c_str());
    else
        sock = EIBSocketURL("ip:localhost"); //by default try localhost

    if (!sock)
    {
        cError() << "Connect to knxd failed.";
        return false;
    }

    return true;
}

void KNXProcess::doRead(int argc, char **argv)
{
    std::unique_ptr<KnxdObj> eibobj (new KnxdObj());

    if (!eibobj->open(eibserver))
        return;

    cout << "Connected to knxd server" << endl;

    string sgroup_addr;
    uint16_t eis = 0;
    eibaddr_t knx_addr = 0;

    //Get parameters
    int cpt = 0;
    for (int i = 1;i < argc;i++)
    {
        string p = argv[i];
        if (Utils::strStartsWith(p, "--")) continue;
        if (Utils::strStartsWith(p, "ip:")) continue;
        if (Utils::strStartsWith(p, "local:")) continue;
        if (cpt == 0)
            sgroup_addr = p;
        else if (cpt == 1)
            Utils::from_string(p, eis);

        cpt++;
    }

    if (Utils::strContains(sgroup_addr, "/"))
        knx_addr = eKnxGroupAddr(sgroup_addr.c_str());
    else
        knx_addr = knx_addr & 0xffff;

    //send a read command
    if (EIBOpenT_Group(eibobj->sock, knx_addr, 1) == -1)
    {
        cError() << "Failed to open group";
        return;
    }

    uint8_t buf[2] = { 0, 0 };
    int len = EIBSendAPDU(eibobj->sock, 2, buf);

    if (len < 0)
    {
        cError() << "Failed to send read command";
        return;
    }

/*    KNXValue v;
    if (v.setValue(eis, data, len))
        cout << "address " << sgroup_addr << "  value: " << v.toString();*/
}

void KNXProcess::doWrite(int argc, char **argv)
{
    std::unique_ptr<KnxdObj> eibobj (new KnxdObj());

    if (!eibobj->open(eibserver) ||
        EIBOpen_GroupSocket (eibobj->sock, 0) == -1)
    {
        cout << "Failed to connect" << endl;
        return;
    }

    cout << "Connected to knxd server"<< endl;

    string sgroup_addr;
    uint16_t eis = 0;
    eibaddr_t knx_addr = 0;
    string value;

    //Get parameters
    int cpt = 0;
    for (int i = 1;i < argc;i++)
    {
        string p = argv[i];
        if (Utils::strStartsWith(p, "--")) continue;
        if (Utils::strStartsWith(p, "ip:")) continue;
        if (Utils::strStartsWith(p, "local:")) continue;
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
        knx_addr = eKnxGroupAddr(sgroup_addr);

    KNXValue kvalue = KNXValue::fromString(eis, value);
    vector<uint8_t> data;

    if (!kvalue.toKnxData(data))
    {
        cError() << "Failed to get KNX formated data";
        return;
    }

    int len = EIBSendGroup(eibobj->sock, knx_addr, data.size(), data.data());
    if (len == -1)
        cError() << "Failed to write data to KNX address " << sgroup_addr;
}

void KNXProcess::doMonitorBus(int argc, char **argv)
{
    std::unique_ptr<KnxdObj> eibobj (new KnxdObj());

    if (!eibobj->open(eibserver) ||
        EIBOpen_GroupSocket (eibobj->sock, 0) == -1)
    {
        cout << "Failed to connect" << endl;
        return;
    }

    cout << "Connected to knxd server" << endl;

    for (;;)
    {
        eibaddr_t dest;
        eibaddr_t src;
        vector<uint8_t> buf(200, 0);

        int len = EIBGetGroup_Src (eibobj->sock, buf.size(), buf.data(), &src, &dest);

        if (len < 0)
        {
            cWarning() << "Error waiting for monitor data, stopping.";
            break;
        }
        else if (len < 2)
        {
            cWarning() << "Packet too short";
            continue;
        }

        buf.resize(len);

        if (buf[0] & 0x3 || (buf[1] & 0xC0) == 0xC0)
        {
            cWarning() << "Unkown APDU from " << knxPhysicalAddr(src) << " to " << knxGroupAddr(dest);
            continue;
        }

        string t;
        bool printValue = true;
        switch (buf[1] & 0xC0)
        {
            case 0x00:
                t = "Read";
                printValue = false;
                break;
            case 0x40:
                t = "Response";
                break;
            case 0x80:
                t = "Write";
                break;
        }

        KNXValue v;
        cout << t << " from " << knxPhysicalAddr(src) << " to " << knxGroupAddr(dest);
        if (printValue && v.setValue(0, buf))
            cout <<  " Value : " << v.toString();
        cout << endl;
    }
}

bool KNXValue::setValue(int _eis, vector<uint8_t> data)
{
    eis = _eis;

    //More info: http://www.openremote.org/display/forums/CEMI+APDU+Field
    //http://www.openremote.org/display/knowledge/KNX+Overview
    //
    // First byte is TCPI+APCI
    // Second byte is ACPI + start of data (if total length is == 2, else data starts at byte 3)
    //remove first TPCI/APCI byte we do not need it for data decoding
    if (data.size() > 1)
        data.erase(data.begin());

    if (eis == 0)
    {
        //try to detect eis based on data length
        switch (data.size())
        {
        case 1: eis = 2; break;
        case 2: eis = 6; break;
        case 3: eis = 5; break;
        case 4: eis = 3; break;
        case 5: eis = 11; break;
        case 15: eis = 15; break;
        break;
        }
    }

    if (eis < 1 || eis > 15 || data.size() < 1)
    {
        cError() << "Unable to convert data for eis " << eis << " data size: " << data.size();
        eis = -1;
        return false;
    }

    int len = data.size();
    data.resize(20); //prevent wrong access later

    switch (eis)
    {
    case 1: //switch 0-1
    case 7:
        value_int = data.at(0) & 0x01;
        value_char = value_float = value_int;
        type = KNXInteger;
        break;
    case 2: //dimming (-)0-7
        value_int = data.at(0) & 0x07;
        value_int *= (data.at(0) & 0x08)?-1:1;
        value_char = value_float = value_int;
        type = KNXInteger;
        break;
    case 8: //priority
        value_int = data.at(0) & 0x03;
        value_char = value_float = value_int;
        type = KNXInteger;
        break;
    case 3: //time
        value_int = (data.at(1) & 0x1F) * 3600 +
                    (data.at(2) & 0x1F) * 60 +
                    (data.at(3) & 0x3F);
        value_char = value_float = value_int;
        type = KNXInteger;
        break;
    case 4: //date
    {
        struct tm dt;
        if (data.at(3) >= 90)
            dt.tm_year = data.at(3) & 0x7F;
        else
            dt.tm_year = (data.at(3) & 0x7F) + 100;
        dt.tm_mon = (data.at(2) & 0x0F) - 1;
        dt.tm_mday = data.at(1) & 0x1F;
        dt.tm_hour = dt.tm_min = dt.tm_sec = 0;
        value_int = mktime(&dt);
        value_char = value_float = value_int;
        type = KNXInteger;
        break;
    }
    case 5: //float -671088,64 - 670760,96
    {
        int sign, expo, mantis = 0;
        sign = ((data.at(1) & 0x80) == 0)?0:1;
        expo = (data.at(1) & 0x78) >> 3;
        mantis = ((data.at(1) & 0x07) << 8) | data.at(2);
        if (sign)
            mantis -= 2048;
        value_float = mantis * 0.01 * pow(2, expo);
        value_char = value_int = value_float;
        type = KNXFloat;
        break;
    }
    case 6:
    case 13:
    case 14: //8bits
        value_int = data.at(1) & 0xFF;
        value_char = value_float = value_int;
        type = KNXInteger;
        break;
    case 9: //IEE float 4-Octet Float Value IEEE 754
        memcpy(&value_float, data.data() + 1, 4);
        value_char = value_int = value_float;
        type = KNXFloat;
        break;
    case 10: //16bits unsigned
        value_int = (data.at(1) << 8) | data.at(2);
        value_char = value_float = value_int;
        type = KNXInteger;
        break;
    case 11: //32bits unsigned
        value_int = (data.at(1) << 24) | (data.at(2) << 16) | (data.at(3) << 8) | data.at(4);
        value_char = value_float = value_int;
        type = KNXInteger;
        break;
    case 12:
    case 15:
    case 16: //string
        for (int i = 1;i < len;i++)
            value_string.push_back(data[i]);
        type = KNXString;
        break;
    default:
        type = KNXError;
        cError() << "Unable to convert data for eis " << eis;
        eis = -1;
        return false;
    }

    return true;
}

bool KNXValue::toKnxData(vector<uint8_t> &data) const
{
    if (eis < 1 || eis > 15)
        return false;

    data.clear();

    switch (eis)
    {
        case 1:
        case 7:
            data.resize(2, 0);
            data[1] = value_int & 0x0F;
            data[1] |= 0x80; //write command
            break;
        case 2:
            data.resize(2, 0);
            data[1] = value_int & 0x0F;
            if (value_int >= 128)
                data[1] |= 0x08;
            data[1] |= 0x80; //write command
        case 3:
        {
            data.resize(5, 0);
            time_t t = value_int;
            struct tm dt;
            localtime_r(&t, &dt);
            data[1] = 0;
            data[2] = dt.tm_hour + (((dt.tm_wday == 0)?7:dt.tm_wday) << 5);
            data[3] = dt.tm_min;
            data[4] = dt.tm_sec;
            data[1] |= 0x80; //write command
            break;
        }
        case 4:
        {
            data.resize(5);
            time_t t = value_int;
            struct tm dt;
            localtime_r(&t, &dt);
            if (dt.tm_year < 90)
            {
                cError() << "KNX do not understand date < 1990";
                return false;
            }
            data[2] = dt.tm_mday;
            data[3] = dt.tm_mon + 1;
            if (dt.tm_year < 100)
                //01/01/1990 - 31/12/1999
                data[4] = dt.tm_year;
            else
                //01/01/2000 - 31/12/2089
                data[5] = dt.tm_year - 10;
            data[1] |= 0x80; //write command
            break;
        }
        case 11:
        {
            data.resize(6, 0);
            uint32_t v, *pv = (uint32_t *)&value_float;
            v = htonl(*pv);
            memcpy(&data[2], &v, 4);
            data[1] |= 0x80; //write command
            break;
        }
        case 9:
            data.resize(6, 0);
            memcpy(&data[2], &value_float, 4);
            data[1] |= 0x80; //write command
            break;
        case 6:
        case 12:
        case 13:
        case 14:
            data.resize(3, 0);
            data[2] = (uint8_t)(value_int & 0xFF);
            data[1] |= 0x80; //write command
            break;
        case 10:
        {
            data.resize(4, 0);
            uint16_t v = htons(value_int);
            memcpy(&data[2], &v, 2);
            data[1] |= 0x80; //write command
            break;
        }
        case 8:
            data.resize(2, 0);
            data[1] = value_int & 0x03;
            data[1] |= 0x80; //write command
            break;
        case 15:
        case 16:
            data.resize(16);
            memcpy(&data[2], value_string.c_str(), 14);
            data[1] |= 0x80; //write command
            break;
        default:
            cError() << "Error converting value of EIS type " << eis;
            return false;
    }

    return true;
}

string KNXValue::toString()
{
    switch (type)
    {
    case KNXError:
        cError() << "No data";
        return string();
    case KNXInteger:
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
    case KNXFloat: return Utils::to_string(value_float);
    case KNXChar: return Utils::to_string((char)value_char);
    case KNXString: return value_string;
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

KNXValue KNXValue::fromString(int eis, const string &s)
{
    KNXValue v;

    switch (eis)
    {
        default:
            Utils::from_string(s, v.value_int);
            v.eis = eis;
            break;
        case 5:
        case 9:
            Utils::from_string(s, v.value_float);
            v.eis = eis;
            break;
        case 12:
        case 13:
        case 15:
        case 16:
            v.value_string = s;
            v.eis = eis;
            break;
    }

    return v;
}
