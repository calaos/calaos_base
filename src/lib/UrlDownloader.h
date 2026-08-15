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
#ifndef CALAOS_URL_UTILS_H
#define CALAOS_URL_UTILS_H

#include <stdint.h>
#include <Utils.h>

class UrlDownloaderCurlConn;
class UrlDownloaderCurlManager;

//Url Downloader class
//
//Transport: libcurl multi interface driven by the libuv loop (T2.5), no
//subprocess, no worker thread, no temporary file.
//
//Lifecycle contract (T2.10):
// - cancel() interrupts a transfer at any point: the libcurl transfer is
//   aborted (the connection drops), every signal is disconnected and no
//   callback fires afterwards. A non-autodelete object is then safe to delete
//   (or to keep around). An autodelete object frees itself after cancel().
// - An autodelete object must NEVER be deleted from outside: it destroys
//   itself (through an Idler) once the transfer completes or is cancelled, an
//   external delete would race that. cancel() is the only external control.
// - Deleting a non-autodelete object mid-transfer is safe: the destructor
//   detaches the in-flight transfer (alive-token) before returning, so
//   nothing can call back into freed memory.
class UrlDownloader: public sigc::trackable
{
private:
    enum RequestType {HTTP_GET, HTTP_PUT, HTTP_POST, HTTP_DELETE};

    /* In-flight libcurl transfer, owned by UrlDownloaderCurlManager. nullptr
     * when no transfer is running. Once handed back to the manager through
     * abortTransfer() (cancel/destroy) it must never be touched again: the
     * manager outlives it and disposes of it. */
    UrlDownloaderCurlConn *m_conn = nullptr;

    /* Alive token for async callbacks: the transfer holds a weak_ptr and
     * never touches this object once it expired (same pattern as
     * JsonApiHandlerHttp::handlerAlive). The destructor expires it first
     * thing, before detaching the transfer. */
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    RequestType m_requestType = HTTP_POST;

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

    //raw response headers of the last transfer (all blocks when redirected),
    //parsed by getResponseHeaders()
    string m_headerData;

    bool m_isRunning = false;
    bool m_cancelled = false;
    bool destroyScheduled = false;

    /* Cap on the internally accumulated response body (m_downloadedData).
     * Streaming consumers (MJPEG) get every byte live through m_signalData,
     * but the internal copy kept for m_signalCompleteData stops growing at
     * this bound so an endless stream cannot eat the RAM. */
    size_t m_bufferMaxSize = defaultBufferMaxSize;

    //Common function for starting download of url
    bool start();

    //Detach and abort the in-flight libcurl transfer, if any (the manager
    //disposes of it, guarded by the alive token)
    void closeHandles();

    bool m_autodelete;
    bool downloadToFile = false;
    vector<string> headersRequest; //header for the request

    void completeCb();
    void dataCb(const char *data, int size);

    //Called by the manager when the transfer reached its end (success or
    //error), after all libcurl state has been disposed of.
    void transferDone(int code, std::string responseHeaders);

    friend class UrlDownloaderCurlManager;

public:
    static constexpr size_t defaultBufferMaxSize = 16 * 1024 * 1024;

    // Constructor
    UrlDownloader(string url, bool autodelete = false);

    // Setter for private members
    void bodyDataSet(string bodyData) {m_bodyData = bodyData;}
    void setHeader(string header, string value);
    void destinationSet(string destination) {m_destination = destination;}
    void authSet(string user, string password) {m_user = user; m_password = password; m_auth = true;}
    void authUnSet() {m_auth = false;}

    bool isRunning() { return m_isRunning; }

    /* Interrupts the transfer: aborts the libcurl transfer (the connection
     * drops), disconnects every signal. No callback fires after this returns.
     * Autodelete objects free themselves, non-autodelete ones become inert (a
     * cancelled object cannot be restarted) and safe to delete. Idempotent. */
    void cancel();

    bool isCancelled() const { return m_cancelled; }

    /* Bound for the internally accumulated response data handed to
     * m_signalCompleteData. m_signalData always receives the full stream. */
    void bufferMaxSizeSet(size_t max) { m_bufferMaxSize = max; }
    size_t bufferMaxSizeGet() const { return m_bufferMaxSize; }

    Params getResponseHeaders();

    bool httpDelete(string destination = "", string bodyData = "");
    bool httpGet(string destination = "", string bodyData = "");
    bool httpPost(string destination = "", string bodyData = "");
    bool httpPut(string destination = "", string bodyData = "");

    static void get(string url, string get_data = "");
    static void post(string url, string post_data = "");

    ~UrlDownloader();

    // Signals/Slots
    sigc::signal<void, int> m_signalComplete;
    sigc::signal<void, const string &, int> m_signalCompleteData;
    sigc::signal<void, int, const char *> m_signalData;

    void Destroy();
};

#endif
