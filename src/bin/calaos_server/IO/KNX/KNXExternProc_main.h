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
#include <eibnetmux/enmx_lib.h>
}

/*
 * EIB request frame
 */
typedef struct __attribute__((packed))
{
        uint8_t  code;
        uint8_t  zero;
        uint8_t  ctrl;
        uint8_t  ntwrk;
        uint16_t saddr;
        uint16_t daddr;
        uint8_t  length;
        uint8_t  tpci;
        uint8_t  apci;
        uint8_t  data[16];
} CEMIFRAME;

/*
 * EIB constants
 */
#define EIB_CTRL_LENGTHTABLE                    0x00
#define EIB_CTRL_LENGTHBYTE                     0x80
#define EIB_CTRL_DATA                           0x00
#define EIB_CTRL_POLL                           0x40
#define EIB_CTRL_REPEAT                         0x00
#define EIB_CTRL_NOREPEAT                       0x20
#define EIB_CTRL_ACK                            0x00
#define EIB_CTRL_NONACK                         0x10
#define EIB_CTRL_PRIO_LOW                       0x0c
#define EIB_CTRL_PRIO_HIGH                      0x04
#define EIB_CTRL_PRIO_ALARM                     0x08
#define EIB_CTRL_PRIO_SYSTEM                    0x00
#define EIB_NETWORK_HOPCOUNT                    0x70
#define EIB_DAF_GROUP                           0x80
#define EIB_DAF_PHYSICAL                        0x00
#define EIB_LL_NETWORK                          0x70
#define T_GROUPDATA_REQ                         0x00
#define A_READ_VALUE_REQ                        0x0000
#define A_WRITE_VALUE_REQ                       0x0080
#define A_RESPONSE_VALUE_REQ                    0x0040

/**
 * cEMI Message Codes
 **/
#define L_BUSMON_IND            0x2B
#define L_RAW_IND               0x2D
#define L_RAW_REQ               0x10
#define L_RAW_CON               0x2F
#define L_DATA_REQ              0x11
#define L_DATA_CON              0x2E
#define L_DATA_IND              0x29
#define L_POLL_DATA_REQ         0x13
#define L_POLL_DATA_CON         0x25
#define M_PROP_READ_REQ         0xFC
#define M_PROP_READ_CON         0xFB
#define M_PROP_WRITE_REQ        0xF6
#define M_PROP_WRITE_CON        0xF5
#define M_PROP_INFO_IND         0xF7
#define M_RESET_REQ             0xF1
#define M_RESET_IND             0xF0

class KNXValue
{
public:
    KNXValue() {}

    int eis = -1;
    int type = enmx_KNXerror;

    int value_int = 0;
    float value_float = 0.0;
    unsigned char value_char = 0;
    string value_string;

    string toString();
    bool setValue(int eis, void *data, int datalen, bool cemiframe = false);

    json_t *toJson() const;
    static KNXValue fromJson(json_t *jval);
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

    virtual bool handleFdSet(int fd);

    EXTERN_PROC_CLIENT_CTOR(KNXProcess)

protected:

    //needs to be reimplemented
    virtual void readTimeout();
    virtual void messageReceived(const string &msg);

    void connectEibNetMux();
    void writeKnxValue(const string &group_addr, const KNXValue &value);

    string knxPhysicalAddr(uint16_t addr);
    string knxGroupAddr(uint16_t addr);

    string eibserver;
    ENMX_HANDLE eibsock = -1;

    bool isConnected() { return eibsock < 0; }
};

#endif // KNXEXTERNPROC_MAIN_H

