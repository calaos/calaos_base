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
#include "Firmwares.h"
#include <jansson.h>
#include <Jansson_Addition.h>

Firmwares::Firmwares(string _cn_user, string _cn_pass, string _fw_version):
    fdownloader(NULL),
    cn_user(_cn_user),
    cn_pass(_cn_pass),
    fw_version(_fw_version),
    download_in_progress(false)
{
    if (cn_user.empty())
        cn_user = Utils::get_config_option("cn_user");
    if (cn_pass.empty())
        cn_pass = Utils::get_config_option("cn_pass");
    if (fw_version.empty())
        fw_version = Utils::get_config_option("fw_version");
}

Firmwares::~Firmwares()
{
    if (fdownloader) fdownloader->Destroy();
    fdownloader = NULL;

    cb_con_check.disconnect();
    cb_con_download.disconnect();
    cb_con_install.disconnect();
}

void Firmwares::checkUpdate(sigc::slot<void, string> callback)
{
    if (download_in_progress)
    {
        cWarningDom("firmwares")
                << "A download is already in progress, aborting checkUpdate()"
                   ;
        return;
    }

    download_in_progress = true;

    cb_con_check.disconnect();
    cb_con_check = cb_signal_check.connect(callback);

    string jdata = "{";
    jdata += "\"cn_user\":\"" + cn_user + "\",";
    jdata += "\"cn_pass\":\"" + cn_pass + "\",";
    jdata += "\"action\":\"firmware\",";
    jdata += "\"command\":\"check\",";
    jdata += "\"fw_target\":\"" + Utils::get_config_option("fw_target") + "\",";
    jdata += "\"fw_version\":\"" + fw_version + "\",";
    jdata += "\"hwid\":\"" + Utils::getHardwareID() + "\"";
    jdata += "}";

    fdownloader = new FileDownloader(CALAOS_NETWORK_URL "/api.php", jdata, "application/json", true);
    fdownloader->addCallback(sigc::mem_fun(*this, &Firmwares::check_cb));
    fdownloader->Start();
}

void Firmwares::check_cb(string result, void *data)
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
        //json_dumpf(json, stdout, JSON_INDENT(4));

        if (!json)
        {
            cb_signal_check.emit("failed");
            cb_con_check.disconnect();

            return;
        }

        if (json_is_object(json))
        {
            if (jansson_int_get(json, "error", -999) != -999)
            {
                json_decref(json);

                cb_signal_check.emit("failed");
                cb_con_check.disconnect();

                return;
            }

            if (jansson_bool_get(json, "need_update"))
            {
                string new_version = jansson_string_get(json, "new_version");

                if (new_version != "")
                {
                    json_decref(json);

                    cb_signal_check.emit(new_version);
                    cb_con_check.disconnect();

                    return;
                }
            }
            else
            {
                json_decref(json);

                cb_signal_check.emit("no_update");
                cb_con_check.disconnect();

                return;
            }
        }

        cb_signal_check.emit("failed");
        cb_con_check.disconnect();

        return;
    }
    else if (result == "failed" || result == "aborted")
    {
        download_in_progress = false;

        cb_signal_check.emit("failed");
        cb_con_check.disconnect();
    }
}

void Firmwares::downloadFirmware(sigc::slot<void, string, FileProgress *> callback)
{
    if (download_in_progress)
    {
        cWarningDom("firmwares")
                << "A download is already in progress, aborting downloadFirmware()"
                   ;
        return;
    }

    download_in_progress = true;

    cb_con_download.disconnect();
    cb_con_download = cb_signal_download.connect(callback);

    string jdata = "{";
    jdata += "\"cn_user\":\"" + cn_user + "\",";
    jdata += "\"cn_pass\":\"" + cn_pass + "\",";
    jdata += "\"action\":\"firmware\",";
    jdata += "\"command\":\"download\",";
    jdata += "\"fw_target\":\"" + Utils::get_config_option("fw_target") + "\",";
    jdata += "\"fw_version\":\"" + fw_version + "\",";
    jdata += "\"hwid\":\"" + Utils::getHardwareID() + "\"";
    jdata += "}";

    fdownloader = new FileDownloader(CALAOS_NETWORK_URL "/api.php", "/tmp/image.tar.bz2", jdata, "application/json", true);
    fdownloader->addCallback(sigc::mem_fun(*this, &Firmwares::download_cb));
    fdownloader->Start();
}

void Firmwares::download_cb(string result, void *data)
{
    if (result == "failed" || result == "aborted")
    {
        download_in_progress = false;

        cb_signal_download.emit("failed", NULL);
        cb_con_download.disconnect();
    }
    else if (result == "progress,update")
    {
        FileProgress *progress = reinterpret_cast<FileProgress *>(data);
        cb_signal_download.emit("progress,update", progress);
    }
    else if (result == "done")
    {
        download_in_progress = false;

        cb_signal_download.emit("done", NULL);
        cb_con_download.disconnect();
    }
}

//static Eina_Bool exe_exit(void *data, int type, void *event)
//{
//    Ecore_Exe_Event_Del *ev = reinterpret_cast<Ecore_Exe_Event_Del *>(event);

//    Firmwares *fm = reinterpret_cast<Firmwares *>(data);
//    if (fm)
//        fm->ExecutableEnded(ev);

//    return EINA_TRUE;
//}

bool Firmwares::installFirmware(sigc::slot<void, string> callback)
{
//    exehandler = ecore_event_handler_add(ECORE_EXE_EVENT_DEL, exe_exit, this);

//    cb_con_install.disconnect();
//    cb_con_install = cb_signal_install.connect(callback);

//    string cmd = "/sbin/fw_update.sh";

//    fwupdate_exe = ecore_exe_run(cmd.c_str(), NULL);
//    if(!fwupdate_exe)
//    {
//        cb_signal_install.emit("failed");
//        cb_con_install.disconnect();
//        ecore_event_handler_del(exehandler);

//        return false;
//    }

    return true;
}

//void Firmwares::ExecutableEnded(Ecore_Exe_Event_Del *event)
//{
//    if (!event) return;

//    if (event->exe == fwupdate_exe)
//    {
//        if (event->exit_code != 0)
//            cb_signal_install.emit("failed");
//        else
//            cb_signal_install.emit("done");

//        cb_con_install.disconnect();
//        ecore_event_handler_del(exehandler);
//    }
//}
