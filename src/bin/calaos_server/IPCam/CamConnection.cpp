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
#include <CamConnection.h>
#include <NTPClock.h>

using namespace Calaos;

static int _CamConnection_CURL_write_callback(void *buffer, size_t size, size_t nmemb, void *stream);

CamConnection::CamConnection(TCPSocket s): socket(s), end_conn(false), login(false),
    pict_buffer(NULL), pict_size(0)
{
    cDebugDom("network");
}

CamConnection::~CamConnection()
{
    cDebugDom("network");
    if (pict_buffer)
    {
        free(pict_buffer);
        pict_buffer = NULL;
    }
}

void CamConnection::ProcessRequest(string &request)
{
    vector<string> list;
    bool streaming = true;
    bool header_sent = false;

    split(request, list);

    if (list.size() < 3)
    {
        cErrorDom("network") << "request too small.";
        return;
    }

    if (list[0] != "GET")
    {
        cErrorDom("network") << "Only GET request supported!";
        return;
    }

    if (list[1].find ("/GetCamera.cgi?id=", 0) == request.npos &&
        list[1].find ("/GetPicture.cgi?id=", 0) == request.npos)
    {
        cErrorDom("network") << "Wrong request! : " << list[1];
        return;
    }

    if (list[1].find ("/GetPicture.cgi?id=", 0) != request.npos)
        streaming = false;

    string _camid = list[1];
    if (streaming)
        _camid.erase(0, 18);
    else
        _camid.erase(0, 19);
    int camid;
    Utils::from_string(_camid, camid);

    if (_camid == "" || camid < 0 || camid > CamManager::Instance().get_size() - 1)
    {
        cErrorDom("network") << "Wrong id!";
        return;
    }

    IPCam *camera = CamManager::Instance().get_camera(camid);
    string url = camera->getPictureUrl();

    stringstream headers;

    if (streaming)
    {
        headers << "HTTP/1.0 200 OK\r\n";
        headers << "Server: Calaos/1.0\r\n";
        headers << "Connection: close\r\n";
        headers << "Content-Type: multipart/x-mixed-replace;boundary=CalaosBoundary\r\n";
        headers << "Pragma: no-cache\r\n";
        headers << "Cache-Control: no-cache\r\n";
        headers << "Expires: 01 Jan 1970 00:00:00 GMT\r\n";
        headers << "\r\n";
    }

    unsigned long int cpt = 0;

    CURL *curl;
    CURLcode res;
    curl = curl_easy_init();

    while (!end_conn)
    {
        //Get a picture from the camera
        bool ret;

        string _url = url;// + "?dummy=" + Utils::to_string(cpt); //force the webserver to reload the picture

        //clear old frame
        if (pict_buffer)
        {
            free(pict_buffer);
            pict_buffer = NULL;
            pict_size = 0;
        }

        if(curl)
        {
            curl_easy_setopt(curl, CURLOPT_URL, _url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _CamConnection_CURL_write_callback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)this);
            /* some servers don't like requests that are made without a user-agent
                                field, so we provide one */
            if (camera->get_param("model") == "ICA-210")
                curl_easy_setopt(curl, CURLOPT_USERAGENT, "");
            else
                curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
            curl_easy_setopt(curl, CURLOPT_ENCODING, "identity");
            curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 0);
            curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 0);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);

            //timeout for request
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 4); //4 seconds timeout
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);

            res = curl_easy_perform(curl);
            if (res != CURLE_OK) break;
        }
        else
            break;

        if (!header_sent)
        {
            //send headers informations
            socket.Send(headers.str());

            header_sent = true;
        }

        //Some camera send empty picture, so to avoid a gstreamer crash, jump the frame
        if (pict_size <= 0) continue;

        unsigned char *buf = (unsigned char *)pict_buffer;

        if (buf[0] != 0xFF || buf[1] != 0xD8 ||
            buf[2] != 0xFF)
        {
            cDebug() << "Error in SOI !!";
            printf("%02X%02X%02X%02X\n", buf[0], buf[1], buf[2], buf[3]);

            continue;
        }

        if (streaming) headers.str("\r\n");
        if (streaming) headers << "--CalaosBoundary\r\n";
        if (streaming) headers << "Content-Type: image/jpeg\r\n";
        if (streaming) headers << "\r\n" << endl;

        if (!streaming)
        {
            headers << "HTTP/1.0 200 OK\r\n";
            headers << "Date: Sat, 01 Feb 2003 12:01:22 GMT\r\n";
            headers << "Server: Calaos/1.0\r\n";
            headers << "Connection: close\r\n";
            headers << "Content-Type: image/jpeg\r\n";
            headers << "Content-Length: " << pict_size << "\r\n";
            headers << "Pragma: no-cache\r\n";
            headers << "Cache-Control: no-cache\r\n";
            headers << "Expires: 01 Jan 1970 00:00:00 GMT\r\n";
            headers << "\r\n";
        }

        //send MIME header
        ret = socket.Send(headers.str());

        //send picture
        if (socket.Send(pict_buffer, pict_size) < 0) ret = false;

        cpt++;
        if (!ret) end_conn = true;

        if (!streaming) end_conn = true;
    }

    curl_easy_cleanup(curl);
}

void CamConnection::ThreadProc()
{
    quit = false;

    std::string request = "";
    char buffer[4096];
    int status = -1;

    while (request.find ("\n", 0 ) == request.npos ||
           request.find ("\r", 0 ) == request.npos ||
           request.find ("\0", 0 ) == request.npos)
    {
        memset(buffer, 0, 4096);
        status = socket.Recv(&buffer, 4096);

        if (status == -1)
        {
            cDebugDom("network") << "Connection ended...";
            break;
        }
        else if ( status == 0 )
            break;
        else
            request += buffer;
    }

    vector<string> headers;
    split(request, headers, "\n\r");

    if (status <= 0)
        quit = true;
    else
    {
        for (uint k = 0;k < headers.size();k++)
        {
            string s = headers[k];
            int i = s.length() - 1;
            while ((s[i] == '\n' || s[i] == '\r' || s[i] == '\0') && i >= 0) i--;
            headers[k] = s;
        }

        if (headers.size() > 0)
            ProcessRequest(headers[0]);
    }

    socket.InboundClose();
    end_conn = true;
    cDebugDom("network") << "Closing remote connexion !";
}

void CamConnection::Clean()
{
    socket.InboundClose();
}

//LibCURL callback
int _CamConnection_CURL_write_callback(void *buffer, size_t size, size_t nmemb, void *data)
{
    CamConnection *con = (CamConnection *)data;
    char *wbuffer = con->get_buffer();
    unsigned long int wsize = con->get_size();

    if (!wbuffer)
    {
        wbuffer = (char *)malloc(size * nmemb);
        memcpy(wbuffer, buffer, size * nmemb);
        wsize += size * nmemb;
        con->set_buffer(wbuffer);
    }
    else
    {
        wbuffer = (char *)realloc(wbuffer, wsize + (size * nmemb));
        memcpy(wbuffer + wsize, buffer, size * nmemb);
        wsize += size * nmemb;
        con->set_buffer(wbuffer);
    }

    con->set_size(wsize);
    return nmemb;
}
