/******************************************************************************
 **  Copyright (c) 2006-2018, Calaos. All Rights Reserved.
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
#ifndef KNXCTRL_H
#define KNXCTRL_H

#include "Calaos.h"
#include "ExternProc.h"

class KNXValue
{
public:
    KNXValue() {}

    enum
    {
        KNXError,
        KNXInteger,
        KNXFloat,
        KNXChar,
        KNXString,
    };

    //EIS types
    enum
    {
        EIS_Autodetect = 0,
        EIS_Switch_OnOff = 1,
        EIS_Dim_UpDown = 2,
        EIS_Time = 3,
        EIS_Date = 4,
        EIS_Value_Float = 5,
        EIS_Value_Int = 6,
        EIS_DriveControl = 7,
        EIS_Priority = 8,
        EIS_Float4 = 9,
        EIS_Count16 = 10,
        EIS_Count32 = 11,
        EIS_AccessControl = 12,
        EIS_Char = 13,
        EIS_Count8 = 14,
        EIS_String = 15, //14 characters of 7bits max
    };

    void setEis(int e) { eis = e; }

    json_t *toJson() const;
    string toString() const;
    bool toBool() const;
    float toFloat() const;
    int toInt() const;
    char toChar() const;

    static KNXValue fromJson(json_t *jval);
    static KNXValue fromString(const string &val, int eis = 0);
    static KNXValue fromBool(bool val, int eis = 0);
    static KNXValue fromFloat(float val, int eis = 0);
    static KNXValue fromInt(int val, int eis = 0);
    static KNXValue fromChar(char val, int eis = 0);

private:
    int type = KNXError;
    int eis = -1;

    int value_int = 0;
    float value_float = 0.0;
    unsigned char value_char = 0;
    string value_string;
};

class KNXCtrl: public sigc::trackable
{
private:
    KNXCtrl(const string host);

    ExternProcServer *process;
    ExternProcServer *processMonitor;

    unordered_map<string, KNXValue> knxCache;

    void processNewMessage(const string &msg);

public:
    static shared_ptr<KNXCtrl> Instance(const string &host);
    ~KNXCtrl();

    // void valueChanged(const string &group_addr, const KNXValue &value)
    sigc::signal<void, const string &, const KNXValue &> valueChanged;

    KNXValue getValue(const string &group_addr);

    void writeValue(const string &group_addr, const KNXValue &value);
    void readValue(const string &group_addr, int eis = 0);
};

#endif // KNXCTRL_H
