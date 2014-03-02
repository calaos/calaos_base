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
#include <Zibase.h>
#include <tcpsocket.h>


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
#define REG_CMD   13
#define ACK_CMD   14
#define UNREG_CMD 22


/********************************************************/
/*                      CONST                           */
/********************************************************/
const unsigned char ZSIG[4]= {'Z','S','I','G'};




ZibaseManager Zibase::zibasemaps;

Eina_Bool zibase_udpClientData(void *data, int type, Ecore_Con_Event_Client_Data *ev)
{
        Zibase *z = reinterpret_cast<Zibase *>(data);

        if (ev && ev->client && (z != ecore_con_server_data_get(ecore_con_client_server_get(ev->client))))
                return ECORE_CALLBACK_PASS_ON;

        if (z) z->udpClientData(ev);

        return ECORE_CALLBACK_RENEW;
}

Eina_Bool zibase_udpListenData(void *data, int type, Ecore_Con_Event_Server_Data *ev)
{
        Zibase *z = reinterpret_cast<Zibase *>(data);

        if (ev && ev->server && (z != ecore_con_server_data_get(ev->server)))
                return ECORE_CALLBACK_PASS_ON;

        if (z) z->udpListenData(ev);

        return ECORE_CALLBACK_RENEW;
}

Zibase::Zibase(std::string h, int p):
                host(h),
                port(p),
                econ_client(nullptr),
                econ_listen(nullptr)
{
        /* allocate Sensor structure for each zibase*/
        InfoSensor = new(ZibaseInfoSensor);

        //Ecore handler
        event_handler_data_cl = ecore_event_handler_add(ECORE_CON_EVENT_CLIENT_DATA, (Ecore_Event_Handler_Cb)zibase_udpClientData, this);
        event_handler_data_listen = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_DATA, (Ecore_Event_Handler_Cb)zibase_udpListenData, this);

        //Create listening udp server on local port to receive frame from zibase
        econ_listen = ecore_con_server_add(ECORE_CON_REMOTE_UDP,
                                           "0.0.0.0", //listen from anyone
                                           port,
                                           this);
        ecore_con_server_data_set(econ_listen, this);

        //Create udp socket to send data (discover zibase, registering, etc...)            
        econ_client = ecore_con_server_connect(ECORE_CON_REMOTE_UDP,
                                               host.c_str(), //zibase host from io.xml
                                               ZIBASE_UDP_PORT,
                                               this);
        ecore_con_server_data_set(econ_client, this);
        
        
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
        ecore_con_server_send(econ_client, (char*)&stZAPI_packet,sizeof(TstZAPI_packet));
        ecore_con_server_flush(econ_client);
        
        
        Utils::logger("zibase") << Priority::INFO << "Zibase(" << host << "," << port << "): Ok" << log4cpp::eol;
}

Zibase::~Zibase()
{
        ecore_event_handler_del(event_handler_data_cl);
        ecore_event_handler_del(event_handler_data_listen);
        ecore_con_server_del(econ_client);
        ecore_con_server_del(econ_listen);
        
        if(InfoSensor)
                delete InfoSensor;

        Utils::logger("zibase") << Priority::INFO << "Zibase::~Zibase(): Ok" << log4cpp::eol;
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

void Zibase::udpClientData(Ecore_Con_Event_Client_Data *ev)
{
	char* c;
	
        TstZAPI_Rxpacket* packet = (TstZAPI_Rxpacket*)ev->data;	
      
	if(InfoSensor)
	{
	        /* check this is a zibase RF_FRAME_RECEIVING frame */
                if((strstr((char*)packet->frame, "Received radio ID") != NULL) && (BIG_ENDIAN_W(packet->packet.command==RFFRAME_RECEIVING_CMD)))
                {
                        /* check ID */		
                        c = strstr ((char*)packet->frame, "<id>");
                        sscanf(c,"<id>%[^<]",InfoSensor->id);		
                        extract_infos((char*)packet->frame,InfoSensor);
                        /* suppress _OFF string if present*/
                        if(strstr (c, "_OFF")!=NULL)
                        {
                                sscanf(c,"<id>%[^_]",InfoSensor->id);
                        }
                        sig_newframe.emit(InfoSensor);	
                }
                
        }	
}

void Zibase::udpListenData(Ecore_Con_Event_Server_Data *ev)
{

        TstZAPI_Rxpacket* packet = (TstZAPI_Rxpacket*)ev->data;
            
        /* Check ACK Frame */
        if((strstr ((char*)ev->data, "ZSIG") != NULL) && (BIG_ENDIAN_W(packet->packet.command==ACK_CMD)))
        {
                std::string remote_ip = ecore_con_server_ip_get(ev->server);
                std::string myip = TCPSocket::GetLocalIPFor(remote_ip);

		
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

                ecore_con_server_send(econ_client, (char*)&stZAPI_packet,sizeof(TstZAPI_packet));
                ecore_con_server_flush(econ_client);	   	
        }
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

int Zibase::extract_infos(char* frame,ZibaseInfoSensor* elm)
{
        char * c;
	
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

        c = strstr (frame, "CMD/INTER");
        if(c != NULL)
        {		
	        extract_zwave_detectOpen(frame,&elm->DigitalVal);
	        elm->type = ZibaseInfoSensor::eDETECT;
        }
        return 0;		
}



