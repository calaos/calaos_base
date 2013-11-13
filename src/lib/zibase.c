/* Sample UDP client */


/*******************************************************/
/*			INCLUDE 
/*******************************************************/
#include "zibase.h"

#include <sys/socket.h>
#include <netinet/in.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h> 
#include <errno.h>

/*******************************************************/
/*			MACROS 
/*******************************************************/
#define BIG_ENDIAN_W(x) ((x<<8)|(x>>8))

#define DBG_SENTFRAME do{\
	int i;\
	unsigned char *p = (unsigned char*)&stZAPI_packet;\
	for(i=0;i<70;i++)\
	{\
		printf("0x%x ",*p++);\
	}\
}while(0);

/*******************************************************/
/*			DEFINE 
/*******************************************************/
#define NOP_CMD    8
#define REG_CMD   13
#define UNREG_CMD 22

/*******************************************************/
/*			TYPEDEF 
/*******************************************************/
typedef struct {
	unsigned char nbDevOpened;
	unsigned char used[NBSENSORMAX];
}TstModuleInfo;

/* udp client */
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
/*			CONST 
/*******************************************************/
const unsigned char ZSIG[4]= {'Z','S','I','G'};


/*******************************************************/
/*			VAR 
/*******************************************************/
TstModuleInfo stModuleInfo;
pthread_t zibase_thread;
unsigned char zibase_running = 0;

/* udp client data */
unsigned char rxbuf[sizeof(TstZAPI_Rxpacket)];
unsigned short my_count =0;
TstZAPI_packet stZAPI_packet;
int sockfd;

/* treatment data */
char buf[256];
TstZibaseInfoSensor stZibaseInfoSensor[NBSENSORMAX];
/*TstZibaseInfoSensor stZibaseInfoSensor[NBSENSORMAX]=
{
	{"OS439213057","room Maxime",eTEMP,0,0},
	{"WS132935","home",eENERGY,0,0},
	{"OS439157263","kitchen",eTEMP,0,0},
};
*/
/*******************************************************/
/*			INTERNAL FUNCTIONS 
/*******************************************************/
int searchSensorById(char* id)
{
	int i;
	for(i=0;i<NBSENSORMAX;i++)
	{
		if(strcmp(id,stZibaseInfoSensor[i].id)==0)
			return i;
	}

	return -1;
}

void extract_energy(char* frame,float *val)
{
	unsigned char * c;
	

	c = strstr (frame, "Power=");
	if(c != NULL)
	{
		sscanf(c,"Power=<w>%[^<]",buf);
		*val = atof(buf);
	}
	/*get total conso */	
/*		c = strstr (frame, "Total Energy=<kwh>");
	if(c != NULL)
	{
		sscanf(c,"Total Energy=<kwh>%[^<]",buf);
		printf("\nConso totale=%.2fKWh",atof(buf));
	}
	else printf("\nunknown string Power");
*/		
}

void extract_temp(char* frame,float *val)
{
	unsigned char * c;
	

	c = strstr (frame, "T=<tem>");
	if(c != NULL)
	{
		sscanf(c,"T=<tem>%[^<]",buf);
		*val = atof(buf);
	}
}


int extract_infos(char* frame,char*id)
{
	unsigned char * c;
	int handle;
	printf("\n**************************************************");	
	printf("\nValid frame received, on ID %s,extract infos",id);
		
	handle = searchSensorById(id);
	if(handle != -1)
	{
		switch(stZibaseInfoSensor[handle].type)
		{
			case eENERGY:
				/* OWL management */
				/*get instant power */	
				extract_energy(frame,&stZibaseInfoSensor[handle].energy);
				printf("\nPower consumption in %s is %.2f W",stZibaseInfoSensor[handle].label,stZibaseInfoSensor[handle].energy);
			break;
			case eTEMP:
				/*get température */	
				extract_temp(frame,&stZibaseInfoSensor[handle].temp);
				printf("\nTemperature in %s is %.2f °C",stZibaseInfoSensor[handle].label,stZibaseInfoSensor[handle].temp);
				
			break;
			default:
			break;
		}
	}
	printf("\n**************************************************");
	return 0;	
	
}

void* zibase_callback (void* name)
{
 	int n;
	int s;	
	TstZAPI_Rxpacket* packet;

	while(zibase_running == 1)
	{
		char *c;
		n=recvfrom(sockfd,rxbuf,sizeof(rxbuf),0,NULL,NULL);
	      	rxbuf[n]=0;
	
		packet = (TstZAPI_Rxpacket*)rxbuf;
		//printf("\nZibase ID: %s",(char*)packet->packet.zibase_id);
		
		//printf("\nFrame: %s",(char*)packet->frame);

		if(strstr (packet->frame, "Received radio ID") != NULL)
		{
			/* check ID */
			c = strstr (packet->frame, "<id>");
			sscanf(c,"<id>%[^<]",buf);			
			/* extract frame */
			extract_infos(packet->frame,buf);
		}
		sleep(1);
	}
    	return NULL;
} 
/*******************************************************/
/*			API 
/*******************************************************/
int openZibaseDev(TstZibaseInfoSensor *p)
{
	int n;
	int s;
	int i;
	struct sockaddr_in servaddr,adr;
	TstZAPI_Rxpacket* packet;
	struct sockaddr_in sin;

	if(stModuleInfo.nbDevOpened == 0)
	{
		/* init udp client */
			
		sockfd=socket(AF_INET,SOCK_DGRAM,0);
		if(sockfd <0)
		{
			printf("\n error creating socket");
			return -1;
		}

		memset(&servaddr,0x00,sizeof(servaddr));
		servaddr.sin_family = AF_INET;
		servaddr.sin_addr.s_addr=inet_addr(p->addip);
		servaddr.sin_port=htons(ZIBASE_UDP_PORT);

		memset(&sin,0,sizeof(sin));
		sin.sin_family=AF_INET;
		sin.sin_port=htons(17100);
		sin.sin_addr.s_addr=INADDR_ANY;
		if (-1==bind(sockfd,(struct sockaddr *)&sin,sizeof(struct sockaddr_in))) return 0;

		/* init command structure */
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
		/* Send nop command */	
		stZAPI_packet.command = BIG_ENDIAN_W(NOP_CMD);

		//printf("\n Send NOP frame\n");

		//DBG_SENTFRAME;
	
		sendto(sockfd,(unsigned char*)&stZAPI_packet,sizeof(TstZAPI_packet),0,
		     (struct sockaddr *)&servaddr,sizeof(servaddr));
	
		//printf("\nReceive frame");
		s = sizeof(adr);
		n=recvfrom(sockfd,rxbuf,sizeof(rxbuf),0,(struct sockaddr *)&adr,&s);
	      	rxbuf[n]=0;
	
		packet = (TstZAPI_Rxpacket*)rxbuf;
		//printf("\nHeader: %s",(char*)packet->packet.header);
		printf("\nZibase ID: %s",(char*)packet->packet.zibase_id);
		printf("\n");


		/* Send register frame */
		my_count++;
		stZAPI_packet.my_count=BIG_ENDIAN_W(my_count);
		stZAPI_packet.command = BIG_ENDIAN_W(REG_CMD);
		stZAPI_packet.param1 = 0x0D01A8C0;  //192.168.1.13 -0xC0A8010D
		stZAPI_packet.param2 = 0xCC420000;  //17100 -0x000042cc
	
		//DBG_SENTFRAME;

		sendto(sockfd,(unsigned char*)&stZAPI_packet,sizeof(TstZAPI_packet),0,
		     (struct sockaddr *)&servaddr,sizeof(servaddr));
	
		//printf("\nReceive frame");
	
		s = sizeof(adr);
		n=recvfrom(sockfd,rxbuf,sizeof(rxbuf),0,(struct sockaddr *)&adr,&s);
	      	rxbuf[n]=0;
	
		packet = (TstZAPI_Rxpacket*)rxbuf;

		/* thread creation for reading data */
		if (pthread_create(&zibase_thread, NULL, zibase_callback, "AA"))
		{
			printf("\n[Zibase Open]create thread KO, err = %s",strerror(errno)); 
			return -2;
		}
		else
		{
			zibase_running = 1;
		}
		
		for(i=0;i<NBSENSORMAX ;i++)
			stModuleInfo.used[i]= 0;


		
	}

	stModuleInfo.nbDevOpened++;

	/* search for a free device */
	for(i=0;i<NBSENSORMAX;i++)
	{
		if(stModuleInfo.used[i]==0)
			break;
	}

	if(i<NBSENSORMAX)
	{
		/* fill info */
		stModuleInfo.used[i]=1;
		memcpy(stZibaseInfoSensor[i].id,p->id,64);
		memcpy(stZibaseInfoSensor[i].label,p->label,64);
		stZibaseInfoSensor[i].type = p->type;
		stZibaseInfoSensor[i].temp = 0;
		stZibaseInfoSensor[i].energy = 0;

		return i;
	}
	else return -1;

}

int closeZibaseDev(int handle)
{
	if(handle >= 0 && stModuleInfo.used[handle]==1)
	{
		stModuleInfo.used[handle]==0;
		stModuleInfo.nbDevOpened--;
		if(stModuleInfo.nbDevOpened == 0)
		{
			if(pthread_join(zibase_thread,NULL))
		      	{
			  	printf("\n[Zibase_CLOSE]error during join pthread");
		      	}
              		
			zibase_running = 0;
			/* Close UDP client */
			/* TBD */

		}
				
		
	}else return -1;
	
}


int readZibaseDev(int handle, eZibaseSensor type, float* val)
{
	if(handle >= 0 && stModuleInfo.used[handle]==1)
	{
			
		switch(type)
		{
			case eTEMP: *val = stZibaseInfoSensor[handle].temp;
			break;
			case eENERGY: *val = stZibaseInfoSensor[handle].energy;
			break;

		}
	}else return -1;

	return 0;
}

/*
//******************************************************
//			MAIN 
//******************************************************
int main(int argc, char**argv)
{

	TstZibaseInfoSensor z;

//*******************************************
//*        Test                             *
//*******************************************

	strcpy(z.addip,"192.168.1.37");
	strcpy(z.id,"OS439213057");
	strcpy(z.label,"room Maxime");
	z.type = eTEMP;

	if(openZibaseDev(&z) == -1)
		printf("\Error while creating %s",z.label);

	strcpy(z.addip,"192.168.1.37");
	strcpy(z.id,"WS132935");
	strcpy(z.label,"home");
	z.type = eENERGY;

	if(openZibaseDev(&z) == -1)
		printf("\Error while creating %s",z.label);

	strcpy(z.addip,"192.168.1.37");
	strcpy(z.id,"WS439157263");
	strcpy(z.label,"kitchen");
	z.type = eTEMP;

	if(openZibaseDev(&z) == -1)
		printf("\Error while creating %s",z.label);

	while(1)
	{

		sleep(1);
	}


}
*/