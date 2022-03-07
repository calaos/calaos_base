/******************************************************************************
 **  Copyright (c) 2006-2019, Calaos. All Rights Reserved.
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
#ifndef __DATA_LOGGER_H
#define __DATA_LOGGER_H

#include <IOBase.h>
#include "Timer.h"

namespace Calaos
{

class DataLogger
{
private:
    DataLogger();
    bool m_enable = false;
    string m_influxdb_host;
    uint16_t m_influxdb_port;
    string m_influxdb_database;
    string m_influxdb_apiurl;
    string m_influxdb_token;
    string m_influxdb_bucket;
    string m_influxdb_org;
    int m_influxdb_version;
    int m_influxdb_log_timeout;
    Timer *timer;

    void logAll();
    void initInfluxDBv2();
    void initInfluxDBv1();

public:
    static DataLogger &Instance()
    {
        static DataLogger inst;
        return inst;
    }
    ~DataLogger();

    void log(IOBase *io);
};

}
#endif /* __DATA_LOGGER_H */

