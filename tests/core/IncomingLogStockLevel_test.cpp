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
 * WHAT AN UNTOUCHED BOX PRINTS ABOUT THE REQUESTS IT RECEIVES.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS SUITE EXISTS, AND WHAT IT IS NOT
 * ---------------------------------------------------------------------------
 * Seven families of lines on the incoming path publish a byte the client
 * chose, at a level a stock install prints: the identity of the client, two
 * static path refusals, the path of a refused websocket handshake, the reason
 * a websocket connection is being torn down, the key name of an uploaded
 * config file, the id of a refused set_state, and the month parameter of a
 * time range. None of them carries a secret this repository distributes -
 * that was established site by site, and re-established here.
 *
 * So this is not a leak being closed. It is a net: every one of those lines
 * renders ONE designated field, and the request that reaches it is stuffed
 * with credentials everywhere else - in the query string, in the headers, in
 * the body, in the payload of a frame. A line that widens by one byte of the
 * input starts rendering one of them and a case here goes red. Before the
 * suite existed, widening the handshake refusal from the path to the raw
 * target - query string, and the password api.php takes as a GET parameter,
 * included - turned nothing red anywhere in the tree.
 *
 * ---------------------------------------------------------------------------
 * WHY EVERY CASE STARTS FROM A SOCKET
 * ---------------------------------------------------------------------------
 * Nothing here calls a logging function, a reducer or a handler. A real
 * HttpServer listens on loopback, an ordinary socket sends the bytes of a real
 * request, libuv delivers them, and what is asserted is what the shipped code
 * wrote to std::cout on the way. A guard exercised by a direct call proves
 * that the guard works, never that the path reaches it.
 *
 * ---------------------------------------------------------------------------
 * WHY THE HAYSTACK IS A SUBSET OF THE JOURNAL
 * ---------------------------------------------------------------------------
 * The question is not "does the process ever print this byte" but "does an
 * install nobody configured print it". This binary raises its own level to
 * DEBUG - it has to, to observe that a debug line carries the byte at all - so
 * every assertion runs against the lines whose level marker is not [DBG],
 * which is exactly what maxLevelPrintable() lets through on a fresh install.
 * That the fallback really is that level is measured in a forked child, in
 * both directions and for the three domains these families use.
 *
 * A line without a marker belongs to a multi-line entry and is kept: erring
 * towards a wider haystack can only produce a red, never a green.
 *
 * ---------------------------------------------------------------------------
 * WHAT THE SENSOR LOOKS FOR
 * ---------------------------------------------------------------------------
 * Not a name and not a length: the longest RUN of a planted value the stock
 * lines give back, so that a line publishing the head or the tail of the same
 * value is caught by the same assertion. The runs are hunted in four forms -
 * raw, percent-decoded, base64-decoded, base64-encoded - plus the reversal the
 * month parameter goes through, and in two haystacks, the second with all
 * whitespace removed so that a value cut across two lines is not a way out.
 *
 * The ceiling is one above the incidental overlap this tree really produces,
 * and the suite re-derives that overlap at every run instead of trusting the
 * number: a fixture that gains a shared word would otherwise widen the blind
 * window with every assertion still green.
 *
 * ---------------------------------------------------------------------------
 * THE HALF THAT MUST NOT DIE WITH THE NET
 * ---------------------------------------------------------------------------
 * These lines exist to say where an abuse comes from and what was refused.
 * Every case that bounds a value also asserts that the designated field
 * survived: the proxy hop, the path, the file name, the io id. Withholding
 * everything would pass a bound and leave an operator with a mute journal.
 *
 * WHAT THIS DOES NOT PROVE: no journal of a real install is read; the families
 * that need an authenticated request are exercised with the credentials this
 * binary sets on itself, so nothing here says what a box with no credentials
 * configured would do; and the per-IP connection cap line, which publishes the
 * same identity as the throttle line, is reached by no case here.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "HttpServer.h"
#include "JsonApi.h"
#include "ListeRoom.h"
#include "Logger.h"
#include "Room.h"
#include "StringUtils.h"
#include "libuvw.h"

using namespace std::chrono;

namespace
{

/*
 * THE CREDENTIALS THE REQUESTS CARRY EVERYWHERE EXCEPT IN THE FIELD THE LINE
 * IS ALLOWED TO NAME.
 *
 * Pairwise unlike on purpose, and unlike the http vocabulary and the file
 * names of a configuration: a run two of them share is republished by
 * whichever line widens first and raises the bound of the other, so the red
 * set would name the fixture instead of the site. Held to the ceiling by a
 * case of its own.
 *
 * None of them carries a long run of hexadecimal, in any of the forms the
 * measure hunts. The journal publishes identifiers of its own in that
 * alphabet, so such a run can be matched by bytes nobody chose and the ceiling
 * becomes a lottery. The forms are where this bites: url encoding puts `%20`
 * in front of a word, and taking the blanks out joins two runs that the clear
 * text keeps apart - a digit after a space is four hexadecimal bytes either
 * way.
 */
const char *const kForwardedSecret = "kiwi7b3-nymphea";
const char *const kBearerSecret = "opale91x-pyrite";
const char *const kCookieSecret = "murmure46q-zythum";
const char *const kUnknownHeaderSecret = "griffon05w-douve";

//Percent-encoded on the wire: a search for the plain form does not see it.
const char *const kQuerySecret = "hamac m82 glaieul";

const char *const kBodySecret = "chalut73v-orfraie";
const char *const kUploadContentSecret = "brebis50k-cygne";
const char *const kFramePayloadSecret = "cyprin68d-wagon";
const char *const kCloseReasonSecret = "hublot24f-jonque";

/*
 * THE DESIGNATED FIELDS. Each is what one family is allowed to render, and
 * each is asserted present: this is the half a bound alone would let die.
 */
const char *const kProxyHop = "203.0.113.77";
const char *const kWebappLeaf = "page-absente-du-disque";
const char *const kDebugLeaf = "trace-absente-du-disque";
const char *const kHandshakePath = "/mauvaise-route";
const char *const kInventedKey = "clef-inventee-du-televersement";
const char *const kIoId = "io_chaine_de_ce_test";
const char *const kTimeRangeId = "io_horaire_de_ce_test";

//Not a bitset, so the month decoder throws and the refusal line runs. What it
//renders is the REVERSED string, which is a form the run search must know.
const char *const kMonthsMark = "mois-illisible-zz";

const char *const kApiUser = "operateur";
const char *const kApiPassword = "sesame-de-ce-binaire";

/*
 * One above the incidental overlap this tree really produces between a planted
 * value and what the stock lines legitimately publish. Re-derived by the
 * calibration case at every run rather than trusted.
 */
const size_t kMaxEcho = 4;

std::string stripSpace(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (char c: s)
    {
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            out.push_back(c);
    }
    return out;
}

//Longest run of `needle` the haystack gives back, wherever that run was cut.
std::string longestRun(const std::string &hay, const std::string &needle)
{
    std::string best;
    for (size_t i = 0; i < needle.size(); i++)
    {
        size_t len = best.size() + 1;
        while (i + len <= needle.size() &&
               hay.find(needle.substr(i, len)) != std::string::npos)
        {
            best = needle.substr(i, len);
            len++;
        }
    }
    return best;
}

//Every form the same bytes can reach a journal in. The reversal is not
//decoration: the month decoder reverses its parameter before publishing it.
std::vector<std::string> formsOf(const std::string &secret)
{
    std::vector<std::string> forms{ secret };

    const std::string decoded = Utils::url_decode(secret);
    if (decoded != secret)
        forms.push_back(decoded);

    //The form a raw request target reaches a journal in: a line that gave the
    //query string back would hand the credential over percent-encoded, and a
    //search for the plain text alone would call that green.
    const std::string encoded = Utils::url_encode(secret);
    if (encoded != secret)
        forms.push_back(encoded);

    std::string b64in = secret;
    const std::string b64out = Utils::Base64_decode(b64in);
    if (!b64out.empty() && b64out != secret)
        forms.push_back(b64out);

    std::string plain = secret;
    const std::string b64 = Utils::Base64_encode(plain);
    if (!b64.empty() && b64 != secret)
        forms.push_back(b64);

    std::string reversed = secret;
    std::reverse(reversed.begin(), reversed.end());
    if (reversed != secret)
        forms.push_back(reversed);

    return forms;
}

//Every form the measure hunts: formsOf() and, for each, the same bytes with
//the blanks taken out. Named apart so that the case bounding what a needle may
//carry reads the same set the measure searches for - a form the measure knows
//and the bound does not is a bound on the wrong string.
std::vector<std::string> measuredForms(const std::string &secret)
{
    std::vector<std::string> out;
    for (const std::string &f: formsOf(secret))
    {
        if (f.empty())
            continue;
        out.push_back(f);
        const std::string ff = stripSpace(f);
        if (!ff.empty() && ff != f)
            out.push_back(ff);
    }
    return out;
}

std::string longestEchoRun(const std::string &log, const std::string &secret)
{
    const std::string flat = stripSpace(log);

    std::string best;
    for (const std::string &f: measuredForms(secret))
    {
        //A form with its blanks taken out is hunted in a journal with its own
        //taken out: that is what makes a value folded over two lines one run.
        const std::string run = longestRun(f == stripSpace(f) ? flat : log, f);
        if (run.size() > best.size())
            best = run;
    }
    return best;
}

size_t longestEcho(const std::string &log, const std::string &secret)
{
    return longestEchoRun(log, secret).size();
}

//The longest run of the journal's own alphabet a form carries: an address and
//a request fingerprint are printed in it, so such a run can be matched by
//bytes nobody chose.
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

/*
 * THE LINES A FRESH INSTALL WOULD HAVE PRINTED, and only those. This process
 * runs at DEBUG so that a case can prove a byte reached the server at all;
 * every bound is measured on what is left once the debug entries are dropped.
 */
std::string stockLines(const std::string &log)
{
    std::istringstream in(log);
    std::string line, out;
    while (std::getline(in, line))
    {
        if (line.find("[DBG]") != std::string::npos)
            continue;
        out += line;
        out += "\n";
    }
    return out;
}

std::string captureStdout(const std::function<void()> &fn)
{
    std::ostringstream sink;
    std::streambuf *saved = std::cout.rdbuf(sink.rdbuf());
    fn();
    std::cout.rdbuf(saved);
    return sink.str();
}

bool someLineHasAll(const std::string &log, const std::vector<std::string> &needles)
{
    std::istringstream in(log);
    std::string line;
    while (std::getline(in, line))
    {
        bool all = true;
        for (const std::string &n: needles)
        {
            if (line.find(n) == std::string::npos)
            {
                all = false;
                break;
            }
        }
        if (all)
            return true;
    }
    return false;
}

void drainLoop(int ms)
{
    auto loop = uvw::Loop::getDefault();
    auto deadline = steady_clock::now() + milliseconds(ms);
    while (steady_clock::now() < deadline)
    {
        loop->run<uvw::Loop::Mode::NOWAIT>();
        std::this_thread::sleep_for(milliseconds(1));
    }
}

int pickFreePort()
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return 0;

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;

    int port = 0;
    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) == 0)
    {
        socklen_t len = sizeof(a);
        if (getsockname(fd, (struct sockaddr *)&a, &len) == 0)
            port = ntohs(a.sin_port);
    }
    close(fd);
    return port;
}

int &serverPort()
{
    static int p = 0;
    return p;
}

//The bytes of one exchange, and what the shipped code wrote to std::cout while
//a real HttpServer read them off a real socket.
struct Exchange
{
    bool connected = false;
    //What the client meant to send, masking undone: a frame payload never
    //appears on the wire in clear, so a fixture check read off the socket
    //bytes would call every websocket case vacuous.
    std::string sent;
    std::string log;
    std::string response;

    std::string stock() const { return stockLines(log); }
};

std::vector<std::pair<std::string, std::string>> credentialHeaders()
{
    return {
        { "X-Forwarded-For", std::string(kForwardedSecret) + ", " + kProxyHop },
        { "Authorization", std::string("Bearer ") + kBearerSecret },
        { "Cookie", std::string("calaos_session=") + kCookieSecret },
        { "X-Calaos-Future-Auth", kUnknownHeaderSecret },
        { "Connection", "close" },
    };
}

std::vector<std::pair<std::string, std::string>> websocketHeaders(const char *connection)
{
    auto headers = credentialHeaders();
    headers.back() = { "Connection", connection };
    headers.push_back({ "Upgrade", "websocket" });
    headers.push_back({ "Sec-WebSocket-Key", "x3JJHMbDL1EzLkh9GBhXDw==" });
    headers.push_back({ "Sec-WebSocket-Version", "13" });
    return headers;
}

//Encoded by the shipped encoder, so the bytes on the wire and the form the
//sensor hunts cannot drift apart.
std::string withQuery(const std::string &path)
{
    return path + "?cn_user=" + kApiUser + "&cn_pass=" + Utils::url_encode(kQuerySecret);
}

std::string buildRequest(const std::string &method,
                         const std::string &target,
                         const std::vector<std::pair<std::string, std::string>> &headers,
                         const std::string &body = std::string())
{
    std::ostringstream req;
    req << method << " " << target << " HTTP/1.1\r\n";
    req << "Host: 127.0.0.1:" << serverPort() << "\r\n";
    for (const auto &h: headers)
        req << h.first << ": " << h.second << "\r\n";
    if (!body.empty())
        req << "Content-Type: application/json\r\n"
            << "Content-Length: " << body.size() << "\r\n";
    req << "\r\n" << body;
    return req.str();
}

int openConnection()
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(serverPort());

    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

Exchange exchange(const std::string &raw)
{
    Exchange ex;
    ex.sent = raw;

    const int fd = openConnection();
    if (fd < 0)
        return ex;
    ex.connected = true;

    ex.log = captureStdout([&]()
    {
        if (send(fd, raw.data(), raw.size(), MSG_NOSIGNAL) < 0)
            return;

        auto loop = uvw::Loop::getDefault();
        const auto deadline = steady_clock::now() + seconds(5);
        while (steady_clock::now() < deadline)
        {
            loop->run<uvw::Loop::Mode::NOWAIT>();

            char buf[4096];
            const ssize_t n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
            if (n > 0)
                ex.response.append(buf, (size_t)n);

            if (ex.response.find("\r\n\r\n") != std::string::npos)
                break;

            std::this_thread::sleep_for(milliseconds(1));
        }

        close(fd);
        drainLoop(150);
    });

    return ex;
}

//A client frame is masked, always. Short payloads only (< 126 bytes).
std::string maskedFrame(unsigned char firstByte, const std::string &payload)
{
    static const unsigned char mask[4] = { 0x37, 0xfa, 0x21, 0x3d };

    std::string f;
    f.push_back((char)firstByte);
    f.push_back((char)(0x80 | (payload.size() & 0x7f)));
    for (int i = 0; i < 4; i++)
        f.push_back((char)mask[i]);
    for (size_t i = 0; i < payload.size(); i++)
        f.push_back((char)(payload[i] ^ mask[i % 4]));
    return f;
}

//A close frame payload: the two status bytes, then the reason the client chose.
std::string closePayload(uint16_t code, const std::string &reason)
{
    std::string p;
    p.push_back((char)((code >> 8) & 0xff));
    p.push_back((char)(code & 0xff));
    p += reason;
    return p;
}

//Handshake, then one frame on the same connection, both under one capture.
Exchange websocketExchange(const std::string &handshake, const std::string &frame,
                           const std::string &clearPayload)
{
    Exchange ex;
    ex.sent = handshake + clearPayload;

    const int fd = openConnection();
    if (fd < 0)
        return ex;
    ex.connected = true;

    ex.log = captureStdout([&]()
    {
        auto loop = uvw::Loop::getDefault();
        char buf[4096];

        if (send(fd, handshake.data(), handshake.size(), MSG_NOSIGNAL) < 0)
            return;

        auto deadline = steady_clock::now() + seconds(5);
        while (steady_clock::now() < deadline)
        {
            loop->run<uvw::Loop::Mode::NOWAIT>();
            const ssize_t n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
            if (n > 0)
                ex.response.append(buf, (size_t)n);
            if (ex.response.find("\r\n\r\n") != std::string::npos)
                break;
            std::this_thread::sleep_for(milliseconds(1));
        }

        if (send(fd, frame.data(), frame.size(), MSG_NOSIGNAL) < 0)
            return;

        deadline = steady_clock::now() + milliseconds(600);
        while (steady_clock::now() < deadline)
        {
            loop->run<uvw::Loop::Mode::NOWAIT>();
            const ssize_t n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
            if (n > 0)
                ex.response.append(buf, (size_t)n);
            std::this_thread::sleep_for(milliseconds(1));
        }

        close(fd);
        drainLoop(150);
    });

    return ex;
}

std::string jsonPost(const std::string &body,
                     const std::vector<std::pair<std::string, std::string>> &headers)
{
    return buildRequest("POST", withQuery("/api.php"), headers, body);
}

//An authenticated body: the credentials this binary set on itself, and a
//value under a key nothing in this tree names.
std::string apiBody(const std::string &fields, bool authenticated = true)
{
    std::ostringstream body;
    body << "{";
    if (authenticated)
        body << "\"cn_user\":\"" << kApiUser << "\",\"cn_pass\":\"" << kApiPassword << "\",";
    body << "\"x-calaos-champ-inconnu\":\"" << kBodySecret << "\"," << fields << "}";
    return body.str();
}

/*
 * EVERY EXCHANGE OF THIS SUITE, RUN ONCE AND IN ORDER.
 *
 * The throttle family needs two logins in a row with no reset between them,
 * and every other family needs the throttle cleared before it: running the
 * whole sequence once keeps that ordering explicit and lets the calibration
 * case re-derive its overlap over exactly what the other cases assert on.
 */
struct Runs
{
    Exchange loginRefused;
    Exchange loginThrottled;
    Exchange webappPath;
    Exchange debugPath;
    Exchange handshakeRefused;
    Exchange frameError;
    Exchange closeFrame;
    Exchange uploadBadKey;
    Exchange uploadBadContent;
    Exchange setState;
    Exchange setTimerange;

    //The first login is only there to open the backoff window: a stock install
    //prints nothing at all about it, so it carries no bound and no overlap.
    std::vector<const Exchange *> measured() const
    {
        return { &loginThrottled, &webappPath, &debugPath, &handshakeRefused,
                 &frameError, &closeFrame, &uploadBadKey, &uploadBadContent,
                 &setState, &setTimerange };
    }
};

const Runs &theRuns()
{
    static Runs runs = []()
    {
        Runs r;

        LoginThrottle::clear();
        r.loginRefused = exchange(jsonPost(apiBody("\"action\":\"get_home\"", false),
                                           credentialHeaders()));
        r.loginThrottled = exchange(jsonPost(apiBody("\"action\":\"get_home\"", false),
                                             credentialHeaders()));

        LoginThrottle::clear();
        r.webappPath = exchange(buildRequest("GET", withQuery(std::string("/app/") + kWebappLeaf),
                                             credentialHeaders()));

        LoginThrottle::clear();
        r.debugPath = exchange(buildRequest("GET", withQuery(std::string("/debug/") + kDebugLeaf),
                                            credentialHeaders()));

        LoginThrottle::clear();
        r.handshakeRefused = exchange(buildRequest("GET", withQuery(kHandshakePath),
                                                   websocketHeaders("Upgrade")));

        //RSV bits set: the frame parser refuses it and names the reason from
        //its own closed list, while the payload is the client's.
        LoginThrottle::clear();
        r.frameError = websocketExchange(buildRequest("GET", withQuery("/api"), websocketHeaders("Upgrade")),
                                         maskedFrame(0xc1, kFramePayloadSecret),
                                         kFramePayloadSecret);

        //A status code no close frame may carry, so the reason is replaced by
        //a literal of this tree while the client's own reason has arrived.
        LoginThrottle::clear();
        r.closeFrame = websocketExchange(buildRequest("GET", withQuery("/api"), websocketHeaders("Upgrade")),
                                         maskedFrame(0x88, closePayload(1005, kCloseReasonSecret)),
                                         closePayload(1005, kCloseReasonSecret));

        LoginThrottle::clear();
        {
            std::ostringstream files;
            files << "\"action\":\"config\",\"type\":\"put\",\"config_files\":{"
                  << "\"" << kInventedKey << "\":\"" << kUploadContentSecret << "\","
                  << "\"io.xml\":42}";
            r.uploadBadKey = exchange(jsonPost(apiBody(files.str()), credentialHeaders()));
        }

        LoginThrottle::clear();
        {
            std::ostringstream files;
            files << "\"action\":\"config\",\"type\":\"put\",\"config_files\":{"
                  << "\"io.xml\":\"" << kUploadContentSecret << "\","
                  << "\"" << kInventedKey << "\":\"x\"}";
            r.uploadBadContent = exchange(jsonPost(apiBody(files.str()), credentialHeaders()));
        }

        LoginThrottle::clear();
        r.setState = exchange(jsonPost(apiBody(std::string("\"action\":\"set_state\",\"id\":\"") +
                                               kIoId + "\",\"value\":\"etat \""),
                                       credentialHeaders()));

        LoginThrottle::clear();
        r.setTimerange = exchange(jsonPost(apiBody(std::string("\"action\":\"set_timerange\",\"id\":\"") +
                                                   kTimeRangeId + "\",\"months\":\"" +
                                                   kMonthsMark + "\""),
                                           credentialHeaders()));

        return r;
    }();

    return runs;
}

//What every request carries, whatever family it exercises: a bound that only
//covered the value one case was written for would be a bound on a name.
struct Needle
{
    const char *label;
    const char *value;
};

std::vector<Needle> travellingNeedles()
{
    return {
        { "the client supplied part of the forwarded line", kForwardedSecret },
        { "the bearer token of the client", kBearerSecret },
        { "the session cookie of the client", kCookieSecret },
        { "the value of a header this tree has never heard of", kUnknownHeaderSecret },
        { "the password api.php takes as a GET parameter", kQuerySecret },
    };
}

//Every value any request of this suite plants, which is what the ceiling is
//cut for.
std::vector<Needle> allNeedles()
{
    std::vector<Needle> n = travellingNeedles();
    n.push_back({ "the value of a body key this tree has never heard of", kBodySecret });
    n.push_back({ "the content of an uploaded config file", kUploadContentSecret });
    n.push_back({ "the payload of a refused frame", kFramePayloadSecret });
    n.push_back({ "the close reason of the client", kCloseReasonSecret });
    return n;
}

void expectNoRunSurvives(const Exchange &ex, const std::vector<Needle> &needles,
                         const std::string &what)
{
    const std::string stock = ex.stock();

    for (const Needle &n: needles)
    {
        //On the wire in whatever form the request encodes it: a fixture check
        //spelled on the plain text would pass for absence, not for withheld.
        bool onTheWire = false;
        for (const std::string &f: formsOf(n.value))
        {
            if (!f.empty() && ex.sent.find(f) != std::string::npos)
                onTheWire = true;
        }
        ASSERT_TRUE(onTheWire)
            << "the request that was sent does not carry " << n.label
            << " in any form, so this case measures nothing";

        const size_t echo = longestEcho(stock, n.value);
        EXPECT_LT(echo, kMaxEcho)
            << what << " gives back " << echo << " consecutive bytes of "
            << n.label << " (" << n.value << "), on a line a stock install "
            "prints:\n" << stock;
    }
}

struct LevelProbe
{
    bool ran = false;
    bool warningPrinted = false;
    bool errorPrinted = false;
    bool debugPrinted = false;
};

LevelProbe &levelProbe(size_t i)
{
    static LevelProbe probes[3];
    return probes[i];
}

const char *const kProbedDomains[3] = { "network", "websocket", "mcp" };

/*
 * WHAT A BOX PRINTS WITH NOBODY TOUCHING ANYTHING, measured in a child.
 *
 * The Logger fills its domain map once and never re-reads it, so a process
 * that has raised the level can no longer observe the default. The child never
 * raises it. The default comes from the fallback of maxLevelPrintable(), not
 * from the registered default of the debug_level option, which is never
 * consulted while the option has never been written.
 */
void measureStockLogLevels()
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

        unsigned char answer[3] = { 0, 0, 0 };
        char tmpl[] = "/tmp/calaos_stocklvl_XXXXXX";
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

            for (size_t i = 0; i < 3; i++)
            {
                answer[i] = 0x8;
                if (Utils::calaosLogger(kProbedDomains[i])->isLevelEnabled(Logger::LOG_LEVEL_WARNING))
                    answer[i] |= 0x1;
                if (Utils::calaosLogger(kProbedDomains[i])->isLevelEnabled(Logger::LOG_LEVEL_ERROR))
                    answer[i] |= 0x2;
                if (Utils::calaosLogger(kProbedDomains[i])->isLevelEnabled(Logger::LOG_LEVEL_DEBUG))
                    answer[i] |= 0x4;
            }
        }

        if (::write(fds[1], answer, 3) != 3)
            answer[0] = 0;
        ::close(fds[1]);
        _exit(0);
    }

    ::close(fds[1]);

    unsigned char answer[3] = { 0, 0, 0 };
    const ssize_t got = ::read(fds[0], answer, 3);
    ::close(fds[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);

    if (got != 3)
        return;

    for (size_t i = 0; i < 3; i++)
    {
        if (!(answer[i] & 0x8))
            continue;
        levelProbe(i).ran = true;
        levelProbe(i).warningPrinted = (answer[i] & 0x1) != 0;
        levelProbe(i).errorPrinted = (answer[i] & 0x2) != 0;
        levelProbe(i).debugPrinted = (answer[i] & 0x4) != 0;
    }
}

class IncomingLogStockLevelTest: public ::testing::Test
{
};

} // namespace

/*
 * THE IDENTITY OF THE CLIENT.
 *
 * Behind haproxy the identity is the LAST entry of the forwarded line, which
 * the proxy wrote; everything before it is chosen by the client and explicitly
 * ignored. The refusal line names the identity, so what must not reach it is
 * the part the client wrote, and the credentials that travel beside it.
 */
TEST_F(IncomingLogStockLevelTest, TheThrottleLineNamesTheProxyHopAndNothingElseTheClientWrote)
{
    const Exchange &ex = theRuns().loginThrottled;

    ASSERT_TRUE(ex.connected) << "no connection to the server, this case measures nothing";

    const std::string stock = ex.stock();
    ASSERT_TRUE(someLineHasAll(stock, {"Too many failed logins"}))
        << "the second login was not refused by the throttle, so the line this "
           "case is written for never ran:\n" << stock;

    //What ranks this family: no credential of this binary is anywhere in the
    //request, and the line ran all the same.
    ASSERT_EQ(std::string::npos, ex.sent.find(kApiPassword))
        << "the request carries the credentials of this binary, so this case "
           "does not show the family is reached without authenticating";

    expectNoRunSurvives(ex, travellingNeedles(), "the throttle refusal");

    //The half a bound would let die: an operator reads this line to know who.
    EXPECT_TRUE(someLineHasAll(stock, {"Too many failed logins", kProxyHop}))
        << "the refusal no longer names the address the proxy hop saw, so the "
           "journal cannot say where the abuse comes from:\n" << stock;
}

/*
 * THE TWO STATIC PATH REFUSALS. The line renders the path with its first
 * segment cut off, so what it must never gain is the query string - which is
 * where api.php takes a password.
 */
TEST_F(IncomingLogStockLevelTest, ARefusedWebappPathDoesNotDragTheQueryStringWithIt)
{
    const Exchange &ex = theRuns().webappPath;

    ASSERT_TRUE(ex.connected) << "no connection to the server, this case measures nothing";

    const std::string stock = ex.stock();
    ASSERT_TRUE(someLineHasAll(stock, {"Rejected webapp path"}))
        << "the request was not refused by the webapp branch, so the line this "
           "case is written for never ran:\n" << stock;

    expectNoRunSurvives(ex, travellingNeedles(), "the webapp refusal");

    EXPECT_TRUE(someLineHasAll(stock, {"Rejected webapp path", kWebappLeaf}))
        << "the refusal no longer names the file that was asked for:\n" << stock;
}

TEST_F(IncomingLogStockLevelTest, ARefusedDebugPathDoesNotDragTheQueryStringWithIt)
{
    const Exchange &ex = theRuns().debugPath;

    ASSERT_TRUE(ex.connected) << "no connection to the server, this case measures nothing";

    const std::string stock = ex.stock();
    ASSERT_TRUE(someLineHasAll(stock, {"Rejected debug path"}))
        << "the debug branch was not taken, so the line this case is written "
           "for never ran - the option gating it is set by main():\n" << stock;

    expectNoRunSurvives(ex, travellingNeedles(), "the debug path refusal");

    EXPECT_TRUE(someLineHasAll(stock, {"Rejected debug path", kDebugLeaf}))
        << "the refusal no longer names the file that was asked for:\n" << stock;
}

/*
 * THE REFUSED HANDSHAKE. This is the line a mutation widened from the path to
 * the raw target with nothing in the tree going red, and a raw target carries
 * the query string of a login.
 */
TEST_F(IncomingLogStockLevelTest, ARefusedHandshakePublishesItsPathAndNotItsTarget)
{
    const Exchange &ex = theRuns().handshakeRefused;

    ASSERT_TRUE(ex.connected) << "no connection to the server, this case measures nothing";

    const std::string stock = ex.stock();
    ASSERT_TRUE(someLineHasAll(stock, {"wrong path"}))
        << "the handshake was not refused for its path, so the line this case "
           "is written for never ran:\n" << stock;

    expectNoRunSurvives(ex, travellingNeedles(), "the handshake refusal");

    EXPECT_TRUE(someLineHasAll(stock, {"wrong path", kHandshakePath}))
        << "the refusal no longer names the path that was asked for:\n" << stock;
}

/*
 * THE REASON A CONNECTION IS TORN DOWN, WHEN THE FRAME DID NOT PARSE. The
 * reason published there comes from a closed list written in this tree; the
 * payload that provoked it is the client's and must not join it.
 */
TEST_F(IncomingLogStockLevelTest, TheTeardownReasonComesFromThisTreeAndNotFromTheFrame)
{
    const Exchange &ex = theRuns().frameError;

    ASSERT_TRUE(ex.connected) << "no connection to the server, this case measures nothing";

    const std::string stock = ex.stock();
    ASSERT_TRUE(someLineHasAll(stock, {"Error in websocket handling"}))
        << "no frame error was reported, so the line this case is written for "
           "never ran:\n" << stock;

    std::vector<Needle> needles = travellingNeedles();
    needles.push_back({ "the payload of the refused frame", kFramePayloadSecret });
    expectNoRunSurvives(ex, needles, "the frame error line");

    EXPECT_TRUE(someLineHasAll(stock, {"Error in websocket handling", "RSV"}))
        << "the teardown no longer says why the frame was refused:\n" << stock;
}

/*
 * THE CLOSE REASON THE CLIENT CHOSE. It reaches the server, it is written to
 * the journal, and the level it goes out at is the whole question: a stock
 * install must print the literal this tree substitutes, never the bytes.
 */
TEST_F(IncomingLogStockLevelTest, TheCloseReasonOfTheClientStaysOffTheLinesAStockBoxPrints)
{
    const Exchange &ex = theRuns().closeFrame;

    ASSERT_TRUE(ex.connected) << "no connection to the server, this case measures nothing";

    //Anti-vacuity, and the point of the case: the bytes really did arrive, and
    //the process really did write them somewhere.
    ASSERT_NE(std::string::npos, ex.log.find(kCloseReasonSecret))
        << "the close reason never reached the frame reader at all, so this "
           "case measures nothing:\n" << ex.log;

    std::vector<Needle> needles = travellingNeedles();
    needles.push_back({ "the close reason of the client", kCloseReasonSecret });
    expectNoRunSurvives(ex, needles, "the close handling");

    const std::string stock = ex.stock();
    EXPECT_TRUE(someLineHasAll(stock, {"websocket", "close status code"}))
        << "a stock install is no longer told that a close frame was refused:\n"
        << stock;
}

/*
 * THE KEY NAME OF AN UPLOADED CONFIG FILE, at a level a stock install prints.
 * The name is what the refusal is about and it survives; the VALUE stored
 * under it never had a reason to leave, and neither had the credentials of the
 * request that carried it.
 */
TEST_F(IncomingLogStockLevelTest, AnUploadRefusedForItsKeyPublishesTheKeyAndNotItsValue)
{
    const Exchange &ex = theRuns().uploadBadKey;

    ASSERT_TRUE(ex.connected) << "no connection to the server, this case measures nothing";

    const std::string stock = ex.stock();
    ASSERT_TRUE(someLineHasAll(stock, {"is not a valid config filename"}))
        << "the upload was not refused for its key name, so the line this case "
           "is written for never ran:\n" << stock;
    ASSERT_TRUE(someLineHasAll(stock, {"is not a string"}))
        << "the second key was not refused for its type, so one of the three "
           "sites of this family never ran:\n" << stock;

    std::vector<Needle> needles = travellingNeedles();
    needles.push_back({ "the value stored under the invented key", kUploadContentSecret });
    needles.push_back({ "the value of a body key this tree has never heard of", kBodySecret });
    expectNoRunSurvives(ex, needles, "the upload refusal");

    EXPECT_TRUE(someLineHasAll(stock, {"is not a valid config filename", kInventedKey}))
        << "the refusal no longer names the key it refused:\n" << stock;
}

/*
 * THE SAME FAMILY, ON THE BRANCH THAT HAS THE FILE CONTENT IN HAND. The size
 * is published and the bytes are not, and that is what must not drift.
 */
TEST_F(IncomingLogStockLevelTest, AnUploadRefusedForItsContentPublishesTheNameAndTheSize)
{
    const Exchange &ex = theRuns().uploadBadContent;

    ASSERT_TRUE(ex.connected) << "no connection to the server, this case measures nothing";

    const std::string stock = ex.stock();
    ASSERT_TRUE(someLineHasAll(stock, {"is not XML"}))
        << "the content was not refused for not being XML, so the line this "
           "case is written for never ran:\n" << stock;

    std::vector<Needle> needles = travellingNeedles();
    needles.push_back({ "the content of the refused config file", kUploadContentSecret });
    needles.push_back({ "the value of a body key this tree has never heard of", kBodySecret });
    expectNoRunSurvives(ex, needles, "the refused content line");

    EXPECT_TRUE(someLineHasAll(stock, {"is not XML", "io.xml", "bytes"}))
        << "the refusal no longer says which file was refused nor how big it "
           "was, so nothing of the diagnosis is left:\n" << stock;
}

/*
 * THE ID OF A REFUSED set_state. The id is what the line is about; the
 * credentials the same body carried, and the headers of the request, are not.
 */
TEST_F(IncomingLogStockLevelTest, ARefusedSetStatePublishesTheIoIdAndNotTheRestOfTheBody)
{
    const Exchange &ex = theRuns().setState;

    ASSERT_TRUE(ex.connected) << "no connection to the server, this case measures nothing";

    const std::string stock = ex.stock();
    ASSERT_TRUE(someLineHasAll(stock, {"set_state refused"}))
        << "the set_state was not refused, so the line this case is written for "
           "never ran - the io it names is created by main():\n" << stock;

    std::vector<Needle> needles = travellingNeedles();
    needles.push_back({ "the value of a body key this tree has never heard of", kBodySecret });
    expectNoRunSurvives(ex, needles, "the set_state refusal");

    EXPECT_TRUE(someLineHasAll(stock, {"set_state refused", kIoId}))
        << "the refusal no longer names the io it refused:\n" << stock;
}

/*
 * THE MONTH PARAMETER OF A TIME RANGE, at a level a stock install prints. It
 * is rendered REVERSED, which is why the run search knows that form: a sensor
 * that only looked for the bytes as sent would be blind to the line as
 * written.
 */
TEST_F(IncomingLogStockLevelTest, ARefusedMonthParameterPublishesItselfAndNothingAroundIt)
{
    const Exchange &ex = theRuns().setTimerange;

    ASSERT_TRUE(ex.connected) << "no connection to the server, this case measures nothing";

    const std::string stock = ex.stock();
    ASSERT_TRUE(someLineHasAll(stock, {"wrong parameters for months"}))
        << "the month parameter was not refused, so the line this case is "
           "written for never ran - the time range it names is created by "
           "main():\n" << stock;

    std::vector<Needle> needles = travellingNeedles();
    needles.push_back({ "the value of a body key this tree has never heard of", kBodySecret });
    expectNoRunSurvives(ex, needles, "the month refusal");

    std::string reversed = kMonthsMark;
    std::reverse(reversed.begin(), reversed.end());
    EXPECT_TRUE(someLineHasAll(stock, {"wrong parameters for months", reversed}))
        << "the refusal no longer names the parameter it refused:\n" << stock;
}

/*
 * THE CEILING, RE-DERIVED INSTEAD OF TRUSTED.
 *
 * A bounded sensor is only as narrow as the number above the noise it was cut
 * for, and that number drifts both ways with nobody watching: a fixture that
 * gains a shared word widens the blind window while every assertion stays
 * green, and a ceiling wider than the run the tree really produces is blind
 * space nobody asked for. Pinning the equality turns both into a red that says
 * which number to write.
 */
TEST_F(IncomingLogStockLevelTest, TheCeilingIsHeldToTheOverlapThisSuiteMeasures)
{
    //A measure that cannot report a run reads as a clean zero everywhere else.
    ASSERT_EQ("bcdef", longestRun("zzbcdefzz", "abcdefg"));
    ASSERT_EQ("", longestRun("zzz", "abc"));

    std::string worst, worstLabel;
    for (const Exchange *ex: theRuns().measured())
    {
        ASSERT_TRUE(ex->connected)
            << "one exchange of the suite never connected, so the overlap this "
               "case pins was not produced at all";

        const std::string stock = ex->stock();
        ASSERT_FALSE(stock.empty())
            << "one exchange printed nothing a stock install would print, so "
               "every bound of this suite reads as a clean zero:\n" << ex->log;

        for (const Needle &n: allNeedles())
        {
            const std::string run = longestEchoRun(stock, n.value);
            if (run.size() > worst.size())
            {
                worst = run;
                worstLabel = n.label;
            }
        }
    }

    EXPECT_EQ(kMaxEcho, worst.size() + 1)
        << "the ceiling is " << kMaxEcho << " while the lines a stock install "
           "prints give back at most " << worst.size() << " bytes of what a "
           "client sent, on " << worstLabel << ", run \"" << worst << "\". "
           "Everything between the two is a window this suite cannot see into: "
           "either a line has widened, or the fixture has, and the ceiling to "
           "write is " << (worst.size() + 1) << ".";
}

/*
 * THE FIXTURE, HELD TO THE SAME CEILING. A run two planted values share is
 * republished by whichever line widens first and raises the bound of the
 * other, and the red set then names the fixture instead of the site.
 */
TEST_F(IncomingLogStockLevelTest, NoTwoPlantedValuesShareARunTheCeilingWouldNotAbsorb)
{
    const std::vector<Needle> needles = allNeedles();

    for (size_t i = 0; i < needles.size(); i++)
    {
        for (size_t j = i + 1; j < needles.size(); j++)
        {
            const std::string run = longestRun(needles[i].value, needles[j].value);
            EXPECT_LT(run.size(), kMaxEcho)
                << needles[i].label << " and " << needles[j].label << " share \""
                << run << "\", " << run.size() << " bytes, which the ceiling of "
                << kMaxEcho << " does not absorb: a widening on either reddens "
                "the bound of the other and the red set stops naming a site.";
        }
    }
}

/*
 * AND THE FIXTURE HELD AGAINST WHAT THE LINES ARE ALLOWED TO SAY. A planted
 * value sharing a run with a designated field would be given back by a line
 * doing exactly its job.
 */
TEST_F(IncomingLogStockLevelTest, NoPlantedValueSharesARunWithAFieldTheLinesMayPublish)
{
    const char *const published[] = {
        kProxyHop, kWebappLeaf, kDebugLeaf, kHandshakePath, kInventedKey,
        kIoId, kTimeRangeId, kMonthsMark, kApiUser, "io.xml",
    };

    for (const Needle &n: allNeedles())
    {
        for (const char *field: published)
        {
            const std::string run = longestRun(field, n.value);
            EXPECT_LT(run.size(), kMaxEcho)
                << n.label << " shares \"" << run << "\", " << run.size()
                << " bytes, with \"" << field << "\", which the lines of this "
                "path publish by design: the ceiling would absorb a real "
                "widening.";
        }
    }
}

/*
 * AND THE FIXTURE HELD OUT OF THE ALPHABET THE JOURNAL DRAWS IN.
 *
 * The equality above is only reproducible if the bytes nobody chose cannot
 * lengthen a run: an object address and a request fingerprint are printed in
 * hexadecimal, so a value carrying a hexadecimal run as long as the measured
 * overlap makes the ceiling a lottery, and its red an intermittent one nobody
 * can reproduce. Every form the measure hunts is bounded and not only the
 * literal: url encoding and taking the blanks out both lengthen a run the
 * clear text keeps short.
 */
TEST_F(IncomingLogStockLevelTest, NoPlantedValueCarriesTheJournalsAlphabetThatFar)
{
    //A measure that cannot report a run reads as a clean zero below.
    ASSERT_EQ("beef", longestHexRun("zzbeefzz"));
    ASSERT_EQ("", longestHexRun("zz"));

    for (const Needle &n: allNeedles())
    {
        for (const std::string &form: measuredForms(n.value))
        {
            const std::string run = longestHexRun(form);
            EXPECT_LT(run.size(), kMaxEcho)
                << n.label << " carries \"" << run << "\", " << run.size()
                << " bytes of the alphabet the journal draws its own "
                   "identifiers in, which the ceiling of " << kMaxEcho
                << " does not absorb: a draw can match them and the equality "
                   "above becomes a lottery. The form is \"" << form << "\".";
        }
    }
}

/*
 * THE LEVEL EVERY BOUND ABOVE RESTS ON.
 *
 * The haystack of this suite is "the lines whose level a fresh install
 * prints". If the shipped fallback moved, that haystack would stop being what
 * its name says - in one direction the bounds would cover lines nobody sees,
 * in the other they would stop covering lines everybody does. Both are pinned,
 * for the three domains these families use.
 */
TEST_F(IncomingLogStockLevelTest, AStockInstallPrintsWarningsAndErrorsButNoDebug)
{
    for (size_t i = 0; i < 3; i++)
    {
        const LevelProbe &probe = levelProbe(i);
        const char *const domain = kProbedDomains[i];

        ASSERT_TRUE(probe.ran)
            << "the stock-level child could not be forked or answered nothing "
               "for the " << domain << " domain, so this case measures no level";

        EXPECT_TRUE(probe.warningPrinted)
            << "the " << domain << " domain does not print at WARNING on a "
               "stock install, so the lines this suite bounds are not the ones "
               "an untouched box shows and every bound covers a line nobody sees";
        EXPECT_TRUE(probe.errorPrinted)
            << "the " << domain << " domain does not print at ERROR on a stock "
               "install";
        EXPECT_FALSE(probe.debugPrinted)
            << "the " << domain << " domain prints at DEBUG on a stock install, "
               "so every debug line of the incoming path leaves a box nobody "
               "configured and the haystack of this suite is too narrow";
    }
}

/*
 * Own main instead of gtest_main: the level has to be raised before the first
 * log line of the process, the Logger domain map being filled once and never
 * re-read; the listening server and the two ios the refusal families need have
 * to exist before any case runs.
 */
int main(int argc, char **argv)
{
    //Before anything raises the level, and before there is a loop to duplicate.
    measureStockLogLevels();

    ::testing::InitGoogleTest(&argc, argv);

    char tmpl[] = "/tmp/calaos_stocklog_cfg_XXXXXX";
    const char *base = ::mkdtemp(tmpl);
    if (base)
    {
        const std::string cfg = std::string(base) + "/config";
        const std::string cache = std::string(base) + "/cache";
        ::mkdir(cfg.c_str(), 0700);
        ::mkdir(cache.c_str(), 0700);

        Utils::initConfigOptions(const_cast<char *>(cfg.c_str()),
                                 const_cast<char *>(cache.c_str()), true);
        Utils::set_config_option("debug_level", "5");
        //Loopback only: this binds a real listening socket.
        Utils::set_config_option("listen_address", "127.0.0.1");
        //One of the seven families sits behind this option.
        Utils::set_config_option("debug_enabled", "true");
        //Set rather than assumed: what a box ships with is another question,
        //and the authenticated families must not depend on the answer.
        Utils::set_config_option("cn_user", kApiUser);
        Utils::set_config_option("cn_pass", kApiPassword);
    }

    Room *room = new Room("Salle de mesure", "salon", 0);
    ListeRoom::Instance().Add(room);
    ListeRoom::Instance().createIO({ { "type", "InternalString" },
                                     { "id", kIoId },
                                     { "name", "Chaine de mesure" } }, room);
    ListeRoom::Instance().createIO({ { "type", "InPlageHoraire" },
                                     { "id", kTimeRangeId },
                                     { "name", "Horaire de mesure" } }, room);

    serverPort() = pickFreePort();
    if (serverPort() <= 0)
    {
        std::cerr << "could not reserve a loopback port" << std::endl;
        return 1;
    }

    HttpServer::Instance(serverPort());
    drainLoop(50);

    return RUN_ALL_TESTS();
}
