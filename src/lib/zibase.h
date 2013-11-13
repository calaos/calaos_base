/** @file  ZibaseAPI.h 
 *  @brief WakeUpZibase access  API
 *  @date   november 2013
 *  @author L. Vaudoit **/

#ifndef ZIBASEAPI_H
#define ZIBASEAPI_H

#ifdef __cplusplus
extern "C"
{
#endif
/**********************************/
/********    DEFINE       *********/
/**********************************/
/** @brief define the default udp port used for zibase communication */
#define ZIBASE_UDP_PORT 49999
/** @brief define the nb sensor max we can declare */
#define NBSENSORMAX 32

/**********************************/
/********    TYPEDEFS     *********/
/**********************************/
/** @brief Enum definition for sensor type */
typedef enum{
/** @brief temperature sensor) */
	eTEMP,
/** @brief energy monitor sensor) */
	eENERGY
}eZibaseSensor;

/** @brief Structure describing sensor information on zibase link */
typedef struct{
	/** @brief ip address of the zibas eon which the sensor is plugged */
	char addip[32];
	/** @brief id of the sensor (should be seen on zibase device activity */
	char id[64];
	/** @brief label of the sensor (ex: room first floor) */
	char label[64];
	/** @brief sensor type */
	eZibaseSensor type;
	/** @brief temperature value (use for eTEMP sensor type) */
	float temp;
	/** @brief energy value (use for eENERGY sensor type) */
	float energy;
}TstZibaseInfoSensor;




/**************************************************/
/*******       API FROM HIGHER LAYER       ********/
/**************************************************/

/** @brief Open communication with Zibase
 *         The library will open the according device and store last frame received from zibase
 *  @param p: structure containing information device and sensor
 *  @returns  handle on the device if success, -1 otherwise */
extern int openZibaseDev(TstZibaseInfoSensor *p);

/** @brief Close a device and if no more, close udp client
 *  @param handle:handle returned by opendevice function
 *  @returns  0 if success, -1 otherwise */
extern int closeZibaseDev(int handle);

/** @brief Read value on a specific device
 *  @param handle: handle returned by opendevice function
 *  @param type: sensor type
 *  @param val: adress where result will be store
 *  @returns  0 if success, -1 otherwise */
extern int readZibaseDev(int handle, eZibaseSensor type, float* val);

#ifdef __cplusplus
}
#endif
#endif
