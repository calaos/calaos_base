/******************************************************************************
 **  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
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
//-----------------------------------------------------------------------------

#include "DownloadManager.h"

DownloadManager::DownloadManager(int _nbDownloadsMax, int _nbTry):
        nbDownloadsMax(_nbDownloadsMax), nbTry(_nbTry)
{
}


void DownloadManager::add(string source, string destination,
                sigc::slot<void, string, string, void*> sig,
                sigc::slot<void, string, string, double, double, void*> sig_progress,
                void *userData)
{
        //check if download isn't already running
        list<DownloadManagerData *>::iterator it;
        for (it = lDownloads.begin();it != lDownloads.end();it++)
        {
                if (source == (*it)->source &&
                    destination == (*it)->destination)
                {
                        return;
                }
        }

        DownloadManagerData *data = new DownloadManagerData();
        data->source = source;
        data->destination = destination;
        data->userData = userData;
        data->sigUser.connect(sig);
        data->sigProgressUpdate.connect(sig_progress);
        data->sigDownload.connect(sigc::mem_fun(*this, &DownloadManager::IPCDownloadDone));
        l.push_back(data);

        downloadFirst();
}

void DownloadManager::add(string source, string destination)
{
        //check if download isn't already running
        list<DownloadManagerData *>::iterator it;
        for (it = lDownloads.begin();it != lDownloads.end();it++)
        {
                if (source == (*it)->source &&
                    destination == (*it)->destination)
                {
                        return;
                }
        }

        DownloadManagerData *data = new DownloadManagerData();
        data->source = source;
        data->destination = destination;
        data->sigDownload.connect(sigc::mem_fun(*this, &DownloadManager::IPCDownloadDone));
        l.push_back(data);

        downloadFirst();
}

void DownloadManager::clear()
{
        for_each(l.begin(),l.end(),Delete());
        l.clear();

        list<DownloadManagerData *>::iterator it;
        for(it = lDownloads.begin();it != lDownloads.end();it++)
        {
                DownloadManagerData *data = *it;
                data->hasDeleted = true;

                //delete ipc handler to avoid a crash when cancelling download take too much time
                IPC::Instance().DeleteHandler(data->sigDownload);

                //Force threadmanager to destroy the thread
                data->downloader->Cancel();

                //Delete data object, it's not needed anymore
                delete data;
        }
        lDownloads.clear();
}

void DownloadManager::del(string source, string destination, void *userData)
{
        DownloadManagerData *item = NULL;
        list<DownloadManagerData *>::iterator it;
        for(it = lDownloads.begin();it != lDownloads.end();it++)
        {
                if ((*it)->source == source &&
                    (*it)->destination == destination &&
                    (*it)->userData == userData)
                {
                        item = (*it);
                        break;
                }
        }

        if (item)
        {
                lDownloads.erase(std::remove(lDownloads.begin(), lDownloads.end(), item), lDownloads.end());

                item->hasDeleted = true;

                //delete ipc handler to avoid a crash when cancelling download take too much time
                IPC::Instance().DeleteHandler(item->sigDownload);

                //Force threadmanager to destroy the thread
                item->downloader->Cancel();

                //Delete data object, it's not needed anymore
                delete item;

                return;
        }

        //Also check in the waiting list
        for(it = l.begin();it != l.end();it++)
        {
                if ((*it)->source == source &&
                    (*it)->destination == destination &&
                    (*it)->userData == userData)
                {
                        item = (*it);
                        break;
                }
        }

        if (item)
        {
                l.erase(std::remove(l.begin(), l.end(), item), l.end());

                item->hasDeleted = true;

                //delete ipc handler to avoid a crash when cancelling download take too much time
                IPC::Instance().DeleteHandler(item->sigDownload);

                //Delete data object, it's not needed anymore
                delete item;

                return;
        }

        downloadFirst();
}

void DownloadManager::downloadFirst()
{
        if(lDownloads.size() >= (uint)nbDownloadsMax || l.size() <= 0)
                return ;

        list<DownloadManagerData *>::iterator it = l.begin();
        DownloadManagerData *download = *it;
        l.erase(it);
        lDownloads.push_back(download);

        IPC::Instance().DeleteHandler(download->sigDownload);
        FileDownloader* downloader = new FileDownloader(download->source,
                        download->destination, true);
        IPC::Instance().AddHandler("downloader::" + Utils::to_string(downloader),
                        "*",download->sigDownload, download);
        download->downloadTimes = 1;
        download->downloader = downloader;
        downloader->Start();
}


void DownloadManager::IPCDownloadDone(string source, string signal,
                                void* listener_data, void* sender_data)
{
        DownloadManagerData *data = reinterpret_cast<DownloadManagerData*>(listener_data);
        list<DownloadManagerData *>::iterator it;

        if((signal == "done" || signal == "failed" || signal == "aborted")
                        && data->hasDeleted)
        {
                IPC::Instance().DeleteHandler(data->sigDownload);
                remove(data->destination.c_str());

                downloadFirst();

                delete data;
        }
        else if(signal == "done")
        {
                IPC::Instance().DeleteHandler(data->sigDownload);

                data->sigUser.emit("done",data->destination,data->userData);

                downloadFirst();

                for(it=lDownloads.begin();it!=lDownloads.end();it++)
                {
                        if((*it) == data)
                        {
                                lDownloads.erase(it);
                                break;
                        }
                }
                delete data;
        }
        else if(signal == "failed" && data->downloadTimes <= nbTry)
        {
                IPC::Instance().DeleteHandler(data->sigDownload);
                //re-try

                FileDownloader* downloader = new FileDownloader(data->source,
                                data->destination, true);
                IPC::Instance().AddHandler("downloader::" + Utils::to_string(downloader),
                                "*",data->sigDownload, data);
                data->downloader = downloader;
                downloader->Start();
        }
        else if(signal == "failed")
        {
                IPC::Instance().DeleteHandler(data->sigDownload);

                data->sigUser.emit("failed",data->destination,data->userData);

                downloadFirst();

                for(it=lDownloads.begin();it!=lDownloads.end();it++)
                {
                        if((*it) == data)
                        {
                                lDownloads.erase(it);
                                break;
                        }
                }

                delete data;
        }
        else if(signal == "progress,update")
        {
                FileProgress *progress = reinterpret_cast<FileProgress *>(sender_data);
                if (progress)
                {
                        if (progress->dltotal == 0.0) progress->dltotal = 1.0;
                        data->sigProgressUpdate.emit(data->source, data->destination, progress->dlnow, progress->dltotal, data->userData);
                }
        }
}

