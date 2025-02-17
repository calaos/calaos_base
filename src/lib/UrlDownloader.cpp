/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
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
#include <Timer.h>
#include "libuvw.h"

UrlDownloader::UrlDownloader(string url, bool autodelete) :
    m_url(url),
    m_autodelete(autodelete)
{
    cInfoDom("urlutils") << "UrlDownloader: " << url;
}

UrlDownloader::~UrlDownloader()
{
    if (exeCurl && exeCurl->referenced())
    {
        exeCurl->kill(SIGTERM);
        exeCurl->close();
    }

    FileUtils::unlink(tempFilename);
    FileUtils::unlink(tmpHeader);

    cDebugDom("urlutils") << "UrlDownloader(" << this << ") destroyed";
}

bool UrlDownloader::start()
{
    if (exeCurl && exeCurl->active())
    {
        cWarningDom("urlutils") << "A download is already in progress...";
        return false;
    }

    if (m_url.empty())
    {
        cWarningDom("urlutils") << "Url is empty";
        return false;
    }

    m_isRunning = true;

    if (!tempFilename.empty())
    {
        FileUtils::unlink(tempFilename);
        tempFilename.clear();
    }
    if (!tmpHeader.empty())
    {
        FileUtils::unlink(tmpHeader);
        tmpHeader.clear();
    }
    tmpHeader = Utils::getTmpFilename("tmp", "_dlheader");

    //Default curl parameters
    //silent --> no progress bar
    //insecure --> do not check for insecure ssl certificates, needed for local https
    //location --> follow redirect

    vector<string> req;
    req.push_back("curl");
    req.push_back(m_url);
    req.push_back("--silent");
    req.push_back("--insecure");
    req.push_back("--location");
    req.push_back("--dump-header");
    req.push_back(tmpHeader);

    for (const string &s: headersRequest)
    {
        req.push_back("--header");
        req.push_back(s);
    }

    req.push_back("--request");

    switch (m_requestType)
    {
    case HTTP_POST:
        req.push_back("POST");
        break;
    case HTTP_GET:
        req.push_back("GET");
        break;
    case HTTP_PUT:
        req.push_back("PUT");
        break;
    case HTTP_DELETE:
        req.push_back("DELETE");
        break;
    default:
        cErrorDom("urlutils") << "Request type error, you should not be there !";
        return false;
    }

    //output to a file
    if (!m_destination.empty())
    {
        req.push_back("--output");
        req.push_back(m_destination);
        downloadToFile = true;
    }
    else
        downloadToFile = false;
    m_downloadedData.clear();

    if (m_auth)
    {
        req.push_back("--anyauth");
        req.push_back("--user");

        string u = m_user;
        if (!m_password.empty())
            u += ":" + m_password;
        req.push_back(u);
    }

    if (!m_bodyData.empty())
    {
        tempFilename = Utils::getTmpFilename();
        std::ofstream ofs;
        ofs.open(tempFilename, ios::out | ios::trunc | ios::binary);
        ofs << m_bodyData;
        ofs.close();

        req.push_back("--data-binary");
        req.push_back("@" + tempFilename);
    }

    isStarted = false;
    hasFailedStarting = false;

    exeCurl = uvw::Loop::getDefault()->resource<uvw::ProcessHandle>();
    exeCurl->once<uvw::ExitEvent>([this](const uvw::ExitEvent &ev, auto &h)
    {
        cDebugDom("urlutils") << "curl exited: " << ev.status;
        h.close();
        this->m_isRunning = false;
        this->completeCb();
    });
    exeCurl->once<uvw::ErrorEvent>([this](const uvw::ErrorEvent &ev, auto &h)
    {
        if (!isStarted) hasFailedStarting = true;
        cCriticalDom("urlutils") << "Process error: " << ev.what();
        h.close();
        if (!downloadToFile)
        {
            pipe->stop();
            pipe->close();
        }
        this->m_isRunning = false;
        this->completeCb();
    });

    if (!downloadToFile)
    {
        cDebugDom("urlutils") << "Setup stdio pipes";
        pipe = uvw::Loop::getDefault()->resource<uvw::PipeHandle>();
        exeCurl->stdio(static_cast<uvw::FileHandle>(0), uvw::ProcessHandle::StdIO::IGNORE_STREAM);

        uv_stdio_flags f = (uv_stdio_flags)(UV_CREATE_PIPE | UV_WRITABLE_PIPE);
        uvw::Flags<uvw::ProcessHandle::StdIO> ff(f);
        exeCurl->stdio(*pipe, ff);

        //When pipe is closed, remove it and close it
        pipe->once<uvw::EndEvent>([this](const uvw::EndEvent &, auto &cl)
        {
            this->pipeClosed = true;
            cl.close();
            this->completeCb();
        });
        pipe->once<uvw::ErrorEvent>([](const uvw::ErrorEvent &, auto &cl) { cl.stop(); });
        pipe->on<uvw::DataEvent>([this](uvw::DataEvent &ev, auto &)
        {
            cDebugDom("urlutils") << "UrlDownloader(" << this << ") Stdio data received: " << ev.length;
            this->dataCb(ev.data.get(), ev.length);
        });

        pipeClosed = false;
    }

    Utils::CStrArray arr(req);
    cDebugDom("urlutils") << "Executing command: " << arr.toString();
    exeCurl->spawn(arr.at(0), arr.data());

    if (!downloadToFile)
    {
        //The start of read() for the pipe has be to done _after_ spawning the process or it crashes
        if (!hasFailedStarting)
            pipe->read();
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

void UrlDownloader::completeCb()
{
    cDebugDom("urlutils") << "Finished with status code: " << statusCode;

    if (downloadToFile)
    {
        getResponseHeaders();

        m_signalComplete.emit(statusCode);

        if (m_autodelete)
            Destroy();
    }
    else
    {
        //we need to wait for pipe closing
        if (pipeClosed && !m_isRunning)
        {
            getResponseHeaders();

            cDebugDom("urlutils") << "Response data: " << m_downloadedData;

            m_signalCompleteData.emit(m_downloadedData, statusCode);
            m_signalComplete.emit(statusCode);

            if (m_autodelete)
                Destroy();
        }
    }
}

void UrlDownloader::dataCb(const char *data, int size)
{
    m_downloadedData.append(data, size);
    m_signalData.emit(size, data);
}

void UrlDownloader::Destroy()
{
    cDebugDom("urlutils") << "UrlDownloader(" << this << ") Launch idler to destroy " << m_url;
    Idler::singleIdler([=]() { delete this; });
}

Params UrlDownloader::getResponseHeaders()
{
    Params headers;

    //Parse headers
    /*
HTTP/1.1 404 Not Found
Server: nginx/1.10.2
Date: Tue, 17 Jan 2017 15:19:43 GMT
Content-Type: text/html
Content-Length: 169
     */

    //Can also be mutliple with redirect:
    /*
HTTP/2 302
date: Fri, 08 Feb 2019 12:13:15 GMT
content-type: text/plain; charset=utf-8
content-length: 0
access-control-allow-origin: *
cache-control: no-cache, no-store, must-revalidate
location: /640/480/?image=743
vary: Accept
x-beluga-cache-status: Miss
x-beluga-document: 134517132967552875205061749237587549195
x-beluga-node: 32
x-beluga-record: f5c531b4640ffd773650c0975b4c67a3a7a45f25
x-beluga-response-time: 155 ms
x-beluga-status: 000
x-beluga-trace: 05c27b98-df54-41ec-9914-fe921d868fc2
x-powered-by: Express
server: BelugaCDN/v2.44.11
x-beluga-response-time-x: 0.157 sec

HTTP/2 200
date: Fri, 08 Feb 2019 12:13:16 GMT
content-type: image/jpeg
content-length: 49219
access-control-allow-origin: *
cache-control: public, max-age=604800
etag: W/"BiWNpbjT/do55p+JUMKZhg=="
x-beluga-cache-status: Hit (1)
x-beluga-node: 32
x-beluga-record: 9e416b65adaa83757fb3d79be36ca6bf6011d202
x-beluga-response-time: 1 ms
x-beluga-status: 003
x-beluga-trace: 1c4887b9-db41-433d-9f2a-dc60b3f47b32
x-powered-by: Express
server: BelugaCDN/v2.44.11
x-beluga-response-time-x: 0.002 sec

     */

    statusCode = 0;
    std::ifstream infile(tmpHeader);
    string line;

    cDebugDom("urlutils") << "Response headers:";

    while (std::getline(infile, line))
    {
        cDebugDom("urlutils") << line;

        if (Utils::strStartsWith(line, "HTTP/"))
        {
            vector<string> tok;
            Utils::split(line, tok, " ", 3);
            Utils::from_string(tok[1], statusCode);
            headers.clear();
        }
        else
        {
            vector<string> tok;
            Utils::split(line, tok, ":", 2);
            headers.Add(Utils::trim(tok[0]), Utils::trim(tok[1]));
        }
    }

    return headers;
}

void UrlDownloader::get(string url, string get_data)
{
    UrlDownloader *downloader = new UrlDownloader(url, true);
    downloader->httpGet(string(),get_data);
}

void UrlDownloader::post(string url, string post_data)
{
    UrlDownloader *downloader = new UrlDownloader(url, true);
    downloader->httpPost(string(), post_data);
}

void UrlDownloader::setHeader(string header, string value)
{
    if (value.empty())
        headersRequest.push_back(header + ";");
    else
        headersRequest.push_back(header + ": " + value);
}
