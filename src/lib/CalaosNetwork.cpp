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
#include "CalaosNetwork.h"
#include <jansson.h>
#include <Jansson_Addition.h>

CalaosNetwork::CalaosNetwork(string _username, string _password):
    username(_username),
    password(_password),
    download_in_progress(false)
{
    if (username == "")
        username = Utils::get_config_option("cn_user");
    if (password == "")
        password = Utils::get_config_option("cn_pass");
}

CalaosNetwork::~CalaosNetwork()
{
}

void CalaosNetwork::Register(string cn_user, string cn_pass)
{
    if (download_in_progress)
    {
        cWarningDom("calaos_network")
                << "A download is already in progress, aborting Register()"
                   ;
        return;
    }

    download_in_progress = true;

    string jdata = "{";
    jdata += "\"cn_user\":\"" + cn_user + "\",";
    jdata += "\"cn_pass\":\"" + cn_pass + "\",";
    jdata += "\"action\":\"register\",";

    //------------------------------------------------------
    //Used to update calaos_id to hwid
    //To be removed in the future when everyone has updated
    string calaos_id = Utils::get_config_option("calaos_id");
    if (calaos_id != "")
        jdata += "\"calaos_id\":\"" + calaos_id + "\",";
    //------------------------------------------------------

    jdata += "\"hwid\":\"" + Utils::getHardwareID() + "\"";
    jdata += "}";

    fdownloader = new FileDownloader(CALAOS_NETWORK_URL "/api.php", jdata, "application/json", true);
    fdownloader->addCallback(sigc::mem_fun(*this, &CalaosNetwork::register_cb));
    fdownloader->Start();
}

void CalaosNetwork::register_cb(string result, void *data)
{
    if (result == "done")
    {
        download_in_progress = false;

        Buffer_CURL *buff = reinterpret_cast<Buffer_CURL *>(data);
        string res((const char *)buff->buffer, buff->bufsize);

        fdownloader = NULL;

        json_error_t jerr;
        json_t *json = json_loads(res.c_str(), 0, &jerr);

        //debug
        //                json_dumpf(json, stdout, JSON_INDENT(4));

        if (!json || !json_is_object(json))
        {
            registered.emit("failed");
            return;
        }

        if (jansson_int_get(json, "error", -999) != -999)
        {
            json_decref(json);

            registered.emit("failed");
            return;
        }

        if (jansson_bool_get(json, "valid"))
        {
            json_decref(json);

            //Remove the calaos_id because we have updated to hwid
            Params opts;
            Utils::get_config_options(opts);

            if (opts.Exists("calaos_id"))
                Utils::del_config_option("calaos_id");

            if (opts.Exists("calaos_user"))
                Utils::del_config_option("calaos_user");

            if (opts.Exists("calaos_password"))
                Utils::del_config_option("calaos_password");

            Utils::set_config_option("cn_user", username);
            Utils::set_config_option("cn_pass", password);

            registered.emit("done");
            return;
        }

        json_decref(json);

        registered.emit("failed");
    }
    else if (result == "failed" || result == "aborted")
    {
        download_in_progress = false;

        registered.emit("failed");
    }
}

void CalaosNetwork::updateIP(string priv_ip)
{
    if (download_in_progress)
    {
        cWarningDom("calaos_network")
                << "A download is already in progress, aborting updateIP()"
                   ;
        return;
    }

    download_in_progress = true;

    string jdata = "{";
    jdata += "\"cn_user\":\"" + username + "\",";
    jdata += "\"cn_pass\":\"" + password + "\",";
    jdata += "\"action\":\"update_ip\",";

    jdata += "\"private_ip\":\"" + priv_ip + "\",";

    jdata += "\"fw_version\":\"" + Utils::get_config_option("fw_version") + "\",";

    jdata += "\"hwid\":\"" + Utils::getHardwareID() + "\"";
    jdata += "}";

    fdownloader = new FileDownloader(CALAOS_NETWORK_URL "/api.php", jdata, "application/json", true);
    fdownloader->addCallback(sigc::mem_fun(*this, &CalaosNetwork::update_ip_cb));
    fdownloader->Start();
}

void CalaosNetwork::update_ip_cb(string result, void *data)
{
    if (result == "done")
    {
        download_in_progress = false;

        Buffer_CURL *buff = reinterpret_cast<Buffer_CURL *>(data);
        string res((const char *)buff->buffer, buff->bufsize);

        fdownloader = NULL;

        json_error_t jerr;
        json_t *json = json_loads(res.c_str(), 0, &jerr);

        //debug
        //                json_dumpf(json, stdout, JSON_INDENT(4));

        if (!json || !json_is_object(json))
        {
            ip_updated.emit("failed");
            return;
        }

        if (jansson_int_get(json, "error", -999) != -999)
        {
            json_decref(json);

            ip_updated.emit("failed");
            return;
        }

        if (jansson_bool_get(json, "updated"))
        {
            json_decref(json);

            ip_updated.emit("done");
            return;
        }

        json_decref(json);

        ip_updated.emit("failed");
    }
    else if (result == "failed" || result == "aborted")
    {
        download_in_progress = false;

        ip_updated.emit("failed");
    }
}

void CalaosNetwork::getIP()
{
    if (download_in_progress)
    {
        cWarningDom("calaos_network")
                << "A download is already in progress, aborting getIP();"
                   ;
        return;
    }

    download_in_progress = true;

    string jdata = "{";
    jdata += "\"cn_user\":\"" + username + "\",";
    jdata += "\"cn_pass\":\"" + password + "\",";
    jdata += "\"action\":\"get_ip\"";
    jdata += "}";

    fdownloader = new FileDownloader(CALAOS_NETWORK_URL "/api.php", jdata, "application/json", true);
    fdownloader->addCallback(sigc::mem_fun(*this, &CalaosNetwork::get_ip_cb));
    fdownloader->Start();
}

void CalaosNetwork::get_ip_cb(string result, void *data)
{
    if (result == "done")
    {
        download_in_progress = false;

        Buffer_CURL *buff = reinterpret_cast<Buffer_CURL *>(data);
        string res((const char *)buff->buffer, buff->bufsize);

        fdownloader = NULL;

        json_error_t jerr;
        json_t *json = json_loads(res.c_str(), 0, &jerr);

        //debug
        //                json_dumpf(json, stdout, JSON_INDENT(4));

        if (!json || !json_is_object(json))
        {
            ip_retrieved.emit("failed", "", "", false);
            return;
        }

        if (jansson_int_get(json, "error", -999) != -999)
        {
            json_decref(json);

            ip_retrieved.emit("failed", "", "", false);
            return;
        }

        string public_ip;
        string private_ip;
        bool at_home;

        public_ip = jansson_string_get(json, "public_ip", "bad_value");
        private_ip = jansson_string_get(json, "private_ip", "bad_value");
        at_home = jansson_bool_get(json, "at_home", false);
        json_decref(json);

        if (public_ip != "bad_value" && private_ip != "bad_value")
            ip_retrieved.emit("done", public_ip, private_ip, at_home);
        else
            ip_retrieved.emit("failed", "", "", false);
    }
    else if (result == "failed" || result == "aborted")
    {
        download_in_progress = false;

        ip_retrieved.emit("failed", "", "", false);
    }
}
