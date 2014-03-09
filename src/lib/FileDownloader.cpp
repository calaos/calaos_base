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
#include <FileDownloader.h>

static Eina_Bool _complete_cb(void *data, int type, void *event)
{
    Ecore_Con_Event_Url_Complete *ev = reinterpret_cast<Ecore_Con_Event_Url_Complete *>(event);
    FileDownloader *fd = reinterpret_cast<FileDownloader *>(data);

    if (data == ecore_con_url_data_get(ev->url_con))
    {
        fd->completeCb(ev->status);
    }

    return ECORE_CALLBACK_RENEW;
}

static Eina_Bool _progress_cb(void *data, int type, void *event)
{
    Ecore_Con_Event_Url_Progress *ev = reinterpret_cast<Ecore_Con_Event_Url_Progress *>(event);
    FileDownloader *fd = reinterpret_cast<FileDownloader *>(data);

    if (data == ecore_con_url_data_get(ev->url_con))
    {
        fd->progressCb(ev->down.now, ev->down.total);
    }

    return ECORE_CALLBACK_RENEW;
}

FileDownloader::FileDownloader(string _url, string _dest, bool auto_del):
    url_con(NULL),
    dl_file(NULL),
    auto_destroy(auto_del),
    idler(NULL),
    url(_url),
    dest(_dest),
    user_data(NULL)
{
    ecore_con_url_init();

    complete_handler = ecore_event_handler_add(ECORE_CON_EVENT_URL_COMPLETE, _complete_cb, this);
    progress_handler = ecore_event_handler_add(ECORE_CON_EVENT_URL_PROGRESS, _progress_cb, this);
}

FileDownloader::FileDownloader(string _url, string _dest, string _postData, string _postContentType, bool auto_del):
    url_con(NULL),
    dl_file(NULL),
    auto_destroy(auto_del),
    idler(NULL),
    url(_url),
    dest(_dest),
    postData(_postData),
    postContentType(_postContentType),
    user_data(NULL)
{
    ecore_con_url_init();

    complete_handler = ecore_event_handler_add(ECORE_CON_EVENT_URL_COMPLETE, _complete_cb, this);
    progress_handler = ecore_event_handler_add(ECORE_CON_EVENT_URL_PROGRESS, _progress_cb, this);
}

FileDownloader::FileDownloader(string _url, string _postData, string _postContentType, bool auto_del):
    url_con(NULL),
    dl_file(NULL),
    auto_destroy(auto_del),
    idler(NULL),
    url(_url),
    postData(_postData),
    postContentType(_postContentType),
    user_data(NULL)
{
    ecore_con_url_init();

    complete_handler = ecore_event_handler_add(ECORE_CON_EVENT_URL_COMPLETE, _complete_cb, this);
    progress_handler = ecore_event_handler_add(ECORE_CON_EVENT_URL_PROGRESS, _progress_cb, this);
}

FileDownloader::~FileDownloader()
{
    cb_connection.disconnect();
    cb_connection_user.disconnect();

    ecore_event_handler_del(complete_handler);
    ecore_event_handler_del(progress_handler);

    Cancel();

    ecore_con_url_shutdown();
}

Eina_Bool _delete_downloader_idler_cb(void *data)
{
    FileDownloader *downloader = reinterpret_cast<FileDownloader *>(data);
    if (!downloader) return ECORE_CALLBACK_CANCEL;

    delete downloader;

    //delete the ecore_idler
    return ECORE_CALLBACK_CANCEL;
}

void FileDownloader::Destroy()
{
    if (!idler)
        idler = ecore_idler_add(_delete_downloader_idler_cb, this);
}

bool FileDownloader::Start()
{
    cDebugDom("downloader") << "Start download (" << url << ")";

    if (url_con)
    {
        cWarningDom("downloader") << "A download is already in progress...";

        return false;
    }

    url_con = ecore_con_url_new(url.c_str());
    if (!url_con)
    {
        string err = "Failed to create Ecore_Con_Url";

        IPC::Instance().SendEvent("downloader::" + Utils::to_string(this),
                                  "failed",
                                  IPCData(new string(err), new DeletorT<string *>()),
                                  true);

        cb_signal.emit("failed", &err);
        cb_signal_user.emit("failed", &err, user_data);

        cErrorDom("downloader") << "Download failed: " << err;

        return false;
    }

    if (dest.empty())
    {
        //Create a temporary file for download
        int cpt = 0;

        //Get a temporary filename
        do
        {
            tmpFile = "/tmp/calaos" + Utils::to_string(getpid()) + "_download_tmp_";
            tmpFile += Utils::to_string(cpt);
            cpt++;
        }
        while (ecore_file_exists(tmpFile.c_str()));

        dl_file = fopen(tmpFile.c_str(), "wb");
    }
    else
    {
        dl_file = fopen(dest.c_str(), "wb");
    }

    if (!dl_file)
    {
        string err = "Failed to open file";

        IPC::Instance().SendEvent("downloader::" + Utils::to_string(this),
                                  "failed",
                                  IPCData(new string(err), new DeletorT<string *>()),
                                  true);

        cb_signal.emit("failed", &err);
        cb_signal_user.emit("failed", &err, user_data);

        cErrorDom("downloader") << "Download failed: " << err;

        ecore_con_url_free(url_con);
        url_con = NULL;

        return false;
    }

    ecore_con_url_fd_set(url_con, fileno(dl_file));
    ecore_con_url_data_set(url_con, this);
    ecore_con_url_ssl_verify_peer_set(url_con, false);

    bool ret = false;
    if (postData.empty())
    {
        ret = ecore_con_url_get(url_con);
    }
    else
    {
        ret = ecore_con_url_post(url_con, postData.c_str(), postData.length(), postContentType.c_str());
    }

    if (!ret)
    {
        string err = "Failed to call GET/POST";

        IPC::Instance().SendEvent("downloader::" + Utils::to_string(this),
                                  "failed",
                                  IPCData(new string(err), new DeletorT<string *>()),
                                  true);

        cb_signal.emit("failed", &err);
        cb_signal_user.emit("failed", &err, user_data);

        cErrorDom("downloader") << "Download failed: " << err;

        ecore_con_url_free(url_con);
        url_con = NULL;
        fclose(dl_file);

        if (dest.empty())
            ecore_file_unlink(tmpFile.c_str());

        return false;
    }

    return true;
}

void FileDownloader::Cancel()
{
    if (!url_con)
        return;

    ecore_con_url_free(url_con);
    url_con = NULL;
    fclose(dl_file);

    if (dest.empty())
        ecore_file_unlink(tmpFile.c_str());

    cDebugDom("downloader") << "Download aborted! (" << url << ")";

    IPC::Instance().SendEvent("downloader::" + Utils::to_string(this),
                              "aborted",
                              IPCData(new string(url), new DeletorT<string *>()),
                              true);

    cb_signal.emit("aborted", &url);
    cb_signal_user.emit("aborted", &url, user_data);

    if (auto_destroy)
    {
        Destroy();
    }
}

void FileDownloader::progressCb(double dlnow, double dltotal)
{
    IPC::Instance().SendEvent("downloader::" + Utils::to_string(this),
                              "progress,update",
                              IPCData(new FileProgress(dlnow, dltotal), new DeletorT<FileProgress *>()),
                              true);

    FileProgress fp(dlnow, dltotal);
    cb_signal.emit("progress,update", &fp);
    cb_signal_user.emit("progress,update", &fp, user_data);
}

void FileDownloader::completeCb(int status)
{
    ecore_con_url_free(url_con);
    url_con = NULL;

    fclose(dl_file);

    if (status >= 400 || status < 100)
    {
        string err = "Error code : " + Utils::to_string(status);

        IPC::Instance().SendEvent("downloader::" + Utils::to_string(this),
                                  "failed",
                                  IPCData(new string(err), new DeletorT<string *>()),
                                  true);

        cb_signal.emit("failed", &err);
        cb_signal_user.emit("failed", &err, user_data);

        cErrorDom("downloader") << "Download failed: " << err;

        if (dest.empty())
            ecore_file_unlink(tmpFile.c_str());

        return;
    }

    cDebugDom("downloader") << "Download done. (" << url << ")";

    if (!dest.empty())
    {
        IPC::Instance().SendEvent("downloader::" + Utils::to_string(this),
                                  "done",
                                  IPCData(new string(dest), new DeletorT<string *>()),
                                  true);

        cb_signal.emit("done", &dest);
        cb_signal_user.emit("done", &dest, user_data);
    }
    else
    {
        Buffer_CURL buff;

        dl_file = fopen(tmpFile.c_str(), "rb");
        fseek(dl_file, 0, SEEK_END);
        buff.bufsize = ftell(dl_file);
        fseek(dl_file, 0, SEEK_SET);
        buff.buffer = (char *)malloc(buff.bufsize);
        if (fread(buff.buffer, buff.bufsize, 1, dl_file) <= 0)
            cCriticalDom("downloader") << "FileDownloader: fread failed ! (" << url << ")";
        fclose(dl_file);

        ecore_file_unlink(tmpFile.c_str());

        IPC::Instance().SendEvent("downloader::" + Utils::to_string(this),
                                  "done",
                                  IPCData(new Buffer_CURL(buff), new DeletorT<Buffer_CURL *>()),
                                  true);

        cb_signal.emit("done", &buff);
        cb_signal_user.emit("done", &buff, user_data);

        if (buff.buffer)
            free(buff.buffer);
    }

    if (auto_destroy)
    {
        Destroy();
    }
}
