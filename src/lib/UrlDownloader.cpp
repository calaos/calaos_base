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
#include <Timer.h>
#include "uvw/src/uvw.hpp"

UrlDownloader::UrlDownloader(string url, bool autodelete) :
    m_url(url),
    m_autodelete(autodelete)
{
    exeCurl = uvw::Loop::getDefault()->resource<uvw::ProcessHandle>();
}

UrlDownloader::~UrlDownloader()
{
    if (exeCurl && exeCurl->active())
        exeCurl->kill(SIGTERM);
    exeCurl->close();

    FileUtils::unlink(tempFilename);
    FileUtils::unlink(tmpHeader);
}

bool UrlDownloader::start()
{
    if (exeCurl->active())
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

    string req = "curl --silent --insecure --location --dump-header " + tmpHeader + " ";

    switch (m_requestType)
    {
    case HTTP_POST:
        req += "--request POST ";
    case HTTP_GET:
        req += "--request GET ";
        break;
    case HTTP_PUT:
        req += "--request PUT ";
        break;
    case HTTP_DELETE:
        req += "--request DELETE ";
        break;
    default:
        cErrorDom("urlutils") << "Request type error, you should not be there !";
        return false;
    }

    //output to a file
    if (!m_destination.empty())
    {
        req += "--output " + m_destination + " ";
        downloadToFile = true;
    }
    else
        downloadToFile = false;
    m_downloadedData.clear();

    if (m_auth)
    {
        req += "--anyauth --user " + m_user;
        if (!m_password.empty())
            req += ":" + m_password;
        req += " ";
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

        req += "--data-binary @" + tempFilename + " ";
    }

    cDebugDom("urlutils") << "Executing command: " << req;
    exeCurl = uvw::Loop::getDefault()->resource<uvw::ProcessHandle>();
    exeCurl->once<uvw::ExitEvent>([this](const uvw::ExitEvent &ev, auto &h)
    {
        h.close();
        this->completeCb(ev.exitStatus);
    });
    exeCurl->once<uvw::ErrorEvent>([this](const uvw::ErrorEvent &ev, auto &h)
    {
        cDebugDom("urlutils") << "Process error: " << ev.what();
        h.close();
        this->completeCb(255);
    });

    if (!downloadToFile)
    {
        std::shared_ptr<uvw::PipeHandle> pipe = uvw::Loop::getDefault()->resource<uvw::PipeHandle>();
        exeCurl->stdio(uvw::ProcessHandle::StdIO::IGNORE_STREAM, static_cast<uvw::FileHandle>(0));

        uv_stdio_flags f = (uv_stdio_flags)(UV_CREATE_PIPE | UV_READABLE_PIPE);
        uvw::Flags<uvw::ProcessHandle::StdIO> ff(f);
        exeCurl->stdio(ff, *pipe);
        pipe->read();

        //When pipe is closed, remove it and close it
        pipe->on<uvw::EndEvent>([](const uvw::EndEvent &, auto &cl) { cl.close(); });
        pipe->on<uvw::DataEvent>([this](uvw::DataEvent &ev, auto &)
        {
            this->dataCb(ev.data.get(), ev.length);
        });
    }

    Utils::CStrArray arr(req);
    exeCurl->spawn(arr.at(0), arr.data());

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
