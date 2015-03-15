/******************************************************************************
 **  Copyright (c) 2006-2015, Calaos. All Rights Reserved.
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
#include <UrlDownloader.h>

Eina_Bool _complete_cb(void *data, int type, void *event)
{
    Ecore_Con_Event_Url_Complete *ev = reinterpret_cast<Ecore_Con_Event_Url_Complete *>(event);
    UrlDownloader *fd = reinterpret_cast<UrlDownloader *>(data);

    if (data == ecore_con_url_data_get(ev->url_con))
    {
        fd->completeCb(ev->status);
    }

    return ECORE_CALLBACK_RENEW;
}

Eina_Bool _data_cb(void *data, int type, void *event)
{
    Ecore_Con_Event_Url_Data *ev = reinterpret_cast<Ecore_Con_Event_Url_Data *>(event);
    UrlDownloader *fd = reinterpret_cast<UrlDownloader *>(data);

    if (data == ecore_con_url_data_get(ev->url_con))
    {
        if (fd->m_downloadedData)
            eina_binbuf_append_length(fd->m_downloadedData, ev->data, ev->size);
        fd->dataCb(ev->data, ev->size);
    }

    return ECORE_CALLBACK_RENEW;
}

Eina_Bool _progress_cb(void *data, int type, void *event)
{
    Ecore_Con_Event_Url_Progress *ev = reinterpret_cast<Ecore_Con_Event_Url_Progress *>(event);
    UrlDownloader *fd = reinterpret_cast<UrlDownloader *>(data);

    if (data == ecore_con_url_data_get(ev->url_con))
    {
        fd->progressCb(ev->down.total, ev->down.now);
    }

    return ECORE_CALLBACK_RENEW;
}

UrlDownloader::UrlDownloader(string url, bool autodelete) :
    m_url(url),
    m_autodelete(autodelete)
{
    ecore_con_url_init();
    m_completeHandler = ecore_event_handler_add(ECORE_CON_EVENT_URL_COMPLETE, _complete_cb, this);
    m_dataHandler = ecore_event_handler_add(ECORE_CON_EVENT_URL_DATA, _data_cb, this);
    m_progressHandler = ecore_event_handler_add(ECORE_CON_EVENT_URL_PROGRESS, _progress_cb, this);
}

UrlDownloader::~UrlDownloader()
{

    if (m_downloadedData)
        eina_binbuf_free(m_downloadedData);

    if (m_file)
        fclose(m_file);

    ecore_con_url_free(m_urlCon);
    ecore_event_handler_del(m_completeHandler);
    ecore_event_handler_del(m_dataHandler);
    ecore_event_handler_del(m_progressHandler);

    ecore_con_url_shutdown();
}

bool UrlDownloader::start()
{
    if (m_urlCon)
    {
        cWarningDom("urlutils") << "A download is already in progress...";
        return false;
    }

    if (m_url.empty())
    {
        cWarningDom("urlutils") << "Url is empty";
        return false;
    }

    switch (m_requestType)
    {
    case HTTP_POST:
    case HTTP_GET:
        m_urlCon = ecore_con_url_new(m_url.c_str());
        break;
    case HTTP_PUT:
        m_urlCon = ecore_con_url_custom_new(m_url.c_str(), "PUT");
        break;
    case HTTP_DELETE:
        m_urlCon = ecore_con_url_custom_new(m_url.c_str(), "DELETE");
        break;
    default:
        cErrorDom("urlutils") << "Request type error, you should not be there !";
        if (m_file) fclose(m_file);
        m_file = NULL;
        return false;
    }

    if (!m_urlCon)
    {
        cErrorDom("urlutils") << "Download failed: Failed to create Ecore_Con_Url";
        return false;
    }


    if (m_fd >= 0)
    {
        ecore_con_url_fd_set(m_urlCon, m_fd);
    }
    else if (!m_destination.empty())
    {
        m_file = fopen(m_destination.c_str(), "wb");
        ecore_con_url_fd_set(m_urlCon, fileno(m_file));
    }
    else
    {
        m_downloadedData = eina_binbuf_new();
    }

    ecore_con_url_data_set(m_urlCon, this);
    ecore_con_url_ssl_verify_peer_set(m_urlCon, false);
    if (m_auth)
        ecore_con_url_httpauth_set(m_urlCon, m_user.c_str(), m_password.c_str(), EINA_FALSE);

    bool ret;
    switch (m_requestType)
    {
    case HTTP_PUT:
    case HTTP_DELETE:
    case HTTP_POST:
        ret = ecore_con_url_post(m_urlCon, m_bodyData.c_str(), m_bodyData.length(),
                                 m_postContentType.c_str());
        break;
    case HTTP_GET:
        ret = ecore_con_url_get(m_urlCon);
        break;

    default:
        cErrorDom("urlutils") << "Request type error, you should not be there !";
        ecore_con_url_free(m_urlCon);
        m_urlCon = NULL;
        if (m_file) fclose(m_file);
        m_file = NULL;
        return false;
    }

    if (!ret)
    {
        cErrorDom("urlutils") << "Download failed: Failed to call GET/POST";
        ecore_con_url_free(m_urlCon);
        m_urlCon = NULL;
        if (m_file) fclose(m_file);
        m_file = NULL;
        return false;
    }

    return true;
}

bool UrlDownloader::httpPost(string destination, string bodyData)
{
    if (!destination.empty())
        destinationSet(destination);
    if (!bodyData.empty())
        bodyDataSet(bodyData);
    m_requestType = HTTP_POST;
    return start();
}

bool UrlDownloader::httpGet(string destination, string bodyData)
{
    if (!destination.empty())
        destinationSet(destination);
    if (!bodyData.empty())
        bodyDataSet(bodyData);
    m_requestType = HTTP_GET;
    return start();
}

bool UrlDownloader::httpPut(string destination, string bodyData)
{
    if (!destination.empty())
        destinationSet(destination);
    if (!bodyData.empty())
        bodyDataSet(bodyData);
    m_requestType = HTTP_PUT;
    return start();
}

bool UrlDownloader::httpDelete(string destination, string bodyData)
{
    if (!destination.empty())
        destinationSet(destination);
    if (!bodyData.empty())
        bodyDataSet(bodyData);
    m_requestType = HTTP_DELETE;
    return start();
}

void UrlDownloader::completeCb(int status)
{
    ecore_con_url_free(m_urlCon);
    m_urlCon = nullptr;

    if (m_downloadedData)
        m_signalCompleteData.emit(m_downloadedData, status);

    if (m_file) fclose(m_file);
    m_file = NULL;
    m_signalComplete.emit(status);

    if (m_autodelete)
        Destroy();
}

void UrlDownloader::dataCb(unsigned char *data, int size)
{
    m_signalData.emit(size, data);
}

void UrlDownloader::progressCb(double now, double tot)
{
    m_signalProgress.emit(now, tot);
}

static Eina_Bool _delete_downloader_idler_cb(void *data)
{
    UrlDownloader *ud = reinterpret_cast<UrlDownloader *>(data);
    if (!ud) return ECORE_CALLBACK_CANCEL;

    delete ud;

    //delete the ecore_idler
    return ECORE_CALLBACK_CANCEL;
}

void UrlDownloader::Destroy()
{
    if (!m_idler)
        m_idler = ecore_idler_add(_delete_downloader_idler_cb, this);
}
