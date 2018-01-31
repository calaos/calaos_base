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
#ifndef CALAOS_URL_UTILS_H
#define CALAOS_URL_UTILS_H

#include <stdint.h>
#include <Utils.h>

namespace uvw {
//Forward declare classes here to prevent long build time
//because of uvw.hpp being header only
class ProcessHandle;
class PipeHandle;
}

//Url Downloader class
class UrlDownloader: public sigc::trackable
{
private:
    enum RequestType {HTTP_GET, HTTP_PUT, HTTP_POST, HTTP_DELETE};

    std::shared_ptr<uvw::ProcessHandle> exeCurl;
    std::shared_ptr<uvw::PipeHandle> pipe;

    RequestType m_requestType = HTTP_POST;

    string tempFilename;
    string tmpHeader;
    int statusCode = 0; //http status code

    bool m_auth = false;
    string m_user;
    string m_password;

    // Url to contact
    string m_url = "";
    // Destination where to store the result
    string m_destination = "";
    // Message Body
    string m_bodyData = "";
    // Content type to be send
    string m_postContentType = "";

    //data downloaded when no destination file is set
    string m_downloadedData;

    bool isStarted = false;
    bool hasFailedStarting = false;

    //Common function for starting download of url
    bool start();

    bool m_autodelete;
    bool downloadToFile = false;
    vector<string> headersRequest; //header for the request

    void completeCb(int status);
    void dataCb(const char *data, int size);

public:
    // Constructor
    UrlDownloader(string url, bool autodelete = false);

    // Setter for private members
    void bodyDataSet(string bodyData) {m_bodyData = bodyData;}
    void setHeader(string header, string value);
    void destinationSet(string destination) {m_destination = destination;}
    void authSet(string user, string password) {m_user = user; m_password = password; m_auth = true;}
    void authUnSet() {m_auth = false;}

    Params getResponseHeaders();

    bool httpDelete(string destination = "", string bodyData = "");
    bool httpGet(string destination = "", string bodyData = "");
    bool httpPost(string destination = "", string bodyData = "");
    bool httpPut(string destination = "", string bodyData = "");

    ~UrlDownloader();

    // Signals/Slots
    sigc::signal<void, int> m_signalComplete;
    sigc::signal<void, const string &, int> m_signalCompleteData;
    sigc::signal<void, int, const char *> m_signalData;

    void Destroy();
};

#endif
