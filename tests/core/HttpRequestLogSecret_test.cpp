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
 * WHAT THE SERVER WRITES ABOUT THE REQUESTS IT RECEIVES.
 *
 * ---------------------------------------------------------------------------
 * THE DEFECT
 * ---------------------------------------------------------------------------
 * Once a request was parsed, HttpClient published its target and then the
 * VALUE of every header it carried, one line each, with no exception and no
 * reduction of any kind. The carriers are not hypothetical and this repository
 * makes them itself: get_mcp_info hands an admin the MCP bearer token together
 * with the instruction to send it back in Authorization, HMACAuthenticator
 * reads a second bearer beside its nonce and its signature, and a browser
 * sends its session cookie. The server wrote into its journal the tokens it
 * had handed out. The target went out raw on the line above, and api.php takes
 * cn_user/cn_pass as GET parameters.
 *
 * The severity is bounded and the ticket says so: these lines are cDebugDom,
 * and the shipped fallback of Logger::maxLevelPrintable() is INFO, so a stock
 * box prints none of them. That is measured here, in a forked child, not
 * asserted - and it is measured in BOTH directions, so that neither a lowered
 * fallback nor a raised one goes unnoticed.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS GOES THROUGH A REAL SERVER
 * ---------------------------------------------------------------------------
 * Nothing here calls the reducer, and nothing here calls processHeaders(). A
 * real HttpServer listens on loopback, an ordinary socket sends the bytes of a
 * real request, libuv delivers them, and what is asserted is what the shipped
 * code wrote to std::cout on the way. Putting the old loop back turns these
 * cases red.
 *
 * ---------------------------------------------------------------------------
 * WHAT THE SENSOR LOOKS FOR, AND WHAT IT DELIBERATELY DOES NOT
 * ---------------------------------------------------------------------------
 * Not a name and not a length. One of the headers the fixture sends has a name
 * that appears NOWHERE in this tree (X-Calaos-Future-Auth): a guard spelled on
 * the names known today is blind to it, which is the whole argument against a
 * redaction list. And the log is asked how much of each planted value it gives
 * back, in the longest RUN, so that a line publishing the first bytes or the
 * last bytes of the same value is caught by the same assertion.
 *
 * The needles are hunted in four forms - raw, percent-decoded, base64-decoded,
 * base64-encoded - and in two haystacks: the journal, and the journal with
 * every space and newline removed, so that a value cut across two lines is not
 * a way through.
 *
 * ---------------------------------------------------------------------------
 * THE HALF THAT MUST NOT DIE WITH THE SECRET
 * ---------------------------------------------------------------------------
 * An operator reads these lines to say WHICH request arrived and WHETHER it
 * was accepted. Both are asserted, and so is the presence of the headers
 * themselves: knowing that an Authorization was there is the diagnosis, its
 * value never was. Two requests differing only in their query string, of the
 * same length and the same parameter count, must still render as two different
 * lines.
 *
 * THE SECOND TRANSPORT HAS A BODY, AND IT WAS RENDERED
 * ---------------------------------------------------------------------------
 * The api request of a websocket client is the payload of a text frame, and
 * the frame line printed its first forty bytes before the json handler ever
 * built its redacted dump. How much of a credential escaped therefore depended
 * on where it sat in the document - a sensor that depends on a length, written
 * into production code. One case sends a real masked frame over a real
 * handshake and bounds what comes back.
 *
 * AND THE BODY WENT OUT THE SAME WAY THE HEADERS DID
 * ---------------------------------------------------------------------------
 * The body of an api request was published through a list of eleven credential
 * NAMES, on both transports, before the credentials were checked. The API of
 * this repository opens that list itself: config/put uploads local_config.xml,
 * io.xml and rules.xml under keys that are FILE NAMES, and local_config.xml is
 * where this server writes the mcp_token it hands out. set_param is the same
 * shape one level down - the word "password" is the VALUE of `param` and the
 * camera credential sits under `value`. The cases below send those requests
 * over the socket, one of them with no credentials at all, and bound what
 * comes back of a token this repository generated itself.
 *
 * WHAT THIS DOES NOT PROVE: no journal of a real install is read, the MCP
 * bearer is planted by the fixture rather than obtained from get_mcp_info, and
 * the upload is never ACCEPTED by any case - a successful config/put restarts
 * the server, so the branch that used to publish a non-XML file content whole
 * is reached by no case here.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ConfigStore.h"
#include "HMACAuthenticator.h"
#include "HttpServer.h"
#include "JsonApi.h"
#include "LogSetup.h"
#include "Logger.h"
#include "StringUtils.h"
#include "libuvw.h"

using namespace std::chrono;

namespace
{

/*
 * FIXTURE, NOT DECORATION.
 *
 * Four carriers, in the four shapes an incoming request has for one: the
 * bearer this server hands out on get_mcp_info, the session cookie of a
 * browser, the credential api.php accepts as a GET parameter, and a header
 * whose NAME does not exist anywhere in this tree. Each value is unique enough
 * that no other string of this binary can produce it by accident, and every
 * case asserts the bytes it just put on the socket really carry them.
 */
//None of them reuses a word of the http vocabulary. Measured: with the first
//wording, "signature-..." shared the run "signature" with the header NAME the
//reduced line legitimately publishes, and "...-websocket-..." shared eleven
//bytes with "sec-websocket-key" - incidental overlap, not a leak, and it would
//have set the ceiling instead of the secret.
//They must also be pairwise unlike EACH OTHER, and two pairs were not: a
//shared word makes a leak at one carrier redden the bound of the other, and
//the red set stops naming a carrier. The disjunction case below holds it.
//And none of them ends in a hexadecimal tail any more. The journal publishes
//identifiers of its own in that alphabet - an object address, a request
//fingerprint - so a hexadecimal needle can be matched by bytes the journal
//drew at random, which turns the ceiling into a lottery no run of the suite
//reproduces.
const char *const kBearerToken = "jeton-porteur-quatorze-anemones";
const char *const kSessionCookie = "biscuit-mirliton-vespral";
const char *const kFutureAuthValue = "cle-que-nul-ne-connait-lampyre";
const char *const kHmacNonce = "alea-tirage-hulotte-pivoine";
const char *const kHmacSignature = "empreinte-scellee-tulipier";
const char *const kUserAgentMark = "marque-du-mobile-genepi";

//Percent-encoded on purpose: a search for the plain form does not see it, and
//that is how a tail of URL walked past the guard of a sibling ticket.
const char *const kQueryPassword = "mot%20de%20passe%20du%20portier%20de%20nuit";

//The other spelling of an Authorization header. Its value is base64, so a
//journal can give the secret back in a form no search for the plain text sees.
const char *const kBasicPlain = "huissier:antienne-glaieul";

//The credential of a websocket login, placed at the head of the frame on
//purpose: the frame line rendered a fixed-length EXCERPT of the payload, so
//how much of a secret it gave back depended on where the secret sat.
const char *const kWsFramePassword = "cadenas-vigie-ombelle";

/*
 * WHAT AN UPLOAD CARRIES, IN THE THREE PLACES NO LIST OF NAMES REACHED.
 *
 * The three values are pairwise unlike on purpose: the longest run two of them
 * share, and the longest run any of them shares with what the reduced lines
 * legitimately publish, are measured in the fiche and both sit far under the
 * ceiling. A common suffix here would let one needle raise the bound of the
 * others and turn a leak into a green.
 */
const char *const kCameraSecret = "brumairependulealpaga";
const char *const kInventedKeySecret = "grimoirevolubilischataigne";
const char *const kSetParamSecret = "talismanverjusnenuphar";
const char *const kWsInventedSecret = "philtrezephyr";

//The content of a file an upload offers and the server refuses. Unlike the
//four above on the same measure: worst run shared with anything else of the
//fixture, 3 bytes.
const char *const kRefusedContentSecret = "grelotmyosotiscerfeuil";

/*
 * Above the incidental overlap between a planted value and what the reduced
 * lines legitimately publish (byte counts, a port, a hexadecimal tag share
 * characters with them) and far below any excerpt of a credential worth
 * having. The number is not a decision: the ceiling case at the end of this
 * file replays every probe the ceiling covers, re-measures that overlap and
 * pins the equality, so a ceiling wider than the tree produces is a red
 * instead of a setting nobody revisits.
 */
const size_t kMaxEcho = 5;

/*
 * AND THE ONE DOCUMENT NOBODY CHOSE NEEDS A WIDER ONE.
 *
 * The generated token is 64 hexadecimal characters, the alphabet the journal
 * draws its own identifiers in, and neither side is under the control of this
 * fixture. Its overlap is therefore not a quantity to re-measure but a
 * probability, and no equality can pin it: at 5 the two would collide about
 * once in thirteen hundred exchanges, at 8 less than once in ten million.
 * That margin, and not slack, is what the difference between the two ceilings
 * buys.
 */
const size_t kMaxDrawnEcho = 8;

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
//The run itself and not its length: a failure that cites the bytes says
//whether what came back is a credential or a word the reduced lines are
//entitled to publish.
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

//Every form the same bytes can reach a journal in, and a haystack with the
//whitespace taken out so that a value split over two lines is still one run.
std::string longestEchoRun(const std::string &log, const std::string &secret)
{
    std::vector<std::string> forms{ secret };

    const std::string decoded = Utils::url_decode(secret);
    if (decoded != secret)
        forms.push_back(decoded);

    std::string b64in = secret;
    const std::string b64out = Utils::Base64_decode(b64in);
    if (!b64out.empty() && b64out != secret)
        forms.push_back(b64out);

    std::string plain = secret;
    const std::string encoded = Utils::Base64_encode(plain);
    if (!encoded.empty() && encoded != secret)
        forms.push_back(encoded);

    const std::string flat = stripSpace(log);

    std::string best;
    const auto keep = [&best](const std::string &run)
    {
        if (run.size() > best.size())
            best = run;
    };

    for (const std::string &f: forms)
    {
        if (f.empty())
            continue;
        keep(longestRun(log, f));
        const std::string ff = stripSpace(f);
        if (!ff.empty())
            keep(longestRun(flat, ff));
    }
    return best;
}

size_t longestEcho(const std::string &log, const std::string &secret)
{
    return longestEchoRun(log, secret).size();
}

std::string captureStdout(const std::function<void()> &fn)
{
    std::ostringstream sink;
    std::streambuf *saved = std::cout.rdbuf(sink.rdbuf());
    fn();
    std::cout.rdbuf(saved);
    return sink.str();
}

std::string lineContaining(const std::string &log, const std::string &needle)
{
    std::istringstream in(log);
    std::string line;
    while (std::getline(in, line))
    {
        if (line.find(needle) != std::string::npos)
            return line;
    }
    return std::string();
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

//The bytes of one request, and what the shipped code wrote to std::cout while
//a real HttpServer read them off a real socket.
struct Exchange
{
    bool connected = false;
    std::string wire;
    std::string log;
    std::string response;
};

std::string buildRequest(const std::string &target,
                         const std::vector<std::pair<std::string, std::string>> &headers,
                         const std::string &body = std::string())
{
    std::ostringstream req;
    req << "GET " << target << " HTTP/1.1\r\n";
    req << "Host: 127.0.0.1:" << serverPort() << "\r\n";
    for (const auto &h: headers)
        req << h.first << ": " << h.second << "\r\n";
    if (!body.empty())
        req << "Content-Type: application/json\r\n"
            << "Content-Length: " << body.size() << "\r\n";
    req << "\r\n" << body;
    return req.str();
}

Exchange exchange(const std::string &raw)
{
    Exchange ex;
    ex.wire = raw;

    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return ex;

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(serverPort());

    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0)
    {
        close(fd);
        return ex;
    }
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

//A client frame is masked, always. Short payloads only (< 126 bytes), which
//is all a login needs.
std::string maskedTextFrame(const std::string &payload)
{
    static const unsigned char mask[4] = { 0x37, 0xfa, 0x21, 0x3d };

    std::string f;
    f.push_back((char)0x81);
    f.push_back((char)(0x80 | (payload.size() & 0x7f)));
    for (int i = 0; i < 4; i++)
        f.push_back((char)mask[i]);
    for (size_t i = 0; i < payload.size(); i++)
        f.push_back((char)(payload[i] ^ mask[i % 4]));
    return f;
}

//Handshake, then one frame on the same connection, both under one capture.
Exchange websocketExchange(const std::string &handshake, const std::string &frame)
{
    Exchange ex;
    ex.wire = handshake + frame;

    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return ex;

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(serverPort());

    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0)
    {
        close(fd);
        return ex;
    }
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

//The headers a RemoteUI box and a browser really send together: the bearer
//this server handed out, its HMAC companions, a session cookie, a user agent,
//and one header nothing in this tree has ever heard of.
std::vector<std::pair<std::string, std::string>> credentialHeaders()
{
    return {
        { "Authorization", std::string("Bearer ") + kBearerToken },
        { "Cookie", std::string("calaos_session=") + kSessionCookie },
        { "X-Auth-Timestamp", "1757030400" },
        { "X-Auth-Nonce", kHmacNonce },
        { "X-Auth-Signature", kHmacSignature },
        { "X-Calaos-Future-Auth", kFutureAuthValue },
        { "User-Agent", std::string("calaos-remote-ui/") + kUserAgentMark },
        { "Connection", "close" },
    };
}

//Every value the fixture plants, so that a case bounds what the journal gives
//back of ALL of them and not of the one it was written for.
std::vector<std::string> plantedValues()
{
    return { kBearerToken, kSessionCookie, kFutureAuthValue, kHmacNonce,
             kHmacSignature, kUserAgentMark };
}

std::vector<std::pair<std::string, std::string>> websocketHeaders()
{
    auto headers = credentialHeaders();
    headers.back() = { "Connection", "Upgrade" };
    headers.push_back({ "Upgrade", "websocket" });
    headers.push_back({ "Sec-WebSocket-Key", "x3JJHMbDL1EzLkh9GBhXDw==" });
    headers.push_back({ "Sec-WebSocket-Version", "13" });
    return headers;
}

std::string loginTarget(const std::string &password)
{
    return std::string("/api.php?cn_user=operateur&cn_pass=") + password;
}

/*
 * A TOKEN THIS REPOSITORY GENERATED, not a literal typed into a test. The
 * generator is the shipped one - the same CSPRNG and the same 64 hex chars
 * McpServerManager writes into local_config.xml - so what the cases below plant
 * in the uploaded file is the shape of the real secret and not a stand-in.
 */
const std::string &emittedToken()
{
    static const std::string token = HMACAuthenticator::generateNonce();
    return token;
}

//The body of a real config/put: the three file names the server accepts, and
//the tokens and camera passwords a real install keeps in them.
std::string configPutBody(bool withCredentials)
{
    std::ostringstream body;
    body << "{";
    if (withCredentials)
        body << "\"cn_user\":\"operateur\",\"cn_pass\":\"" << kQueryPassword << "\",";
    body << "\"action\":\"config\",\"type\":\"put\",\"msg_id\":\"42\","
         << "\"" << "x-calaos-clef-inventee" << "\":\"" << kInventedKeySecret << "\","
         << "\"config_files\":{"
         << "\"local_config.xml\":\"<?xml version=\\\"1.0\\\"?><calaos>"
         << "<mcp_token>" << emittedToken() << "</mcp_token>"
         << "<mcp_service_token>" << emittedToken() << "</mcp_service_token>"
         << "</calaos>\","
         << "\"io.xml\":\"<?xml version=\\\"1.0\\\"?><calaos>"
         << "<calaos:input password=\\\"" << kCameraSecret << "\\\" />"
         << "</calaos>\","
         << "\"rules.xml\":\"<?xml version=\\\"1.0\\\"?><calaos/>\""
         << "}}";
    return body.str();
}

//The other shape of the same hole, one level down: the word "password" is the
//VALUE of `param` here, so no list of key names can reach the credential.
std::string setParamBody()
{
    std::ostringstream body;
    body << "{\"action\":\"set_param\",\"id\":\"io_camera_portail\","
         << "\"param\":\"password\",\"value\":\"" << kSetParamSecret << "\"}";
    return body.str();
}

std::string jsonPost(const std::string &body)
{
    std::ostringstream req;
    req << "POST /api.php HTTP/1.1\r\n";
    req << "Host: 127.0.0.1:" << serverPort() << "\r\n";
    req << "Content-Type: application/json\r\n";
    req << "Content-Length: " << body.size() << "\r\n";
    req << "Connection: close\r\n";
    req << "\r\n" << body;
    return req.str();
}

struct DefaultLevelProbe
{
    bool ran = false;
    bool warningPrinted = false;
    bool debugPrinted = false;
};

DefaultLevelProbe &defaultLevelProbe()
{
    static DefaultLevelProbe probe;
    return probe;
}

/*
 * WHAT A BOX PRINTS WITH NOBODY TOUCHING ANYTHING, measured in a child.
 *
 * The Logger fills its domain map once and never re-reads it, so a process
 * that has raised the level can no longer observe the default. The child never
 * raises it. The default of a new box comes from the fallback of
 * Logger::maxLevelPrintable(), not from the registered default of the
 * debug_level option, which is never consulted when the option has never been
 * written.
 */
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

        unsigned char answer = 0;
        char tmpl[] = "/tmp/calaos_httpin_stock_XXXXXX";
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

            answer = 0x4;
            if (Utils::calaosLogger("network")->isLevelEnabled(Logger::LOG_LEVEL_WARNING))
                answer |= 0x1;
            if (Utils::calaosLogger("network")->isLevelEnabled(Logger::LOG_LEVEL_DEBUG))
                answer |= 0x2;
        }

        if (::write(fds[1], &answer, 1) != 1)
            answer = 0;
        ::close(fds[1]);
        _exit(0);
    }

    ::close(fds[1]);

    unsigned char answer = 0;
    const ssize_t got = ::read(fds[0], &answer, 1);
    ::close(fds[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);

    if (got == 1 && (answer & 0x4))
    {
        defaultLevelProbe().ran = true;
        defaultLevelProbe().warningPrinted = (answer & 0x1) != 0;
        defaultLevelProbe().debugPrinted = (answer & 0x2) != 0;
    }
}

class HttpRequestLogSecretTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        //One failed login blocks the address for a second, and a blocked
        //request never reaches the branch these cases read.
        LoginThrottle::clear();
        drainLoop(20);
    }

    void TearDown() override
    {
        drainLoop(50);
    }
};

} // namespace

/*
 * THE LEAK ITSELF, BOUNDED INSTEAD OF NAMED.
 *
 * Six values planted in six headers, one of them under a name that exists
 * nowhere in this tree. The case does not look for any of them by name: it
 * asks how many consecutive bytes of each the journal gives back.
 */
TEST_F(HttpRequestLogSecretTest, NoRunOfAnIncomingHeaderValueSurvivesInTheLog)
{
    const Exchange ex = exchange(buildRequest(loginTarget(kQueryPassword),
                                              credentialHeaders()));

    ASSERT_TRUE(ex.connected) << "no connection to the server, this case measures nothing";
    ASSERT_FALSE(ex.response.empty()) << "the server never answered, so the request was "
                                         "probably never parsed at all";

    //Anti-vacuity: the domain is not mute at this level in this process.
    ASSERT_NE(std::string::npos, ex.log.find("network"))
        << "nothing of the network domain was printed, so finding no secret in "
           "this log proves nothing";

    for (const std::string &value: plantedValues())
    {
        //Fixture check: a green below must mean withheld, not absent.
        ASSERT_NE(std::string::npos, ex.wire.find(value))
            << "the request that was sent does not carry " << value
            << " at all, so this case measures nothing";

        const size_t echo = longestEcho(ex.log, value);
        EXPECT_LT(echo, kMaxEcho)
            << "the journal gives back " << echo << " consecutive bytes of a "
               "header value the client sent (" << value << "):\n" << ex.log;
    }
}

/*
 * THE SAME, NAMED. Unreadable failures are useless failures: these three
 * assertions say which carrier came back, and they are the ones that name the
 * tokens this repository hands out itself.
 */
TEST_F(HttpRequestLogSecretTest, TheTokensThisServerHandsOutNeverComeBackInItsJournal)
{
    const Exchange ex = exchange(buildRequest(loginTarget(kQueryPassword),
                                              credentialHeaders()));

    ASSERT_TRUE(ex.connected) << "no connection to the server, this case measures nothing";
    ASSERT_NE(std::string::npos, ex.log.find("network"))
        << "nothing of the network domain was printed:\n" << ex.log;

    EXPECT_EQ(std::string::npos, ex.log.find(kBearerToken))
        << "the bearer token get_mcp_info tells a client to send back is in the "
           "journal:\n" << ex.log;
    EXPECT_EQ(std::string::npos, ex.log.find(kSessionCookie))
        << "the session cookie of the client is in the journal:\n" << ex.log;
    EXPECT_EQ(std::string::npos, ex.log.find(kFutureAuthValue))
        << "the value of a header whose name appears nowhere in this tree is in "
           "the journal, so the guard is spelled on the names known today:\n"
        << ex.log;
}

/*
 * THE OTHER SPELLING OF THE SAME HEADER. A Basic credential is base64, so a
 * journal can hand the secret back in a form no search for the plain text
 * sees. Both forms are hunted.
 */
TEST_F(HttpRequestLogSecretTest, TheBase64OfABasicCredentialSurvivesInNeitherForm)
{
    std::string plain = kBasicPlain;
    const std::string encoded = Utils::Base64_encode(plain);
    ASSERT_FALSE(encoded.empty()) << "the fixture could not build a Basic credential";

    auto headers = credentialHeaders();
    headers[0] = { "Authorization", "Basic " + encoded };

    const Exchange ex = exchange(buildRequest("/api.php", headers));

    ASSERT_TRUE(ex.connected) << "no connection to the server, this case measures nothing";
    ASSERT_NE(std::string::npos, ex.wire.find(encoded))
        << "the request does not carry the encoded credential, this case measures nothing";
    ASSERT_NE(std::string::npos, ex.log.find("network"))
        << "nothing of the network domain was printed:\n" << ex.log;

    const size_t echoEncoded = longestEcho(ex.log, encoded);
    const size_t echoPlain = longestEcho(ex.log, kBasicPlain);

    EXPECT_LT(echoEncoded, kMaxEcho)
        << "the journal gives back " << echoEncoded
        << " consecutive bytes of the base64 of a Basic credential:\n" << ex.log;
    EXPECT_LT(echoPlain, kMaxEcho)
        << "the journal gives back " << echoPlain
        << " consecutive bytes of the decoded Basic credential:\n" << ex.log;
}

/*
 * THE REFUSAL PATH, AND THE CREDENTIAL IT WAS REFUSED FOR.
 *
 * api.php accepts cn_user/cn_pass as GET parameters, so a login is a request
 * whose password is in its TARGET, and a refused login is the request a
 * journal says the most about. The password is percent-encoded on the wire.
 */
TEST_F(HttpRequestLogSecretTest, TheCredentialOfARefusedLoginIsNotRepublished)
{
    const std::string target = loginTarget(kQueryPassword);
    const Exchange ex = exchange(buildRequest(target, credentialHeaders()));

    ASSERT_TRUE(ex.connected) << "no connection to the server, this case measures nothing";

    //Anti-vacuity: the login really was refused, and refused for the
    //credentials rather than swallowed by the throttle.
    ASSERT_FALSE(lineContaining(ex.log, "Login failed").empty())
        << "the request was not refused for its credentials, so this case does "
           "not exercise the refusal path at all:\n" << ex.log;

    ASSERT_NE(std::string::npos, ex.wire.find(kQueryPassword))
        << "the request does not carry the query password, this case measures nothing";

    const size_t at = target.find('?');
    ASSERT_NE(std::string::npos, at);
    const std::string query = target.substr(at + 1);

    const size_t echo = longestEcho(ex.log, query);
    EXPECT_LT(echo, kMaxEcho)
        << "the journal gives back " << echo
        << " consecutive bytes of the query string of a refused login, where "
           "api.php takes its password:\n" << ex.log;

    EXPECT_EQ(std::string::npos, ex.log.find(Utils::url_decode(kQueryPassword)))
        << "the password of the refused login is in the journal, "
           "percent-decoded:\n" << ex.log;
}

/*
 * THE REDUCTION MUST NOT DEPEND ON WHAT THE QUERY CARRIES.
 *
 * A target is reduced as an absolute one when it opens with a scheme, and what
 * decides that is a POSITION. A client that passes a url as a parameter puts a
 * "://" inside an ordinary origin-form target, and a test on the mere presence
 * of those three bytes reads the query as an authority and publishes
 * everything in front of it - here, the password api.php takes as a GET
 * parameter. Same failure shape as the excerpt in the frame line: a rule keyed
 * on where a byte happens to sit.
 */
TEST_F(HttpRequestLogSecretTest, AUrlInsideTheQueryDoesNotUnreduceTheTarget)
{
    const std::string target = loginTarget(kQueryPassword) +
                               "&next=http://ailleurs.invalide/suite";
    const Exchange ex = exchange(buildRequest(target, credentialHeaders()));

    ASSERT_TRUE(ex.connected) << "no connection to the server, this case measures nothing";
    ASSERT_NE(std::string::npos, ex.wire.find("://"))
        << "the target sent carries no url in its query, this case measures nothing";
    ASSERT_NE(std::string::npos, ex.log.find("network"))
        << "nothing of the network domain was printed:\n" << ex.log;

    const size_t at = target.find('?');
    ASSERT_NE(std::string::npos, at);
    const std::string query = target.substr(at + 1);

    const size_t echo = longestEcho(ex.log, query);
    EXPECT_LT(echo, kMaxEcho)
        << "the journal gives back " << echo
        << " consecutive bytes of a query string that happens to carry a url, "
           "so what is withheld depends on what the client put in it:\n" << ex.log;

    //The counterweight: the path is still named, url in the query or not.
    EXPECT_TRUE(someLineHasAll(ex.log, {"network", "/api.php"}))
        << "no network line names the path of the request that arrived:\n" << ex.log;
}

/*
 * THE WEBSOCKET HANDSHAKE. It carries the same headers - the RemoteUI socket
 * authenticates with the same bearer, its nonce and its signature - and the
 * question is whether it is read by the same code or by another one.
 */
TEST_F(HttpRequestLogSecretTest, TheWebsocketHandshakeIsHeldByTheSameGuard)
{
    const Exchange ex = exchange(buildRequest("/api", websocketHeaders()));

    ASSERT_TRUE(ex.connected) << "no connection to the server, this case measures nothing";

    //Anti-vacuity: the upgrade branch really was taken, otherwise this case
    //measures the ordinary http path a second time.
    ASSERT_NE(std::string::npos, ex.log.find("Upgrading connection to WebSocket"))
        << "the handshake was not routed to the websocket path, this case "
           "measures nothing:\n" << ex.log;

    for (const std::string &value: plantedValues())
    {
        ASSERT_NE(std::string::npos, ex.wire.find(value))
            << "the handshake does not carry " << value << ", this case measures nothing";

        const size_t echo = longestEcho(ex.log, value);
        EXPECT_LT(echo, kMaxEcho)
            << "the journal of a websocket handshake gives back " << echo
            << " consecutive bytes of a header value the client sent (" << value
            << "):\n" << ex.log;
    }
}

/*
 * THE BODY OF THE OTHER TRANSPORT.
 *
 * The api request of a websocket client is the payload of a text frame, and
 * the frame line rendered an EXCERPT of it - the first forty bytes, whatever
 * they were. That excerpt runs BEFORE the redacted dump the json handler
 * builds, so a credential sitting near the head of the document left in clear
 * whatever the redaction list said about its key. How much of a secret escaped
 * depended on where it sat in the document, which is the failure mode of a
 * sensor that depends on a length, written into production code.
 */
TEST_F(HttpRequestLogSecretTest, TheHeadOfAWebsocketPayloadIsNotRenderedEither)
{
    const std::string payload =
        std::string("{\"cn_pass\":\"") + kWsFramePassword + "\",\"msg\":\"login\"}";

    //Fixture check: the secret really sits inside the excerpt that used to be
    //published, otherwise this case would be green for the wrong reason.
    ASSERT_LT(payload.find(kWsFramePassword) + strlen(kWsFramePassword), (size_t)40)
        << "the credential does not fit in the first forty bytes of the frame, "
           "so this case measures nothing";

    const Exchange ex = websocketExchange(buildRequest("/api", websocketHeaders()),
                                          maskedTextFrame(payload));

    ASSERT_TRUE(ex.connected) << "no connection to the server, this case measures nothing";

    //Anti-vacuity: the frame really reached the frame reader.
    ASSERT_NE(std::string::npos, ex.log.find("Got a new frame"))
        << "no frame was read at all, this case measures nothing:\n" << ex.log;

    const size_t echo = longestEcho(ex.log, kWsFramePassword);
    EXPECT_LT(echo, kMaxEcho)
        << "the journal gives back " << echo
        << " consecutive bytes of the credential a websocket client sent in its "
           "login frame:\n" << ex.log;

    //The counterweight of this line: an operator still sees a frame arrived,
    //what kind, and how big.
    EXPECT_TRUE(someLineHasAll(ex.log, {"websocket", "payloadSize:"}))
        << "the frame line no longer says anything about the frame that "
           "arrived:\n" << ex.log;
}

/*
 * THE UPLOAD THAT HANDS THE SERVER ITS OWN SECRETS BACK.
 *
 * config/put is not a hypothetical carrier: it is how a configuration reaches
 * this server, and it carries local_config.xml - the file into which this same
 * server writes the mcp_token it hands out on get_mcp_info - under a key that
 * is a FILE NAME. io.xml travels in the same body and holds the camera and
 * broker passwords of the install. Neither key was in any list of credential
 * names, and none ever would have been.
 *
 * The token planted here is generated by the shipped generator, so the case
 * bounds what the journal gives back of a secret this repository emitted.
 */
TEST_F(HttpRequestLogSecretTest, AConfigUploadDoesNotHandBackTheTokensThisServerEmitted)
{
    const std::string body = configPutBody(true);
    const Exchange ex = exchange(jsonPost(body));

    ASSERT_TRUE(ex.connected) << "no connection to the server, this case measures nothing";
    ASSERT_FALSE(emittedToken().empty())
        << "the shipped generator produced no token, this case measures nothing";
    ASSERT_NE(std::string::npos, ex.log.find("network"))
        << "nothing of the network domain was printed, so finding no secret in "
           "this log proves nothing:\n" << ex.log;

    //Anti-vacuity: the body really was parsed and really was described, so a
    //green below means withheld and not "never read".
    //Whole log, not one line: the shape of the description is what this ticket
    //changes, and an anti-vacuity check that depends on it measures nothing.
    ASSERT_NE(std::string::npos, ex.log.find("config_files"))
        << "nothing of the body of the upload was described at all, so this "
           "case does not exercise the line it is written for:\n" << ex.log;

    struct Carried { std::string what; std::string value; size_t ceiling; };
    const std::vector<Carried> carried = {
        { "the mcp_token this server generated", emittedToken(), kMaxDrawnEcho },
        { "the camera password of io.xml", kCameraSecret, kMaxEcho },
        { "the value of a key no list of this tree names", kInventedKeySecret, kMaxEcho },
    };

    for (const auto &c: carried)
    {
        ASSERT_NE(std::string::npos, ex.wire.find(c.value))
            << "the upload does not carry " << c.what << ", this case measures nothing";

        const size_t echo = longestEcho(ex.log, c.value);
        EXPECT_LT(echo, c.ceiling)
            << "the journal gives back " << echo << " consecutive bytes of "
            << c.what << ":\n" << ex.log;
    }

    //Named, because an unreadable failure is a useless failure.
    EXPECT_EQ(std::string::npos, ex.log.find(emittedToken()))
        << "the token this server writes into local_config.xml is in its own "
           "journal, verbatim:\n" << ex.log;
}

/*
 * AND IT DOES NOT NEED TO BE AUTHENTICATED FOR THE LINE TO LEAVE.
 *
 * The description of the body runs BEFORE checkCredentials(), so anyone who can
 * reach the port can put bytes of their choosing into this server's journal,
 * and could read this server's own tokens back out of it. The body carries no
 * credential at all here, and the case asserts BOTH halves: the request was
 * refused, and it was described anyway.
 */
TEST_F(HttpRequestLogSecretTest, AnUploadRefusedForItsCredentialsIsDescribedAnyway)
{
    const std::string body = configPutBody(false);
    ASSERT_EQ(std::string::npos, body.find("cn_pass"))
        << "the body carries a credential, so this case is not the "
           "unauthenticated one it claims to be";

    const Exchange ex = exchange(jsonPost(body));

    ASSERT_TRUE(ex.connected) << "no connection to the server, this case measures nothing";

    //Anti-vacuity, both halves: never authenticated, and described all the same.
    ASSERT_FALSE(lineContaining(ex.log, "Login failed").empty())
        << "the request was not refused for its credentials, so this case does "
           "not exercise the pre-authentication path:\n" << ex.log;
    ASSERT_NE(std::string::npos, ex.log.find("config_files"))
        << "the body of an unauthenticated upload is not described at all, so "
           "this case measures nothing:\n" << ex.log;

    const std::vector<std::pair<std::string, size_t>> carried = {
        { emittedToken(), kMaxDrawnEcho },
        { kCameraSecret, kMaxEcho },
        { kInventedKeySecret, kMaxEcho },
    };

    for (const auto &c: carried)
    {
        ASSERT_NE(std::string::npos, ex.wire.find(c.first))
            << "the upload does not carry " << c.first << ", this case measures nothing";

        const size_t echo = longestEcho(ex.log, c.first);
        EXPECT_LT(echo, c.second)
            << "the journal of an UNAUTHENTICATED upload gives back " << echo
            << " consecutive bytes of a secret of the install:\n" << ex.log;
    }
}

/*
 * THE SAME HOLE ONE LEVEL DOWN, AND THE ARGUMENT AGAINST A LIST OF NAMES IN
 * ITS PUREST FORM. set_param writes any parameter of any IO, so the credential
 * of a camera arrives under the key `value` while the word "password" is the
 * VALUE of `param`. Adding either key to a list would withhold every
 * legitimate set_param; leaving them out publishes the credential.
 */
TEST_F(HttpRequestLogSecretTest, TheCredentialSetParamCarriesUnderValueIsNotRepublished)
{
    const Exchange ex = exchange(jsonPost(setParamBody()));

    ASSERT_TRUE(ex.connected) << "no connection to the server, this case measures nothing";
    ASSERT_NE(std::string::npos, ex.wire.find(kSetParamSecret))
        << "the request does not carry the credential, this case measures nothing";
    ASSERT_NE(std::string::npos, ex.log.find("param"))
        << "nothing of the body was described at all, this case measures "
           "nothing:\n" << ex.log;

    const size_t echo = longestEcho(ex.log, kSetParamSecret);
    EXPECT_LT(echo, kMaxEcho)
        << "the journal gives back " << echo << " consecutive bytes of the "
           "credential set_param carries under the key `value`:\n" << ex.log;
}

/*
 * THE OTHER SITE OF THE SAME UPLOAD, AND HOW A CASE REACHES IT.
 *
 * A config file whose content is not XML was republished WHOLE, outside the
 * reducer, at error level - the level a stock box prints. The branch is behind
 * an upload that authenticated, and an accepted upload restarts the server,
 * which is what kept it untested on a harness whose loop is shared by every
 * case. It does not have to be accepted: a second entry under a name that is
 * not one of the three config files makes the reply a failure, so the restart
 * never arms, while the refused content is still read and still logged.
 */
TEST_F(HttpRequestLogSecretTest, TheContentOfARefusedConfigFileIsNotRepublished)
{
    //The credentials a freshly seeded local_config.xml carries, which is what
    //the temporary configuration of this binary is.
    std::ostringstream body;
    body << "{\"cn_user\":\"user\",\"cn_pass\":\"pass\","
         << "\"action\":\"config\",\"type\":\"put\",\"config_files\":{"
         << "\"io.xml\":\"" << kRefusedContentSecret << "\","
         << "\"pas-un-fichier\":\"x\"}}";

    const Exchange ex = exchange(jsonPost(body.str()));

    ASSERT_TRUE(ex.connected) << "no connection to the server, this case measures nothing";
    ASSERT_NE(std::string::npos, ex.wire.find(kRefusedContentSecret))
        << "the upload does not carry the content, this case measures nothing";

    //Anti-vacuity, and it is the whole difficulty of this case: the upload has
    //to have been ACCEPTED as far as the file loop, or the site is never
    //reached and a green says nothing.
    ASSERT_EQ(std::string::npos, ex.log.find("Login failed"))
        << "the upload never authenticated, so the refusing branch was never "
           "reached and this case measures nothing:\n" << ex.log;
    ASSERT_NE(std::string::npos, ex.log.find("is not XML"))
        << "the content was not refused for not being XML, so this case does "
           "not exercise the site it is written for:\n" << ex.log;

    const size_t echo = longestEcho(ex.log, kRefusedContentSecret);
    EXPECT_LT(echo, kMaxEcho)
        << "the journal gives back " << echo << " consecutive bytes of the "
           "content of a refused upload:\n" << ex.log;

    //The counterweight: what was refused stays knowable without its bytes.
    EXPECT_NE(std::string::npos, ex.log.find("io.xml"))
        << "the log does not say which file was refused:\n" << ex.log;
}

/*
 * THE SECOND TRANSPORT READS THE SAME BODIES THROUGH THE SAME REDUCER, and the
 * websocket dispatch has no "after the credentials" to move to: the login IS
 * the body being described. A key nobody here has heard of, inside `data`.
 */
TEST_F(HttpRequestLogSecretTest, TheBodyOfAWebsocketRequestIsHeldByTheSameGuard)
{
    const std::string payload =
        std::string("{\"msg\":\"login\",\"msg_id\":\"7\",\"data\":{\"grimoire\":\"") +
        kWsInventedSecret + "\"}}";
    ASSERT_LT(payload.size(), (size_t)126)
        << "the fixture only builds short frames";

    const Exchange ex = websocketExchange(buildRequest("/api", websocketHeaders()),
                                          maskedTextFrame(payload));

    ASSERT_TRUE(ex.connected) << "no connection to the server, this case measures nothing";
    ASSERT_NE(std::string::npos, ex.log.find("Got a new frame"))
        << "no frame was read at all, this case measures nothing:\n" << ex.log;
    ASSERT_NE(std::string::npos, ex.log.find("grimoire"))
        << "the body of the frame is not described at all, so this case does "
           "not exercise the websocket reducer:\n" << ex.log;

    const size_t echo = longestEcho(ex.log, kWsInventedSecret);
    EXPECT_LT(echo, kMaxEcho)
        << "the journal gives back " << echo << " consecutive bytes of a value "
           "a websocket client sent under a key this tree has never heard "
           "of:\n" << ex.log;
}

/*
 * THE HALF THE REDUCTION KILLS FIRST, ON THE BODY. An integrator reads this
 * line to see what its client actually sent: which command, which sub-command,
 * which files an upload carried. All three survive, and two uploads that differ
 * only in the bytes that are withheld must still render as two different lines.
 */
TEST_F(HttpRequestLogSecretTest, TheLogStillSaysWhichApiCommandArrivedAndWithWhatShape)
{
    const Exchange ex = exchange(jsonPost(configPutBody(true)));

    ASSERT_TRUE(ex.connected) << "no connection to the server, this case measures nothing";

    EXPECT_TRUE(someLineHasAll(ex.log, {"network", "config", "put"}))
        << "no network line says which command and which sub-command arrived:\n" << ex.log;
    EXPECT_TRUE(someLineHasAll(ex.log, {"network", "config_files", "local_config.xml", "io.xml"}))
        << "no network line names the files the upload carried, so an "
           "integrator cannot see what its client sent:\n" << ex.log;
    EXPECT_TRUE(someLineHasAll(ex.log, {"network", "x-calaos-clef-inventee"}))
        << "no network line names the key this tree has never heard of, so an "
           "operator cannot see what an unknown client sent:\n" << ex.log;
}

TEST_F(HttpRequestLogSecretTest, TwoUploadsDifferingOnlyInWhatIsWithheldAreStillTellableApart)
{
    //Same keys, same shape, same byte counts: only the withheld bytes differ.
    const std::string a =
        "{\"action\":\"config\",\"type\":\"put\","
        "\"config_files\":{\"io.xml\":\"<?xml?><a>aaaaaaaa</a>\"}}";
    const std::string b =
        "{\"action\":\"config\",\"type\":\"put\","
        "\"config_files\":{\"io.xml\":\"<?xml?><a>bbbbbbbb</a>\"}}";
    ASSERT_EQ(a.size(), b.size())
        << "the two bodies do not have the same length, so telling them apart "
           "would prove nothing about the reduced form";

    const Exchange first = exchange(jsonPost(a));
    LoginThrottle::clear();
    const Exchange second = exchange(jsonPost(b));

    ASSERT_TRUE(first.connected && second.connected)
        << "a connection failed, this case measures nothing";

    //Found by a word BOTH forms publish, not by a wording this ticket
    //introduces: a case that reddens on a renamed prefix says nothing.
    const std::string lineA = lineContaining(first.log, "config_files");
    const std::string lineB = lineContaining(second.log, "config_files");

    ASSERT_FALSE(lineA.empty()) << "no line describes the body at all:\n" << first.log;
    ASSERT_FALSE(lineB.empty()) << "no line describes the body at all:\n" << second.log;

    EXPECT_NE(lineA, lineB)
        << "two different uploads render the same journal line, so a reader "
           "cannot tell which one arrived:\n" << lineA << "\n" << lineB;
}

/*
 * THE COUNTERWEIGHT. Publishing nothing would pass every case above and leave
 * an operator unable to say which request arrived, what it carried, or whether
 * it was accepted. Knowing that an Authorization was PRESENT is the diagnosis;
 * its value never was.
 */
TEST_F(HttpRequestLogSecretTest, TheLogStillSaysWhichRequestArrivedAndWhatItCarried)
{
    const Exchange ex = exchange(buildRequest(loginTarget(kQueryPassword),
                                              credentialHeaders()));

    ASSERT_TRUE(ex.connected) << "no connection to the server, this case measures nothing";

    EXPECT_TRUE(someLineHasAll(ex.log, {"network", "GET", "/api.php"}))
        << "no network line names the method and the path of the request that "
           "arrived, so a journal can no longer say what was asked:\n" << ex.log;

    EXPECT_TRUE(someLineHasAll(ex.log, {"network", "authorization"}))
        << "no network line says an Authorization header was present:\n" << ex.log;
    EXPECT_TRUE(someLineHasAll(ex.log, {"network", "cookie"}))
        << "no network line says a Cookie header was present:\n" << ex.log;
    EXPECT_TRUE(someLineHasAll(ex.log, {"network", "x-calaos-future-auth"}))
        << "no network line names the header this tree has never heard of, so "
           "an operator cannot see what an unknown client sent:\n" << ex.log;

    EXPECT_FALSE(lineContaining(ex.log, "Login failed").empty())
        << "no line says whether the request was accepted:\n" << ex.log;
}

/*
 * THE HALF A REDUCTION KILLS FIRST. Two requests to the same path differing
 * only in their query string - same length, same parameter count - must still
 * render as two different lines, or the journal of a server whose API is one
 * path becomes unreadable.
 *
 * Only the request line is compared: the rest of the journal carries the
 * address of the HttpClient object, which differs between two connections all
 * by itself and would do the telling apart in its place.
 */
TEST_F(HttpRequestLogSecretTest, TwoRequestsDifferingOnlyInTheirQueryAreStillTellableApart)
{
    const std::string a = "/api.php?cn_user=operateur&cn_pass=alpha1111111111111";
    const std::string b = "/api.php?cn_user=operateur&cn_pass=beta22222222222222";
    ASSERT_EQ(a.size(), b.size())
        << "the two targets do not have the same length, so telling them apart "
           "would prove nothing about the reduced form";

    const Exchange first = exchange(buildRequest(a, credentialHeaders()));
    LoginThrottle::clear();
    const Exchange second = exchange(buildRequest(b, credentialHeaders()));

    ASSERT_TRUE(first.connected && second.connected)
        << "a connection failed, this case measures nothing";

    //The line is found by the PATH both forms publish, not by a wording this
    //ticket introduces: a case that went red on a renamed prefix would say
    //nothing about what the two lines render of the query.
    const std::string lineA = lineContaining(first.log, "/api.php");
    const std::string lineB = lineContaining(second.log, "/api.php");

    ASSERT_FALSE(lineA.empty()) << "no line names the request target at all:\n" << first.log;
    ASSERT_FALSE(lineB.empty()) << "no line names the request target at all:\n" << second.log;

    EXPECT_NE(lineA, lineB)
        << "two requests differing only in their query render the same journal "
           "line, so a reader cannot tell which one arrived:\n"
        << lineA << "\n" << lineB;
}

/*
 * THE CEILING ITSELF, HELD TO WHAT THIS SUITE MEASURES AT EVERY RUN.
 *
 * A bounded sensor is only as narrow as the number above the noise it was cut
 * for, and that number drifts both ways with nobody watching: a fixture that
 * gains a word shared with what the reduced lines legitimately publish widens
 * the blind window while every assertion above stays green, and a ceiling left
 * wider than the run this tree really produces is blind space nobody asked
 * for. Pinning the equality turns both into a red that says which number to
 * write.
 *
 * The probes are replayed here rather than accumulated from the cases above:
 * a measure that depends on which cases ran, and in which order, would be the
 * first thing a --gtest_filter breaks.
 */
TEST_F(HttpRequestLogSecretTest, TheCeilingIsHeldToTheOverlapThisSuiteMeasures)
{
    //A measure that cannot report a run reads as a clean zero everywhere else.
    ASSERT_EQ("bcdef", longestRun("zzbcdefzz", "abcdefg"));
    ASSERT_EQ("", longestRun("zzz", "abc"));

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

    //One failed login blocks the address for a second, and a request the
    //throttle turned back never reaches the branch whose overlap is wanted.
    const auto fresh = []()
    {
        LoginThrottle::clear();
        drainLoop(20);
    };

    const std::string loginWire = loginTarget(kQueryPassword);
    const std::string urlInQuery = loginWire + "&next=http://ailleurs.invalide/suite";
    const std::string query = loginWire.substr(loginWire.find('?') + 1);

    std::string basicPlain = kBasicPlain;
    const std::string basicEncoded = Utils::Base64_encode(basicPlain);
    ASSERT_FALSE(basicEncoded.empty()) << "the fixture could not build a Basic credential";
    auto basicHeaders = credentialHeaders();
    basicHeaders[0] = { "Authorization", "Basic " + basicEncoded };

    const std::string wsLogin =
        std::string("{\"cn_pass\":\"") + kWsFramePassword + "\",\"msg\":\"login\"}";
    const std::string wsBody =
        std::string("{\"msg\":\"login\",\"msg_id\":\"7\",\"data\":{\"grimoire\":\"") +
        kWsInventedSecret + "\"}}";

    std::ostringstream refused;
    refused << "{\"cn_user\":\"user\",\"cn_pass\":\"pass\","
            << "\"action\":\"config\",\"type\":\"put\",\"config_files\":{"
            << "\"io.xml\":\"" << kRefusedContentSecret << "\","
            << "\"pas-un-fichier\":\"x\"}}";

    const std::vector<std::string> uploaded = { kCameraSecret, kInventedKeySecret };

    struct Probe
    {
        const char *label;
        std::string request;
        //The CLEAR payload of a websocket frame, empty for an ordinary http
        //exchange. Clear and not masked, because the fixture check below has
        //to find the planted value in what the client sent, and a client frame
        //is masked on the wire.
        std::string payload;
        std::vector<std::string> values;    //measured, and folded into the equality
        //Documents drawn in the journal's own alphabet: bounded, never folded
        //into the equality, for the reason kMaxDrawnEcho carries.
        std::vector<std::string> drawn;
        std::vector<std::string> onWire;    //asserted present; empty means `values`
        const char *witness;        //what the log must carry for the probe to count
    };

    const std::vector<Probe> probes = {
        { "the headers of a refused login", buildRequest(loginWire, credentialHeaders()),
          "", plantedValues(), {}, {}, "network" },
        { "the query of a refused login", buildRequest(loginWire, credentialHeaders()),
          "", { query }, {}, {}, "Login failed" },
        //Only the base64 travels; the plain form is measured all the same
        //because a journal can hand the secret back decoded.
        { "a Basic credential", buildRequest("/api.php", basicHeaders),
          "", { basicEncoded, kBasicPlain }, {}, { basicEncoded }, "network" },
        { "a query that carries a url", buildRequest(urlInQuery, credentialHeaders()),
          "", { urlInQuery.substr(urlInQuery.find('?') + 1) }, {}, {}, "network" },
        { "the headers of a websocket handshake", buildRequest("/api", websocketHeaders()),
          "", plantedValues(), {}, {}, "Upgrading connection to WebSocket" },
        { "the head of a websocket login frame", buildRequest("/api", websocketHeaders()),
          wsLogin, { kWsFramePassword }, {}, {}, "Got a new frame" },
        { "the body of a websocket request", buildRequest("/api", websocketHeaders()),
          wsBody, { kWsInventedSecret }, {}, {}, "grimoire" },
        { "an authenticated config upload", jsonPost(configPutBody(true)),
          "", uploaded, { emittedToken() }, {}, "config_files" },
        { "an unauthenticated config upload", jsonPost(configPutBody(false)),
          "", uploaded, { emittedToken() }, {}, "config_files" },
        { "a set_param", jsonPost(setParamBody()), "", { kSetParamSecret }, {}, {}, "param" },
        { "the content of a refused config file", jsonPost(refused.str()),
          "", { kRefusedContentSecret }, {}, {}, "is not XML" },
    };

    for (const Probe &p: probes)
    {
        fresh();
        const Exchange ex = p.payload.empty()
                                ? exchange(p.request)
                                : websocketExchange(p.request,
                                                    maskedTextFrame(p.payload));

        ASSERT_TRUE(ex.connected)
            << "no connection to the server for " << p.label << ", so the "
               "overlap this case exists to pin was not produced at all";
        ASSERT_NE(std::string::npos, ex.log.find(p.witness))
            << "the journal of " << p.label << " does not carry \"" << p.witness
            << "\", so the branch its bound is written for was never taken and "
               "the overlap measured here is not the one the cases above are "
               "bounded against:\n" << ex.log;

        const std::string &sent = p.payload.empty() ? ex.wire : p.payload;
        for (const std::string &v: p.onWire.empty() ? p.values : p.onWire)
        {
            ASSERT_NE(std::string::npos, sent.find(v))
                << "what was sent for " << p.label << " does not carry " << v
                << ", so this probe measures nothing";
        }
        for (const std::string &v: p.values)
            keep(longestEchoRun(ex.log, v), p.label);

        for (const std::string &v: p.drawn)
        {
            ASSERT_NE(std::string::npos, sent.find(v))
                << "what was sent for " << p.label << " does not carry the "
                   "generated token, so this probe measures nothing";
            const std::string run = longestEchoRun(ex.log, v);
            EXPECT_LT(run.size(), kMaxDrawnEcho)
                << "the journal of " << p.label << " gives back \"" << run
                << "\", " << run.size() << " bytes of the token this server "
                   "generated.";
        }
    }

    //The margin the drawn document needs, held apart from the measured one:
    //collapsing the two would put a lottery inside the equality below.
    EXPECT_GT(kMaxDrawnEcho, kMaxEcho)
        << "the ceiling of the document nobody chose is no wider than the one "
           "the equality pins, so the hexadecimal the journal draws for itself "
           "can reach it";

    EXPECT_EQ(kMaxEcho, worst.size() + 1)
        << "the ceiling is " << kMaxEcho << " while this tree gives back at "
           "most " << worst.size() << " bytes of a value a client sent, at "
        << worstLabel << ", on the run \"" << worst << "\". Everything between "
           "the two is a window this suite cannot see into: either a leak has "
           "widened the overlap, or the fixture has, and the ceiling to write "
           "is " << (worst.size() + 1) << ".";
}

/*
 * THE FIXTURE, HELD TO THE SAME CEILING.
 *
 * A run two planted values share is republished by whichever carrier leaks
 * first, so it raises the bound of every other one: the red set then names the
 * fixture instead of the carrier.
 */
TEST_F(HttpRequestLogSecretTest, NoTwoPlantedValuesShareARunTheCeilingWouldNotAbsorb)
{
    std::string basicPlain = kBasicPlain;

    struct Doc { std::string label; std::string text; };
    const std::vector<Doc> docs = {
        { "the bearer token", kBearerToken },
        { "the session cookie", kSessionCookie },
        { "the value of the unknown header", kFutureAuthValue },
        { "the hmac nonce", kHmacNonce },
        { "the hmac signature", kHmacSignature },
        { "the user agent mark", kUserAgentMark },
        { "the query password", kQueryPassword },
        { "the Basic credential", basicPlain },
        { "the base64 of the Basic credential", Utils::Base64_encode(basicPlain) },
        { "the websocket frame password", kWsFramePassword },
        { "the websocket invented value", kWsInventedSecret },
        { "the camera password of io.xml", kCameraSecret },
        { "the value of the invented key", kInventedKeySecret },
        { "the credential of set_param", kSetParamSecret },
        { "the content of the refused file", kRefusedContentSecret },
    };

    for (size_t i = 0; i < docs.size(); i++)
    {
        for (size_t j = i + 1; j < docs.size(); j++)
        {
            const std::string run = longestRun(docs[i].text, docs[j].text);
            EXPECT_LT(run.size(), kMaxEcho)
                << docs[i].label << " and " << docs[j].label << " share \""
                << run << "\", " << run.size() << " bytes, which the ceiling of "
                << kMaxEcho << " does not absorb: a leak at either one reddens "
                   "the bound of the other and the red set stops naming a "
                   "carrier.";
        }
    }
}

/*
 * THE LEVEL THE SEVERITY RESTS ON, MEASURED INSTEAD OF ASSERTED.
 *
 * These lines are cDebugDom, so a stock box prints none of them - that is what
 * keeps this ticket a notch below its siblings, and it is not something this
 * process can observe: it raises the level in its own main(). Both directions
 * are pinned, so that a fallback moved either way is seen.
 */
TEST_F(HttpRequestLogSecretTest, AStockInstallDoesNotPrintTheNetworkDebugLines)
{
    const DefaultLevelProbe &probe = defaultLevelProbe();

    ASSERT_TRUE(probe.ran)
        << "the stock-level child could not be forked or answered nothing, so "
           "this case measures no level at all";

    EXPECT_TRUE(probe.warningPrinted)
        << "the network domain does not print at WARNING on a stock install, so "
           "this binary is not measuring the shipped level at all";
    EXPECT_FALSE(probe.debugPrinted)
        << "the network domain prints at DEBUG on a stock install, so the "
           "request and header lines leave with a journal nobody had to switch "
           "on, and this ticket is a notch more severe than its fiche says";
}

/*
 * Own main instead of gtest_main: the level has to be raised before the first
 * log line of the process, the Logger domain map being filled once and never
 * re-read, and the listening server has to exist before any case runs.
 */
int main(int argc, char **argv)
{
    //Before anything raises the level, and before there is a loop to duplicate.
    measureStockLogLevel();

    ::testing::InitGoogleTest(&argc, argv);

    char tmpl[] = "/tmp/calaos_httpin_cfg_XXXXXX";
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
    }

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
