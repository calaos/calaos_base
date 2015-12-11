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
#ifndef  DOWNLOADMANAGER_INC
#define  DOWNLOADMANAGER_INC

#include <FileDownloader.h>

class DownloadManagerData
{
public:
    string source;
    string destination;
    sigc::signal<void, string, string, void*, void*> sigDownload;
    int downloadTimes;

    void *userData;
    sigc::signal<void, string, string, void*> sigUser;
    sigc::signal<void, string, string, double, double, void*> sigProgressUpdate;

    FileDownloader* downloader;

    //true if the download must be aborted
    //no signal is sent if the download is aborted
    bool hasDeleted;

    DownloadManagerData(): downloadTimes(0),
        userData(NULL), downloader(NULL),
        hasDeleted(false)
    { }
};

/**
 * The download manager allows to download a list of files, only n files will be download at the same time
 */
class DownloadManager
{
private:
    /**
                 * The list of files to download
                 */
    list<DownloadManagerData *> l;

    /**
                 * List of downloads
                 */
    list<DownloadManagerData *> lDownloads;

    /**
                 * The maximum of downloads at the same time
                 */
    int nbDownloadsMax;
    /**
                 * The number of try before abandon
                 */
    int nbTry;

    void downloadFirst();
    void IPCDownloadDone(string source, string signal,
                         void* listener_data, void* sender_data);


public:
    /**
                 * @param _nbDownloadsMax, the maximum of download at the same time
                 * @param _nbTry the number of try before abandon
                 */
    DownloadManager(int _nbDownloadsMax = 3, int _nbTry = 3);
    ~DownloadManager();

    /**
                 * Add a file to download
                 * @param source, the source file (http:....)
                 * @param destination, the local destination (/tmp...)
                 * @param sig, the signal called when the download is done or failed (emission, source, data)
                 * @param sig_progress, the signal called when download progress updates (url, destination_file, dl_now, dl_total, data)
                 * @param userData, user data
                 */
    void add(string source, string destination,
             sigc::slot<void, string, string, void*> sig,
             sigc::slot<void, string, string, double, double, void*> sig_progress,
             void *userData);

    void add(string source, string destination);



    /**
                 * Delete a download. Stop the download if it is allready started.
                 * @param source, the source file (http:....)
                 * @param destination, the local destination (/tmp...)
                 * @param userData, user data
                 */
    void del(string source, string destination, void *userData);

    /**
                 * Clear the list
                 */
    void clear();
};

#endif
