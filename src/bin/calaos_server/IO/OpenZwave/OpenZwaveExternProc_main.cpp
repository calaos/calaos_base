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
#include "ExternProc.h"
#include <Manager.h>
#include <Options.h>
#include <Notification.h>
#include <platform/Log.h>
#include <Node.h>
#include <value_classes/ValueID.h>

using namespace OpenZWave;


typedef struct
{
     uint32			m_homeId;
     uint8			m_nodeId;
     list<ValueID>	m_values;
}NodeInfo;

static list<NodeInfo*> g_nodes;
static pthread_mutex_t g_criticalSection;
static pthread_cond_t  initCond  = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t initMutex = PTHREAD_MUTEX_INITIALIZER;

typedef enum
{
     COMMAND_CLASS_NO_OPERATION  = 0x00,
//     COMMAND_CLASS_BASIC  = 0x20,
//     COMMAND_CLASS_CONTROLLER_REPLICATION  = 0x21,
//     COMMAND_CLASS_APPLICATION_STATUS  = 0x22,
     COMMAND_CLASS_ZIP_SERVICES  = 0x23,
     COMMAND_CLASS_ZIP_SERVER  = 0x24,
     COMMAND_CLASS_SWITCH_BINARY  = 0x25,
     COMMAND_CLASS_SWITCH_MULTILEVEL  = 0x26,
     COMMAND_CLASS_SWITCH_ALL  = 0x27,
     COMMAND_CLASS_SWITCH_TOGGLE_BINARY  = 0x28,
     COMMAND_CLASS_SWITCH_TOGGLE_MULTILEVEL  = 0x29,
     COMMAND_CLASS_CHIMNEY_FAN  = 0x2A,
     COMMAND_CLASS_SCENE_ACTIVATION  = 0x2B,
     COMMAND_CLASS_SCENE_ACTUATOR_CONF  = 0x2C,
     COMMAND_CLASS_SCENE_CONTROLLER_CONF  = 0x2D,
     COMMAND_CLASS_ZIP_CLIENT  = 0x2E,
     COMMAND_CLASS_ZIP_ADV_SERVICES  = 0x2F,
     COMMAND_CLASS_SENSOR_BINARY  = 0x30,
     COMMAND_CLASS_SENSOR_MULTILEVEL  = 0x31,
     COMMAND_CLASS_METER  = 0x32,
     COMMAND_CLASS_ZIP_ADV_SERVER  = 0x33,
     COMMAND_CLASS_ZIP_ADV_CLIENT  = 0x34,
     COMMAND_CLASS_METER_PULSE  = 0x35,
     COMMAND_CLASS_METER_TBL_CONFIG  = 0x3C,
     COMMAND_CLASS_METER_TBL_MONITOR  = 0x3D,
     COMMAND_CLASS_METER_TBL_PUSH  = 0x3E,
     COMMAND_CLASS_THERMOSTAT_HEATING  = 0x38,
     COMMAND_CLASS_THERMOSTAT_MODE  = 0x40,
     COMMAND_CLASS_THERMOSTAT_OPERATING_STATE  = 0x42,
     COMMAND_CLASS_THERMOSTAT_SETPOINT  = 0x43,
     COMMAND_CLASS_THERMOSTAT_FAN_MODE  = 0x44,
     COMMAND_CLASS_THERMOSTAT_FAN_STATE  = 0x45,
     COMMAND_CLASS_CLIMATE_CONTROL_SCHEDULE  = 0x46,
     COMMAND_CLASS_THERMOSTAT_SETBACK  = 0x47,
     COMMAND_CLASS_DOOR_LOCK_LOGGING  = 0x4C,
     COMMAND_CLASS_SCHEDULE_ENTRY_LOCK  = 0x4E,
     COMMAND_CLASS_BASIC_WINDOW_COVERING  = 0x50,
     COMMAND_CLASS_MTP_WINDOW_COVERING  = 0x51,
     COMMAND_CLASS_MULTI_INSTANCE  = 0x60,
     COMMAND_CLASS_DOOR_LOCK  = 0x62,
     COMMAND_CLASS_USER_CODE  = 0x63,
     COMMAND_CLASS_BARRIER_OPERATOR  = 0x66,
     COMMAND_CLASS_CONFIGURATION  = 0x70,
     COMMAND_CLASS_ALARM  = 0x71,
     COMMAND_CLASS_MANUFACTURER_SPECIFIC  = 0x72,
     COMMAND_CLASS_POWERLEVEL  = 0x73,
     COMMAND_CLASS_PROTECTION  = 0x75,
     COMMAND_CLASS_LOCK  = 0x76,
     COMMAND_CLASS_NODE_NAMING  = 0x77,
     COMMAND_CLASS_FIRMWARE_UPDATE_MD  = 0x7A,
     COMMAND_CLASS_GROUPING_NAME  = 0x7B,
     COMMAND_CLASS_REMOTE_ASSOCIATION_ACTIVATE  = 0x7C,
     COMMAND_CLASS_REMOTE_ASSOCIATION  = 0x7D,
     COMMAND_CLASS_BATTERY  = 0x80,
     COMMAND_CLASS_CLOCK  = 0x81,
//     COMMAND_CLASS_HAIL  = 0x82,
     COMMAND_CLASS_WAKE_UP  = 0x84,
     COMMAND_CLASS_ASSOCIATION  = 0x85,
     COMMAND_CLASS_VERSION  = 0x86,
     COMMAND_CLASS_INDICATOR  = 0x87,
     COMMAND_CLASS_PROPRIETARY  = 0x88,
     COMMAND_CLASS_LANGUAGE  = 0x89,
     COMMAND_CLASS_TIME  = 0x8A,
     COMMAND_CLASS_TIME_PARAMETERS  = 0x8B,
     COMMAND_CLASS_GEOGRAPHIC_LOCATION  = 0x8C,
     COMMAND_CLASS_COMPOSITE  = 0x8D,
     COMMAND_CLASS_MULTI_INSTANCE_ASSOCIATION  = 0x8E,
     COMMAND_CLASS_MULTI_CMD  = 0x8F,
     COMMAND_CLASS_ENERGY_PRODUCTION  = 0x90,
     COMMAND_CLASS_MANUFACTURER_PROPRIETARY  = 0x91,
     COMMAND_CLASS_SCREEN_MD  = 0x92,
     COMMAND_CLASS_SCREEN_MD_V2  = 0x92,
     COMMAND_CLASS_SCREEN_ATTRIBUTES  = 0x93,
     COMMAND_CLASS_SIMPLE_AV_CONTROL  = 0x94,
     COMMAND_CLASS_AV_CONTENT_DIRECTORY_MD  = 0x95,
     COMMAND_CLASS_AV_RENDERER_STATUS  = 0x96,
     COMMAND_CLASS_AV_CONTENT_SEARCH_MD  = 0x97,
     COMMAND_CLASS_SECURITY  = 0x98,
     COMMAND_CLASS_AV_TAGGING_MD  = 0x99,
     COMMAND_CLASS_IP_CONFIGURATION  = 0x9A,
     COMMAND_CLASS_ASSOCIATION_COMMAND_CONFIGURATION  = 0x9B,
     COMMAND_CLASS_SENSOR_ALARM  = 0x9C,
     COMMAND_CLASS_SILENCE_ALARM  = 0x9D,
     COMMAND_CLASS_SENSOR_CONFIGURATION  = 0x9E,
     COMMAND_CLASS_MARK  = 0xEF,
     COMMAND_CLASS_NON_INTEROPERABLE  = 0xF0,
}CommandClassIDType;

const string commandClassId2String(const uint8_t id)
{
     switch(id)
     {
     case COMMAND_CLASS_NO_OPERATION :
          return "No Operation";
     case COMMAND_CLASS_BASIC :
          return "Basic";
     case COMMAND_CLASS_CONTROLLER_REPLICATION :
          return "Controller Replication";
     case COMMAND_CLASS_APPLICATION_STATUS:
          return "Application Status";
     case COMMAND_CLASS_ZIP_SERVICES:
          return "Zip Services";
     case COMMAND_CLASS_ZIP_SERVER:
          return "Zip Server";
     case COMMAND_CLASS_SWITCH_BINARY:
          return "Switch Binary";
     case COMMAND_CLASS_SWITCH_MULTILEVEL:
          return "Switch Multilevel";
     case COMMAND_CLASS_SWITCH_ALL:
          return "Switch All";
     case COMMAND_CLASS_SWITCH_TOGGLE_BINARY:
          return "Swtich Toggle Binary";
     case COMMAND_CLASS_SWITCH_TOGGLE_MULTILEVEL:
          return "Switch Toggle Multilevel";
     case COMMAND_CLASS_CHIMNEY_FAN:
          return "Chimney Fan";
     case COMMAND_CLASS_SCENE_ACTIVATION:
          return "Scene Activation";
     case COMMAND_CLASS_SCENE_ACTUATOR_CONF:
          return "Scene Actuator Conf";
     case COMMAND_CLASS_SCENE_CONTROLLER_CONF:
          return "Scene Controller Conf";
     case COMMAND_CLASS_ZIP_CLIENT:
          return "Zip Client";
     case COMMAND_CLASS_ZIP_ADV_SERVICES:
          return "Zip Adv Services";
     case COMMAND_CLASS_SENSOR_BINARY:
          return "Sensor Binary";
     case COMMAND_CLASS_SENSOR_MULTILEVEL:
          return "Sensor Multilevel";
     case COMMAND_CLASS_METER:
          return "Meter";
     case COMMAND_CLASS_ZIP_ADV_SERVER:
          return "Zip Adv Server";
     case COMMAND_CLASS_ZIP_ADV_CLIENT:
          return "Zip ADV Client";
     case COMMAND_CLASS_METER_PULSE:
          return "Meter Pulse";
     case COMMAND_CLASS_METER_TBL_CONFIG:
          return "Meter TBL Config";
     case COMMAND_CLASS_METER_TBL_MONITOR:
          return "Meter TBL Monitor";
     case COMMAND_CLASS_METER_TBL_PUSH:
          return "Meter TBL Push";
     case COMMAND_CLASS_THERMOSTAT_HEATING:
          return "Thermostat Heating";
     case COMMAND_CLASS_THERMOSTAT_MODE:
          return "Thermostat Mode";
     case COMMAND_CLASS_THERMOSTAT_OPERATING_STATE:
          return "Theromostat Operating State";
     case COMMAND_CLASS_THERMOSTAT_SETPOINT:
          return "Thermostat SetPOint";
     case COMMAND_CLASS_THERMOSTAT_FAN_MODE:
          return "Thermostat Fan Mode";
     case COMMAND_CLASS_THERMOSTAT_FAN_STATE:
          return "Thermostat Fan State";
     case COMMAND_CLASS_CLIMATE_CONTROL_SCHEDULE:
          return "Climate Control Schedule";
     case COMMAND_CLASS_THERMOSTAT_SETBACK :
          return "Thermostat SetBack";
     case COMMAND_CLASS_DOOR_LOCK_LOGGING :
          return "Door lock logging";
     case COMMAND_CLASS_SCHEDULE_ENTRY_LOCK :
          return "schedule entry lock";
     case COMMAND_CLASS_BASIC_WINDOW_COVERING:
          return "Basic Windows covering";
     case COMMAND_CLASS_MTP_WINDOW_COVERING:
          return "MTP window covering";
     case COMMAND_CLASS_MULTI_INSTANCE:
          return "MultiInstance";
     case COMMAND_CLASS_DOOR_LOCK:
          return "Door lock";
     case COMMAND_CLASS_USER_CODE:
          return "user code";
     case COMMAND_CLASS_BARRIER_OPERATOR:
          return "barrier operator";
     case COMMAND_CLASS_CONFIGURATION:
          return "Configuration";
     case COMMAND_CLASS_ALARM:
          return "Alarm";
     case COMMAND_CLASS_MANUFACTURER_SPECIFIC:
          return "Manufacturer specific";
     case COMMAND_CLASS_POWERLEVEL:
          return "power level";
     case COMMAND_CLASS_PROTECTION:
          return "protection";
     case COMMAND_CLASS_LOCK:
          return "lock";
     case COMMAND_CLASS_NODE_NAMING:
          return "Node Naming";
     case COMMAND_CLASS_FIRMWARE_UPDATE_MD:
          return "Firmware Update MD";
     case COMMAND_CLASS_GROUPING_NAME:
          return "Grouping NAME";
     case COMMAND_CLASS_REMOTE_ASSOCIATION_ACTIVATE:
          return "Remote Association Activate";
     case COMMAND_CLASS_REMOTE_ASSOCIATION:
          return "Remote association";
     case COMMAND_CLASS_BATTERY:
          return "battery";
     case COMMAND_CLASS_CLOCK:
          return "Clock";
     case COMMAND_CLASS_HAIL:
          return "Hail";
     case COMMAND_CLASS_WAKE_UP:
          return "Wake Up";
     case COMMAND_CLASS_ASSOCIATION:
          return "Association";
     case COMMAND_CLASS_VERSION:
          return "Version";
     case COMMAND_CLASS_INDICATOR:
          return "Indicator";
     case COMMAND_CLASS_PROPRIETARY:
          return "Proprietary";
     case COMMAND_CLASS_LANGUAGE:
          return "Language";
     case COMMAND_CLASS_TIME:
          return "Time";
     case COMMAND_CLASS_TIME_PARAMETERS:
          return "Time Parameters";
     case COMMAND_CLASS_GEOGRAPHIC_LOCATION:
          return "Geographic location";
     case COMMAND_CLASS_COMPOSITE:
          return "Composite";
     case COMMAND_CLASS_MULTI_INSTANCE_ASSOCIATION:
          return "MultiInstance association";
     case COMMAND_CLASS_MULTI_CMD:
          return "Multi CMD";
     case COMMAND_CLASS_ENERGY_PRODUCTION:
          return "Energey producation";
     case COMMAND_CLASS_MANUFACTURER_PROPRIETARY:
          return "Manufacturer proprietary";
     case COMMAND_CLASS_SCREEN_MD:
          return "Screen MD";
     case COMMAND_CLASS_SCREEN_ATTRIBUTES:
          return "Screen Attributes";
     case COMMAND_CLASS_SIMPLE_AV_CONTROL:
          return "Simpl AV COntrol";
     case COMMAND_CLASS_AV_CONTENT_DIRECTORY_MD:
          return "Av Content Directory MD";
     case COMMAND_CLASS_AV_RENDERER_STATUS:
          return "AV Renderer Status";
     case COMMAND_CLASS_AV_CONTENT_SEARCH_MD:
          return "Av Content Search MD";
     case COMMAND_CLASS_SECURITY:
          return "Secturity";
     case COMMAND_CLASS_AV_TAGGING_MD:
          return "AV Tagging MD";
     case COMMAND_CLASS_IP_CONFIGURATION:
          return "IP Configuration";
     case COMMAND_CLASS_ASSOCIATION_COMMAND_CONFIGURATION:
          return "Association Command Configuration";
     case COMMAND_CLASS_SENSOR_ALARM:
          return "Sensor Alarm";
     case COMMAND_CLASS_SILENCE_ALARM:
          return "Silence Alarm";
     case COMMAND_CLASS_SENSOR_CONFIGURATION:
          return "Sensor Configuration";
     case COMMAND_CLASS_MARK:
          return "Mark";
     case COMMAND_CLASS_NON_INTEROPERABLE:
          return "Non Interoperable";
     default:
          return "Unknown class";
     }
}


const string valueType2String(const ValueID::ValueType vType)
{

     switch (vType)
     {
     case ValueID::ValueType_Bool:
          return "bool";
     case ValueID::ValueType_Byte:
          return "byte";
     case ValueID::ValueType_Decimal:
          return "decimal";
     case ValueID::ValueType_Int:
          return "int";
     case ValueID::ValueType_List:
          return "list";
     case ValueID::ValueType_Schedule:
          return "schedule";
     case ValueID::ValueType_Short:
          return "short";
     case ValueID::ValueType_String:
          return "string";
     case ValueID::ValueType_Button:
          return "button";
     case ValueID::ValueType_Raw:
          return "raw";
     default:
          return "unknown";
     }
}

const string valueGenre2String(const ValueID::ValueGenre vGenre)
{
     switch(vGenre)
     {
     case ValueID::ValueGenre_Basic:
          return "basic";
     case ValueID::ValueGenre_User:
          return "user";
     case ValueID::ValueGenre_Config:
          return "config";
     case ValueID::ValueGenre_System:
          return "system";
     default:
          return "unknown";
     };
}

NodeInfo* GetNodeInfo(const Notification* _notification)
{
     const uint32 homeId = _notification->GetHomeId();
     const uint8 nodeId = _notification->GetNodeId();
     for(list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it)
     {
          NodeInfo* nodeInfo = *it;
          if((nodeInfo->m_homeId == homeId) && (nodeInfo->m_nodeId == nodeId))
          {
               return nodeInfo;
          }
     }

     return NULL;
}

void OnNotification(Notification const* _notification, void* _context)
{
     // Must do this inside a critical section to avoid conflicts with the main thread
     pthread_mutex_lock( &g_criticalSection );

     switch( _notification->GetType() )
     {
     case Notification::Type_ValueAdded:
     {
          if( NodeInfo* nodeInfo = GetNodeInfo( _notification ) )
          {
               ValueID v =  _notification->GetValueID();
               // Add the new value to our list
               nodeInfo->m_values.push_back(_notification->GetValueID());
               cDebugDom("openzwave") << "ValueGenre : " << valueGenre2String(v.GetGenre()) << " ValueType : " << valueType2String(v.GetType());
               cDebugDom("openzwave") << "CommandClassID : " << commandClassId2String(v.GetCommandClassId());
          }
          break;
     }
     case Notification::Type_ValueRemoved:
     {
          if( NodeInfo* nodeInfo = GetNodeInfo( _notification ) )
          {
               // Remove the value from out list
               for( list<ValueID>::iterator it = nodeInfo->m_values.begin(); it != nodeInfo->m_values.end(); ++it )
               {
                    if( (*it) == _notification->GetValueID() )
                    {
                         nodeInfo->m_values.erase( it );
                         break;
                    }
               }
          }
          break;
     }
     case Notification::Type_ValueChanged:
     {
          // One of the node values has changed
          if( NodeInfo* nodeInfo = GetNodeInfo( _notification ) )
          {
               nodeInfo = nodeInfo;		// placeholder for real action
          }
          break;
     }

     case Notification::Type_NodeAdded:
     {
          NodeInfo* nodeInfo = new NodeInfo();

          nodeInfo->m_homeId = _notification->GetHomeId();
          nodeInfo->m_nodeId = _notification->GetNodeId();
          g_nodes.push_back(nodeInfo);
          cDebugDom("openzwave") << "NodeAdded " << "Home ID : " <<  nodeInfo->m_homeId
                                 << " Node ID : " <<  (int)nodeInfo->m_nodeId;
          break;
     }

     case Notification::Type_NodeRemoved:
     {

          uint32 const homeId = _notification->GetHomeId();
          uint8 const nodeId = _notification->GetNodeId();
          for( list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it )
          {
               NodeInfo* nodeInfo = *it;
               if ((nodeInfo->m_homeId == homeId) && (nodeInfo->m_nodeId == nodeId))
               {
                    cDebugDom("openzwave") << "NodeRemoved " << "Home ID : " <<  nodeInfo->m_homeId
                                           << " Node ID : " <<  (int)nodeInfo->m_nodeId;
                    g_nodes.erase(it);
                    delete nodeInfo;
                    break;
               }
          }
          break;
     }

     case Notification::Type_DriverReady:
     {
          cDebugDom("openzwave") << "DriverReady";
          break;
     }

     case Notification::Type_DriverFailed:
     {
          cDebugDom("openzwave") << "DriverFailed";
          pthread_cond_broadcast(&initCond);
          break;
     }

     case Notification::Type_AwakeNodesQueried:
     case Notification::Type_AllNodesQueried:
     case Notification::Type_AllNodesQueriedSomeDead:
     {
          cDebugDom("openzwave") << "Queried";
          pthread_cond_broadcast(&initCond);

          for( list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it )
          {
               NodeInfo* nodeInfo = *it;



          }

          break;
     }
     case Notification::Type_NodeEvent:
     case Notification::Type_PollingDisabled:
     case Notification::Type_PollingEnabled:
     case Notification::Type_Group:
     case Notification::Type_DriverReset:
     case Notification::Type_Notification:
     case Notification::Type_NodeNaming:
     case Notification::Type_NodeProtocolInfo:
     case Notification::Type_NodeQueriesComplete:
     default:
     {
          //cDebugDom("openzwave") << "Default";
     }
     }

     pthread_mutex_unlock( &g_criticalSection );
}


class OpenZwaveProcess: public ExternProcClient
{
public:

     //needs to be reimplemented
     virtual bool setup(int &argc, char **&argv);
     virtual int procMain();

     EXTERN_PROC_CLIENT_CTOR(OpenZwaveProcess)

     protected:

     unsigned int universe = 0;

     //needs to be reimplemented
     virtual void readTimeout();
     virtual void messageReceived(const string &msg);
};

void OpenZwaveProcess::readTimeout()
{
}

void OpenZwaveProcess::messageReceived(const string &msg)
{
     json_error_t jerr;
     json_t *jroot = json_loads(msg.c_str(), 0, &jerr);

     if (!jroot || !json_is_array(jroot))
     {
          cWarningDom("openzwave") << "Error parsing json from sub process: " << jerr.text;
          if (jroot)
               json_decref(jroot);
          return;
     }

     int idx;
     json_t *value;

     json_array_foreach(jroot, idx, value)
     {
          Params p;
          jansson_decode_object(value, p);

          if (p.Exists("channel") && p.Exists("value"))
          {
               unsigned int channel;
               unsigned int val;
               Utils::from_string(p["channel"], channel);
               Utils::from_string(p["value"], val);

               cDebugDom("openzwave") << "Set channel " << channel << " with value: " << val;
          }
     }
}

bool OpenZwaveProcess::setup(int &argc, char **&argv)
{
#if 0
     if (!connectSocket())
     {
          cError() << "process cannot connect to calaos_server";
          return false;
     }

     if (argc >= 1)
          Utils::from_string(argv[1], universe);

     cDebug() << "Universe: " << universe;
#endif
     return true;
}

int OpenZwaveProcess::procMain()
{
     pthread_mutexattr_t mutexattr;

     pthread_mutexattr_init ( &mutexattr );
     pthread_mutexattr_settype( &mutexattr, PTHREAD_MUTEX_RECURSIVE );
     pthread_mutex_init( &g_criticalSection, &mutexattr );
     pthread_mutexattr_destroy( &mutexattr );


     pthread_mutex_lock( &initMutex );


     printf("Starting calaos OpenZwave external process with OpenZWave Version %s\n", Manager::getVersionAsString().c_str());

     // the log file will appear in the program's working directory.
     Options::Create( "../../../config/", "", "" );
     Options::Get()->AddOptionInt( "SaveLogLevel", LogLevel_Error );
     Options::Get()->AddOptionInt( "QueueLogLevel", LogLevel_Error );
     Options::Get()->AddOptionInt( "DumpTrigger", LogLevel_Error );
     Options::Get()->AddOptionInt( "PollInterval", 500 );
     Options::Get()->AddOptionBool( "IntervalBetweenPolls", true );
     Options::Get()->AddOptionBool("ValidateValueChanges", true);
     Options::Get()->Lock();

     Manager::Create();

     Manager::Get()->AddWatcher( OnNotification, NULL );

     string port = "/dev/ttyUSB0";

     Manager::Get()->AddDriver( port );
     pthread_cond_wait( &initCond, &initMutex );

     while(1);
//     run();

     return 0;
}

EXTERN_PROC_CLIENT_MAIN(OpenZwaveProcess)
