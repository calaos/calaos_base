/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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
#include <DataLogger.h>
#include <Eet.h>
#include <IOBase.h>

using namespace Calaos;

typedef struct _Calaos_DataLogger_Mean Calaos_DataLogger_Mean;
typedef struct _Calaos_DataLogger_Values Calaos_DataLogger_Values;
typedef struct _Calaos_DataLogger_List Calaos_DataLogger_List;

struct _Calaos_DataLogger_Mean
{
    double mean;

};

struct _Calaos_DataLogger_Values
{
    time_t timestamp;
    double value;

};

struct _Calaos_DataLogger_List
{
//    Eina_List *list;
};

//Eet_Data_Descriptor *calaos_datalogger_values_eed;
//Eet_Data_Descriptor *calaos_datalogger_list_edd;
//Eet_Data_Descriptor *calaos_datalogger_mean_edd;

#define CALAOS_EET_DATALOGGER_FILE "datalogger.eet"

//static void
//_hash_values_free_cb(void *data)
//{
//    free(data);
//}


DataLogger::DataLogger()
{
//    string db_file = getCacheFile(CALAOS_EET_DATALOGGER_FILE);

//    eet_init();
//    initEetDescriptors();

//    ef = eet_open(db_file.c_str(), EET_FILE_MODE_READ_WRITE);

//    hash_values = eina_hash_string_superfast_new(_hash_values_free_cb);
}

DataLogger::~DataLogger()
{
//    eet_close(ef);
//    releaseEetDescriptors();
//    eet_shutdown();
}

void DataLogger::initEetDescriptors()
{
//    Eet_Data_Descriptor_Class eddc;
//    Eet_Data_Descriptor *edd;


//    /* Data Descriptor for mean values */
//    EET_EINA_FILE_DATA_DESCRIPTOR_CLASS_SET(&eddc, Calaos_DataLogger_Mean);
//    edd = eet_data_descriptor_stream_new(&eddc);

//    EET_DATA_DESCRIPTOR_ADD_BASIC(edd, Calaos_DataLogger_Mean, "mean", mean, EET_T_DOUBLE);

//    calaos_datalogger_mean_edd = edd;

//    /* Data Descriptor for time/value */
//    EET_EINA_FILE_DATA_DESCRIPTOR_CLASS_SET(&eddc, Calaos_DataLogger_Values);
//    edd = eet_data_descriptor_stream_new(&eddc);

//    EET_DATA_DESCRIPTOR_ADD_BASIC(edd, Calaos_DataLogger_Values, "timestamp", timestamp, EET_T_INT);
//    EET_DATA_DESCRIPTOR_ADD_BASIC(edd, Calaos_DataLogger_Values, "value", value, EET_T_DOUBLE);

//    calaos_datalogger_values_eed = edd;

//    /* Data Descriptor for list of values */
//    EET_EINA_FILE_DATA_DESCRIPTOR_CLASS_SET(&eddc, Calaos_DataLogger_List);
//    edd = eet_data_descriptor_stream_new(&eddc);

//    EET_DATA_DESCRIPTOR_ADD_LIST(edd, Calaos_DataLogger_List, "list", list, calaos_datalogger_values_eed);

//    calaos_datalogger_list_edd = edd;
}

void DataLogger::releaseEetDescriptors()
{

}

void DataLogger::log(IOBase *io)
{
//    char section[1024];
//    Calaos_DataLogger_List *list;
//    Calaos_DataLogger_Values *value;
//    Calaos_DataLogger_Mean *mean;
//    struct tm *ctime = NULL;

//    tzset(); //Force reload of timezone data
//    time_t t = time(NULL);
//    ctime = localtime(&t);

//    // return immediatly if logged is not active for this IO
//    if (io->get_param("logged") != "true")
//        return;

//    snprintf(section, sizeof(section), "calaos/sonde/%s/%d/%d/%d/%d/values", io->get_param("id").c_str(), ctime->tm_year + 1900, ctime->tm_mon + 1, ctime->tm_mday, ctime->tm_hour);

//    //TODO if month or year changed since last write remove list from hash to save ram

//    // Find the section in memory first
//    list = (Calaos_DataLogger_List*)eina_hash_find(hash_values, section);
//    // Then try to find it from disk
//    if (!list)
//    {
//        list = (Calaos_DataLogger_List*)eet_data_read(ef, calaos_datalogger_values_eed, section);

//        // And create an empty list if it's the first time we need it
//        if (!list)
//            list = (Calaos_DataLogger_List*)calloc(1, sizeof(Calaos_DataLogger_List));
//        eina_hash_add(hash_values, section, list);
//    }

//    value = (Calaos_DataLogger_Values*)calloc(1, sizeof(Calaos_DataLogger_Values));
//    value->timestamp = time(NULL);
//    value->value = io->get_value_double();

//    list->list = eina_list_append(list->list, value);
//    eet_data_write(ef, calaos_datalogger_list_edd, section, list, EINA_TRUE);

//    // Mean Value for month
//    snprintf(section, sizeof(section), "calaos/sonde/%s/%d/%d/mean", io->get_param("id").c_str(), ctime->tm_year + 1900, ctime->tm_mon + 1);
//    mean = (Calaos_DataLogger_Mean*)eina_hash_find(hash_values, section);
//    if (!mean)
//    {
//        mean = (Calaos_DataLogger_Mean*)calloc(1, sizeof(Calaos_DataLogger_Mean));
//        eina_hash_add(hash_values, section, mean);
//    }

//    for (uint32_t i = 0; i < eina_list_count(list->list); i++)
//    {
//        Calaos_DataLogger_Values *v = (Calaos_DataLogger_Values*)eina_list_nth(list->list, i);
//        mean->mean += v->value;
//    }
//    mean->mean /= eina_list_count(list->list);

//    eet_data_write(ef, calaos_datalogger_mean_edd, section, mean, EINA_TRUE);

//    eet_sync(ef);
}
