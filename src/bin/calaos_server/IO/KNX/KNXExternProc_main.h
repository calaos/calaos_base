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
#ifndef KNXEXTERNPROC_MAIN_H
#define KNXEXTERNPROC_MAIN_H

#include "ExternProc.h"
#include "Jansson_Addition.h"

extern "C" {
#include <eibclient.h>
}

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

    int eis = -1;
    int type = KNXError;

    int64_t value_int = 0;
    float value_float = 0.0;
    unsigned char value_char = 0;
    string value_string;

    string toString();
    bool setValue(int eis, vector<uint8_t> data);
    bool toKnxData(vector<uint8_t> &data) const;

    json_t *toJson() const;
    static KNXValue fromJson(json_t *jval);
    static KNXValue fromString(int eis, const string &s);
};

class KnxdObj
{
public:
    KnxdObj() {}
    ~KnxdObj() { if (sock != 0) EIBClose(sock); }

    bool open(const string &server);

    EIBConnection * sock = nullptr;
};

class KNXProcess: public ExternProcClient
{
public:

    //Those are called from command line by user
    void doRead(int argc, char **argv);
    void doWrite(int argc, char **argv);
    void doMonitorBus(int argc, char **argv);

    //needs to be reimplemented
    virtual bool setup(int &argc, char **&argv);
    virtual int procMain();

    EXTERN_PROC_CLIENT_CTOR(KNXProcess)

protected:

    bool monitorWait();

    //needs to be reimplemented
    virtual void readTimeout();
    virtual void messageReceived(const string &msg);

    void connectKnxd();
    void writeKnxValue(const string &group_addr, const KNXValue &value);
    void sendReadKnxCommand(const string &group_addr);

    string knxPhysicalAddr(eibaddr_t addr);
    string knxGroupAddr(eibaddr_t addr);
    eibaddr_t eKnxGroupAddr(const string &group_addr);
    eibaddr_t eKnxPhysicalAddr(const string &addr);

    string eibserver;
    EIBConnection *eibsock = nullptr;

    bool monitorMode = false;

    bool isConnected() { return eibsock; }
};

#endif // KNXEXTERNPROC_MAIN_H

