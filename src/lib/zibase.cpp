/******************************************************************************
**  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zibase.h"

#include <Ecore.h>
#include <Ecore_Con.h>
#include <vector>
#include <Calaos.h>
#include <tcpsocket.h>

/*******************************************************/
/*			MACROS                         */
/*******************************************************/
//#define DEBUG

#ifdef DEBUG
#define IN_DEBUG_DO(str) str
#else
#define IN_DEBUG_DO(str) 
#endif
#define BIG_ENDIAN_W(x) (unsigned short)((x<<8)|(x>>8))
#define BIG_ENDIAN_L(x) (unsigned long)(((x&0x000000FF)<<24)|((x&0x0000FF00)<<8) | ((x&0x00FF0000)>>8) | ((x&0xFF000000)>>24)) 

#define DBG_SENTFRAME do{\
	int i;\
	unsigned char *p = (unsigned char*)&stZAPI_packet;\
	for(i=0;i<70;i++)\
	{\
		printf("0x%x ",*p++);\
	}\
}while(0);

/*******************************************************/
/*			DEFINE                         */
/*******************************************************/
#define NOP_CMD    8
#define REG_CMD   13
#define UNREG_CMD 22

#define ZIBASE_UDP_PORT 49999
/*******************************************************/
/*			TYPEDEF                        */
/*******************************************************/
typedef struct {
	unsigned char nbDevOpened;
	unsigned char used[NBSENSORMAX];
}TstModuleInfo;

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


/*******************************************************/
/*			CONST                          */
/*******************************************************/
const unsigned char ZSIG[4]= {'Z','S','I','G'};


/*******************************************************/
/*			VAR                            */
/*******************************************************/
unsigned short my_count =0;
TstZAPI_packet stZAPI_packet;


/* treatment buffer */
static char buf[256];

/* client-server data */
Ecore_Con_Server *udp_sender;
std::vector<TstZibaseInfoSensor*> ListZibaseInfoSensor;
unsigned long udplocalport;

//***********************************************************/
/*			INTERNAL                            */
/***********************************************************/
/*  Zibase Extract Infos functions */
void extract_energy(char* frame,float *val)
{
	char * c;
	

	c = strstr (frame, "Power=");
	if(c != NULL)
	{
		sscanf(c,"Power=<w>%[^<]",buf);
		*val = atof(buf);
	}

}

void extract_temp(char* frame,float *val)
{
	char * c;
	

	c = strstr (frame, "T=<tem>");
	if(c != NULL)
	{
		sscanf(c,"T=<tem>%[^<]",buf);
		*val = atof(buf);
	}
}


int extract_infos(char* frame,char*id,TstZibaseInfoSensor* elm)
{
	char * c;
	float val;
	IN_DEBUG_DO(printf("\n**************************************************"));	
	IN_DEBUG_DO(printf("\nValid frame received, on ID %s,extract infos",id));
	
	c = strstr (frame, "Power=");
	if(c != NULL)
	{
		extract_energy(frame,&val);
		elm->Analog = val;
		IN_DEBUG_DO(printf("\nPower consumption in %s is %.2f W",elm->label,val));
	}

	c = strstr (frame, "T=<tem>");
	if(c != NULL)
	{
		extract_temp(frame,&val);
		elm->Analog=val;
		IN_DEBUG_DO(printf("\nTempérature in %s is %.2f degrees",elm->label,val));
	}
	
	
	IN_DEBUG_DO(printf("\n**************************************************"));
	return 0;	
	
}

//***********************************************************/
/*  Zibase eth communication functions */
//***********************************************************/
Eina_Bool zibase_udpAdd(void *data, int type, Ecore_Con_Event_Server_Add *ev)
{
   IN_DEBUG_DO(printf("Server with ip %s connected!\n", ecore_con_server_ip_get(ev->server));)
   //ecore_con_server_send(ev->server, "hello!", 6);
   //ecore_con_server_flush(ev->server);

   return ECORE_CALLBACK_RENEW;
}

Eina_Bool zibase_udpDel(void *data, int type, Ecore_Con_Event_Server_Del *ev)
{
   IN_DEBUG_DO(printf("Lost server with ip %s!\n", ecore_con_server_ip_get(ev->server));)
   ecore_main_loop_quit();
   return ECORE_CALLBACK_RENEW;
}

Eina_Bool zibase_udpData(void *data, int type, Ecore_Con_Event_Server_Data *ev)
{
	TstZAPI_Rxpacket* packet = (TstZAPI_Rxpacket*)ev->data;

	/* Check ACK Frame */
	if((strstr ((char*)ev->data, "ZSIG") != NULL) && (BIG_ENDIAN_W(packet->packet.command==14)))
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
		stZAPI_packet.param1 = BIG_ENDIAN_L(ipHexa);//0x0d01A8C0;
		stZAPI_packet.param2 = BIG_ENDIAN_L(udplocalport);//0xCC420000;  //17100 -0x000042cc

		
		ecore_con_server_send(udp_sender, (char*)&stZAPI_packet,sizeof(TstZAPI_packet));
		ecore_con_server_flush(udp_sender);

	   	
		return ECORE_CALLBACK_DONE;
	}else return ECORE_CALLBACK_PASS_ON;
	
}

Eina_Bool zibase_udpDatasvr(void *data, int type, Ecore_Con_Event_Server_Data *ev)
{
	char* c;
	TstZAPI_Rxpacket* packet = (TstZAPI_Rxpacket*)ev->data;

	
	/* check this is a zibase RF_FRAME_RECEIVING frame */
	if((strstr((char*)packet->frame, "Received radio ID") != NULL) && (BIG_ENDIAN_W(packet->packet.command==3)))
	{
		/* check ID */
		c = strstr ((char*)packet->frame, "<id>");
		sscanf(c,"<id>%[^<]",buf);
		/* search id in list */
		for ( size_t i = 0, size = ListZibaseInfoSensor.size(); i < size; ++i )
		{
		    if (strcmp(ListZibaseInfoSensor[i]->id,buf) == 0)
		    {	
			/* extract frame */
			extract_infos((char*)packet->frame,buf,ListZibaseInfoSensor[i]);
			break;
		    }
			
		}		
		
		return ECORE_CALLBACK_DONE;
	}   
	else return ECORE_CALLBACK_PASS_ON;

	
	
}
/***********************************************************/
/*			API                                */
/***********************************************************/
zibase::zibase(TstZibaseInfoSensor *p) 
{
	IN_DEBUG_DO(printf("\nzibase: creator");)
 	//if(stModuleInfo.nbDevOpened == 0)
	if ( ListZibaseInfoSensor.empty() )
	{
		IN_DEBUG_DO(printf("\ncreate udp client");)
		//eina_init();
		//ecore_init();
		//ecore_con_init();

		ecore_con_server_add((Ecore_Con_Type)(ECORE_CON_REMOTE_UDP), "192.168.1.13", p->port, NULL);
		ecore_event_handler_add(ECORE_CON_EVENT_CLIENT_DATA, (Ecore_Event_Handler_Cb)zibase_udpDatasvr, NULL);

		if (!(udp_sender = ecore_con_server_connect(ECORE_CON_REMOTE_UDP, "192.168.1.37", ZIBASE_UDP_PORT, NULL)))
     		{
			IN_DEBUG_DO(printf("\nZibase: error connecting to zibase udp server");)
			exit(-1);
		}
				
		/* set event handler for server connect */
		ecore_event_handler_add(ECORE_CON_EVENT_SERVER_ADD, (Ecore_Event_Handler_Cb)zibase_udpAdd, NULL);
		/* set event handler for server disconnect */
		ecore_event_handler_add(ECORE_CON_EVENT_SERVER_DEL, (Ecore_Event_Handler_Cb)zibase_udpDel, NULL);
		/* set event handler for receiving server data */
		ecore_event_handler_add(ECORE_CON_EVENT_SERVER_DATA, (Ecore_Event_Handler_Cb)zibase_udpData, NULL);
		
		


		/* init command structure */
		udplocalport = p->port;
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
		IN_DEBUG_DO(printf("\nSend NOP Frame");)
		stZAPI_packet.command = BIG_ENDIAN_W(NOP_CMD);
		ecore_con_server_send(udp_sender, (char*)&stZAPI_packet,sizeof(TstZAPI_packet));
		ecore_con_server_flush(udp_sender);
				

		/* start client -> this is done bu calaos core code*/
  		 //ecore_main_loop_begin();

   		//ecore_con_init();
  		//ecore_init();
   	        //eina_init();

	}
	/* store element in list */	
	InfoSensor = *p;
	/* insert in list */
	ListZibaseInfoSensor.push_back(&InfoSensor);
	
	
}

zibase::~zibase()
{
      /* TBD */
      /*suppress element in list*/
 	
	for ( size_t i = 0, size = ListZibaseInfoSensor.size(); i < size; ++i )
	{
	    if (strcmp(ListZibaseInfoSensor[i]->id,InfoSensor.id) == 0)
	    {
		/* erase element in list */
		std::vector<TstZibaseInfoSensor*>::iterator it=ListZibaseInfoSensor.begin()+i;
    		ListZibaseInfoSensor.erase(it);
		break;
	    }
	}
	/*if list empty, send unreg frame to zibase */
	if ( ListZibaseInfoSensor.empty() )
	{
		//kill server code... 
		//to be write, should send also unregister zibase frame, but not working for now
	}	 
}

int zibase::zibase_getAnalog(double * val)
{
	*val = (double)InfoSensor.Analog;
  	return 0;    
}

int zibase::zibase_getDigital(bool* val)
{
	*val = InfoSensor.Digital;
  	return 0;    
}



