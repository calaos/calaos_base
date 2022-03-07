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
#include <chrono>
#include <DataLogger.h>
#include <IOBase.h>
#include <Utils.h>
#include "UrlDownloader.h"
#include "Room.h"
#include "ListeRoom.h"

using namespace Calaos;

void DataLogger::initInfluxDBv2()
{
    m_influxdb_org = Utils::get_config_option("influxdb_org");
    if (m_influxdb_org == "")
    {
       m_influxdb_org = "calaos";
    }

    m_influxdb_token = Utils::get_config_option("influxdb_token");
    if (m_influxdb_token == "")
    {
        m_enable = false;
        cErrorDom("datalogger") << " Error, unable to retrieve influxdb token in configuration";
        return;
    }

    m_influxdb_bucket = Utils::get_config_option("influxdb_bucket");
    if (m_influxdb_bucket == "")
    {
        m_influxdb_bucket = "calaos-data";
    }

    m_influxdb_apiurl += "/api/v2";
    UrlDownloader *query = new UrlDownloader(m_influxdb_apiurl + "/authorizations", true);
    query->setHeader("Authorization", "Token " + m_influxdb_token);
    query->httpGet();
    query->m_signalCompleteData.connect([=](const string &data, int status)
    {
        if (status == 401)
        {
            m_enable = false;
            cErrorDom("datalogger") << " Error : Unauthorized access (" << status << ") while connecting to influxdb database. Please check influxdb_token in configuration";
        }
        else if (status < 20 || status >= 300)
        {
            m_enable = false;
            cErrorDom("datalogger") << " Error " << status << " while connecting to influxdb database. Please check influxdb is up and running";
        }
        else
        {
            m_enable = true;
            m_influxdb_log_timeout = 0;

            Utils::from_string(Utils::get_config_option("influxdb_log_timeout"), m_influxdb_log_timeout);
            if (!m_influxdb_log_timeout)
                m_influxdb_log_timeout = 300; // log if nothing happend in last 5 minutes



            logAll();

            timer = new Timer(m_influxdb_log_timeout, [=]()
                        { logAll(); });
        }
        cInfoDom("datalogger") << "DataLogger is " << (m_enable ? "enabled" : "disabled");
    });
}

void DataLogger::initInfluxDBv1()
{
    m_influxdb_database = Utils::get_config_option("influxdb_database");
    if (m_influxdb_database == "")
        m_influxdb_database = "calaos";

    cInfoDom("datalogger") << "Create database  " << m_influxdb_database << " on host " << m_influxdb_host;
    UrlDownloader *query = new UrlDownloader(m_influxdb_apiurl + "/query", true);

    string postData = "q=CREATE DATABASE " + m_influxdb_database;

    query->httpPost(string(), postData);
    query->m_signalComplete.connect([=](int status)
    {
        if (status < 20 || status >= 300)
        {
            m_enable = false;
            cErrorDom("datalogger") << " Error " << status << " while creating influxdb ("
                                    << m_influxdb_database << ") database. Please check influxdb is up and running";
        }
        else
        {
            m_enable = true;
            m_influxdb_log_timeout = 0;

            Utils::from_string(Utils::get_config_option("influxdb_log_timeout"), m_influxdb_log_timeout);
            if (!m_influxdb_log_timeout)
                m_influxdb_log_timeout = 300; // log if nothing happend in last 5 minutes

            logAll();

            timer = new Timer(m_influxdb_log_timeout, [=]()
                        { logAll(); });
        }
        cInfoDom("datalogger") << "DataLogger is " << (m_enable ? "enabled" : "disabled");
    });
}


DataLogger::DataLogger()
{
    if (Utils::get_config_option("influxdb_enabled") == "true")
    {
        m_influxdb_version = 1;
        Utils::from_string(Utils::get_config_option("influxdb_version"), m_influxdb_version);

        cInfoDom("datalogger") << "Configured for influxdb API version " << m_influxdb_version;

        m_influxdb_host = Utils::get_config_option("influxdb_host");
        if (m_influxdb_host == "")
            m_influxdb_host = "127.0.0.1";


        m_influxdb_port = 0;
        Utils::from_string(Utils::get_config_option("influxdb_port"), m_influxdb_port);
        if (m_influxdb_port == 0)
            m_influxdb_port = 8086;

        m_influxdb_apiurl = "http://" + m_influxdb_host + ":" + Utils::to_string(m_influxdb_port);
        if (m_influxdb_version == 2)
        {
           initInfluxDBv2();
        }
        else
        {
            initInfluxDBv1();
        }
    }
}

DataLogger::~DataLogger()
{
    delete timer;
}

void DataLogger::logAll()
{
    for (int i = 0; i < ListeRoom::Instance().size(); i++)
    {
        Room *room = ListeRoom::Instance().get_room(i);
        for (int j = 0; j < room->get_size(); j++)
        {
            log(room->get_io(j));
        }
    }
}

void DataLogger::log(IOBase *io)
{
    if (!m_enable)
        return;

    if (io->get_param("logged") != "true")
        return;

    Room *room = ListeRoom::Instance().getRoomByIO(io);

    string url = m_influxdb_apiurl;
    if (m_influxdb_version == 2 )
    {
        url += "/write?org=" + Utils::escape_space(m_influxdb_org) + "&bucket=" + Utils::escape_space(m_influxdb_bucket) + "&precision=ns";
    }
    else
    {
        url += "/write?db=" + m_influxdb_database;
    }

    UrlDownloader *query = new UrlDownloader(url, true);

    stringstream postData;
    uint64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    string value = "";

    if (io->get_type() == TBOOL)
        value = Utils::to_string(io->get_value_bool());
    else if (io->get_type() == TINT)
        value = Utils::to_string(io->get_value_double());
    else if (io->get_type() == TSTRING)
        value = io->get_value_string();

    postData << Utils::escape_space(io->get_param("name")) << ",room=" << Utils::escape_space(room->get_name()) << " value=" << value << " " << now;

    if (m_influxdb_version == 2 )
    {
        query->setHeader("Authorization", "Token " + m_influxdb_token);
        query->setHeader("Content-Type","text/plain; charset=utf-8");
        query->setHeader("Accept", "application/json");
    }

    cInfoDom("datalogger") << "send value " << postData.str();
    query->httpPost(string(), postData.str());
    query->m_signalComplete.connect([=](int status)
    {
        if (status < 20 || status >= 300)
        {
            cErrorDom("datalogger") << " Error " << status << " while sending value to influxdb ("
                                    << m_influxdb_database << ") database. Please check influxdb is up and running";
        }
    });
}
