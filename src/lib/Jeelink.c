#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdarg.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h> 

#include "Jeelink.h"

 

/**************************************************************/
/*                          TYPEDEF                           */
/**************************************************************/
typedef struct
{
  int handle;
  TstJeelinkSensorInfo SensorInfos;
  unsigned char NewData;
  unsigned short len;
  unsigned char buf[256];
}TstJeelink_Data;

/**************************************************************/
/*                      GLOBAL DATA                           */
/**************************************************************/
static int isopened = 0;
static int handle;

TstJeelink_Data Jeelink_Data[NBMAXHANDLE];


pthread_t jeelink_thread;
unsigned char  jeelink_running;

static unsigned char buf[256];
static unsigned char bufTmp[256];
/**************************************************************/
/*                          FUNCTIONS                          */
/**************************************************************/
void extract (unsigned char * data) {

unsigned short sonde = *(unsigned short*)data;
//int sign = (data[6]&0x8) ? -1 : 1;
int sign = 1;
float temp = ((data[5]&0xF0) >> 4)*10 + (data[5]&0xF) + (float)(((data[4]&0xF0) >> 4) / 10.0);
int hum = (data[7]&0xF) * 10 + ((data[6]&0xF0) >> 4);

printf("\nSonde-> 0x%x",sonde);

printf(", Channel-> 0x%x",data[2]);
printf("\nTemp-> % C",sign*temp);

printf("\n calcul= (%d*10 + (%d) + (float)((%d / 10.0)",((data[5]&0xF0)>>4),(data[5]&0xF),((data[4]&0xF0)>>4));

printf("\nHumidity-> %d%",hum);

}



void* jeelink_callback (void* name)
{ 
	int ret;
	int maxfd = 0;
	fd_set readfs;
	fd_set writefs;
	fd_set errfs; 
	struct timeval timeout;

	unsigned char len;
	unsigned short SensorId;
	unsigned char Chn;

	timeout.tv_sec  = 0;
	timeout.tv_usec = 1000;
		
    
    	while( jeelink_running == 1)
   	{

	
 /*       FD_ZERO(&readfs);
        FD_ZERO(&writefs);
        FD_ZERO(&errfs);
	FD_SET(Jeelink_Data[0].handle, &readfs);           
	FD_SET(Jeelink_Data[0].handle, &writefs);            
	FD_SET(Jeelink_Data[0].handle, &errfs);
	
        
        
        ret = select(maxfd + 1, &readfs, &writefs, &errfs, &timeout);
        if(ret < 0)
        {
            printf("\n[Jeelink ]select failed, error %s",strerror(errno));
        }
        else if (ret == 0)
        {
            // do nothing in timeout case 
           // printf("\n[Jeelink ] select timeout");
        }
        else
*/       {

           

               
                //if(FD_ISSET(Jeelink_Data[0].handle, &readfs))
                { 
                    
		    
		    
                /* read len */
                //char *test="A";
		unsigned int tmp;
                unsigned char buf[256];
		unsigned char test[8];
                unsigned char prot;
                int i,found=0;
                if(read(handle,&test,2)==2)
		{
                	sscanf((char*)test,"%02x",(unsigned int*)&len);
		
			//printf("\nlen: %d, 0x%x",len,len);
		
			
            		if( len <0)
		    	{
				printf("\n[Jeelink]read failed, error %s",strerror(errno));
			    
		    	}
		    	else if(read(handle,&buf,(2*len)+2+2)) //+2 +2 for \n character
	            	{
	                 	printf("\n[Jeelink]JEELINK FRAME: %s",buf);

				for(i=0;i<len;i++)
				{
					//printf("\n**");
					sscanf((buf+(2*i)),"%02x",(unsigned int*)&tmp);
					if(i==0)
					{
						/* Protocole */
						prot = tmp;
						//printf("\nProt: %d, 0x%x",Jeelink_Data[0].prot,Jeelink_Data[0].prot);
					}
					else
					{
						/* Data */
						bufTmp[i-1]=tmp;
						//printf("Output: %d, 0x%x",Jeelink_Data[0].buf[i-1],Jeelink_Data[0].buf[i-1]);
					}

				}
				if(prot == OSV2)
				{
					/* we know sensor id and chanel */
					SensorId = ((unsigned short)(bufTmp[0]))<<8;
					SensorId |= bufTmp[1];
					Chn = bufTmp[2];	
				}else
				{
					SensorId = 0xFF;
					Chn = 0xFF;
				}
			      
			 

				/* Search corresponding entry */
			        for(i=0;i<NBMAXHANDLE && found==-1;i++)
				{
					if( (Jeelink_Data[i].SensorInfos.prot == prot) && 
					    (Jeelink_Data[i].SensorInfos.SensorId == SensorId) &&
 					    (Jeelink_Data[i].SensorInfos.Chn == Chn) )
					{
						found = i;
					}	
				}
				if(found != -1)
				{					
					/* New Data are avalaible */ 
                    			printf("\nJeelink Thread: new data on handle %d",found);
					Jeelink_Data[found].NewData = 1;   
					Jeelink_Data[found].len = len;  
					memcpy(Jeelink_Data[found].buf,bufTmp,len);                       
		
				
					
					
				}
				else printf("\n[Jeelink]frame with no corresponding entry, prot 0x%02x, Id 0x%02x, Chn 0x%02x",prot,SensorId,Chn);
	                
	            	}
		}
		    
	}
                

 /*               if(FD_ISSET(data->s, &errfs))
                {
                    if(data->errcallback != NULL)
                    {
                        data->errcallback();
                    }
                }
 */                         
                
            
        }
       
    }
    printf("********************************************************************************\n");    
    return NULL;
} 

signed int openJeelink(char* dev,TstJeelinkSensorInfo * p)
{
	int i;
	int ret;
	struct termios options;

	if(isopened==0)
        {
		handle = open(dev, O_RDWR| O_NOCTTY | O_NDELAY);
	 
	
		if(handle < 0)
		{
		  perror("\n[Jeelink ]Error opening serial port ");
			return(-1);
		}
		else
		{
			
			isopened = 1;
			printf("\n[Jeelink ]Serial port %s open",dev);

			tcgetattr(handle, &options);
			//B115200 bauds
			cfsetospeed(&options, B115200);
			options.c_cflag |= (CLOCAL | CREAD);
	
			options.c_cflag &= ~PARENB; //no parity
			options.c_cflag &= ~CSTOPB; // 1 stop bit
			options.c_cflag &= ~CSIZE; //
			options.c_cflag |= CS8; //8 data bits
			tcsetattr(handle, TCSANOW, &options); 
			printf("\n[Jeelink ]Serial port configuration OK ");
			fcntl(handle,F_SETFL,10);//mode bloquant pour la fonction read() si aucun caractere dispo, programme attend

			
			

			/* thread creation for reading data */
			if (pthread_create(&jeelink_thread, NULL, jeelink_callback, "AA"))
			{
				printf("\n[Jeelink Open]create thread KO, err = %s",strerror(errno)); 
				handle = -2;
			}
			else
			{
				jeelink_running = 1;
			}
			
			for(i=0;i<NBMAXHANDLE ;i++)
				Jeelink_Data[i].handle = -1;
		
		
		}
	
	}

	if(handle < 0)
	{}
	else
	{
		
		/* search object */
		for(i=0;i<NBMAXHANDLE && Jeelink_Data[i].handle != -1;i++);
		
		if(i<NBMAXHANDLE)
		{	
			/* store infos */
			memset(&Jeelink_Data[i],0,sizeof(TstJeelink_Data));
			Jeelink_Data[i].handle = i;
			Jeelink_Data[i].SensorInfos=*p;
		}
		else (i = -1);
	}

/*	for(i=0;i<10;i++)
	{
		printf("\nJeelink: %d: handle:%d, prot:%d, SensorId:0x%02x, Chn:0x%x",i,Jeelink_Data[i].handle,Jeelink_Data[i].SensorInfos.prot,Jeelink_Data[i].SensorInfos.SensorId,Jeelink_Data[i].SensorInfos.Chn); 

	}*/

	return i;
}	 
signed int closeJeelink(signed int handle)
{
	printf("\n[Jeelink ]Close Serial port %d ",handle);
	if(handle >=0)
	{
	      if(pthread_join(jeelink_thread,NULL))
              {
                  printf("\n[Jeelink_CLOSE]error during join pthread");
              }
              jeelink_running = 0;
	      close(handle);//fermeture du port serie
	      handle = -1;
	  
	}
	return 0;
}

signed int readJeelink(signed int handle,unsigned short *size,unsigned char * buf)
{
  int ret = 0;
  if(handle>=0)
    {

      if(Jeelink_Data[handle].NewData == 1)
	{
	  //*size = Jeelink_Data[0].len;//min(*size,Jeelink_Data[0].len);
	  if(*size > Jeelink_Data[handle].len)
		*size = Jeelink_Data[handle].len;
	  memcpy(buf,Jeelink_Data[handle].buf,*size);
	  Jeelink_Data[handle].NewData = 0;

	printf("\n[Jeelink]new data read on handle %d)",handle);
	 
	  ret = 0;
        }
      else
	{
	  ret = -1;
	}
      
    }
    else
    {
	  ret = -2;
    }

  return ret;
}


