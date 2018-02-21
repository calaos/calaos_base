/******************************************************************************
**  Copyright (c) 2006-2018, Calaos. All Rights Reserved.
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
#include <Zibase.h>
#include <tcpsocket.h>
#include "libuvw.h"

/********************************************************/
/*                      MACROS                          */
/********************************************************/
#define BIG_ENDIAN_W(x) (unsigned short)((x<<8)|(x>>8))
#define BIG_ENDIAN_L(x) (unsigned long)(((x&0x000000FF)<<24)|((x&0x0000FF00)<<8) | ((x&0x00FF0000)>>8) | ((x&0xFF000000)>>24))

/********************************************************/
/*                      DEFINE                          */
/********************************************************/
#define RFFRAME_RECEIVING_CMD 3
#define NOP_CMD    8
#define MULTI_CMD 11
#define REG_CMD   13
#define ACK_CMD   14
#define UNREG_CMD 22

/* Param definition */
#define RFFRAME_SENDING  0
#define ACTION_OFF 0
#define ACTION_ON 1
#define ACTION_DIMBRIGHT 2
#define ACTION_ALL_LIGHT_ON 4
#define ACTION_ALL_LIGHT_OFF 5
#define ACTION_ALL_OFF 6
#define ACTION_ASSOC 7
#define ACTION_UNASSOC 8

#define PROTOCOL(n) (n<<8)



#define RW_VARIABLE 5
#define PARAM3_READVAR   0
#define PARAM3_WRITEVAR  1
#define PARAM3_READCAL   2
#define PARAM3_WRITECAL  3
#define PARAM3_READZWAVE 4


#define ZWAVE_VAR_MIN 256
/********************************************************/
/*                      CONST                           */
/********************************************************/
const unsigned char ZSIG[4]= {'Z','S','I','G'};


ZibaseManager Zibase::zibasemaps;

Zibase::Zibase(std::string h, int p):
    host(h),
    port(p)
{
    //Create listening udp server on local port to receive frame from zibase
    listenHandle = uvw::Loop::getDefault()->resource<uvw::UDPHandle>();

    listenHandle->on<uvw::UDPDataEvent>([this](const uvw::UDPDataEvent &ev, auto &)
    {
        this->udpListenData(ev.data.get(), ev.length, ev.sender.ip, ev.sender.port);
    });

    listenHandle->once<uvw::ErrorEvent>([this](const uvw::ErrorEvent &ev, uvw::UDPHandle &hl)
    {
        cErrorDom("network") << "UDP server error: " << ev.what();
        hl.stop();
    });

    listenHandle->bind("0.0.0.0", port, uvw::UDPHandle::Bind::REUSEADDR);
    listenHandle->recv();

    //Create udp socket to send data (discover zibase, registering, etc...)
    //Create listening udp server on local port to receive frame from zibase
    clientHandle = uvw::Loop::getDefault()->resource<uvw::UDPHandle>();

    clientHandle->on<uvw::UDPDataEvent>([this](const uvw::UDPDataEvent &ev, auto &)
    {
        this->udpClientData(ev.data.get(), ev.length, ev.sender.ip, ev.sender.port);
    });

    clientHandle->once<uvw::ErrorEvent>([this](const uvw::ErrorEvent &ev, uvw::UDPHandle &hl)
    {
        cErrorDom("network") << "UDP server error: " << ev.what();
        hl.stop();
    });

    clientHandle->bind(host, ZIBASE_UDP_PORT, uvw::UDPHandle::Bind::REUSEADDR);
    clientHandle->recv();

    /* send NOP frame to check Zibase here */
    memcpy(stZAPI_packet.header,ZSIG,4);
    memset(	stZAPI_packet.reserved1,0x00,16);
    memset(	stZAPI_packet.reserved2,0x00,12);
    memset(	stZAPI_packet.zibase_id,0x00,16);
    stZAPI_packet.param1 = 0;
    stZAPI_packet.param2 = 0;
    stZAPI_packet.param3 = 0;
    stZAPI_packet.param4 = 0;
    stZAPI_packet.my_count = 0;
    stZAPI_packet.your_count = 0;
    /* Send nop command to see if zibase  present*/

    stZAPI_packet.command = BIG_ENDIAN_W(NOP_CMD);

    ZibaseQueuRequest req;
    zibase_timer = NULL;
    req.RunningReq = ZibaseQueuRequest::eNOP;
    req.ID="NOID";
    /* do not copy data as we are on the first frame sent */
    if (zibase_queue_req.empty())
    {
        /*push request as we wait for an answer */
        zibase_queue_req.push(req);
        clientHandle->send(host, ZIBASE_UDP_PORT, (char*)&stZAPI_packet,sizeof(TstZAPI_packet));
    }
}

Zibase::~Zibase()
{
    listenHandle->stop();
    listenHandle->close();
    clientHandle->stop();
    clientHandle->close();
    StopTimer();
}

Zibase &Zibase::Instance(std::string h, int p)
{

    for (uint i = 0;i < zibasemaps.maps.size();i++)
    {
        if (zibasemaps.maps[i]->get_host() == h &&
            zibasemaps.maps[i]->get_port() == p)
        {
            return *zibasemaps.maps[i];
        }
    }

    // Create a new zibase object
    Zibase *zbase = new Zibase(h, p);
    zibasemaps.maps.push_back(zbase);

    return *zibasemaps.maps[zibasemaps.maps.size() - 1];
}

void Zibase::stopAllZibase()
{
    std::for_each(zibasemaps.maps.begin(), zibasemaps.maps.end(), Delete());
    zibasemaps.maps.clear();
}

void Zibase::udpClientData(const char *data, std::size_t length, string remoteIp, int remotePort)
{
    VAR_UNUSED(length)
    VAR_UNUSED(remoteIp)
    VAR_UNUSED(remotePort)
    char* c;

    ZibaseInfoSensor *InfoSensor = new(ZibaseInfoSensor);
    TstZAPI_Rxpacket* packet = (TstZAPI_Rxpacket*)data;
    unsigned char *p = packet->frame;

    if(InfoSensor)
    {
        /* check this is a zibase RF_FRAME_RECEIVING frame */
        if((strstr((char*)p, "Received radio ID") != NULL) && (BIG_ENDIAN_W(packet->packet.command==RFFRAME_RECEIVING_CMD)))
        {
            /* check ID */
            p = packet->frame;
            c = strstr ((char*)p, "<id>");
            sscanf(c,"<id>%[^<]",InfoSensor->id);
            extract_infos((char*)p,InfoSensor);
            /* suppress _OFF string if present*/
            if(strstr (c, "_OFF")!=NULL)
            {
                sscanf(c,"<id>%[^_]",InfoSensor->id);
            }
            InfoSensor->Error = false;
            sig_newframe.emit(InfoSensor);
        }
        p = (unsigned char*)data;
        if(strstr ((char*)p, "ZSIG") != NULL)
        {
            p = packet->frame;
            if(strstr ((char*)p, "ERR_") != NULL)
            {
                /* Check fifo is no empty (to avoid treating error frame not associated to a request ->
                 *Zibase ERR device unreachable frame for example)*/
                if(!zibase_queue_req.empty())
                {
                    /* Send Error signal */
                    ZibaseQueuRequest req = zibase_queue_req.front();

                    /* Convert variable in string ID*/
                    memset(InfoSensor->id,0,sizeof(InfoSensor->id));
                    req.ID.copy(InfoSensor->id,req.ID.size(),0);

                    InfoSensor->Error = true;
                    sig_newframe.emit(InfoSensor);

                    PopAndCheckFifo();
                    //Utils::logger("zibase") << Priority::INFO << "Zibase::An error from zibase has been received" <<log4cpp::eol;
                }
            }
        }
        delete(InfoSensor);
    }
}

void Zibase::udpListenData(const char *data, std::size_t length, string remoteIp, int remotePort)
{
    VAR_UNUSED(length)
    VAR_UNUSED(remotePort)
    TstZAPI_Rxpacket* packet = (TstZAPI_Rxpacket*)data;
    ZibaseInfoSensor *InfoSensor = new(ZibaseInfoSensor);
    bool checkFifoEmpty = true;

    if(InfoSensor)
    {
        /* Check ACK Frame */
        if((strstr ((char*)data, "ZSIG") != NULL) && (BIG_ENDIAN_W(packet->packet.command==ACK_CMD)))
        {
            /* check element in fifo*/
            if(!zibase_queue_req.empty())
            {
                ZibaseQueuRequest req = zibase_queue_req.front();
                /* check request type*/
                if(req.RunningReq == ZibaseQueuRequest::eNOP)
                {
                    std::string myip = TCPSocket::GetLocalIPFor(remoteIp);


                    vector<string> splitter;
                    Utils::split(myip, splitter, ".", 4);
                    int ipHexa = (atoi(splitter[0].c_str())<<24) |
                            (atoi(splitter[1].c_str())<<16) |
                            (atoi(splitter[2].c_str())<<8) |
                            (atoi(splitter[3].c_str()));


                    /*Zibase present,  Send register frame */
                    my_count++;
                    stZAPI_packet.my_count=BIG_ENDIAN_W(my_count);
                    stZAPI_packet.command = BIG_ENDIAN_W(REG_CMD);
                    stZAPI_packet.param1 = BIG_ENDIAN_L(ipHexa);
                    stZAPI_packet.param2 = BIG_ENDIAN_L(port);

                    clientHandle->send(host, ZIBASE_UDP_PORT, (char*)&stZAPI_packet,sizeof(TstZAPI_packet));
                }
                else if(req.RunningReq == ZibaseQueuRequest::eREAD)
                {

                    /* check param of frame */
                    /* for now, only read zwave ACK frame are treated, otherwise, assume this is a standard ACK command*/
                    if((BIG_ENDIAN_L(packet->packet.param2)) == PARAM3_READZWAVE)
                    {

                        /* Convert variable in string ID*/
                        vartoId(BIG_ENDIAN_L(packet->packet.param3),InfoSensor->id);
                        InfoSensor->DigitalVal = BIG_ENDIAN_L(packet->packet.param1);
                        InfoSensor->Error = false;
                        sig_newframe.emit(InfoSensor);
                    }

                }
                else if(req.RunningReq == ZibaseQueuRequest::eWRITE)
                {

                    /*an ack has been received, now wait if device is reachable or not*/
                    req.ackReceived = true;
                    /* restart timeout */
                    StopTimer();
                    StartTimer(3.0);
                    checkFifoEmpty = false;
                }
            }
            if(checkFifoEmpty == true)
            {
                PopAndCheckFifo();
            }

        }
        delete(InfoSensor);
    }
}

void Zibase::ZibaseCommand_Timeout()
{
    StopTimer();
    if(!zibase_queue_req.empty())
    {
        ZibaseQueuRequest req = zibase_queue_req.front();
        /* check if ack msg have been received for rf frame sending*/
        if((req.RunningReq == ZibaseQueuRequest::eWRITE)&&(req.ackReceived == true))
        {
            ZibaseInfoSensor *InfoSensor = new(ZibaseInfoSensor);
            if(InfoSensor)
            {
                /* Convert variable in string ID*/
                memset(InfoSensor->id,0,sizeof(InfoSensor->id));
                req.ID.copy(InfoSensor->id,req.ID.size(),0);

                InfoSensor->DigitalVal = req.valueWritten;
                InfoSensor->Error = false;
                sig_newframe.emit(InfoSensor);
                delete(InfoSensor);
            }
        }

        zibase_queue_req.pop();
        if(!zibase_queue_req.empty())
        {
            /* pop element in timeout */
            req = zibase_queue_req.front();

            clientHandle->send(host, ZIBASE_UDP_PORT, (char*)&req.frame[0],sizeof(TstZAPI_packet));

            /* arm timer*/
            StartTimer(3.0);
        }
    }
}

int Zibase::rf_frame_sending(bool val,ZibaseInfoProtocol * prot)

{
    int ret = 0;
    unsigned long action = 0;


    if(prot->nb_burst <= 5)
        action |= (prot->nb_burst<<24);

    if (val)
        action |= ACTION_ON;
    else
        action |= ACTION_OFF;

    /*
     * Is this correct? I don't think so...
     *
    switch (val)
    {
    case ZibaseInfoProtocol::eON:
        action |= ACTION_ON;
        break;
    case ZibaseInfoProtocol::eOFF:
        action |= ACTION_OFF;
        break;
    default:
        ret = -1;
        break;
    }
    */

    action |= PROTOCOL(prot->protocol);

    if(ret ==0)
    {   /* Prepare frame sending */
        my_count++;
        stZAPI_packet.my_count=BIG_ENDIAN_W(my_count);
        stZAPI_packet.command = BIG_ENDIAN_W(MULTI_CMD);
        stZAPI_packet.param1 = BIG_ENDIAN_L(RFFRAME_SENDING);
        stZAPI_packet.param2 = BIG_ENDIAN_L(action);
        stZAPI_packet.param3 = BIG_ENDIAN_L((prot->device_number-1));
        stZAPI_packet.param4 = BIG_ENDIAN_L(prot->house_code);

        ZibaseQueuRequest req;
        req.RunningReq = ZibaseQueuRequest::eWRITE;
        req.valueWritten=val;
        req.ID=prot->ID;
        req.ackReceived = false;
        memcpy(req.frame,(unsigned char*)&stZAPI_packet,sizeof(TstZAPI_packet));

        if (zibase_queue_req.empty())
        {
            /*push request as we wait for an answer */
            zibase_queue_req.push(req);
            /* send immediatly data*/
            clientHandle->send(host, ZIBASE_UDP_PORT, (char*)&stZAPI_packet, sizeof(TstZAPI_packet));
            /* restart timer*/
            StopTimer();
            StartTimer(3.0);
        }
        else zibase_queue_req.push(req);
    }
    return ret;
}

int Zibase::rw_variable(ZibaseInfoProtocol * prot)
{
    unsigned long NumVar_offset = 0;
    int ret = 0;

    if(prot->protocol == ZibaseInfoProtocol::eZWAVE)
    {
        NumVar_offset = 256;
        stZAPI_packet.param3 = BIG_ENDIAN_L(PARAM3_READZWAVE);
    }
    else if(prot->protocol == ZibaseInfoProtocol::eCHACON)
    {
        NumVar_offset = 0;
        stZAPI_packet.param3 = BIG_ENDIAN_L(PARAM3_READZWAVE);
    }
    else ret = -1;

    if(ret == 0)
    {
        /* Prepare frame sending */
        my_count++;
        stZAPI_packet.my_count=BIG_ENDIAN_W(my_count);
        stZAPI_packet.command = BIG_ENDIAN_W(MULTI_CMD);
        stZAPI_packet.param1 = BIG_ENDIAN_L(RW_VARIABLE);
        stZAPI_packet.param2 = 0;

        stZAPI_packet.param4 = BIG_ENDIAN_L((NumVar_offset + (prot->house_code<<4) + (prot->device_number-1)));


        ZibaseQueuRequest req;
        req.RunningReq = ZibaseQueuRequest::eREAD;
        req.ID=prot->ID;
        memcpy(req.frame,(unsigned char*)&stZAPI_packet,sizeof(TstZAPI_packet));

        if (zibase_queue_req.empty())
        {
            /*push request as we wait for an answer */
            zibase_queue_req.push(req);
            /* send immediatly data*/
            clientHandle->send(host, ZIBASE_UDP_PORT, (char*)&stZAPI_packet, sizeof(TstZAPI_packet));
            /* restart timer*/
            StopTimer();
            StartTimer(3.0);
        }
        else zibase_queue_req.push(req);
    }
    return ret;
}

/*  Zibase Extract Infos functions */
void Zibase::extract_energy(char* frame,float *val)
{
    char * c;
    char localBuf[16];

    c = strstr (frame, "Power=");
    if(c != NULL)
    {
        sscanf(c,"Power=<w>%[^<]",localBuf);
        *val = atof(localBuf);
    }

}
void Zibase::extract_temp(char* frame,float *val)
{
    char * c;
    char localBuf[16];

    c = strstr (frame, "T=<tem>");
    if(c != NULL)
    {
        sscanf(c,"T=<tem>%[^<]",localBuf);
        *val = atof(localBuf);
    }
}

void Zibase::extract_lux(char* frame,float *val)
{
    char * c;
    char localBuf[16];

    c = strstr (frame, "Level=<uvl>");
    if(c != NULL)
    {
        sscanf(c,"Level=<uvl>%[^<]",localBuf);
        *val = atof(localBuf);
    }
}

void Zibase::extract_totalrain(char* frame,float *val)
{
    char * c;
    char localBuf[16];

    c = strstr (frame, "Rain=<tra>");
    if(c != NULL)
    {
        sscanf(c,"Rain=<tra>%[^<]",localBuf);
        *val = atof(localBuf);
    }
}

void Zibase::extract_wind(char* frame,float *val)
{
    char * c;
    char localBuf[16];

    c = strstr (frame, "Wind=<awi>");
    if(c != NULL)
    {
        sscanf(c,"Wind=<awi>%[^<]",localBuf);
        *val = atof(localBuf);
    }
}

void Zibase::extract_zwave_detectOpen(char* frame,bool *val)
{
    char * c;
    char localBuf[16];

    c = strstr (frame, "CMD/INTER");
    if(c != NULL)
    {
        /* search <id> */
        c = strstr (frame, "<id>");
        if(c != NULL)
        {
            sscanf(c,"<id>%[^<]",localBuf);
            /*search _OFF in id name, to know if door close, val = 1 if door opened */
            if(strstr (localBuf, "_OFF")==NULL)
                *val = 1;
            else *val = 0;
        }
    }
}

void Zibase::extract_Chacon(char* frame,ZibaseInfoSensor* elm)
{
    char * c;

    /* search <sta>ON */
    c = strstr (frame, "<sta>ON");
    if(c != NULL)
    {
        elm->DigitalVal = 1;
        elm->type = ZibaseInfoSensor::eINTER;
    }
    /* search <sta>OFF */
    c = strstr (frame, "<sta>OFF");
    if(c != NULL)
    {
        elm->DigitalVal = 0;
        elm->type = ZibaseInfoSensor::eINTER;
    }
}

void Zibase::extract_Enocean(char* frame,ZibaseInfoSensor* elm)
{
    char * c;

    /* search <sta>ON */
    c = strstr (frame, "ON");
    if(c != NULL)
    {
        elm->DigitalVal = 1;
        elm->type = ZibaseInfoSensor::eINTER;
    }
    /* search <sta>OFF */
    c = strstr (frame, "OFF");
    if(c != NULL)
    {
        elm->DigitalVal = 0;
        elm->type = ZibaseInfoSensor::eINTER;
    }
}

int Zibase::extract_infos(char* frame,ZibaseInfoSensor* elm)
{
    char * c;
printf("Extract infos\n");
    c = strstr (frame, "Power=");
    if(c != NULL)
    {
        extract_energy(frame,&elm->AnalogVal);
        elm->type = ZibaseInfoSensor::eENERGY;
    }

    c = strstr (frame, "T=<tem>");
    if(c != NULL)
    {
        extract_temp(frame,&elm->AnalogVal);
        elm->type = ZibaseInfoSensor::eTEMP;
    }

    c = strstr (frame, "Level=<uvl>");
    if(c != NULL)
    {
        extract_lux(frame,&elm->AnalogVal);
        elm->type = ZibaseInfoSensor::eLUX;
    }

    c = strstr (frame, "Rain=<tra>");
    if(c != NULL)
    {
        extract_totalrain(frame,&elm->AnalogVal);
        elm->type = ZibaseInfoSensor::eTOTALRAIN;
    }

    c = strstr (frame, "Wind=<awi>");
    if(c != NULL)
    {
        extract_wind(frame,&elm->AnalogVal);
        elm->type = ZibaseInfoSensor::eWIND;
    }

    c = strstr (frame, "ZWAVE");
    if(c != NULL)
    {
        extract_zwave_detectOpen(frame,&elm->DigitalVal);
        elm->type = ZibaseInfoSensor::eDETECT;
    }

    c = strstr (frame, "Chacon");
    if(c != NULL)
    {
        extract_Chacon(frame,elm);
    }

    c = strstr (frame, "Decoded by EEP"); //Enocean frame
    if(c != NULL)
    {
        extract_Enocean(frame,elm);
    }

    return 0;
}


int Zibase::vartoId(unsigned long var, char*id)
{
    unsigned char house_code;
    unsigned char device;
    char ID[4];
    unsigned char offset = 0;
    memset(ID,0x00,sizeof(ID));
    house_code = (var&0xF0)>>4;
    device = (var&0x0f);

    if(var>=ZWAVE_VAR_MIN)
    {

        /* ZWAVE variable*/
        ID[0]='Z';
        offset = 1;
    }
    ID[offset]='A'+house_code;
    if(device < 10)
        ID[offset+1]='1'+device;
    else
    {
        ID[offset+1]='1';
        ID[offset+2]='0'+(device-10);
    }
    strcpy(id,ID);

    return 0;
}

int Zibase::StopTimer(void)
{
    if (zibase_timer)
    {
        delete zibase_timer;
        zibase_timer = NULL;
    }
    return (0);
}

int Zibase::StartTimer(float timeout)
{
    zibase_timer = new Timer(timeout, (sigc::slot<void>)sigc::mem_fun(*this, &Zibase::ZibaseCommand_Timeout));
    return (0);
}

int Zibase::PopAndCheckFifo(void)
{
    ZibaseQueuRequest req;
    zibase_queue_req.pop();
    StopTimer();
    if(!zibase_queue_req.empty())
    {
        /* pop element in timeout */
        req = zibase_queue_req.front();
        clientHandle->send(host, ZIBASE_UDP_PORT, (char*)&req.frame[0], sizeof(TstZAPI_packet));
        /* rearm timer*/
        StartTimer(3.0);
    }

    return(0);
}
