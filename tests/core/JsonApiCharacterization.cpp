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
//Implementation of the E4.0 characterization harness. Everything a sub-ticket
//needs to know is documented in JsonApiCharacterization.h; this file only has
//to be read to change the harness itself.

#include "JsonApiCharacterization.h"

#include "JsonApi.h"
#include "Utils.h"
#include "libuvw.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace CalaosTest
{

/*******************************************************************************
 * The oracle
 ******************************************************************************/

bool parseJsonText(const std::string &text, Json &out, std::string &error)
{
    try
    {
        out = Json::parse(text);
        error.clear();
        return true;
    }
    catch (const std::exception &e)
    {
        out = Json(Json::value_t::discarded);
        error = e.what();
        return false;
    }
}

Json asJsonDocument(const std::string &text)
{
    Json j;
    std::string err;
    parseJsonText(text, j, err);
    return j;
}

static std::string describeValue(const Json &j)
{
    //dump() here is a DIAGNOSTIC, never a comparison. Truncated because a
    //failing get_home is a few thousand characters.
    std::string s = j.dump();
    if (s.size() > 200)
        s = s.substr(0, 200) + "...";
    return s;
}

static std::string typeName(const Json &j)
{
    return j.type_name();
}

//Escapes a key the way RFC6901 wants it inside a JSON pointer.
static std::string escapePointerToken(const std::string &key)
{
    std::string out;
    for (char c: key)
    {
        if (c == '~') out += "~0";
        else if (c == '/') out += "~1";
        else out += c;
    }
    return out;
}

static std::string diffRecurse(const Json &expected, const Json &actual,
                               const std::string &path)
{
    const std::string p = path.empty() ? std::string("/") : path;

    if (expected.type() != actual.type())
    {
        //Strictness on types is wanted: "3" and 3 are two different contracts.
        //nlohmann sees unsigned/signed/float as distinct types but compares
        //them numerically, so only report when they really are not equal.
        if (expected.is_number() && actual.is_number() && expected == actual)
            return std::string();

        return p + ": type mismatch, expected " + typeName(expected) +
               " (" + describeValue(expected) + ") got " + typeName(actual) +
               " (" + describeValue(actual) + ")";
    }

    if (expected.is_object())
    {
        for (auto it = expected.begin(); it != expected.end(); ++it)
        {
            const std::string child = path + "/" + escapePointerToken(it.key());
            auto found = actual.find(it.key());
            if (found == actual.end())
                return child + ": key missing from actual (expected " +
                       describeValue(it.value()) + ")";

            const std::string d = diffRecurse(it.value(), *found, child);
            if (!d.empty())
                return d;
        }

        for (auto it = actual.begin(); it != actual.end(); ++it)
        {
            if (expected.find(it.key()) == expected.end())
                return path + "/" + escapePointerToken(it.key()) +
                       ": unexpected key in actual (" + describeValue(it.value()) + ")";
        }

        return std::string();
    }

    if (expected.is_array())
    {
        //Array order IS semantic (room, IO and rule order drive evaluation
        //order). Never sort before comparing.
        if (expected.size() != actual.size())
        {
            std::ostringstream ss;
            ss << p << ": array size mismatch, expected " << expected.size()
               << " got " << actual.size();
            return ss.str();
        }

        for (size_t i = 0; i < expected.size(); i++)
        {
            std::ostringstream child;
            child << path << "/" << i;
            const std::string d = diffRecurse(expected[i], actual[i], child.str());
            if (!d.empty())
                return d;
        }

        return std::string();
    }

    if (expected == actual)
        return std::string();

    return p + ": expected " + describeValue(expected) +
           " got " + describeValue(actual);
}

std::string firstJsonDifference(const Json &expected, const Json &actual)
{
    if (expected == actual)
        return std::string();

    const std::string d = diffRecurse(expected, actual, std::string());
    if (!d.empty())
        return d;

    //Should not happen: operator== said different but the walk found nothing.
    return std::string("/: documents compare unequal");
}

/*******************************************************************************
 * Golden files
 ******************************************************************************/

static bool directoryExists(const std::string &path)
{
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static std::string goldenDirectory()
{
    static std::string cached;
    if (!cached.empty())
        return cached;

    std::vector<std::string> candidates;

    if (const char *env = ::getenv("CALAOS_GOLDEN_DIR"))
        candidates.emplace_back(env);
#ifdef CALAOS_GOLDEN_DIR
    candidates.emplace_back(CALAOS_GOLDEN_DIR);
#endif
    if (const char *srcdir = ::getenv("srcdir"))
        candidates.emplace_back(std::string(srcdir) + "/core/golden");
    candidates.emplace_back("core/golden");
    candidates.emplace_back("tests/core/golden");
    candidates.emplace_back("../tests/core/golden");

    for (const std::string &c: candidates)
    {
        if (directoryExists(c))
        {
            cached = c;
            return cached;
        }
    }

    //Nothing exists yet: keep the best guess so the update mode can create it.
    cached = candidates.empty() ? std::string("core/golden") : candidates.front();
    return cached;
}

std::string goldenFilePath(const std::string &goldenName)
{
    return goldenDirectory() + "/" + goldenName + ".json";
}

static bool updateModeEnabled()
{
    const char *env = ::getenv("CALAOS_GOLDEN_UPDATE");
    return env && *env && std::string(env) != "0";
}

bool goldenUpdateModeEnabled()
{
    return updateModeEnabled();
}

static bool readWholeFile(const std::string &path, std::string &out)
{
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f.is_open())
        return false;

    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

static bool writeGoldenFile(const std::string &path, const std::string &content,
                            std::string &error)
{
    //mkdir -p on the single level we need
    const std::string dir = goldenDirectory();
    if (!directoryExists(dir))
        ::mkdir(dir.c_str(), 0755);

    std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!f.is_open())
    {
        error = "cannot open " + path + " for writing";
        return false;
    }

    f << content << "\n";
    f.close();
    return true;
}

static ::testing::AssertionResult goldenCompare(const char *actualExpr,
                                                const std::string &goldenName,
                                                const Json &actual)
{
    if (actual.is_discarded())
        return ::testing::AssertionFailure()
                << actualExpr << " is not parsable JSON, cannot compare with golden "
                << goldenName;

    const std::string path = goldenFilePath(goldenName);

    if (updateModeEnabled())
    {
        //Goldens are GENERATED BY THE CODE. UTF-8, dump(2), readable diff.
        std::string err;
        if (!writeGoldenFile(path, actual.dump(2), err))
            return ::testing::AssertionFailure() << "golden update failed: " << err;

        std::cout << "[  GOLDEN  ] wrote " << path << std::endl;
        return ::testing::AssertionSuccess();
    }

    std::string content;
    if (!readWholeFile(path, content))
        return ::testing::AssertionFailure()
                << "golden file " << path << " not found. Generate it with "
                << "CALAOS_GOLDEN_UPDATE=1 <test binary>, then review the diff "
                << "before committing it.";

    Json expected;
    std::string err;
    if (!parseJsonText(content, expected, err))
        return ::testing::AssertionFailure()
                << "golden file " << path << " is not parsable JSON: " << err;

    const std::string diff = firstJsonDifference(expected, actual);
    if (diff.empty())
        return ::testing::AssertionSuccess();

    return ::testing::AssertionFailure()
            << "payload diverges from golden " << path << "\n"
            << "  (semantic comparison: key order ignored, array order "
            << "significant, types strict)\n"
            << "  actual expression: " << actualExpr << "\n"
            << "  " << diff << "\n"
            << "--- golden ---\n" << expected.dump(2) << "\n"
            << "--- actual ---\n" << actual.dump(2);
}

::testing::AssertionResult jsonMatchesGolden(const char *nameExpr,
                                             const char *actualExpr,
                                             const std::string &goldenName,
                                             const std::string &actualJsonText)
{
    VAR_UNUSED(nameExpr);
    return goldenCompare(actualExpr, goldenName, asJsonDocument(actualJsonText));
}

::testing::AssertionResult jsonMatchesGolden(const char *nameExpr,
                                             const char *actualExpr,
                                             const std::string &goldenName,
                                             const Json &actual)
{
    VAR_UNUSED(nameExpr);
    return goldenCompare(actualExpr, goldenName, actual);
}

/*******************************************************************************
 * Loop pumping
 ******************************************************************************/

void pumpEventLoop(int iterations)
{
    auto loop = uvw::Loop::getDefault();
    for (int i = 0; i < iterations; i++)
        loop->run<uvw::Loop::Mode::NOWAIT>();
}

/*******************************************************************************
 * Websocket session
 ******************************************************************************/

//Subclass only to reach the protected session state. The production class is
//not modified, and RemoteUIWebSocketHandler already does the same thing.
class WsTestSession::Handler: public JsonApiHandlerWS
{
public:
    Handler(): JsonApiHandlerWS(nullptr) {}

    using JsonApiHandlerWS::setAuthenticated;
    void setScope(bool s) { serviceScope = s; }
};

WsTestSession::WsTestSession(bool authenticated, bool serviceScope):
    handler(new Handler())
{
    handler->sendData.connect([this](const std::string &data)
    {
        sentMessages.push_back(data);
    });
    handler->closeConnection.connect([this](int code, const std::string &reason)
    {
        closeEvents.emplace_back(code, reason);
    });

    handler->setAuthenticated(authenticated);
    handler->setScope(serviceScope);
}

WsTestSession::~WsTestSession() = default;

void WsTestSession::send(const std::string &jsonText)
{
    handler->processApi(jsonText, Params());
}

void WsTestSession::send(const Json &request)
{
    //dump() here builds the REQUEST, it is not a comparison.
    handler->processApi(request.dump(), Params());
}

std::string WsTestSession::lastMessage() const
{
    if (sentMessages.empty())
        return std::string();
    return sentMessages.back();
}

Json WsTestSession::lastEnvelope() const
{
    return asJsonDocument(lastMessage());
}

Json WsTestSession::lastData() const
{
    const Json env = lastEnvelope();
    if (!env.is_object())
        return Json();

    auto it = env.find("data");
    if (it == env.end())
        return Json();

    return *it;
}

void WsTestSession::clear()
{
    sentMessages.clear();
    closeEvents.clear();
}

void WsTestSession::setAuthenticated(bool auth)
{
    handler->setAuthenticated(auth);
}

void WsTestSession::setServiceScope(bool scope)
{
    handler->setScope(scope);
}

/*******************************************************************************
 * HTTP request
 ******************************************************************************/

//A real HttpClient on an unconnected uvw::TcpHandle. Measured safe:
//  - the constructor only needs the handle to be non-null (it registers an
//    ErrorEvent callback on it) and allocates a read Timer,
//  - buildHttpResponse() (HttpClient.cpp:541-584) touches resHeaders,
//    request_headers and conn_close only, never the socket,
//  - getClientIp() null- and exception-guards its way to "unknown",
//  - ~HttpClient() does not call CloseConnection().
class HttpTestRequest::Client: public HttpClient
{
public:
    explicit Client(const std::shared_ptr<uvw::TcpHandle> &h):
        HttpClient(h),
        handle(h)
    {}

    std::shared_ptr<uvw::TcpHandle> handle;
};

HttpTestRequest::HttpTestRequest()
{
    auto loop = uvw::Loop::getDefault();
    auto tcp = loop->resource<uvw::TcpHandle>();

    client = std::make_shared<Client>(tcp);
    handler.reset(new JsonApiHandlerHttp(client.get()));

    handler->sendData.connect([this](const std::string &data)
    {
        responses.push_back(data);
    });
    handler->closeConnection.connect([this](int code, const std::string &reason)
    {
        closeEvents.emplace_back(code, reason);
    });
}

HttpTestRequest::~HttpTestRequest()
{
    //Destroy the handler first: it holds a raw pointer on the client.
    handler.reset();

    if (client && client->handle)
        client->handle->close();

    client.reset();

    //Let uv run the close callbacks so handles do not pile up between cases.
    pumpEventLoop(2);
}

void HttpTestRequest::send(const std::string &requestBody, const Params &paramsGET)
{
    //buildHttpResponse() mutates the connection state (resHeaders accumulate,
    //conn_close flips). A second request on the same client would be
    //characterizing the leftovers of the first one.
    ASSERT_FALSE(alreadySent)
            << "HttpTestRequest is single shot on purpose: build a NEW one for "
               "each request (buildHttpResponse() mutates the HttpClient state).";
    alreadySent = true;

    handler->processApi(requestBody, paramsGET);
}

void HttpTestRequest::send(const Json &requestBody, const Params &paramsGET)
{
    //dump() here builds the REQUEST, it is not a comparison.
    send(requestBody.dump(), paramsGET);
}

std::string HttpTestRequest::statusLine() const
{
    if (responses.empty())
        return std::string();

    const std::string &r = responses.front();
    const std::string::size_type eol = r.find("\r\n");
    if (eol == std::string::npos)
        return r;

    return r.substr(0, eol);
}

static std::string lowered(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (char c: s)
        out += (char)::tolower((unsigned char)c);
    return out;
}

std::string HttpTestRequest::header(const std::string &key) const
{
    if (responses.empty())
        return std::string();

    const std::string &r = responses.front();
    const std::string::size_type end = r.find("\r\n\r\n");
    const std::string head = r.substr(0, end == std::string::npos ? r.size() : end);
    const std::string wanted = lowered(key);

    std::istringstream ss(head);
    std::string line;
    bool first = true;
    while (std::getline(ss, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (first) { first = false; continue; } //status line

        const std::string::size_type colon = line.find(':');
        if (colon == std::string::npos)
            continue;

        if (lowered(line.substr(0, colon)) != wanted)
            continue;

        std::string value = line.substr(colon + 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
            value.erase(value.begin());

        return value;
    }

    return std::string();
}

bool HttpTestRequest::hasHeader(const std::string &key) const
{
    return !header(key).empty();
}

std::string HttpTestRequest::body() const
{
    if (responses.empty())
        return std::string();

    const std::string &r = responses.front();
    const std::string::size_type sep = r.find("\r\n\r\n");
    if (sep == std::string::npos)
        return std::string();

    return r.substr(sep + 4);
}

Json HttpTestRequest::bodyJson() const
{
    return asJsonDocument(body());
}

/*******************************************************************************
 * Fixture
 ******************************************************************************/

const char *const JsonApiCharacterizationTest::HOUSE_ROOM1_NAME = "Salon";
const char *const JsonApiCharacterizationTest::HOUSE_ROOM2_NAME = "Cuisine";
const char *const JsonApiCharacterizationTest::HOUSE_BOOL_IN = "e40_bool_in";
const char *const JsonApiCharacterizationTest::HOUSE_BOOL_OUT = "e40_bool_out";
const char *const JsonApiCharacterizationTest::HOUSE_INT = "e40_int";
const char *const JsonApiCharacterizationTest::HOUSE_STRING = "e40_string";
const char *const JsonApiCharacterizationTest::HOUSE_ACCENTED = "e40_accented";
const char *const JsonApiCharacterizationTest::HOUSE_ROOM3_NAME = "Technique";
const char *const JsonApiCharacterizationTest::HOUSE_CAMERA_PTZ = "e40_cam_ptz";
const char *const JsonApiCharacterizationTest::HOUSE_CAMERA_PLAIN = "e40_cam_plain";
const char *const JsonApiCharacterizationTest::HOUSE_PLAYER = "e40_player";
const char *const JsonApiCharacterizationTest::HOUSE_RULE_NAME = "E40 reference rule";

const char *JsonApiCharacterizationTest::apiUser() { return "e40user"; }
const char *JsonApiCharacterizationTest::apiPassword() { return "e40pass"; }

Json JsonApiCharacterizationTest::authenticated(Json body)
{
    body["cn_user"] = apiUser();
    body["cn_pass"] = apiPassword();
    return body;
}

void JsonApiCharacterizationTest::SetUp()
{
    CoreFixture::SetUp();

    //checkCredentials() prefers cn_user/cn_pass over calaos_user/calaos_password
    //when both are set (JsonApi.cpp:124-141).
    Utils::set_config_option("cn_user", apiUser());
    Utils::set_config_option("cn_pass", apiPassword());

    //Every session in this harness reports the client ip "unknown" (no socket).
    //Without this, one failed-login case would throttle all the following ones.
    LoginThrottle::clear();

    //Config's IO state cache is process wide and never cleared.
    forgetIOState(HOUSE_BOOL_IN);
    forgetIOState(HOUSE_BOOL_OUT);
    forgetIOState(HOUSE_INT);
    forgetIOState(HOUSE_STRING);
    forgetIOState(HOUSE_ACCENTED);
    forgetIOState(HOUSE_CAMERA_PTZ);
    forgetIOState(HOUSE_CAMERA_PLAIN);
    forgetIOState(HOUSE_PLAYER);
}

void JsonApiCharacterizationTest::TearDown()
{
    LoginThrottle::clear();

    CoreFixture::TearDown();

    //DRAIN AFTER THE CORE STATE IS CLEARED, AND THE ORDER IS THE WHOLE POINT
    //(E4.0g). CoreFixture::TearDown() above starts with clearCoreState()
    //(CalaosCoreFixture.cpp:184-186), which destroys the rooms; ~Room() calls
    //RemoveIO() for every IO it still owns and each one raises an
    //EventIODeleted (Room.cpp:77) - eight of them for the reference house.
    //Those events are therefore PRODUCED BY THE PARENT TEARDOWN ITSELF.
    //
    //Pumping before it (what this fixture used to do, E4.0a..E4.0f) drained
    //everything EXCEPT them: they stayed in the EventManager idler and were
    //delivered to the first session of the NEXT case, at its first pump - even
    //to a session created afterwards, since newEvent is emitted at FLUSH time,
    //not at QUEUE time. A case pinning the ABSENCE of a message could then
    //pass alone and fail in a full run, under some orderings only - or crash
    //the binary outright (uv__finish_close assertion, std::bad_alloc).
    //
    //Five workarounds had accumulated against that leak, in the SetUp() of the
    //sub-tickets and in one case body; E4.0g removed all five in this same
    //diff, and this function no longer drains anything either (see
    //loadReferenceHouse()). The control that catches a regression here is
    //--gtest_shuffle on several seeds, not the default order.
    pumpEventLoop();
}

//CalaosCoreFixture only writes <calaos:internal> nodes. Cameras and audio
//players live under <calaos:camera> / <calaos:audio> (Room.cpp:158-164), so the
//harness writes those itself rather than modifying the shared fixture.
static std::string typedIoXml(const std::string &nodeName, const std::string &type,
                              const std::string &id, const std::string &name,
                              const std::string &extraAttributes = std::string())
{
    std::string x = "    <" + nodeName + " type=\"" + type + "\"";
    x += " id=\"" + id + "\"";
    x += " name=\"" + name + "\"";
    x += " enabled=\"true\"";
    if (!extraAttributes.empty())
        x += " " + extraAttributes;
    x += " />\n";
    return x;
}

void JsonApiCharacterizationTest::loadReferenceHouse()
{
    std::string salon;
    salon += internalIoXml("InternalBool", HOUSE_BOOL_IN, "Bool input");
    salon += internalIoXml("InternalBool", HOUSE_BOOL_OUT, "Bool output");
    salon += internalIoXml("InternalInt", HOUSE_INT, "Int value");
    salon += internalIoXml("InternalString", HOUSE_STRING, "String value");

    std::string cuisine;
    //Accented, non-ASCII name on purpose: jansson serializes it as \uXXXX
    //escapes (JSON_ENSURE_ASCII), nlohmann writes raw UTF-8, and the semantic
    //oracle has to be blind to the difference.
    //
    //It is ALSO the only IO of the house carrying every optional param of
    //buildJsonIO() (JsonApi.cpp:258-262). ADDED IN E4.0b AFTER A REVIEW
    //COUNTER-MUTATION: without these attributes, six of the sixteen candidate
    //keys - hits, chauffage_id, autoscenario_uid, step, io_style, value_warning -
    //were absent from every golden of the series because NO IO SET THEM, not
    //because the code chose not to emit them. Renaming any of the six in
    //buildJsonIO() left the whole suite green, which is exactly the silent
    //failure E4.0 exists to prevent. They are real production params
    //(ListeRoom.cpp:276 reads chauffage_id, IO/OutputAnalog.cpp:39 documents
    //step, IO/InputSwitch.cpp:48 io_style, IO/InputAnalog.cpp:66
    //value_warning), so one IO now sets them all and the goldens pin them.
    //Everything else in the house stays sparse ON PURPOSE: the contrast inside
    //the same golden is what pins the "absent param -> absent key, never null"
    //contract (JsonApi.cpp:285-286 `continue`).
    //
    //E4.6f. It carries BOTH scenario markers, deliberately: E4.6b put
    //`autoscenario_uid` NEXT TO `auto_scenario`, never in its place, so an IO
    //of a live configuration really does hold the two at once. Which of them
    //buildJsonIO() publishes is therefore a CHOICE the payload makes, not an
    //accident of what the configuration happens to carry - and a witness of
    //that choice is red on a re-key in either direction.
    cuisine += internalIoXml("InternalString", HOUSE_ACCENTED,
                             "\xc3\x89" "clairage caf\xc3\xa9",
                             "hits=\"12\" chauffage_id=\"e40_heater\" "
                             "unit=\"\xc2\xb0""C\" auto_scenario=\"e40_scenario\" "
                             "autoscenario_uid=\"as_e40\" "
                             "step=\"0.5\" io_style=\"temperature\" "
                             "value_warning=\"false\"");

    //buildJsonCameras() / buildJsonAudio() filter ListeRoom's caches by
    //dynamic_cast<IPCam*> / <AudioPlayer*>. Empty arrays would leave that
    //filter, and the whole camera/audio payload, uncharacterized - so the
    //reference house carries real ones.
    //StandardMjpeg is the cheapest concrete IPCam: its constructor is pure
    //parameter and documentation work, no socket, no timer, and it needs no
    //object beyond IPCam.o which CORE_SERVER_OBJECTS already provides.
    //Two of them, to cover both branches of the ptz capability.
    std::string technique;
    technique += typedIoXml("calaos:camera", "StandardMjpeg", HOUSE_CAMERA_PTZ,
                            "Camera PTZ",
                            "url_jpeg=\"http://camera.invalid/snap.jpg\" ptz=\"true\"");
    technique += typedIoXml("calaos:camera", "StandardMjpeg", HOUSE_CAMERA_PLAIN,
                            "Camera plain",
                            "url_jpeg=\"http://camera2.invalid/snap.jpg\"");
    //RoonPlayer is the cheapest concrete AudioPlayer: unlike Squeezebox it
    //pulls no AVR/UrlDownloader/SqueezeboxDB closure and links no uv_tcp_connect.
    //zone_id is left EMPTY ON PURPOSE: RoonPlayer's constructor returns early on
    //an empty zone (RoonPlayer.cpp), before RoonCtrl::Instance() spawns the Roon
    //helper process and before its Timer::singleShot fires. That keeps make
    //check hermetic. It costs one [ERR] line in the log and changes nothing in
    //the payload, which only reads id/name/type/canPlaylist/canDatabase/amp.
    //The amp param is set so the OPTIONAL "avr" key is exercised.
    technique += typedIoXml("calaos:audio", "Roon", HOUSE_PLAYER, "Player",
                            "zone_id=\"\" amp=\"e40_avr\"");

    const std::string ios =
            roomXml(HOUSE_ROOM1_NAME, "salon", salon, 3) +
            roomXml(HOUSE_ROOM2_NAME, "cuisine", cuisine, 0) +
            roomXml(HOUSE_ROOM3_NAME, "technique", technique, 0);

    const std::string rules = simpleRuleXml(HOUSE_RULE_NAME, HOUSE_BOOL_IN, "==",
                                            "true", HOUSE_BOOL_OUT, "true");

    loadConfig(ioXmlDocument(ios), rulesXmlDocument(rules));

    //CORRECTED IN E4.0d. This used to say the load raises one EventIOAdded per
    //IO, "5 here". BOTH HALVES WERE WRONG: the load raises NOTHING AT ALL, and
    //the house holds EIGHT IOs. EventIOAdded has exactly one call site,
    //ListeRoom::createIO() (ListeRoom.cpp:466), which is the RUNTIME path of
    //the JSON API; config loading goes through Room::LoadFromXml()
    //(Room.cpp:152-175), which builds and attaches the IOs silently. A client
    //connected while the server boots sees nothing of the house being built.
    //Pinned by JsonApiEvents_test.cpp:LoadingAHouseFromConfigRaisesNoEventAtAll.
    //NOTE FOR THAT KIND OF CASE: since this function no longer pumps (below),
    //a case asserting that the load is SILENT must pump itself, otherwise it
    //would read zero for the wrong reason - undelivered is not unraised.
    //
    //THERE IS NO DRAIN HERE ANY MORE EITHER, AND ADDING ONE BACK NEEDS A
    //MEASUREMENT. This function used to end with a pumpEventLoop(). Its real
    //job was to absorb the PREVIOUS case's backlog - its teardown destroyed
    //its rooms and ~Room() raised one EventIODeleted per IO (Room.cpp:77)
    //after TearDown() had already pumped. E4.0g moved that pump after
    //CoreFixture::TearDown(), so the backlog no longer exists and this drain
    //became dead code. MEASURED, twice and independently: with it removed,
    //every binary sharing this harness is green over fifteen --gtest_shuffle
    //seeds, and no caller of loadReferenceHouse() raises an event before
    //calling it. Keeping it would have contradicted the rule TearDown() states
    //for sub-tickets - do not keep a defensive pump nothing exercises - with
    //the harness exempting itself from its own rule.
    //
    //If a future case DOES raise events before asking for the house, pump in
    //that case, where the need is visible, and say what you are draining.
}

} //namespace CalaosTest
