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

#include <curl/curl.h>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

/* T2.5: libcurl-multi backend, driven by the libuv loop.
 *
 * UrlDownloader used to fork the `curl` binary and read its stdout through a
 * pipe, with two temporary files (response headers, request body). It now
 * uses the canonical curl_multi_socket_action integration: libcurl tells us
 * which sockets to watch (one uvw::PollHandle each) and when its next
 * internal timeout is (one shared uvw::TimerHandle); we feed the events back
 * with curl_multi_socket_action. No subprocess, no thread, no temp file.
 *
 * The CLI flags of the old backend map to easy options:
 *   --silent       -> CURLOPT_NOPROGRESS (libcurl default, nothing to do)
 *   --insecure     -> T2.17: no longer global. Certificates are verified by
 *                     default (libcurl defaults, system CA bundle); local
 *                     self-signed devices opt out per call with setInsecure()
 *   --location     -> CURLOPT_FOLLOWLOCATION (+ MAXREDIRS 50, the CLI default)
 *   --dump-header  -> CURLOPT_HEADERFUNCTION into an in-memory buffer
 *   --request X    -> CURLOPT_CUSTOMREQUEST
 *   --header       -> CURLOPT_HTTPHEADER
 *   --anyauth      -> CURLOPT_HTTPAUTH = CURLAUTH_ANY
 *   --user u:p     -> CURLOPT_USERPWD (no longer visible in ps/procfs)
 *   --data-binary @file -> CURLOPT_COPYPOSTFIELDS (no temp file)
 *   --output file  -> write callback streaming into the destination file
 * Proxy environment variables (http_proxy, ...) are honored by libcurl just
 * like the CLI did. CURLOPT_FORBID_REUSE keeps the one-connection-per-
 * transfer behavior of the old one-process-per-transfer backend, so no
 * cached connection can outlive a transfer (fd accounting stays flat).
 *
 * Lifecycle: each transfer lives in a UrlDownloaderCurlConn owned by the
 * manager, not by the UrlDownloader. cancel()/delete only *detach* the
 * connection (owner pointer cleared, abort requested); the manager then tears
 * it down either immediately or, when we sit inside a libcurl callback where
 * curl_multi_* calls are forbidden, from the write callback return value and
 * an Idler sweep. This is what makes cancel()/delete safe at any point,
 * including from within a m_signalData handler. */

class UrlDownloaderCurlConn
{
public:
    CURL *easy = nullptr;
    struct curl_slist *headerList = nullptr;

    /* Owner downloader. Cleared when the downloader cancels the transfer or
     * dies: the connection then only survives to be torn down. */
    UrlDownloader *owner = nullptr;
    std::weak_ptr<bool> ownerAlive;

    std::string headerBuf; //accumulated raw response headers (--dump-header)

    bool toFile = false;
    std::ofstream destFile;

    //Credential-masked URL for log statements (T2.17)
    std::string logUrl;

    /* Set when the transfer must die: write/header callbacks return an error
     * to make libcurl abort, and the manager sweeps the connection up. */
    bool abortRequested = false;
    //True while our write callback dispatches into user code
    bool inCallback = false;

    //Guards against pointer reuse in deferred sweeps
    uint64_t serial = 0;

    char errorBuf[CURL_ERROR_SIZE] = {0};
};

class UrlDownloaderCurlManager
{
public:
    static UrlDownloaderCurlManager &instance()
    {
        /* Intentionally leaked: handles tied to the default loop must not be
         * destroyed from a static destructor after the loop is gone. */
        static UrlDownloaderCurlManager *mgr = new UrlDownloaderCurlManager();
        return *mgr;
    }

    //Register the connection and hand its easy handle to libcurl.
    //On failure the connection is disposed of and false is returned.
    bool startTransfer(UrlDownloaderCurlConn *conn)
    {
        conn->serial = ++serialCounter;
        live.insert(conn);

        if (dispatchDepth > 0)
        {
            /* We're inside a libcurl callback (a m_signalData consumer starts
             * a new download): curl_multi_add_handle is forbidden here, defer
             * it to the next loop iteration. */
            uint64_t serial = conn->serial;
            Idler::singleIdler([this, conn, serial]()
            {
                if (!isLive(conn, serial))
                    return; //cancelled before the add even happened
                if (!addNow(conn))
                {
                    //Report the failure like a failed transfer
                    UrlDownloader *owner = conn->owner;
                    auto aliveToken = conn->ownerAlive;
                    disposeConn(conn);
                    if (owner && !aliveToken.expired())
                        owner->transferDone(0, std::string());
                }
            });
            return true;
        }

        if (!addNow(conn))
        {
            //Full disposal: easy handle, slist, destination file, conn
            disposeConn(conn);
            return false;
        }
        return true;
    }

    /* Detach the connection from its owner and abort the transfer. Safe to
     * call from anywhere, including from within our own write callback (the
     * owner is being cancelled or deleted from a m_signalData handler). The
     * UrlDownloader must forget the pointer right after this call. */
    void abortTransfer(UrlDownloaderCurlConn *conn)
    {
        if (!live.count(conn))
            return;

        conn->owner = nullptr;
        conn->ownerAlive.reset();
        conn->abortRequested = true;

        if (dispatchDepth > 0 || conn->inCallback)
        {
            /* Inside libcurl's stack: multi functions are off-limits. The
             * callbacks now return errors (abortRequested), which makes
             * libcurl fail the transfer; the DONE handling tears it down.
             * The Idler below is a safety net in case no callback runs
             * anymore for that connection (e.g. a stalled server). */
            uint64_t serial = conn->serial;
            Idler::singleIdler([this, conn, serial]()
            {
                if (isLive(conn, serial))
                    disposeConn(conn);
            });
            return;
        }

        disposeConn(conn);
    }

    //Shim for the C write callback: relays body data to the downloader's
    //private dataCb (this class is a friend, the free callback is not)
    static void dispatchData(UrlDownloader *owner, const char *data, int size)
    {
        owner->dataCb(data, size);
    }

private:
    UrlDownloaderCurlManager()
    {
        curl_global_init(CURL_GLOBAL_ALL); //refcounted, libquickmail also calls it

        multi = curl_multi_init();
        curl_multi_setopt(multi, CURLMOPT_SOCKETFUNCTION, sSocketCb);
        curl_multi_setopt(multi, CURLMOPT_SOCKETDATA, this);
        curl_multi_setopt(multi, CURLMOPT_TIMERFUNCTION, sTimerCb);
        curl_multi_setopt(multi, CURLMOPT_TIMERDATA, this);

        timer = uvw::Loop::getDefault()->resource<uvw::TimerHandle>();
        timer->on<uvw::TimerEvent>([this](const uvw::TimerEvent &, auto &)
        {
            int running = 0;
            dispatchDepth++;
            curl_multi_socket_action(multi, CURL_SOCKET_TIMEOUT, 0, &running);
            dispatchDepth--;
            checkMultiInfo();
        });
    }

    //Per-socket context handed to libcurl through curl_multi_assign()
    struct SockCtx
    {
        std::shared_ptr<uvw::PollHandle> poll;
    };

    bool isLive(UrlDownloaderCurlConn *conn, uint64_t serial) const
    {
        return live.count(conn) && conn->serial == serial;
    }

    bool addNow(UrlDownloaderCurlConn *conn)
    {
        CURLMcode rc = curl_multi_add_handle(multi, conn->easy);
        if (rc != CURLM_OK)
        {
            cErrorDom("urlutils") << "curl_multi_add_handle failed: " << curl_multi_strerror(rc);
            return false;
        }
        return true;
    }

    //Tear a connection down completely (multi removal + easy cleanup).
    //Must not be called from inside a libcurl callback.
    void disposeConn(UrlDownloaderCurlConn *conn)
    {
        live.erase(conn);
        /* Harmless no-op (CURLM_BAD_EASY_HANDLE) when the easy handle was
         * never added to the multi, i.e. the add-failure paths. */
        curl_multi_remove_handle(multi, conn->easy);
        curl_easy_cleanup(conn->easy);
        if (conn->headerList)
            curl_slist_free_all(conn->headerList);
        if (conn->destFile.is_open())
            conn->destFile.close();
        delete conn;
    }

    void onSocketEvent(curl_socket_t fd, int flags)
    {
        int running = 0;
        dispatchDepth++;
        curl_multi_socket_action(multi, fd, flags, &running);
        dispatchDepth--;
        checkMultiInfo();
    }

    /* Collect finished transfers. Runs after curl_multi_socket_action
     * returned, i.e. outside any libcurl callback: all libcurl state of the
     * finished transfer is disposed of *before* user code runs, so completion
     * handlers can freely start new transfers, cancel or delete objects. */
    void checkMultiInfo()
    {
        CURLMsg *msg;
        int pending = 0;
        while ((msg = curl_multi_info_read(multi, &pending)))
        {
            if (msg->msg != CURLMSG_DONE)
                continue;

            CURL *easy = msg->easy_handle;
            CURLcode result = msg->data.result;

            UrlDownloaderCurlConn *conn = nullptr;
            curl_easy_getinfo(easy, CURLINFO_PRIVATE, &conn);

            long code = 0;
            curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &code);

            UrlDownloader *owner = conn->owner;
            auto aliveToken = conn->ownerAlive;
            std::string headers = std::move(conn->headerBuf);

            if (result != CURLE_OK && !conn->abortRequested)
            {
                cWarningDom("urlutils") << "Transfer failed for " << conn->logUrl << ": "
                                        << curl_easy_strerror(result)
                                        << (conn->errorBuf[0]? string(" (") + conn->errorBuf + ")": string());
                if (result == CURLE_PEER_FAILED_VERIFICATION)
                    cWarningDom("urlutils") << "TLS certificate verification failed: the remote certificate "
                                               "is not trusted by the system CA bundle (self-signed?). "
                                               "Certificates are now verified by default; local devices with "
                                               "self-signed certificates (IP cameras, Hue bridge) must use "
                                               "the insecure opt-in (UrlDownloader::setInsecure()).";
            }

            disposeConn(conn);

            if (owner && !aliveToken.expired())
                owner->transferDone(static_cast<int>(code), std::move(headers));
        }
    }

    static int sTimerCb(CURLM *, long timeoutMs, void *userp)
    {
        auto *self = static_cast<UrlDownloaderCurlManager *>(userp);
        if (timeoutMs < 0)
            self->timer->stop();
        else
            self->timer->start(uvw::TimerHandle::Time{static_cast<uint64_t>(timeoutMs)},
                               uvw::TimerHandle::Time{0});
        return 0;
    }

    static int sSocketCb(CURL *, curl_socket_t s, int what, void *userp, void *socketp)
    {
        auto *self = static_cast<UrlDownloaderCurlManager *>(userp);
        auto *ctx = static_cast<SockCtx *>(socketp);

        if (what == CURL_POLL_REMOVE)
        {
            if (ctx)
            {
                ctx->poll->stop();
                ctx->poll->close();
                curl_multi_assign(self->multi, s, nullptr);
                delete ctx;
            }
            return 0;
        }

        if (!ctx)
        {
            ctx = new SockCtx;
            ctx->poll = uvw::Loop::getDefault()->resource<uvw::PollHandle>(uvw::OSSocketHandle{s});
            curl_multi_assign(self->multi, s, ctx);

            curl_socket_t fd = s;
            ctx->poll->on<uvw::PollEvent>([self, fd](const uvw::PollEvent &ev, auto &)
            {
                int flags = 0;
                if (ev.flags & uvw::PollHandle::Event::READABLE)
                    flags |= CURL_CSELECT_IN;
                if (ev.flags & uvw::PollHandle::Event::WRITABLE)
                    flags |= CURL_CSELECT_OUT;
                self->onSocketEvent(fd, flags);
            });
            ctx->poll->on<uvw::ErrorEvent>([self, fd](const uvw::ErrorEvent &, auto &)
            {
                /* POLLERR. Typical on loopback: the peer closed with our
                 * request still unread (RST) while the response sits in the
                 * kernel buffer. Hand curl a readable event so it read()s the
                 * pending data and discovers the EOF/reset by itself, exactly
                 * like the curl CLI did; CURL_CSELECT_ERR would make it drop
                 * the buffered response and fail the transfer. */
                self->onSocketEvent(fd, CURL_CSELECT_IN);
            });
        }

        uvw::Flags<uvw::PollHandle::Event> events;
        if (what == CURL_POLL_IN)
            events = uvw::PollHandle::Event::READABLE;
        else if (what == CURL_POLL_OUT)
            events = uvw::PollHandle::Event::WRITABLE;
        else //CURL_POLL_INOUT
            events = uvw::Flags<uvw::PollHandle::Event>(uvw::PollHandle::Event::READABLE) |
                     uvw::PollHandle::Event::WRITABLE;
        ctx->poll->start(events);

        return 0;
    }

    CURLM *multi = nullptr;
    std::shared_ptr<uvw::TimerHandle> timer;

    //Connections currently owned by the manager, with a serial number to make
    //deferred sweeps immune to pointer reuse
    std::set<UrlDownloaderCurlConn *> live;
    uint64_t serialCounter = 0;

    /* >0 while curl_multi_socket_action runs (libcurl may call back into
     * user code from there): multi functions must then be deferred. */
    int dispatchDepth = 0;
};

//libcurl body callback: stream to destination file, or dispatch to the
//downloader (accumulation + m_signalData). Returning something != len makes
//libcurl abort the transfer with CURLE_WRITE_ERROR.
size_t urlDownloaderWriteCb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *conn = static_cast<UrlDownloaderCurlConn *>(userdata);
    size_t len = size * nmemb;

    if (conn->abortRequested)
        return len + 1; //cancelled/deleted: abort the transfer

    if (conn->toFile)
    {
        conn->destFile.write(ptr, len);
        if (!conn->destFile)
        {
            cErrorDom("urlutils") << "Write to destination file failed";
            return len + 1;
        }
        return len;
    }

    UrlDownloader *owner = conn->owner;
    if (!owner || conn->ownerAlive.expired())
        return len + 1; //nobody listens anymore

    /* The dispatch can call cancel() or delete the downloader: the manager
     * then only *marks* the connection (inCallback prevents an immediate
     * teardown under our feet), and we abort through the return value. Only
     * conn may be touched after the dispatch. */
    conn->inCallback = true;
    UrlDownloaderCurlManager::dispatchData(owner, ptr, static_cast<int>(len));
    conn->inCallback = false;

    if (conn->abortRequested)
        return len + 1;
    return len;
}

namespace
{

//libcurl header callback: accumulate the raw header lines of every response
//(redirects included), the equivalent of the old --dump-header temp file
size_t headerCb(char *buffer, size_t size, size_t nitems, void *userdata)
{
    auto *conn = static_cast<UrlDownloaderCurlConn *>(userdata);
    size_t len = size * nitems;

    if (conn->abortRequested)
        return len + 1;

    conn->headerBuf.append(buffer, len);
    return len;
}

/* WHAT MAY BE PUBLISHED OF A RESPONSE HEADER. The block is written by the
 * other end and what a name means is decided there: a session cookie, the
 * nonce of a challenge, an authorization echoed back all arrive under names
 * this end has never heard of. So the VALUES that leave are enumerated - each
 * of them describes the message and can never hold a credential - Location
 * goes through the url reducer because it is one, and everything else keeps
 * its name and gives up its value. An unknown name is reduced, never
 * published: that is the whole difference with a list of secrets to hide. */
string headerForLog(const string &name, const string &value)
{
    static const std::set<string> publishable = {
        "accept-ranges", "connection", "content-encoding", "content-length",
        "content-range", "content-type", "retry-after", "transfer-encoding",
    };

    string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(tolower(c)); });

    if (lower == "location" || lower == "content-location")
        return name + ": " + Utils::urlForLog(value);

    if (publishable.count(lower))
        return name + ": " + value;

    return name + ": [" + Utils::to_string(value.size()) + "B] #" + Utils::logTag(value);
}

} // namespace

UrlDownloader::UrlDownloader(string url, bool autodelete) :
    m_url(url),
    m_autodelete(autodelete)
{
    cInfoDom("urlutils") << "UrlDownloader: " << Utils::urlForLog(url);
}

UrlDownloader::~UrlDownloader()
{
    /* Expire the alive token before anything else: every async callback
     * checks it, so whatever fires from here on never touches this object. */
    alive.reset();

    closeHandles();

    cDebugDom("urlutils") << "UrlDownloader(" << this << ") destroyed";
}

void UrlDownloader::closeHandles()
{
    if (m_conn)
    {
        /* Detach + abort: the manager owns the connection from now on and
         * tears it down (the remote peer sees the connection drop). */
        UrlDownloaderCurlManager::instance().abortTransfer(m_conn);
        m_conn = nullptr;
    }
}

void UrlDownloader::cancel()
{
    if (m_cancelled)
        return;
    m_cancelled = true;

    cDebugDom("urlutils") << "UrlDownloader(" << this << ") cancel " << Utils::urlForLog(m_url);

    //The consumer asked out: nothing must fire after this point
    m_signalComplete.clear();
    m_signalCompleteData.clear();
    m_signalData.clear();

    closeHandles();

    m_isRunning = false;

    /* An autodelete object must never be deleted from outside (that would
     * race the completion Idler), so cancel() is its only exit: free it
     * ourselves. Destroy() is idempotent in case completion already went by. */
    if (m_autodelete)
        Destroy();
}

bool UrlDownloader::start()
{
    if (m_cancelled)
    {
        cWarningDom("urlutils") << "Downloader was cancelled, it cannot be restarted";
        return false;
    }

    if (m_conn)
    {
        cWarningDom("urlutils") << "A download is already in progress...";
        return false;
    }

    if (m_url.empty())
    {
        cWarningDom("urlutils") << "Url is empty";
        return false;
    }

    //Make sure curl_global_init ran before any easy handle is created
    UrlDownloaderCurlManager &manager = UrlDownloaderCurlManager::instance();

    m_downloadedData.clear();
    m_headerData.clear();
    statusCode = 0;

    const string logUrl = Utils::urlForLog(m_url);

    auto conn = new UrlDownloaderCurlConn;
    conn->easy = curl_easy_init();
    if (!conn->easy)
    {
        cErrorDom("urlutils") << "curl_easy_init() failed, aborting " << logUrl;
        delete conn;
        return false;
    }
    conn->logUrl = logUrl;

    conn->owner = this;
    conn->ownerAlive = alive;

    downloadToFile = !m_destination.empty();
    conn->toFile = downloadToFile;
    if (downloadToFile)
    {
        conn->destFile.open(m_destination, ios::out | ios::trunc | ios::binary);
        if (!conn->destFile.is_open())
        {
            cErrorDom("urlutils") << "Cannot open destination file " << m_destination
                                  << ", aborting " << logUrl;
            curl_easy_cleanup(conn->easy);
            delete conn;
            return false;
        }
    }

    CURL *e = conn->easy;

    //Same defaults as the old curl CLI invocation (minus --insecure):
    //silent --> no progress meter (libcurl default)
    //location --> follow redirects
    curl_easy_setopt(e, CURLOPT_URL, m_url.c_str());
    curl_easy_setopt(e, CURLOPT_NOSIGNAL, 1L);
    if (m_insecure)
    {
        /* T2.17: explicit per-call opt-out of certificate checks for local
         * devices serving self-signed HTTPS (IP cameras, Hue bridge). The
         * default is now the libcurl default: VERIFYPEER=1 / VERIFYHOST=2
         * against the system CA bundle. */
        curl_easy_setopt(e, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(e, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    curl_easy_setopt(e, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(e, CURLOPT_MAXREDIRS, 50L); //curl CLI default
    curl_easy_setopt(e, CURLOPT_FORBID_REUSE, 1L); //one connection per transfer, like the old subprocess
    curl_easy_setopt(e, CURLOPT_PRIVATE, conn);
    curl_easy_setopt(e, CURLOPT_WRITEFUNCTION, urlDownloaderWriteCb);
    curl_easy_setopt(e, CURLOPT_WRITEDATA, conn);
    curl_easy_setopt(e, CURLOPT_HEADERFUNCTION, headerCb);
    curl_easy_setopt(e, CURLOPT_HEADERDATA, conn);
    curl_easy_setopt(e, CURLOPT_ERRORBUFFER, conn->errorBuf);

    const char *method = "GET";
    switch (m_requestType)
    {
    case HTTP_POST:   method = "POST"; break;
    case HTTP_GET:    method = "GET"; break;
    case HTTP_PUT:    method = "PUT"; break;
    case HTTP_DELETE: method = "DELETE"; break;
    }
    curl_easy_setopt(e, CURLOPT_CUSTOMREQUEST, method);

    for (const string &s: headersRequest)
        conn->headerList = curl_slist_append(conn->headerList, s.c_str());
    if (conn->headerList)
        curl_easy_setopt(e, CURLOPT_HTTPHEADER, conn->headerList);

    if (m_auth)
    {
        curl_easy_setopt(e, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
        string u = m_user;
        if (!m_password.empty())
            u += ":" + m_password;
        curl_easy_setopt(e, CURLOPT_USERPWD, u.c_str()); //copied by libcurl
    }

    if (!m_bodyData.empty())
    {
        //Size first so COPYPOSTFIELDS copies binary data correctly
        curl_easy_setopt(e, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(m_bodyData.size()));
        curl_easy_setopt(e, CURLOPT_COPYPOSTFIELDS, m_bodyData.data());
    }

    cDebugDom("urlutils") << "Starting transfer: " << method << " " << logUrl
                          << (m_insecure? " (insecure: certificate checks disabled)": "");

    if (!manager.startTransfer(conn))
    {
        //startTransfer() fully disposed of conn (easy handle, header list,
        //destination file) on failure, nothing to clean up here
        return false;
    }

    m_conn = conn;
    m_isRunning = true;

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

void UrlDownloader::transferDone(int code, std::string responseHeaders)
{
    //All libcurl state of the transfer is already disposed of at this point
    m_conn = nullptr;
    m_isRunning = false;

    if (m_cancelled)
        return; //cancel() detached the transfer, nothing may fire anymore

    m_headerData = std::move(responseHeaders);
    getResponseHeaders(); //sets statusCode from the parsed status line
    if (statusCode == 0)
        statusCode = code; //fall back to libcurl's own view

    completeCb();
}

void UrlDownloader::completeCb()
{
    cDebugDom("urlutils") << "Finished with status code: " << statusCode;

    if (downloadToFile)
    {
        m_signalComplete.emit(statusCode);
    }
    else
    {
        /* Never the body: this transport carries the answers of every HTTP
         * driver of the tree and holds a std::string, not fields - it cannot
         * tell a light state from a device token, an authorization listing or
         * a session id. What is published is what can never be a secret; the
         * content type is on the header lines just above. */
        cDebugDom("urlutils") << "Response body: " << m_downloadedData.size() << " bytes";

        m_signalCompleteData.emit(m_downloadedData, statusCode);
        m_signalComplete.emit(statusCode);
    }

    if (m_autodelete)
        Destroy();
}

void UrlDownloader::dataCb(const char *data, int size)
{
    if (size <= 0)
        return;

    /* Bounded accumulation: streaming consumers (MJPEG) get every byte live
     * through m_signalData below, the internal copy only serves
     * m_signalCompleteData at the end and must not grow without limit when
     * the stream never ends. Beyond the cap the data is streamed but no
     * longer accumulated. */
    if (m_downloadedData.size() < m_bufferMaxSize)
    {
        size_t room = m_bufferMaxSize - m_downloadedData.size();
        m_downloadedData.append(data, std::min(static_cast<size_t>(size), room));
        if (m_downloadedData.size() >= m_bufferMaxSize)
            cWarningDom("urlutils") << "Download buffer cap (" << m_bufferMaxSize
                                    << " bytes) reached for " << Utils::urlForLog(m_url)
                                    << ", data is streamed but no longer accumulated";
    }

    m_signalData.emit(size, data);
}

void UrlDownloader::Destroy()
{
    /* Idempotent: completion and cancel() can both end up here, only one
     * Idler must ever be queued or the object would be freed twice. */
    if (destroyScheduled)
        return;
    destroyScheduled = true;

    cDebugDom("urlutils") << "UrlDownloader(" << this << ") Launch idler to destroy " << Utils::urlForLog(m_url);
    //T3.40: this is NOT the use-after-free pattern of the sweep even though
    //it looks like one. The idler does not USE the object, it is the
    //object's own deferred destruction: `this` must still be alive when the
    //callback runs, which is exactly what the anonymous uvw handle
    //guarantees. A lifetime tag here would suppress the delete and leak.
    Idler::singleIdler([=]() { delete this; });
}

Params UrlDownloader::getResponseHeaders()
{
    Params headers;

    //Parse the raw headers accumulated during the transfer. Multiple blocks
    //when redirected, e.g.:
    /*
HTTP/2 302
date: Fri, 08 Feb 2019 12:13:15 GMT
location: /640/480/?image=743

HTTP/2 200
date: Fri, 08 Feb 2019 12:13:16 GMT
content-type: image/jpeg
content-length: 49219
     */
    //Only the last block is kept, like the old --dump-header parsing did.

    statusCode = 0;
    std::istringstream infile(m_headerData);
    string line;

    cDebugDom("urlutils") << "Response headers:";

    while (std::getline(infile, line))
    {
        if (Utils::strStartsWith(line, "HTTP/"))
        {
            cDebugDom("urlutils") << line;

            vector<string> tok;
            Utils::split(line, tok, " ", 3);
            Utils::from_string(tok[1], statusCode);
            headers.clear();
        }
        else
        {
            vector<string> tok;
            Utils::split(line, tok, ":", 2);
            const string name = Utils::trim(tok[0]);
            const string value = Utils::trim(tok[1]);
            headers.Add(name, value);

            //The blank line between two blocks of a redirect is not a header
            if (name.empty() && value.empty())
                continue;

            /* A line with no colon is the folded tail of the value above it,
             * and a name is only a name when a colon says so: read as one, the
             * tail of a cookie leaves under the very rule that withholds its
             * head. */
            if (line.find(':') == string::npos)
                cDebugDom("urlutils") << "[folded " << name.size() << "B] #"
                                      << Utils::logTag(name);
            else
                cDebugDom("urlutils") << headerForLog(name, value);
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

void UrlDownloader::insecureGet(string url, string get_data)
{
    //T2.17: fire-and-forget GET for local self-signed devices (camera PTZ)
    UrlDownloader *downloader = new UrlDownloader(url, true);
    downloader->setInsecure();
    downloader->httpGet(string(), get_data);
}

void UrlDownloader::setHeader(string header, string value)
{
    if (value.empty())
        headersRequest.push_back(header + ";");
    else
        headersRequest.push_back(header + ": " + value);
}
