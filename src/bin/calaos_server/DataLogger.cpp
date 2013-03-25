/******************************************************************************
**  Copyright (c) 2007-2013, Calaos. All Rights Reserved.
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
#include <DataLogger.h>
#include <Eet.h>
#include <IOBase.h>

using namespace Calaos;

typedef struct _Calaos_DataLogger_Values Calaos_DataLogger_Values;
typedef struct _Calaos_DataLogger_Array Calaos_DataLogger_Array;

struct _Calaos_DataLogger_Values
{
        time_t timestamp;
        double value;

};

struct _Calaos_DataLogger_Array
{
        Eina_Array *array;
        int array_count;
};

Eet_Data_Descriptor *calaos_datalogger_values_eed;
Eet_Data_Descriptor *calaos_datalogger_array_edd;

#define CALAOS_EET_DATALOGGER_FILE "datalogger.eet"

static void
_hash_values_free_cb(void *data)
{
        free(data);
}


DataLogger::DataLogger()
{
        char db_file[PATH_MAX + 1];
        char **sections;
        int num, i;
        Calaos_DataLogger_Array *array;

        eet_init();
        initEetDescriptors();

        snprintf(db_file, sizeof(db_file), "%s/%s", ETC_DIR, CALAOS_EET_DATALOGGER_FILE);
	ef = eet_open(db_file, EET_FILE_MODE_WRITE);
        printf("Open file : %s\n", db_file);

        hash_values = eina_hash_string_superfast_new(_hash_values_free_cb);

        sections = eet_list(ef, "calaos/sondes/*", &num);
        if (sections)
          {
             for (i = 0; i < num; i++)
             {
                     array = (Calaos_DataLogger_Array*)eet_data_read(ef, calaos_datalogger_values_eed, sections[i]);
                     if (!array)
                             array = (Calaos_DataLogger_Array*)calloc(1, sizeof(Calaos_DataLogger_Array));
                     eina_hash_add(hash_values, sections[i], array);
                     printf("<<<<<<<<<<<<<<<<<<<<< Key stored: %s\n", sections[i]);
             }
             free(sections);
          }



}

DataLogger::~DataLogger()
{
        eet_close(ef);
        releaseEetDescriptors();
        eet_shutdown();
}

void DataLogger::initEetDescriptors()
{
        Eet_Data_Descriptor_Class eddc;
        Eet_Data_Descriptor *edd;


        /* Data Descriptor for time/value */
        EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, Calaos_DataLogger_Values);
        edd = eet_data_descriptor_stream_new(&eddc);

        EET_DATA_DESCRIPTOR_ADD_BASIC(edd, Calaos_DataLogger_Values, "timestamp", timestamp, EET_T_INT);
        EET_DATA_DESCRIPTOR_ADD_BASIC(edd, Calaos_DataLogger_Values, "value", value, EET_T_DOUBLE);

        calaos_datalogger_values_eed = edd;


        EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, Calaos_DataLogger_Array);
        edd = eet_data_descriptor_stream_new(&eddc);

        EET_DATA_DESCRIPTOR_ADD_VAR_ARRAY(edd, Calaos_DataLogger_Array, "array", array, calaos_datalogger_values_eed);

        calaos_datalogger_array_edd = edd;
}

void DataLogger::releaseEetDescriptors()
{

}

void DataLogger::log(IOBase *io)
{
        char section[1024];
        Calaos_DataLogger_Array *array;
        Calaos_DataLogger_Values *value;

        printf("<<<<<<<<<<<<<<<<<<<<<<< Log value %3.3f - type %d - id %s\n", io->get_value_double(), io->get_type(), io->get_param("id").c_str());


        snprintf(section, sizeof(section), "calaos/sonde/%s/%s/%d/values", io->get_param("id").c_str(), "2013", 3);
        array = (Calaos_DataLogger_Array*)eina_hash_find(hash_values, section);
        if (!array)
        {
                array = (Calaos_DataLogger_Array*)calloc(1, sizeof(Calaos_DataLogger_Array));
                array->array = eina_array_new(16);
        }
        value = (Calaos_DataLogger_Values*)calloc(1, sizeof(Calaos_DataLogger_Values));
        value->timestamp = time(NULL);
        value->value = io->get_value_double();
        eina_array_push(array->array, value);

        printf("before write\n");
        eet_data_write(ef, calaos_datalogger_array_edd, section, array, EINA_FALSE);
        printf("after write\n");
        eet_sync(ef);

}
