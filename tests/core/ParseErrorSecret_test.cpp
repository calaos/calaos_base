/******************************************************************************
 **  Copyright (c) 2006-2026, Calaos. All Rights Reserved.
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

/*******************************************************************************
 * WHAT NINE `catch` BLOCKS PUBLISH OF A DOCUMENT THE PARSER REFUSED.
 *
 * ---------------------------------------------------------------------------
 * THE DEFECT, AND WHY IT IS NOT VISIBLE IN THE FILE THAT CARRIES IT
 * ---------------------------------------------------------------------------
 * Nine sites stream e.what() of an exception raised by Json::parse() of a
 * runtime document, at a level a box prints with nobody enabling anything.
 * Nothing is concatenated at any of them - it is the LIBRARY that puts the
 * input back into its message: a nlohmann parse_error ends with
 * `last read: '<the token the parser choked on>'`, which is a slice of the
 * document, wherever the malformation happens to sit.
 *
 * So the bytes that leave are chosen by the parser, not by the field a reader
 * of the source would look at, and a sensor spelled on the name of a secret is
 * blind here BY CONSTRUCTION. Each fixture below puts its needle in the
 * document twice - in clear OUTSIDE the refused token, percent encoded INSIDE
 * it - so a search for the clear form stays green while the credential leaves,
 * exactly the way a credential travels once it has been put inside a url.
 * What carries every leak case is therefore the longest RUN of the document
 * the journal gives back, not a value.
 *
 * ---------------------------------------------------------------------------
 * WHAT MUST NOT DIE WITH THE BYTES
 * ---------------------------------------------------------------------------
 * A document that will not parse is exactly when an operator needs the
 * journal. That a parse failed, on which path, AT WHAT POSITION and on how
 * many bytes is required of all nine: the position and the size are what makes
 * a parse error actionable, and neither carries a byte of the input.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS DOES NOT PROVE
 * ---------------------------------------------------------------------------
 * No HiFi Rose amplifier, no remote UI panel, no web service. The four
 * amplifier sites are fed by a real TLS peer this file brings up, so libcurl,
 * the loop and AVRRose are the production ones; the websocket site is entered
 * through the call WebSocket.cpp makes, without any framing; and the three
 * file sites are handed a real file through their public entry point. The
 * chain proven stops at what std::cout of the server receives.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "AVRRose.h"
#include "CalaosConfig.h"
#include "ConfigStore.h"
#include "FirmwareManifest.h"
#include "HttpClient.h"
#include "LogSetup.h"
#include "Logger.h"
#include "Params.h"
#include "RemoteUIWebSocketHandler.h"
#include "StringUtils.h"
#include "WebCtrl.h"
#include "json.hpp"
#include "libuvw.h"

using namespace Calaos;

namespace
{

/*
 * THE NINE SITES, AND WHAT THE DOCUMENT HANDED TO EACH CAN CARRY.
 *
 * `plain` sits before the refused token and never leaves; `encoded` sits
 * inside it and is what the parser quotes back. No two documents share a run
 * of consequence, key names and locators included, so the red set of a
 * mutation names the site it hit instead of its neighbours.
 */
struct Site
{
    const char *label;
    const char *domain;    //empty for the two sites logging without a domain
    const char *marker;    //wording common to the line before and after the fix
    const char *plain;
    const char *encoded;
    std::string input;     //the bytes really handed over
    std::string witness;   //what else identifies the line among the others
    bool fed = false;      //the document really reached the site
    int lines = 0;
    size_t errorByte = 0;  //where nlohmann says it stopped, computed here
};

/*
 * The malformation is an invalid escape INSIDE the second string, so the token
 * the parser quotes back starts at that string and carries `encoded`.
 *
 * No two documents may share a run anywhere near the ceiling below, key names
 * included: a shared skeleton makes a leak on one site redden the bound of
 * six others, and a red set is only readable as "which site" when nothing but
 * json punctuation is common to two of them. Measured across the nine: 6.
 */
std::string refusedDocument(const std::string &keyPlain, const std::string &plain,
                            const std::string &keySecret, const std::string &encoded,
                            const std::string &locator, const std::string &host)
{
    return "{\"" + keyPlain + "\":\"" + plain + "\",\"" + keySecret + "\":\"" +
           locator + encoded + "@" + host + "/\\q\"}";
}

std::vector<Site> &sites()
{
    static std::vector<Site> s = []()
    {
        std::vector<Site> v;

        //The registration answer, and the body deviceRoseToken comes out of.
        v.push_back({ "rose_device_connected", "hifirose",
                      "Failed to parse device_connected response",
                      "jeton appareil 8f3a2c", "jeton%20appareil%208f3a2c",
                      "{\"modelName\":\"RS520 jeton appareil 8f3a2c\",\"data\":"
                      "{\"deviceRoseToken\":\"jeton%20appareil%208f3a2c-a4e1\\q\"}}",
                      "hifirose" });

        v.push_back({ "rose_get_current_state", "hifirose",
                      "Error parsing get_current_state",
                      "etat courant 4b71ee", "etat%20courant%204b71ee",
                      refusedDocument("etatNom", "etat courant 4b71ee",
                                      "etatCle", "etat%20courant%204b71ee",
                                      "ftp://e3:", "10.9.4.2"),
                      "hifirose" });

        v.push_back({ "rose_get_control_info", "hifirose",
                      "Error parsing get_control_info",
                      "reglage volume 91c0da", "reglage%20volume%2091c0da",
                      refusedDocument("reglageTitre", "reglage volume 91c0da",
                                      "reglageMot", "reglage%20volume%2091c0da",
                                      "ldap://w2:", "192.168.55.3"),
                      "hifirose" });

        v.push_back({ "rose_mute_state_get", "hifirose",
                      "Error parsing mute.state.get",
                      "silence ampli 27fa63", "silence%20ampli%2027fa63",
                      refusedDocument("silenceRef", "silence ampli 27fa63",
                                      "silenceSceau", "silence%20ampli%2027fa63",
                                      "smb://y6:", "203.0.113.7"),
                      "hifirose" });

        //Inbound: anything on the LAN can post to the notification port.
        v.push_back({ "rose_notification", "hifirose",
                      "Failed to parse notification JSON from",
                      "avis borne 5e08b1", "avis%20borne%205e08b1",
                      refusedDocument("avisTag", "avis borne 5e08b1",
                                      "avisPin", "avis%20borne%205e08b1",
                                      "ow://r4:", "172.31.8.4"),
                      "127.0.0.1" });

        //Inbound: a websocket text frame, which on this transport is where a
        //login message with its password arrives.
        v.push_back({ "remoteui_websocket", "remote_ui",
                      "JSON parse error",
                      "session distante 6d34c9", "session%20distante%206d34c9",
                      refusedDocument("sessionAlias", "session distante 6d34c9",
                                      "sessionBadge", "session%20distante%206d34c9",
                                      "nfs://t5:", "198.51.100.9"),
                      "remote_ui" });

        //A document a third party web service answered, downloaded to a file.
        v.push_back({ "webctrl_document", "",
                      "Error parsing",
                      "service tiers 3a95f7", "service%20tiers%203a95f7",
                      refusedDocument("serviceEnseigne", "service tiers 3a95f7",
                                      "serviceGage", "service%20tiers%203a95f7",
                                      "sftp://k8:", "10.200.6.5"),
                      "webctrl_doc.json" });

        v.push_back({ "firmware_manifest", "ota",
                      "Failed to parse manifest",
                      "manifeste micro 82be40", "manifeste%20micro%2082be40",
                      refusedDocument("manifesteRubrique", "manifeste micro 82be40",
                                      "manifesteChiffre", "manifeste%20micro%2082be40",
                                      "rtsp://m1:", "169.254.9.6"),
                      "manifest.json" });

        //The state cache this daemon writes itself, and ioparams is where the
        //password of every IO that has one is stored.
        v.push_back({ "state_cache", "",
                      "Error parsing",
                      "parametre camera 0c7d15", "parametre%20camera%200c7d15",
                      "{\"libelle\":\"parametre camera 0c7d15\",\"ioparams\":"
                      "{\"io_1\":{\"password\":\"parametre%20camera%200c7d15-b9\\q\"}}}",
                      "iostates.cache" });

        return v;
    }();

    return s;
}

/*
 * THE LONGEST RUN OF THE DOCUMENT THE JOURNAL GIVES BACK.
 *
 * A value search only sees the slice it was spelled for, and the parser
 * decides where it cuts. What must not reach a journal is any RUN of the
 * document, wherever it was cut, so the log is asked how much it gives back.
 */
std::string longestEchoRun(const std::string &log, const std::string &text)
{
    std::string best;
    for (size_t i = 0; i < text.size(); i++)
    {
        size_t len = best.size() + 1;
        while (i + len <= text.size() &&
               log.find(text.substr(i, len)) != std::string::npos)
        {
            best = text.substr(i, len);
            len++;
        }
    }
    return best;
}

size_t longestEcho(const std::string &log, const std::string &text)
{
    return longestEchoRun(log, text).size();
}

//The longest run of the journal's own alphabet a document carries: an address
//and a fingerprint are printed in it, so such a run can be matched by bytes
//nobody chose.
std::string longestHexRun(const std::string &s)
{
    std::string best, cur;
    for (const char c: s)
    {
        if (::isxdigit(static_cast<unsigned char>(c)))
        {
            cur += c;
            if (cur.size() > best.size())
                best = cur;
        }
        else
            cur.clear();
    }
    return best;
}

/* One above the overlap the corrected tree really produces, and the suite
 * re-measures that overlap at every run rather than trusting this line: see
 * the calibration case at the end of the file for why a ceiling nobody
 * re-measures drifts in both directions.
 */
const size_t kMaxEcho = 9;

bool someLineHasAll(const std::string &log, const std::string &a,
                    const std::string &b, const std::string &c)
{
    std::istringstream in(log);
    std::string line;
    while (std::getline(in, line))
    {
        if (line.find(a) != std::string::npos &&
            line.find(b) != std::string::npos &&
            line.find(c) != std::string::npos)
            return true;
    }
    return false;
}

int countLinesWith(const std::string &log, const std::string &a, const std::string &b)
{
    int n = 0;
    std::istringstream in(log);
    std::string line;
    while (std::getline(in, line))
    {
        if (line.find(a) != std::string::npos && line.find(b) != std::string::npos)
            n++;
    }
    return n;
}

/*
 * WHAT A BOX PRINTS WITH NOBODY TOUCHING ANYTHING, measured in a child.
 *
 * The severity of this ticket is "these lines print on a stock install", and
 * it comes from the LOG_LEVEL_INFO fallback of Logger::maxLevelPrintable(),
 * not from a written option. The Logger fills its domain map once and never
 * re-reads it, so a process that raised the level can no longer observe the
 * default: the child is forked before this one owns a loop or a socket.
 */
struct DefaultLevelProbe
{
    bool ran = false;
    std::vector<unsigned char> answer;
};

DefaultLevelProbe &defaultLevelProbe()
{
    static DefaultLevelProbe probe;
    return probe;
}

//The distinct domains of the nine sites, empty meaning the domainless logger.
const char *const kDomains[] = { "hifirose", "remote_ui", "ota", "" };
const size_t kDomainCount = 4;

void measureStockLogLevel()
{
    int fds[2];
    if (::pipe(fds) != 0)
        return;

    const pid_t pid = ::fork();
    if (pid < 0)
    {
        ::close(fds[0]);
        ::close(fds[1]);
        return;
    }

    if (pid == 0)
    {
        ::close(fds[0]);

        std::vector<unsigned char> answer(kDomainCount + 1, 0);
        char tmpl[] = "/tmp/calaos_parseerr_stock_XXXXXX";
        const char *base = ::mkdtemp(tmpl);
        if (base)
        {
            const std::string cfg = std::string(base) + "/config";
            const std::string cache = std::string(base) + "/cache";
            ::mkdir(cfg.c_str(), 0700);
            ::mkdir(cache.c_str(), 0700);

            //Nothing is set afterwards: this is a stock install.
            Utils::initConfigOptions(const_cast<char *>(cfg.c_str()),
                                     const_cast<char *>(cache.c_str()), true);

            answer[0] = 0x4;
            for (size_t i = 0; i < kDomainCount; i++)
            {
                Logger *l = (kDomains[i][0] == '\0')? Utils::calaosLogger()
                                                    : Utils::calaosLogger(kDomains[i]);
                unsigned char a = 0;
                if (l->isLevelEnabled(Logger::LOG_LEVEL_WARNING))
                    a |= 0x1;
                if (l->isLevelEnabled(Logger::LOG_LEVEL_DEBUG))
                    a |= 0x2;
                answer[i + 1] = a;
            }
        }

        if (::write(fds[1], answer.data(), answer.size()) !=
            static_cast<ssize_t>(answer.size()))
            answer[0] = 0;
        ::close(fds[1]);
        _exit(0);
    }

    ::close(fds[1]);

    std::vector<unsigned char> answer(kDomainCount + 1, 0);
    const ssize_t got = ::read(fds[0], answer.data(), answer.size());
    ::close(fds[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);

    if (got == static_cast<ssize_t>(kDomainCount + 1) && (answer[0] & 0x4))
    {
        defaultLevelProbe().ran = true;
        defaultLevelProbe().answer = answer;
    }
}

//The one sandbox of this process. Empty when mkdtemp() fails, so a case FAILS
//instead of quietly writing next to the sources.
const std::string &sandboxDir()
{
    static std::string dir = []() -> std::string
    {
        char tmpl[] = "/tmp/calaos_parse_error_XXXXXX";
        const char *d = ::mkdtemp(tmpl);
        return d? std::string(d) : std::string();
    }();

    return dir;
}

bool writeFile(const std::string &path, const std::string &content)
{
    std::ofstream f(path.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
    if (!f.is_open())
        return false;
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    return f.good();
}

/*
 * A REAL TLS PEER, because the amplifier transport is https and nothing below
 * it can be reached without one.
 *
 * The key and the self signed certificate are generated in process at each
 * run: a certificate committed next to a test expires, and the amplifier
 * driver disables certificate checks anyway (self signed is what these devices
 * serve). The thread only ever answers on the loopback port it bound.
 */
class TlsPeer
{
public:
    ~TlsPeer()
    {
        stop.store(true);
        if (worker.joinable())
            worker.join();
        if (ctx)
            SSL_CTX_free(ctx);
        if (fd >= 0)
            ::close(fd);
    }

    void answer(const std::string &path, const std::string &body) { bodies[path] = body; }

    bool start()
    {
        EVP_PKEY *key = generateKey();
        if (!key)
            return false;

        X509 *cert = selfSign(key);
        if (!cert)
        {
            EVP_PKEY_free(key);
            return false;
        }

        ctx = SSL_CTX_new(TLS_server_method());
        bool ok = ctx &&
                  SSL_CTX_use_certificate(ctx, cert) == 1 &&
                  SSL_CTX_use_PrivateKey(ctx, key) == 1;
        X509_free(cert);
        EVP_PKEY_free(key);
        if (!ok)
            return false;

        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
            return false;

        int on = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
            return false;

        socklen_t len = sizeof(addr);
        if (::getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &len) != 0)
            return false;
        port = ntohs(addr.sin_port);

        if (::listen(fd, 8) != 0)
            return false;

        worker = std::thread([this]() { serve(); });
        return true;
    }

    int port = 0;

private:
    static EVP_PKEY *generateKey()
    {
        //The 1.1.1 spelling, which 3.x still accepts: the tree only requires
        //openssl >= 1.1.0.
        EVP_PKEY *key = nullptr;
        EVP_PKEY_CTX *c = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
        if (!c)
            return nullptr;
        if (EVP_PKEY_keygen_init(c) > 0 &&
            EVP_PKEY_CTX_set_rsa_keygen_bits(c, 2048) > 0)
            EVP_PKEY_keygen(c, &key);
        EVP_PKEY_CTX_free(c);
        return key;
    }

    static X509 *selfSign(EVP_PKEY *key)
    {
        X509 *x = X509_new();
        if (!x)
            return nullptr;

        X509_set_version(x, 2);
        ASN1_INTEGER_set(X509_get_serialNumber(x), 1);
        X509_gmtime_adj(X509_getm_notBefore(x), -3600);
        X509_gmtime_adj(X509_getm_notAfter(x), 3600 * 24);
        X509_set_pubkey(x, key);

        X509_NAME *name = X509_get_subject_name(x);
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                   reinterpret_cast<const unsigned char *>("localhost"),
                                   -1, -1, 0);
        X509_set_issuer_name(x, name);

        if (!X509_sign(x, key, EVP_sha256()))
        {
            X509_free(x);
            return nullptr;
        }
        return x;
    }

    void serve()
    {
        while (!stop.load())
        {
            pollfd p = { fd, POLLIN, 0 };
            if (::poll(&p, 1, 50) <= 0)
                continue;

            const int c = ::accept(fd, nullptr, nullptr);
            if (c < 0)
                continue;

            SSL *ssl = SSL_new(ctx);
            SSL_set_fd(ssl, c);
            if (SSL_accept(ssl) == 1)
            {
                const std::string req = readRequest(ssl);
                const std::string path = requestPath(req);
                const auto it = bodies.find(path);
                const std::string body = (it != bodies.end())? it->second : std::string("{}");

                const std::string resp =
                    "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                    "Content-Length: " + std::to_string(body.size()) +
                    "\r\nConnection: close\r\n\r\n" + body;
                SSL_write(ssl, resp.data(), static_cast<int>(resp.size()));
                SSL_shutdown(ssl);
            }
            SSL_free(ssl);
            ::close(c);
        }
    }

    static std::string readRequest(SSL *ssl)
    {
        std::string req;
        char buf[4096];
        for (;;)
        {
            const int n = SSL_read(ssl, buf, sizeof(buf));
            if (n <= 0)
                break;
            req.append(buf, static_cast<size_t>(n));

            const size_t headerEnd = req.find("\r\n\r\n");
            if (headerEnd == std::string::npos)
                continue;

            size_t bodyLen = 0;
            const size_t cl = req.find("Content-Length:");
            if (cl != std::string::npos && cl < headerEnd)
                bodyLen = std::strtoul(req.c_str() + cl + 15, nullptr, 10);
            if (req.size() >= headerEnd + 4 + bodyLen)
                break;
        }
        return req;
    }

    static std::string requestPath(const std::string &req)
    {
        const size_t a = req.find(' ');
        if (a == std::string::npos)
            return std::string();
        const size_t b = req.find(' ', a + 1);
        if (b == std::string::npos)
            return std::string();
        return req.substr(a + 1, b - a - 1);
    }

    int fd = -1;
    SSL_CTX *ctx = nullptr;
    std::atomic<bool> stop { false };
    std::thread worker;
    std::map<std::string, std::string> bodies;
};

//Run the default loop until pred() holds, with a wall clock deadline: a
//regression must be able to fail a case, never to hang `make check`.
bool pumpLoopUntil(const std::function<bool()> &pred, int timeoutMs)
{
    auto loop = uvw::Loop::getDefault();
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);

    for (;;)
    {
        loop->run<uvw::Loop::Mode::NOWAIT>();

        if (pred())
            return true;
        if (std::chrono::steady_clock::now() > deadline)
            return false;

        ::usleep(2000);
    }
}

//A blocking client on the notification port, which is what a device is.
bool postNotification(int port, const std::string &path, const std::string &body)
{
    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
        return false;

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (::connect(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
    {
        ::close(s);
        return false;
    }

    const std::string req = "POST " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                            "Content-Type: application/json\r\nContent-Length: " +
                            std::to_string(body.size()) + "\r\n\r\n" + body;

    const ssize_t sent = ::send(s, req.data(), req.size(), 0);
    ::close(s);
    return sent == static_cast<ssize_t>(req.size());
}

struct Observation
{
    bool installed = false;
    std::string log;
    std::string webCtrlFile;
    std::string manifestFile;
    std::string cacheFile;
    bool webCtrlErr = false;
    bool manifestLoaded = true;
    std::string webCtrlEcho;   //the file really reached WebCtrl, read back
};

//Where main() planted the state cache, so the observation can name it.
std::string &plantedCachePath()
{
    static std::string p;
    return p;
}

const Observation &theObservation()
{
    static Observation *obs = new Observation();
    static bool done = false;
    if (done)
        return *obs;
    done = true;

    obs->installed = !sandboxDir().empty() && !plantedCachePath().empty();
    if (!obs->installed)
        return *obs;

    //Redirected by hand rather than through a helper: the pump below has to
    //read what has been written so far, so the sink must stay reachable while
    //the loop runs.
    std::ostringstream sink;
    std::streambuf *saved = std::cout.rdbuf(sink.rdbuf());

    /* The state cache first: Config is a singleton whose CONSTRUCTOR reads it,
     * which is the production path and happens exactly once.
     */
    obs->cacheFile = plantedCachePath();
    sites()[8].fed = true;
    (void)Config::Instance();

    //A document a web service answered, downloaded to a file, read back
    //through the entry point every WebInput* takes.
    obs->webCtrlFile = sandboxDir() + "/webctrl_doc.json";
    if (writeFile(obs->webCtrlFile, sites()[6].input))
    {
        sites()[6].fed = true;
        Params wp;
        wp.Add("url", "http://127.0.0.1/doc");
        WebCtrl &web = WebCtrl::Instance(wp);
        obs->webCtrlEcho = web.getValueJson("mesure", obs->webCtrlFile, obs->webCtrlErr);
    }

    obs->manifestFile = sandboxDir() + "/manifest.json";
    if (writeFile(obs->manifestFile, sites()[7].input))
    {
        sites()[7].fed = true;
        FirmwareManifest manifest;
        obs->manifestLoaded = manifest.loadFromFile(obs->manifestFile);
    }

    /* The websocket frame, through the call WebSocket.cpp makes on a text
     * frame, word for word. The client is real because the handler's
     * constructor asks it for the peer address; both are destroyed at once so
     * the read timeout of the connection cannot fire into the pump below.
     */
    {
        auto conn = uvw::Loop::getDefault()->resource<uvw::TcpHandle>();
        HttpClient *client = new HttpClient(conn);
        RemoteUIWebSocketHandler *handler = new RemoteUIWebSocketHandler(client);
        sites()[5].fed = true;
        handler->processApi(sites()[5].input, Params());
        delete handler;
        delete client;
    }

    //The two amplifiers, and the real https transport underneath them.
    static TlsPeer peerA;
    static TlsPeer peerB;

    peerA.answer("/device_connected", sites()[0].input);
    peerA.answer("/get_current_state", sites()[1].input);

    //B has to get past registration and past the state read to reach the two
    //sites that come after them in the poll chain.
    peerB.answer("/device_connected", "{\"data\":{\"deviceRoseToken\":\"jeton-b\"}}");
    peerB.answer("/get_current_state", "{\"code\":\"G0000\"}");
    peerB.answer("/get_control_info", sites()[2].input);
    peerB.answer("/mute.state.get", sites()[3].input);

    static AVRRose *roseA = nullptr;
    static AVRRose *roseB = nullptr;

    if (peerA.start() && peerB.start())
    {
        sites()[0].fed = sites()[1].fed = true;
        sites()[2].fed = sites()[3].fed = true;

        Params pa;
        pa.Add("id", "avr_rose_a");
        pa.Add("host", "127.0.0.1");
        pa.Add("port", std::to_string(peerA.port));
        roseA = new AVRRose(pa);

        Params pb;
        pb.Add("id", "avr_rose_b");
        pb.Add("host", "127.0.0.1");
        pb.Add("port", std::to_string(peerB.port));
        roseB = new AVRRose(pb);
    }

    pumpLoopUntil([&sink]()
    {
        const std::string sofar = sink.str();
        for (size_t i = 0; i < 4; i++)
            if (countLinesWith(sofar, sites()[i].marker, sites()[i].witness) < 1)
                return false;
        return true;
    }, 30000);

    //The notification server the amplifiers share is now listening, and a
    //device is a plain http client on it.
    if (roseA)
    {
        //The notification port is a private constant of the server; a
        //connection refused here fails the case rather than passing quietly.
        sites()[4].fed = postNotification(9284, "/device_state_noti", sites()[4].input);
        pumpLoopUntil([&sink]()
        {
            return countLinesWith(sink.str(), sites()[4].marker, sites()[4].witness) >= 1;
        }, 10000);
    }

    std::cout.rdbuf(saved);
    obs->log = sink.str();

    for (Site &s: sites())
        s.lines = countLinesWith(obs->log, s.marker, s.witness);

    return *obs;
}

} // namespace

/*
 * ONE GTEST CASE PER SITE, not one loop over nine.
 *
 * The set of cases a counter mutation reddens says WHICH site it hit only if
 * the cases are separate; a single looping case answers "the loop" to every
 * one of them.
 */
class ParseErrorSecretTest: public ::testing::TestWithParam<size_t> {};

/*
 * THE CASE THIS SUITE EXISTS FOR: nothing of a document the parser refused
 * reaches the journal.
 *
 * ANTI-VACUITY FIRST. The domain is asserted printable at WARNING, the
 * document is asserted to really carry its needle in both forms, and the
 * failure line is required to have been written before anything is looked for
 * - a quieter environment or a document that never arrived would otherwise
 * make this case green while measuring nothing.
 */
TEST_P(ParseErrorSecretTest, NothingOfADocumentTheParserRefusedReachesTheLog)
{
    const Observation &obs = theObservation();
    ASSERT_TRUE(obs.installed) << "the sandbox could not be created";

    const Site &s = sites()[GetParam()];
    SCOPED_TRACE(s.label);

    Logger *logger = (s.domain[0] == '\0')? Utils::calaosLogger()
                                          : Utils::calaosLogger(s.domain);
    ASSERT_TRUE(logger->isLevelEnabled(Logger::LOG_LEVEL_WARNING))
        << "this domain is muted below WARNING here, so this case cannot "
           "observe the line it exists to check";

    ASSERT_NE(std::string::npos, s.input.find(s.plain))
        << "the document does not carry its needle in clear at all, so a green "
           "here would mean nothing: " << s.input;
    ASSERT_NE(std::string::npos, s.input.find(s.encoded))
        << "the document does not carry its needle percent encoded at all: "
        << s.input;

    ASSERT_TRUE(s.fed)
        << "the document was never handed to this site, so nothing was parsed. "
           "Log: " << obs.log;
    ASSERT_GE(s.lines, 1)
        << "no parse failure was reported for this site, so this case cannot "
           "say whether the document would have been quoted. Log: " << obs.log;

    /* Spelled on the needle, and expected to stay green even before this fix:
     * the parser cuts where the malformation is, and the clear form sits
     * outside that token. It is here to say so, not to carry the case.
     */
    EXPECT_EQ(std::string::npos, obs.log.find(s.plain))
        << "the document is in the journal in its clear form. Log: " << obs.log;

    EXPECT_EQ(std::string::npos, obs.log.find(s.encoded))
        << "the document the parser refused is in the journal in its percent "
           "encoded form, which a search for the clear form does not see, and "
           "these levels print on a stock install. Log: " << obs.log;

    //What carries this case: the parser decides where it cuts, so no value
    //search can be relied on. The needle in clear sits OUTSIDE the refused
    //token on purpose and is expected to stay out of the journal too.
    const size_t echo = longestEcho(obs.log, s.input);
    EXPECT_LT(echo, kMaxEcho)
        << "the journal gives back " << echo << " consecutive bytes of a "
           "document this end refused to parse, which no line of this site has "
           "a reason to carry. Log: " << obs.log;
}

/*
 * THE COUNTERWEIGHT: deleting the line would also pass the case above.
 *
 * A document that will not parse is the reason this line exists. What must
 * survive is that a parse failed, on which path, WHERE it stopped and on how
 * many bytes - the position is compared to what nlohmann really reports for
 * these exact bytes, and the size to the document really handed over, because
 * a number nobody compares to anything is not a measurement.
 */
TEST_P(ParseErrorSecretTest, AParseFailureIsStillReportedWithItsPositionAndSize)
{
    const Observation &obs = theObservation();
    ASSERT_TRUE(obs.installed) << "the sandbox could not be created";

    const Site &s = sites()[GetParam()];
    SCOPED_TRACE(s.label);

    ASSERT_TRUE(s.fed)
        << "the document was never handed to this site. Log: " << obs.log;
    ASSERT_GT(s.errorByte, 0u)
        << "these bytes parse cleanly, so this fixture measures nothing";

    const std::string size = std::to_string(s.input.size()) + " bytes";
    EXPECT_TRUE(someLineHasAll(obs.log, s.marker, s.witness, size))
        << "the line written for a document that could not be parsed does not "
           "say how many bytes it was: a peer that has started answering "
           "nonsense is then indistinguishable from one that has gone quiet. "
           "Log: " << obs.log;

    const std::string pos = "byte " + std::to_string(s.errorByte);
    EXPECT_TRUE(someLineHasAll(obs.log, s.marker, s.witness, pos))
        << "the line does not say WHERE the parser stopped, which is what "
           "makes a parse failure actionable and carries no byte of the input. "
           "Log: " << obs.log;
}

INSTANTIATE_TEST_SUITE_P(EachSite, ParseErrorSecretTest,
                         ::testing::Range<size_t>(0, 9));

class ParseErrorFixtureTest: public ::testing::Test {};

/*
 * ANTI-VACUITY OF THE THREE SITES THAT ARE NOT FED THROUGH A SOCKET.
 *
 * "The file could not be opened" and "the file does not parse" are reported by
 * the same line at two of them, so what was handed over is read back through a
 * path that hands it over untouched, and compared to what was written.
 */
TEST_F(ParseErrorFixtureTest, TheThreeFileSitesReallyReadTheDocumentThatWasPlanted)
{
    const Observation &obs = theObservation();
    ASSERT_TRUE(obs.installed) << "the sandbox could not be created";

    std::ifstream cache(obs.cacheFile.c_str(), std::ios::binary);
    ASSERT_TRUE(cache.is_open()) << "the state cache was never planted";
    const std::string cacheContent((std::istreambuf_iterator<char>(cache)),
                                   std::istreambuf_iterator<char>());
    EXPECT_EQ(sites()[8].input, cacheContent);

    EXPECT_TRUE(obs.webCtrlErr)
        << "WebCtrl did not report a failure on a document that is not json, "
           "so it never entered the branch this suite checks";
    EXPECT_TRUE(obs.webCtrlEcho.empty())
        << "WebCtrl answered a value for a document that does not parse";
    EXPECT_FALSE(obs.manifestLoaded)
        << "the manifest loader accepted a document that is not json";
}

/*
 * THE ONE PLACE ALL NINE NOW GO THROUGH, held to the same rule directly.
 *
 * The nine lines above prove what the sites publish today. This one holds the
 * reducer itself: it is what a tenth catch block will call, and a reducer that
 * gave a slice back would reopen the class in one line without touching any of
 * the nine.
 */
TEST_F(ParseErrorFixtureTest, TheReducerGivesBackNothingOfWhatItWasHanded)
{
    for (const Site &s: sites())
    {
        SCOPED_TRACE(s.label);

        std::string reduced;
        size_t seenByte = 0;
        try
        {
            nlohmann::json::parse(s.input);
            FAIL() << "these bytes parse cleanly, so this case measures nothing";
        }
        catch (const nlohmann::json::parse_error &e)
        {
            seenByte = e.byte;
            reduced = Utils::jsonErrorForLog(e, s.input.size());
        }

        ASSERT_NE(std::string::npos, s.input.find(s.encoded));

        EXPECT_EQ(std::string::npos, reduced.find(s.plain)) << reduced;
        EXPECT_EQ(std::string::npos, reduced.find(s.encoded)) << reduced;

        const size_t echo = longestEcho(reduced, s.input);
        EXPECT_LT(echo, kMaxEcho)
            << "the reducer gives back " << echo << " consecutive bytes of what "
               "it was handed: " << reduced;

        EXPECT_NE(std::string::npos, reduced.find("byte " + std::to_string(seenByte)))
            << reduced;
        EXPECT_NE(std::string::npos,
                  reduced.find(std::to_string(s.input.size()) + " bytes")) << reduced;
    }
}

/*
 * The catch blocks are spelled on std::exception, so what arrives is not always
 * a json error - one of the nine has a throw of its own two lines above it.
 * Naming the type is the most that can be said of an exception whose message
 * this tree does not control.
 */
TEST_F(ParseErrorFixtureTest, TheReducerNeverPublishesTheMessageOfAnythingElse)
{
    const std::string secret = "message%20porteur%20de%20secret";
    const std::invalid_argument thrown("cache refused: " + secret);

    const std::string reduced = Utils::jsonErrorForLog(thrown, 41);

    EXPECT_EQ(std::string::npos, reduced.find(secret)) << reduced;
    EXPECT_NE(std::string::npos, reduced.find("invalid_argument")) << reduced;
    EXPECT_NE(std::string::npos, reduced.find("41 bytes")) << reduced;
}

class StockLevelTest: public ::testing::Test {};

/*
 * THE LEVEL THE WHOLE TICKET RESTS ON, MEASURED INSTEAD OF ASSERTED.
 */
TEST_F(StockLevelTest, AStockInstallPrintsTheseParseFailureLines)
{
    const DefaultLevelProbe &probe = defaultLevelProbe();

    ASSERT_TRUE(probe.ran)
        << "the stock-level child could not be forked or answered nothing, so "
           "this case measures no level at all";

    for (size_t i = 0; i < kDomainCount; i++)
    {
        SCOPED_TRACE(kDomains[i][0]? kDomains[i] : "<no domain>");

        EXPECT_TRUE((probe.answer[i + 1] & 0x1) != 0)
            << "this domain does not print at WARNING on a stock install, so "
               "these lines were never the default-level defect this ticket is "
               "filed as";
        EXPECT_FALSE((probe.answer[i + 1] & 0x2) != 0)
            << "this domain prints at DEBUG on a stock install, so the level "
               "this suite reads is not the one a shipped box uses";
    }
}

/*
 * THE CEILING ITSELF, HELD TO WHAT THIS SUITE MEASURES AT EVERY RUN.
 *
 * A bounded sensor is only as narrow as the number above the noise it was cut
 * for, and that number drifts both ways with nobody watching: a fixture that
 * gains a shared word widens the blind window while every assertion stays
 * green, and a ceiling left wider than the run it was cut for is blind space
 * no one asked for. Pinning the equality turns both into a red that says which
 * number to write, and makes ONE more byte of echo than the tree really
 * produces a failure - well under the ceiling the cases above enforce.
 */
TEST_F(ParseErrorFixtureTest, TheCeilingIsHeldToTheOverlapThisSuiteMeasures)
{
    const Observation &obs = theObservation();
    ASSERT_TRUE(obs.installed) << "the sandbox could not be created";

    //A measure that cannot report a run reads as a clean zero everywhere below.
    ASSERT_EQ("bcdef", longestEchoRun("zzbcdefzz", "abcdefg"));
    ASSERT_EQ("", longestEchoRun("zzz", "abc"));
    ASSERT_EQ(5u, longestEcho("zzbcdefzz", "abcdefg"));
    ASSERT_EQ(0u, longestEcho("zzz", "abc"));

    std::string worst, worstLabel;
    const auto keep = [&worst, &worstLabel](const std::string &run,
                                            const std::string &label)
    {
        if (run.size() > worst.size())
        {
            worst = run;
            worstLabel = label;
        }
    };

    for (const Site &s: sites())
    {
        ASSERT_TRUE(s.fed)
            << s.label << " was never handed its document, so the overlap this "
               "case exists to pin was not produced at all";
        keep(longestEchoRun(obs.log, s.input), s.label);
    }

    //The reducer is held to the same ceiling, so it belongs to the same
    //measurement.
    for (const Site &s: sites())
    {
        try
        {
            nlohmann::json::parse(s.input);
            FAIL() << "these bytes parse cleanly, so this case measures nothing";
        }
        catch (const nlohmann::json::parse_error &e)
        {
            keep(longestEchoRun(Utils::jsonErrorForLog(e, s.input.size()), s.input),
                 std::string("reducer/") + s.label);
        }
    }

    EXPECT_EQ(kMaxEcho, worst.size() + 1)
        << "the ceiling is " << kMaxEcho << " while this tree gives back at "
           "most " << worst.size() << " bytes of a document, at " << worstLabel
        << ", on the run \"" << worst << "\". Everything between the two is a "
           "window this suite cannot see into: either a leak has widened the "
           "overlap, or the fixture has, and the ceiling to write is "
        << (worst.size() + 1) << ".";
}

/*
 * THE FIXTURE, HELD TO THE SAME CEILING.
 *
 * A run two documents share is republished by whichever site leaks first, so
 * it raises the bound of every neighbour that carries it: the red set then
 * names the fixture instead of the site. A shared key suffix is enough, and
 * nothing about the documents makes it visible when reading them.
 */
TEST_F(ParseErrorFixtureTest, NoTwoDocumentsShareARunTheCeilingWouldNotAbsorb)
{
    for (size_t i = 0; i < sites().size(); i++)
    {
        for (size_t j = i + 1; j < sites().size(); j++)
        {
            const std::string run = longestEchoRun(sites()[i].input, sites()[j].input);
            EXPECT_LT(run.size(), kMaxEcho)
                << sites()[i].label << " and " << sites()[j].label << " share \""
                << run << "\", " << run.size() << " bytes, which the ceiling of "
                << kMaxEcho << " does not absorb: a leak at either one reddens "
                   "the bound of the other and the red set stops naming a site.";
        }
    }
}

/*
 * THE ALPHABET THE JOURNAL DRAWS IN, HELD OUT OF THE FIXTURE.
 *
 * The equality above is only reproducible if the bytes nobody chose cannot
 * lengthen a run: an object address and a request fingerprint are printed in
 * hexadecimal, so a document carrying a hexadecimal run as long as the
 * measured overlap makes the ceiling a lottery, and its red an intermittent
 * one nobody can reproduce. What is bounded is the form the measure really
 * hunts - the document as it is handed over, percent encoding included - and
 * not the needle a human wrote: `%20` in front of a hexadecimal word lengthens
 * the run by two, which is how such a run gets into a fixture unnoticed.
 */
TEST_F(ParseErrorFixtureTest, NoDocumentCarriesTheJournalsAlphabetThatFar)
{
    //A measure that cannot report a run reads as a clean zero below.
    ASSERT_EQ("beef", longestHexRun("zzbeefzz"));
    ASSERT_EQ("", longestHexRun("zz"));

    for (const Site &s: sites())
    {
        const std::string run = longestHexRun(s.input);
        EXPECT_LT(run.size(), kMaxEcho)
            << s.label << " carries \"" << run << "\", " << run.size()
            << " bytes of the alphabet the journal draws its own identifiers "
               "in, which the ceiling of " << kMaxEcho << " does not absorb: a "
               "draw can match them and the equality above becomes a lottery.";
    }
}

/*
 * Own main instead of gtest_main.
 *
 * The level is deliberately NOT raised: this suite reads what a stock box
 * writes, and the Logger domain map is filled once and never re-read.
 *
 * The exit is by _exit(): the amplifiers hold libuv handles on a loop this
 * binary has stopped pumping, and the Config singleton would rewrite the state
 * cache this suite deliberately left corrupt.
 */
int main(int argc, char **argv)
{
    //Before anything has a loop or a socket to duplicate.
    measureStockLogLevel();

    ::testing::InitGoogleTest(&argc, argv);

    SSL_library_init();
    OpenSSL_add_all_algorithms();

    //Where nlohmann really stops on each of these documents, so the
    //counterweight compares the published position to a measured one.
    for (Site &s: sites())
    {
        try
        {
            nlohmann::json::parse(s.input);
        }
        catch (const nlohmann::json::parse_error &e)
        {
            s.errorByte = e.byte;
        }
    }

    char tmpl[] = "/tmp/calaos_parseerr_cfg_XXXXXX";
    const char *base = ::mkdtemp(tmpl);
    if (base)
    {
        const std::string cfg = std::string(base) + "/config";
        const std::string cache = std::string(base) + "/cache";
        ::mkdir(cfg.c_str(), 0700);
        ::mkdir(cache.c_str(), 0700);

        Utils::initConfigOptions(const_cast<char *>(cfg.c_str()),
                                 const_cast<char *>(cache.c_str()), true);

        //Planted before the Config singleton exists: its constructor is what
        //reads it, and it runs once.
        const std::string path = Utils::getCacheFile("iostates.cache");
        if (writeFile(path, sites()[8].input))
            plantedCachePath() = path;
    }

    const int ret = RUN_ALL_TESTS();

    std::cout.flush();
    _exit(ret);
}
