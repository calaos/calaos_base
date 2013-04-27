/** @file  JeelinkAPI.h 
 *  @brief WakeUpJeelink access  API
 *  @date   April 2013
 *  @author L. Vaudoit **/

#ifndef JEELINKAPI_H
#define JEELINKAPI_H

#ifdef __cplusplus
extern "C"
{
#endif
/**********************************/
/********    DEFINE       *********/
/**********************************/
/** @brief define the maximum handle the library supports */
#define NBMAXHANDLE 10

/**********************************/
/********    TYPEDEFS     *********/
/**********************************/
/** @brief Enum definition for protocol list, managed by the library */
typedef enum{
  /** @brief OSV2: oregon scientific V2 protocol (used for Temperature sensor like Geonaute WS700) */
  OSV2,
 /** @brief OSV3: oregon scientific V3 protocol (used for energy monitor like OWL CM160) */
  OSV3
}TeJeelinkProtocol;



/** @brief Structure describing sensor information on Jeelink link */
typedef struct{
  /** @brief prot: modulation used (OSV2/OSV3) according to modulation managed in Jeelink*/
  TeJeelinkProtocol prot;
 /** @brief SensorId: Sensor ID. Knonw stuff: Geonaute WS700 -> 0x1A2D */
  unsigned long SensorId;
 /** @brief chn: Channel communication for this sensor */
  unsigned char Chn;
}TstJeelinkSensorInfo;

/**************************************************/
/*******       API FROM HIGHER LAYER       ********/
/**************************************************/

/** @brief Open communication with Jeelink
 *         The library will open the according device and store last frame received from Jeelink
 *  @param dev: device to open ("/dev/ttyS0" for example)
 *  @param p pointer on structure describing sensor informations
 *  @returns  handle on the device if uccess, -1 otherwise */
extern signed int openJeelink(char* dev, TstJeelinkSensorInfo * p);

/** @brief Close communication with Jeelink (will close device if no more user)
 *  @param handle: handle return by open function
 *  @returns  0 if success, -1 otherwise */
extern signed int closeJeelink(signed int handle);

/** @brief Read last frame received from jeelink, corresponding to the ID and protocole set at open
 *  @param handle: handle return by open function
 *  @param size: Buffer size allowed (if frame is longer than size, only size bytes will be written in buffer
 *  @param buf:  Buffer in which data will be stored. 
 *  @returns  0 if uccess, -1 if no data, -2 if invalid parameters, -3 otherwise */
extern signed int readJeelink(signed int handle,unsigned short *size,unsigned char * buf);

#ifdef __cplusplus
}
#endif
#endif
