/******************************************************************************
 **  Copyright (c) 2006-2017, Calaos. All Rights Reserved.
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
#include "uvw/src/uvw.hpp"

UrlDownloader::UrlDownloader(string url, bool autodelete) :
    m_url(url),
    m_autodelete(autodelete)
{
}

UrlDownloader::~UrlDownloader()
{
    if (exeCurl && exeCurl->referenced())
    {
        exeCurl->kill(SIGTERM);
        exeCurl->close();

        //Here is a workaround to keep a reference to the exeCurl until the CloseEvent comes.
        //This prevent a crash when exeCurl ref is deleted and the CloseEvent is called
        exeCurl->once<uvw::CloseEvent>([h = exeCurl](const uvw::CloseEvent &, auto &) { });
    }

    FileUtils::unlink(tempFilename);
    FileUtils::unlink(tmpHeader);
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

    if ((m_requestType == HTTP_POST ||
         m_requestType == HTTP_PUT) &&
        !m_bodyData.empty())
    {
        tempFilename = Utils::getTmpFilename();
        std::ofstream ofs;
        ofs.open(tempFilename, ios::out | ios::trunc | ios::binary);
        ofs << m_bodyData;
        ofs.close();

        req.push_back("--data-binary");
        req.push_back("@" + tempFilename);
    }

    exeCurl = uvw::Loop::getDefault()->resource<uvw::ProcessHandle>();
    exeCurl->once<uvw::ExitEvent>([this](const uvw::ExitEvent &ev, auto &h)
    {
        cDebugDom("urlutils") << "curl exited: " << ev.status;
        h.close();
        this->completeCb(ev.status);
    });
    exeCurl->once<uvw::ErrorEvent>([this](const uvw::ErrorEvent &ev, auto &h)
    {
        cDebugDom("urlutils") << "Process error: " << ev.what();
        h.close();
        if (downloadToFile)
        {
            pipe->stop();
            pipe->close();
        }
        this->completeCb(255);
    });

    if (!downloadToFile)
    {
        cDebugDom("urlutils") << "Setup stdio pipes";
        pipe = uvw::Loop::getDefault()->resource<uvw::PipeHandle>();
        exeCurl->stdio(static_cast<uvw::FileHandle>(0), uvw::ProcessHandle::StdIO::IGNORE_STREAM);

        uv_stdio_flags f = (uv_stdio_flags)(UV_CREATE_PIPE | UV_READABLE_PIPE);
        uvw::Flags<uvw::ProcessHandle::StdIO> ff(f);
        exeCurl->stdio(*pipe, ff);

        //When pipe is closed, remove it and close it
        pipe->once<uvw::EndEvent>([](const uvw::EndEvent &, auto &cl) { cl.close(); });
        pipe->on<uvw::DataEvent>([this](uvw::DataEvent &ev, auto &)
        {
            cDebugDom("urlutils") << "Stdio data received: " << ev.length;
            this->dataCb(ev.data.get(), ev.length);
        });
    }

    Utils::CStrArray arr(req);
    cDebugDom("urlutils") << "Executing command: " << arr.toString();
    exeCurl->spawn(arr.at(0), arr.data());

    if (!downloadToFile)
    {
        //The start of read() for the pipe has be to done _after_ spawning the process or it crashes
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

void UrlDownloader::completeCb(int status)
{
    getResponseHeaders();

    if (!downloadToFile)
        m_signalCompleteData.emit(m_downloadedData, statusCode);

    m_signalComplete.emit(statusCode);

    if (m_autodelete)
        Destroy();
}

void UrlDownloader::dataCb(const char *data, int size)
{
    m_downloadedData.append(data, size);
    m_signalData.emit(size, data);
}

void UrlDownloader::Destroy()
{
    cDebugDom("urlutils") << "Launch idler to destroy " << m_url;
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

    statusCode = 0;
    std::ifstream infile(tmpHeader);
    string line;
    bool first = true;
    while (std::getline(infile, line))
    {
        if (first)
        {
            first = false;
            if (Utils::strStartsWith(line, "HTTP/"))
            {
                vector<string> tok;
                Utils::split(line, tok, " ", 3);
                Utils::from_string(tok[1], statusCode);
            }
            continue;
        }

        vector<string> tok;
        Utils::split(line, tok, ":", 2);
        headers.Add(Utils::trim(tok[0]), Utils::trim(tok[1]));
    }

    return headers;
}

void UrlDownloader::setHeader(string header, string value)
{
    if (value.empty())
        headersRequest.push_back(header + ";");
    else
        headersRequest.push_back(header + ": " + value);
}
