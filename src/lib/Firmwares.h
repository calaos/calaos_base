/******************************************************************************
 **  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
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
#ifndef FIRMWARES_H
#define FIRMWARES_H

#include <Utils.h>
#include <FileDownloader.h>

class Firmwares
{
private:
    FileDownloader *fdownloader;
    string cn_user, cn_pass;
    string fw_version;

    bool download_in_progress;

    Ecore_Exe *fwupdate_exe;
    Ecore_Event_Handler *exehandler;

    sigc::signal<void, string> cb_signal_check;
    sigc::signal<void, string, FileProgress *> cb_signal_download;
    sigc::signal<void, string> cb_signal_install;

    sigc::connection cb_con_check;
    sigc::connection cb_con_download;
    sigc::connection cb_con_install;

    void check_cb(string result, void *data);
    void download_cb(string result, void *data);

public:
    Firmwares(string cn_user = "", string cn_pass = "", string fw_version = "");
    ~Firmwares();

    /* Check for an update for current user
                 * Callback message will be:
                 * failed : failed to get update info
                 * no_update : system is up to date
                 * <version number> : system should be updated to <version number>
                 */
    void checkUpdate(sigc::slot<void, string> callback);

    /* download new firmware if a new one is available
                   callback will be called with progress info as in FileDownloader
                   return false if error (no new firmware available)
                */
    void downloadFirmware(sigc::slot<void, string, FileProgress *> callback);

    /* install the new downloaded firmware
                   return false if error (firmware not downloaded)
                   callback will be called with progress info (if available)
                */
    bool installFirmware(sigc::slot<void, string> callback);

    /* private don't use */
    void ExecutableEnded(Ecore_Exe_Event_Del *ev);
};

#endif
