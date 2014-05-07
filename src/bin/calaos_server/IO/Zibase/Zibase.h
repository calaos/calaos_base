/******************************************************************************
**  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
**
**  This file is part of Calaos Home.
**
**  Calaos Home is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 3 of the License, or
**  (at your option) any later version.
**
**  Calaos Home is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Foobar; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#ifndef S_ZIBASE_H
#define S_ZIBASE_H

#include <Calaos.h>
#include <EcoreTimer.h>
#include <Ecore_Con.h>

#define ZIBASE_UDP_PORT     49999

class Zibase;
class ZibaseManager
{
public:
    ~ZibaseManager()
    {
        std::for_each(maps.begin(), maps.end(), Delete());
        maps.clear();
    }

    vector<Zibase *> maps;
};

class ZibaseInfoSensor
{
private:

public:
    ZibaseInfoSensor() { }

    enum eZibaseSensor{ eTEMP,eENERGY,eDETECT,eUNKNOWN};
    char id[32];
    eZibaseSensor type;
    float AnalogVal;
    bool DigitalVal;
    bool Error;
};

class ZibaseInfoProtocol
{
private:

public:
    ZibaseInfoProtocol() { }

    enum eZibaseProtocol{eDFLT_PROTOCOL,eVISONIC433,eVISONIC868,eCHACON,eDOMIA,eRFX10,eZWAVE,eRFSTS10,eXDD433alrm,eXDD868alrmn,eXDD868shutter,eXDD868pilot,eXDD868boiler};
    enum eZibaseAction{ eOFF,eON};
    enum eZibaseHouseCode{A,B,C,D,E,F,G,H,I,J,K,L,M,N,O,P};
    eZibaseProtocol protocol;
    eZibaseAction action;
    unsigned char  nb_burst;
    eZibaseHouseCode house_code;
    unsigned char device_number;
    bool value;
    std::string ID;
};


class ZibaseQueuRequest
{
private:

public:
    ZibaseQueuRequest() { }
    enum eZibaseRunningReq{ eNOP,eREAD,eWRITE};
    eZibaseRunningReq RunningReq;
    std::string ID;
    bool valueWritten;
    unsigned char frame[70];
    bool ackReceived;
};

class Zibase
{

protected:
    std::string host;
    int port;

    Zibase(std::string host, int port);

    static ZibaseManager zibasemaps;

    Ecore_Con_Server *econ_client, *econ_listen;
    Ecore_Event_Handler *event_handler_data_cl;
    Ecore_Event_Handler *event_handler_data_listen;

    friend Eina_Bool zibase_udpClientData(void *data, int type, Ecore_Con_Event_Client_Data *ev);
    friend Eina_Bool zibase_udpListenData(void *data, int type, Ecore_Con_Event_Server_Data *ev);

    void udpListenData(Ecore_Con_Event_Server_Data *ev);
    void udpClientData(Ecore_Con_Event_Client_Data *ev);


    void ZibaseCommand_Timeout();

    queue<ZibaseQueuRequest> zibase_queue_req;
    EcoreTimer *zibase_timer;

    int extract_infos(char* frame,ZibaseInfoSensor* elm);
    void extract_temp(char* frame,float *val);
    void extract_energy(char* frame,float *val);
    void extract_zwave_detectOpen(char* frame,bool *val);
    int vartoId(unsigned long var, char*id);
    int StopTimer(void);
    int StartTimer(float timeout);
    int PopAndCheckFifo(void);


#pragma pack(1)
    typedef struct {
        unsigned char header[4];
        unsigned short command;
        unsigned char reserved1[16];
        unsigned char zibase_id[16];
        unsigned char reserved2[12];
        unsigned long param1;
        unsigned long param2;
        unsigned long param3;
        unsigned long param4;
        unsigned short my_count;
        unsigned short your_count;
    }TstZAPI_packet;
#pragma pack()

#pragma pack(1)
    typedef struct {
        TstZAPI_packet packet;
        unsigned char frame[401];
    }TstZAPI_Rxpacket;
#pragma pack()

    unsigned short my_count =0;
    TstZAPI_packet stZAPI_packet;

public:
    ~Zibase();

    //Singleton
    static Zibase &Instance(std::string host, int port);
    static vector<Zibase *> &get_maps() { return zibasemaps.maps; }
    static void stopAllZibase();

    std::string get_host() { return host; }
    int get_port() { return port; }

    //IO classes needs to connect to this signal to receive sensor frames from zibase
    sigc::signal<void, ZibaseInfoSensor *> sig_newframe;

    int rf_frame_sending(bool val,ZibaseInfoProtocol * prot);
    int rw_variable(ZibaseInfoProtocol * prot);
};

#endif
