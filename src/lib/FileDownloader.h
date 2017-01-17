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
#ifndef CALAOS_FILE_DL_h
#define CALAOS_FILE_DL_h

#include <Utils.h>
#include <sigc++/sigc++.h>
#include <IPC.h>
#include <Ecore.h>
#include <Ecore_Con.h>
#include <Ecore_File.h>

using namespace Utils;

class FileProgress
{
public:
    FileProgress(double _dlnow, double _dltotal):
        dlnow(_dlnow),
        dltotal(_dltotal)
    {}

    double dlnow;
    double dltotal;
};

//File Downloader thread
class FileDownloader
{
protected:
    bool quit;

    Ecore_Event_Handler *complete_handler;
    Ecore_Event_Handler *progress_handler;

    Ecore_Con_Url *url_con;
    FILE *dl_file;

    bool auto_destroy;
    Ecore_Idler *idler;

    string url;
    string dest;
    string tmpFile;
    string postData;
    string postContentType;

    sigc::signal<void, string, void *> cb_signal;
    sigc::connection cb_connection;

    sigc::signal<void, string, void *, void *> cb_signal_user;
    sigc::connection cb_connection_user;
    void *user_data;

public:
    /* Simple file download */
    FileDownloader(string url, string destFile, bool auto_destroy = false);
    /* POST data with content type, and return data in a buffer */
    FileDownloader(string url, string postData, string postContentType, bool auto_destroy = false);
    /* POST data with content type, and return data in a file */
    FileDownloader(string url, string destFile, string postData, string postContentType, bool auto_destroy = false);

    ~FileDownloader();

    /* here are the callback message passed:
                   failed : download failed
                   done : download is successfully done
                   aborted : download is aborted
                   progress,update : progress update message
                 */
    void addCallback(sigc::slot<void, string, void *> slot)
    {
        cb_connection.disconnect();
        cb_connection = cb_signal.connect(slot);
    }

    void addCallback(sigc::slot<void, string, void *, void *> slot, void *_user_data)
    {
        user_data = _user_data;
        cb_connection_user.disconnect();
        cb_connection_user = cb_signal_user.connect(slot);
    }

    //Start download
    bool Start();

    // Cancel download
    void Cancel();

    void Destroy();

    /* Used by internal callback */
    void completeCb(int status);
    void progressCb(double dlnow, double dltotal);
};

#endif
