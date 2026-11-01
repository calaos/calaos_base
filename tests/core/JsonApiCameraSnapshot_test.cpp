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

/* T3.17d - the one-shot camera snapshot: the get_picture branch of
 * JsonApiHandlerHttp::processCamera().
 *
 * WHY THIS FILE EXISTS AT ALL
 * ---------------------------
 * This branch is the ONLY site of the whole T3.17 series that lives in a
 * handler rather than in JsonApi.cpp: the other 42 call sites are covered
 * transitively, because both handlers derive from JsonApi and share its
 * apiAlive token. This one captures the handler itself in a callback the
 * IPCam keeps across a real HTTP round trip to the camera, and needs the
 * handlerAlive token of JsonApiHandlerHttp.h:64.
 *
 * AND WHY IT NEEDS ITS OWN NET. E4.0 excludes get_camera_pic and get_cover
 * from characterization because they spawn calaos_picture through
 * uvw::ProcessHandle (JsonApiCharacterization.h, "OUT OF SCOPE"). MEASURED:
 * get_picture is a DIFFERENT action (action=camera, type=get_picture,
 * JsonApiHandlerHttp.cpp:168-169) from get_camera_pic (:164-165, the one that
 * spawns). It spawns nothing at all - it downloads a snapshot and formats an
 * HTTP response - so it is fully characterizable in process, and this file
 * does it. No E4.0 sub-ticket covers it either way.
 *
 * THE ANSWER IS NOT JSON. get_picture replies image/jpeg. So the risk of a
 * badly placed guard here is not a truncated document, it is a client that
 * receives NOTHING, or an empty body that a browser would happily render as a
 * broken image. The cases below pin the exact bytes of the answer and, above
 * all, pin that a LATE answer to a still connected client is still delivered
 * whole (LateSnapshotStillReachesTheClient) - that is the case a guard
 * checking the wrong token would break, silently.
 *
 * ON THE HARNESS: the E4.0a harness (JsonApiCharacterization.h) gives the
 * single-shot HTTP request over a real HttpClient. Read its header, in
 * particular the "TRAPS" section. Trap 2 (an HttpTestRequest kept alive across
 * a long pump fires HttpClient's read timeout Timer) is avoided here: without
 * a width no case pumps the loop and the deferred cases fire the parked
 * callback by hand; with one, pumpUntil() only waits for a worker thread or a
 * child process, a few milliseconds.
 *
 * ON THE CAMERA: the reference house of the harness carries two StandardMjpeg
 * cameras, whose downloadSnapshot() really talks to the network. A local fake
 * is used instead, in two modes - immediate (the answer comes back inside
 * processApi()) and deferred (the test parks the callback and decides when, or
 * whether, it fires). Deferred is how a real camera behaves: IPCam holds the
 * callback in a member (snapshotWaiters) and hands it back when the HTTP GET to
 * the camera completes, which can be long after the client hung up.
 *
 * IO ids are prefixed t317d_ and used nowhere else: Config's IO state cache is
 * process wide and never cleared (see CalaosCoreFixture.h).
 */

#include "JsonApiCharacterization.h"

#include "AudioPlayer.h"
#include "IPCam.h"
#include "ListeRoom.h"
#include "PictureResizer.h"
#include "Timer.h"
#include "Utils.h"
#include "core/JpegTestFrames.h"
#include "libuvw.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace CalaosTest;
using namespace Calaos;

namespace
{

const char *const CAMERA_ID = "t317d_camera";
const char *const PLAYER_ID = "p3cp_player";
const char *const COVER_URL = "http://p3cp-cover.invalid/cover.jpg";
const char *const CAMERA_URL = "http://t317d-camera.invalid/snapshot?token=t317d";

//A recognisable, non empty "jpeg". The production code never looks at the
//bytes, it just puts them in the body, so any payload with a NUL in it is a
//better probe than a printable string: it also proves the body is not built
//through a C string anywhere on the way.
const std::string SNAPSHOT_BYTES("\xff\xd8\xff\xe0" "t317d\0jpeg\xff\xd9", 15);

/* An IPCam whose snapshot is decided by the test.
 *
 * immediate mode (default): downloadSnapshot() answers inside the call, so the
 * whole response is available as soon as processApi() returns.
 *
 * deferred mode: the callback is parked and the test fires it later - or hands
 * it to takeCallback() so it can outlive the camera itself.
 */
class FakeSnapshotCamera: public IPCam
{
public:
    FakeSnapshotCamera(Params &p): IPCam(p) {}

    bool deferred = false;
    std::string snapshot;

    //How many times the handler asked for a snapshot. The anti-storm probe: a
    //one-shot get_picture must ask exactly once and never re-arm (unlike the
    //mjpeg fallback path of downloadCameraPicture()).
    int downloadCount = 0;

    void downloadSnapshot(std::function<void(const std::string &)> dataCb) override
    {
        downloadCount++;

        if (deferred)
            pending.push_back(dataCb);
        else
            dataCb(snapshot);
    }

    std::string getPictureUrl() override { return CAMERA_URL; }

    size_t pendingCount() const { return pending.size(); }

    //Deliver the oldest parked answer. False when nothing is parked.
    bool fireNext()
    {
        if (pending.empty())
            return false;
        std::function<void(const std::string &)> cb = pending.front();
        pending.pop_front();
        cb(snapshot);
        return true;
    }

    //Detach the oldest parked answer from the camera, so it can be fired after
    //the camera itself has been deleted.
    std::function<void()> takeNext()
    {
        if (pending.empty())
            return std::function<void()>();
        std::function<void(const std::string &)> cb = pending.front();
        pending.pop_front();
        const std::string data = snapshot;
        return [cb, data]() { cb(data); };
    }

private:
    std::deque<std::function<void(const std::string &)>> pending;
};

//Workers and child processes finish on their own time: the loop has to turn
//until they do, and a timeout makes a lost answer a failure instead of a hang.
bool pumpUntil(const std::function<bool()> &done, int timeoutMs = 10000)
{
    auto loop = uvw::Loop::getDefault();
    const auto limit = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!done())
    {
        if (std::chrono::steady_clock::now() > limit)
            return false;
        loop->run<uvw::Loop::Mode::NOWAIT>();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

void pumpFor(int ms)
{
    const auto limit = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    pumpUntil([&limit]()
              {
        return std::chrono::steady_clock::now() > limit;
    });
}

PictureResizer &resizer()
{
    return PictureResizer::Instance();
}

JpegThumbnail::Result thumbOf(int width)
{
    return { JpegThumbnail::Status::Resized, "thumb:" + std::to_string(width) };
}

JpegThumbnail::Result fakeDecode(const std::string &, int width)
{
    return thumbOf(width);
}

//Holds the decoder on its worker until the case lets it go. The wait is bounded
//so that a decoder running on the loop thread fails the case rather than hangs.
class DecoderGate
{
public:
    JpegThumbnail::Result pass(int width)
    {
        std::unique_lock<std::mutex> lock(mutex);
        entries++;
        worker = std::this_thread::get_id();
        changed.wait_for(lock, std::chrono::seconds(5), [this]()
                         {
            return isOpen;
        });
        return thumbOf(width);
    }

    void open()
    {
        std::lock_guard<std::mutex> lock(mutex);
        isOpen = true;
        changed.notify_all();
    }

    int entered()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return entries;
    }

    std::thread::id workerThread()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return worker;
    }

private:
    std::mutex mutex;
    std::condition_variable changed;
    bool isOpen = false;
    int entries = 0;
    std::thread::id worker;
};

using Requests = std::vector<std::unique_ptr<HttpTestRequest>>;

bool allAnswered(const Requests &clients)
{
    for (const std::unique_ptr<HttpTestRequest> &client: clients)
    {
        if (client->count() != 1)
            return false;
    }
    return true;
}

//The logger writes to std::cout: what a case produces is read back from there.
std::string captureStdout(const std::function<void()> &fn)
{
    std::ostringstream sink;
    std::streambuf *saved = std::cout.rdbuf(sink.rdbuf());
    fn();
    std::cout.rdbuf(saved);
    return sink.str();
}

size_t countOf(const std::string &text, const std::string &needle)
{
    size_t count = 0;
    for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1))
        count++;
    return count;
}

void expectNoCameraIdentity(const std::string &log)
{
    EXPECT_EQ(std::string::npos, log.find(CAMERA_ID)) << log;
    EXPECT_EQ(std::string::npos, log.find(CAMERA_URL)) << log;
    EXPECT_EQ(std::string::npos, log.find("t317d-camera")) << log;
}

const char *const NO_DECODER_LINE = "Built without libjpeg: camera pictures are sent in full resolution";
const char *const UNDECODABLE_LINE = "Camera frame cannot be resized: sent in full resolution";
const char *const BUSY_LINE = "Too many pictures to resize: sent in full resolution";
const char *const NO_CONVERT_LINE = "calaos_picture cannot resize (ImageMagick): sent in full resolution";

/* A calaos_picture of the test's own, found through CALAOS_BIN_PREFIX. Every
 * run notes its first argument in a file, which is how a case counts them. */
class FakePictureTool
{
public:
    explicit FakePictureTool(const std::string &body)
    {
        char tmpl[] = "/tmp/calaos_t317d_picture_XXXXXX";
        const char *created = ::mkdtemp(tmpl);
        dir = created ? created : "";
        if (dir.empty())
            return;

        {
            std::ofstream script(toolPath(), std::ios::out | std::ios::trunc);
            script << "#!/bin/sh\necho \"$1\" >> '" << runsPath() << "'\n"
                   << body;
        }
        ready = ::chmod(toolPath().c_str(), 0755) == 0 &&
                ::setenv("CALAOS_BIN_PREFIX", dir.c_str(), 1) == 0;
    }

    ~FakePictureTool()
    {
        ::unsetenv("CALAOS_BIN_PREFIX");
        if (dir.empty())
            return;
        ::unlink(toolPath().c_str());
        ::unlink(runsPath().c_str());
        ::rmdir(dir.c_str());
    }

    bool isReady() const { return ready; }

    void uninstall() { ::unlink(toolPath().c_str()); }

    std::vector<std::string> runs() const
    {
        std::vector<std::string> lines;
        std::ifstream in(runsPath());
        for (std::string line; std::getline(in, line);)
            lines.push_back(line);
        return lines;
    }

private:
    std::string toolPath() const { return dir + "/calaos_picture"; }
    std::string runsPath() const { return dir + "/runs"; }

    std::string dir;
    bool ready = false;
};

//Answers the cover url synchronously, like a player whose cover is already known.
class FakeCoverPlayer: public AudioPlayer
{
public:
    FakeCoverPlayer(Params &p):
        AudioPlayer(p)
    {
    }

    std::string coverUrl;

    void get_album_cover(AudioRequest_cb cb, AudioPlayerData = AudioPlayerData()) override
    {
        AudioPlayerData d;
        d.svalue = coverUrl;
        cb(d);
    }
};
}

class JsonApiCameraSnapshotTest: public JsonApiCharacterizationTest
{
protected:
    void SetUp() override
    {
        JsonApiCharacterizationTest::SetUp();

        forgetIOState(CAMERA_ID);
        forgetIOState(PLAYER_ID);

        loadConfig();

        resizer().resetForTest(fakeDecode);
    }

    //The cameras die in the parent teardown: their resizes have to end before.
    void TearDown() override
    {
        gate.open();
        EXPECT_TRUE(pumpUntil([]()
                              {
            return resizer().idle();
        }));
        resizer().resetForTest();

        JsonApiCharacterizationTest::TearDown();
    }

    void useGate()
    {
        resizer().resetForTest([this](const std::string &, int width)
                               {
            return gate.pass(width);
        });
    }

    //Occupies every place of the resizer. Needs useGate().
    void saturateResizer()
    {
        auto source = std::make_shared<const std::string>("frame");
        for (size_t i = 0; i < PictureResizer::maxRunning + PictureResizer::maxPending; i++)
            EXPECT_TRUE(resizer().submit(source, 100, [](JpegThumbnail::Result)
                                         {
                                         }));
    }

    DecoderGate gate;

    /* Register a fake camera in ListeRoom exactly like a config loaded IO:
     * owned by its room (clearCoreState() deletes it) and reachable through
     * ListeRoom::get_io(), which is what processCamera() looks it up with.
     */
    static FakeSnapshotCamera *addCamera(const std::string &snapshot)
    {
        Params p;
        p.Add("id", CAMERA_ID);
        p.Add("name", "Fake snapshot camera");
        p.Add("type", "FakeSnapshotCamera");

        FakeSnapshotCamera *camera = new FakeSnapshotCamera(p);
        camera->snapshot = snapshot;

        firstRoom()->AddIO(camera);
        ListeRoom::Instance().addIOHash(camera);
        return camera;
    }

    static Json pictureRequest(const std::string &id)
    {
        return authenticated(Json{
                                 { "action", "camera" },
                                 { "type", "get_picture" },
                                 { "id", id },
                             });
    }

    static Json pictureRequest(const std::string &id, const std::string &width)
    {
        Json request = pictureRequest(id);
        request["width"] = width;
        return request;
    }

    static Requests sendPictures(int clients, const std::string &width)
    {
        Requests requests;
        for (int i = 0; i < clients; i++)
        {
            requests.push_back(std::make_unique<HttpTestRequest>());
            requests.back()->send(pictureRequest(CAMERA_ID, width));
        }
        return requests;
    }

    static FakeCoverPlayer *addPlayer(const std::string &cover)
    {
        Params p;
        p.Add("id", PLAYER_ID);
        p.Add("name", "Fake cover player");
        p.Add("type", "FakeCoverPlayer");

        FakeCoverPlayer *player = new FakeCoverPlayer(p);
        player->coverUrl = cover;

        firstRoom()->AddIO(player);
        ListeRoom::Instance().addIOHash(player);
        return player;
    }

    static Json coverRequest()
    {
        return authenticated(Json{
            { "action", "get_cover" },
            { "id", PLAYER_ID },
        });
    }

    static Json audioCoverRequest()
    {
        return authenticated(Json{
            { "action", "audio" },
            { "audio_action", "get_cover" },
            { "id", PLAYER_ID },
        });
    }

    //Fills the process limit with `limit` sleeping runs, checks that one more
    //request is refused at once, then that a freed place is given back.
    void expectPictureLimit(size_t limit);

    static Json cameraPicRequest(const std::string &width = std::string())
    {
        Json request = authenticated(Json{
            { "action", "get_camera_pic" },
            { "id", CAMERA_ID },
        });
        if (!width.empty())
            request["width"] = width;
        return request;
    }
};

/*******************************************************************************
 * The nominal answer. get_picture is NOT a JSON operation: the body is the
 * snapshot itself, byte for byte, under an image/jpeg content type.
 ******************************************************************************/

TEST_F(JsonApiCameraSnapshotTest, GetPictureAnswersTheSnapshotBytes)
{
    FakeSnapshotCamera *camera = addCamera(SNAPSHOT_BYTES);

    HttpTestRequest req;
    req.send(pictureRequest(CAMERA_ID));

    ASSERT_EQ(1u, req.count());
    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_EQ("image/jpeg", req.header("Content-Type"));
    EXPECT_EQ("close", Utils::str_to_lower(req.header("Connection")));
    EXPECT_EQ(Utils::to_string(SNAPSHOT_BYTES.length()), req.header("Content-Length"));
    EXPECT_EQ(SNAPSHOT_BYTES, req.body());

    //One shot: the snapshot is asked for once and the branch never re-arms
    EXPECT_EQ(1, camera->downloadCount);
    //And nothing is sent on the connection besides the picture
    EXPECT_TRUE(req.closes().empty());
}

/* A camera that answers nothing falls back on the shipped camfail.jpg. The
 * file lives under the install data directory and is normally absent from a
 * test tree, which is why only the shape is pinned here and not the bytes: the
 * point of the case is that the branch answers 200 image/jpeg and NOT the
 * snapshot payload.
 */
TEST_F(JsonApiCameraSnapshotTest, EmptyDownloadAnswersTheFallbackPicture)
{
    FakeSnapshotCamera *camera = addCamera(std::string());

    HttpTestRequest req;
    req.send(pictureRequest(CAMERA_ID));

    ASSERT_EQ(1u, req.count());
    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_EQ("image/jpeg", req.header("Content-Type"));
    EXPECT_EQ(1, camera->downloadCount);
}

/*******************************************************************************
 * The error shapes. These two ARE json, and go through the semantic oracle.
 ******************************************************************************/

//An id that is not an IPCam answers a JSON error, synchronously, and the
//misspelling of "unkown" is production behaviour: freeze it, do not fix it.
TEST_F(JsonApiCameraSnapshotTest, UnknownCameraIdAnswersJsonError)
{
    HttpTestRequest req;
    req.send(pictureRequest(ID_BOOL_IN));

    ASSERT_EQ(1u, req.count());
    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    const Json expected = Json{{ "error", "unkown camera id" }};
    EXPECT_JSON_EQ(expected, req.body());
}

TEST_F(JsonApiCameraSnapshotTest, MissingCameraAnswersJsonError)
{
    HttpTestRequest req;
    req.send(pictureRequest("t317d_no_such_io"));

    ASSERT_EQ(1u, req.count());
    const Json expected = Json{{ "error", "unkown camera id" }};
    EXPECT_JSON_EQ(expected, req.body());
}

/* FLIPPED BY T3.59: processCamera() has an else now. The camera half of the
 * assertion is what the else must NOT break - a refused type still asks the
 * camera for nothing.
 */
TEST_F(JsonApiCameraSnapshotTest, UnknownTypeAnswersAnErrorAndAsksTheCameraForNothing)
{
    FakeSnapshotCamera *camera = addCamera(SNAPSHOT_BYTES);

    HttpTestRequest req;
    req.send(authenticated(Json{
                               { "action", "camera" },
                               { "type", "t317d_not_a_type" },
                               { "id", CAMERA_ID },
                           }));

    ASSERT_EQ(1u, req.count());
    const Json expected = Json{{ "error", "unknown camera type" }};
    EXPECT_JSON_EQ(expected, req.body());
    EXPECT_EQ(0, camera->downloadCount);
}

/*******************************************************************************
 * THE ANTI-SWALLOW CASE - the reason a guard can be dangerous here.
 *
 * The snapshot really is a network round trip: IPCam parks the callback and
 * gives it back when the camera answers, which is after processApi() has
 * returned. The client is still connected the whole time and MUST get its
 * picture, whole. A lifetime guard checking the wrong token, or capturing the
 * shared_ptr instead of a weak_ptr, would turn this into a request that never
 * answers - no crash, no log, nothing to notice.
 ******************************************************************************/

TEST_F(JsonApiCameraSnapshotTest, LateSnapshotStillReachesTheClient)
{
    FakeSnapshotCamera *camera = addCamera(SNAPSHOT_BYTES);
    camera->deferred = true;

    HttpTestRequest req;
    req.send(pictureRequest(CAMERA_ID));

    //The request returned without answering anything: the camera is still
    //downloading.
    EXPECT_EQ(0u, req.count());
    ASSERT_EQ(1u, camera->pendingCount());

    ASSERT_TRUE(camera->fireNext());

    ASSERT_EQ(1u, req.count());
    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_EQ("image/jpeg", req.header("Content-Type"));
    EXPECT_EQ(SNAPSHOT_BYTES, req.body());
}

/*******************************************************************************
 * LIFETIME - what T3.17d actually fixes.
 *
 * The snapshot round trip outlives processApi(), and the callback is held by
 * the IPCam, not by the handler. Two things can die in between, and the T3.17
 * parent ticket expects BOTH to be a use-after-free here, as they were on the
 * playlist chain of T3.17a. MEASURED, and only one of them is:
 *
 *  - the handler (the client disconnected, HttpClient.cpp:162 deletes the
 *    whole JsonApi object, derived part included). REAL: the late callback
 *    formats its response through httpClient and emits it on sendData, both
 *    freed. ClientGoneBeforeTheSnapshotIsIgnored below is a heap-use-after-free
 *    without the guard.
 *
 *  - the IPCam itself (deleted through the API while the transfer runs). NOT a
 *    use-after-free at this site, and the reason is in the code, not in luck:
 *    the lambda captures `camera` through [=] but never dereferences it, and
 *    ~IPCam() deletes the UrlDownloader that owns the callback (IPCam.cpp:48),
 *    so a deleted camera simply never calls back. That is why this branch
 *    keeps the pointer and does NOT need the by-id re-lookup T3.17a had to
 *    introduce for AudioPlayer. CameraDeletedMidTransferStillAnswers pins the
 *    behaviour a detached callback would have, so a future refactor that makes
 *    the callback outlive the camera cannot silently reintroduce the question.
 ******************************************************************************/

/* The client hangs up while the camera is still sending. The late answer must
 * not touch the dead handler at all.
 *
 * Without the guard this is, under ASan, a heap-use-after-free in
 * HttpClient::buildHttpResponse() reached from the snapshot callback. It does
 * not crash a plain build, which is exactly why the case exists.
 */
TEST_F(JsonApiCameraSnapshotTest, ClientGoneBeforeTheSnapshotIsIgnored)
{
    FakeSnapshotCamera *camera = addCamera(SNAPSHOT_BYTES);
    camera->deferred = true;

    {
        HttpTestRequest req;
        req.send(pictureRequest(CAMERA_ID));

        //Still downloading: nothing was answered, and the callback the camera
        //holds is the one that will run on a dead handler.
        ASSERT_EQ(0u, req.count());
        ASSERT_EQ(1u, camera->pendingCount());
        //req dies here, exactly as when the client disconnects
    }

    //The camera answers to nobody. The guard has to swallow this one.
    EXPECT_TRUE(camera->fireNext());
    EXPECT_EQ(0u, camera->pendingCount());
    //One request, one download, and nothing re-armed by the dead handler
    EXPECT_EQ(1, camera->downloadCount);
}

/* Same, on the empty-download branch: the fallback picture is read and sent by
 * a different code path (buildHttpResponseFromFile), so it needs the guard as
 * much as the nominal one - a single check at the top covers both, and this
 * case is what proves it.
 */
TEST_F(JsonApiCameraSnapshotTest, ClientGoneBeforeAnEmptySnapshotIsIgnored)
{
    FakeSnapshotCamera *camera = addCamera(std::string());
    camera->deferred = true;

    {
        HttpTestRequest req;
        req.send(pictureRequest(CAMERA_ID));
        ASSERT_EQ(1u, camera->pendingCount());
    }

    EXPECT_TRUE(camera->fireNext());
    EXPECT_EQ(1, camera->downloadCount);
}

/* The camera IO is deleted through the API while the transfer runs, and the
 * answer is fired from a callback that was detached from it beforehand - the
 * only way this site can be reached with a dead camera at all.
 *
 * The client is still connected, so it MUST get its picture (T3.17 invariant:
 * no silent disappearance). It does: the guard keys on the handler, and the
 * handler is alive. A guard that had keyed on the camera instead would turn
 * this into a request that never answers.
 */
TEST_F(JsonApiCameraSnapshotTest, CameraDeletedMidTransferStillAnswers)
{
    FakeSnapshotCamera *camera = addCamera(SNAPSHOT_BYTES);
    camera->deferred = true;

    HttpTestRequest req;
    req.send(pictureRequest(CAMERA_ID));
    ASSERT_EQ(1u, camera->pendingCount());

    std::function<void()> lateAnswer = camera->takeNext();
    ASSERT_TRUE((bool)lateAnswer);
    ASSERT_TRUE(deleteIO(camera));
    camera = nullptr;

    EXPECT_EQ(0u, req.count());
    lateAnswer();
    lateAnswer = std::function<void()>();

    ASSERT_EQ(1u, req.count());
    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_EQ("image/jpeg", req.header("Content-Type"));
    EXPECT_EQ(SNAPSHOT_BYTES, req.body());
}

/* Both gone: the camera first, then the client, and the answer arrives last.
 * The guard is the only thing standing between that callback and two freed
 * objects.
 */
TEST_F(JsonApiCameraSnapshotTest, ClientAndCameraGoneIsIgnored)
{
    FakeSnapshotCamera *camera = addCamera(SNAPSHOT_BYTES);
    camera->deferred = true;

    std::function<void()> lateAnswer;
    {
        HttpTestRequest req;
        req.send(pictureRequest(CAMERA_ID));
        ASSERT_EQ(1u, camera->pendingCount());

        lateAnswer = camera->takeNext();
        ASSERT_TRUE((bool)lateAnswer);
        ASSERT_TRUE(deleteIO(camera));
        camera = nullptr;
    }

    lateAnswer();
    lateAnswer = std::function<void()>();
}

/*******************************************************************************
 * The width of get_picture: answered from a worker, never from the loop.
 ******************************************************************************/

TEST_F(JsonApiCameraSnapshotTest, WidthAnswersTheThumbnail)
{
    FakeSnapshotCamera *camera = addCamera(SNAPSHOT_BYTES);

    HttpTestRequest req;
    req.send(pictureRequest(CAMERA_ID, "236"));

    EXPECT_EQ(0u, req.count());
    ASSERT_TRUE(pumpUntil([&req]()
                          {
        return req.count() == 1;
    }));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_EQ("image/jpeg", req.header("Content-Type"));
    EXPECT_EQ("thumb:236", req.body());
    EXPECT_EQ(1, camera->downloadCount);
    EXPECT_EQ(1u, resizer().decodesStarted());
}

TEST_F(JsonApiCameraSnapshotTest, TheLoopKeepsTurningDuringADecode)
{
    addCamera(SNAPSHOT_BYTES);
    useGate();

    HttpTestRequest thumbnail;
    thumbnail.send(pictureRequest(CAMERA_ID, "236"));
    ASSERT_TRUE(pumpUntil([this]()
                          {
        return gate.entered() == 1;
    }));

    bool fired = false;
    Timer::singleShot(0.001, [&fired]()
                      {
        fired = true;
    });
    EXPECT_TRUE(pumpUntil([&fired]()
                          {
        return fired;
    }));

    HttpTestRequest other;
    other.send(pictureRequest(CAMERA_ID));
    ASSERT_EQ(1u, other.count());
    EXPECT_EQ(SNAPSHOT_BYTES, other.body());

    EXPECT_NE(std::this_thread::get_id(), gate.workerThread());
    EXPECT_EQ(0u, thumbnail.count());

    gate.open();
    ASSERT_TRUE(pumpUntil([&thumbnail]()
                          {
        return thumbnail.count() == 1;
    }));
    EXPECT_EQ("thumb:236", thumbnail.body());
}

TEST_F(JsonApiCameraSnapshotTest, NoWidthAndEmptyWidthSkipTheResizer)
{
    addCamera(SNAPSHOT_BYTES);

    for (const Json &request: { pictureRequest(CAMERA_ID), pictureRequest(CAMERA_ID, "") })
    {
        HttpTestRequest req;
        req.send(request);

        ASSERT_EQ(1u, req.count());
        EXPECT_EQ(SNAPSHOT_BYTES, req.body());
    }

    EXPECT_EQ(0u, resizer().decodesStarted());
}

TEST_F(JsonApiCameraSnapshotTest, InvalidWidthIsRefusedBeforeAnyDownload)
{
    FakeSnapshotCamera *camera = addCamera(SNAPSHOT_BYTES);

    for (const char *width: { "0", "-1", "10001", "abc", "12a", " 12", "1.5", "99999999999999999999" })
    {
        SCOPED_TRACE(width);

        HttpTestRequest req;
        req.send(pictureRequest(CAMERA_ID, width));

        ASSERT_EQ(1u, req.count());
        const Json expected = Json{ { "error", "invalid width parameter" } };
        EXPECT_JSON_EQ(expected, req.body());
    }

    EXPECT_EQ(0, camera->downloadCount);
    EXPECT_EQ(0u, resizer().decodesStarted());
}

TEST_F(JsonApiCameraSnapshotTest, LargestWidthIsAccepted)
{
    addCamera(SNAPSHOT_BYTES);

    HttpTestRequest req;
    req.send(pictureRequest(CAMERA_ID, "10000"));

    ASSERT_TRUE(pumpUntil([&req]()
                          {
        return req.count() == 1;
    }));
    EXPECT_EQ("thumb:10000", req.body());
}

TEST_F(JsonApiCameraSnapshotTest, WidthChangesNothingToAuthentication)
{
    FakeSnapshotCamera *camera = addCamera(SNAPSHOT_BYTES);

    Json request = Json{
        { "action", "camera" },
        { "type", "get_picture" },
        { "id", CAMERA_ID },
    };

    HttpTestRequest refused;
    refused.send(request);

    request["width"] = "236";
    HttpTestRequest refusedWithWidth;
    refusedWithWidth.send(request);

    ASSERT_EQ(1u, refused.count());
    EXPECT_EQ(refused.rawResponses(), refusedWithWidth.rawResponses());
    EXPECT_EQ(0, camera->downloadCount);
}

/*******************************************************************************
 * One download and one decode, however many clients poll the same width.
 ******************************************************************************/

TEST_F(JsonApiCameraSnapshotTest, ClientsOfOneWidthShareTheDownloadAndTheDecode)
{
    FakeSnapshotCamera *camera = addCamera(SNAPSHOT_BYTES);
    camera->deferred = true;

    Requests clients = sendPictures(5, "236");
    EXPECT_EQ(1, camera->downloadCount);

    ASSERT_TRUE(camera->fireNext());
    ASSERT_TRUE(pumpUntil([&clients]()
                          {
        return allAnswered(clients);
    }));

    for (const std::unique_ptr<HttpTestRequest> &client: clients)
        EXPECT_EQ("thumb:236", client->body());

    EXPECT_EQ(1, camera->downloadCount);
    EXPECT_EQ(1u, resizer().decodesStarted());
}

TEST_F(JsonApiCameraSnapshotTest, ClientArrivingDuringTheDecodeJoinsIt)
{
    FakeSnapshotCamera *camera = addCamera(SNAPSHOT_BYTES);
    useGate();

    Requests clients = sendPictures(1, "236");
    ASSERT_TRUE(pumpUntil([this]()
                          {
        return gate.entered() == 1;
    }));

    clients.push_back(std::make_unique<HttpTestRequest>());
    clients.back()->send(pictureRequest(CAMERA_ID, "236"));

    gate.open();
    ASSERT_TRUE(pumpUntil([&clients]()
                          {
        return allAnswered(clients);
    }));

    EXPECT_EQ("thumb:236", clients.back()->body());
    EXPECT_EQ(1, camera->downloadCount);
    EXPECT_EQ(1u, resizer().decodesStarted());
}

//A frame is never kept: the next poll has to get a fresh picture.
TEST_F(JsonApiCameraSnapshotTest, AFinishedDecodeIsNotReused)
{
    FakeSnapshotCamera *camera = addCamera(SNAPSHOT_BYTES);

    for (int i = 0; i < 2; i++)
    {
        HttpTestRequest req;
        req.send(pictureRequest(CAMERA_ID, "236"));
        ASSERT_TRUE(pumpUntil([&req]()
                              {
            return req.count() == 1;
        }));
    }

    EXPECT_EQ(2, camera->downloadCount);
    EXPECT_EQ(2u, resizer().decodesStarted());
}

TEST_F(JsonApiCameraSnapshotTest, EachWidthHasItsOwnDecode)
{
    FakeSnapshotCamera *camera = addCamera(SNAPSHOT_BYTES);

    Requests narrow = sendPictures(1, "236");
    Requests wide = sendPictures(1, "640");

    ASSERT_TRUE(pumpUntil([&]()
                          {
        return allAnswered(narrow) && allAnswered(wide);
    }));

    EXPECT_EQ("thumb:236", narrow[0]->body());
    EXPECT_EQ("thumb:640", wide[0]->body());
    EXPECT_EQ(2u, resizer().decodesStarted());
    EXPECT_EQ(2, camera->downloadCount);
}

//Without this bound a client looping on width would queue a decode per value.
TEST_F(JsonApiCameraSnapshotTest, TooManyWidthsOfOneCameraAnswerTheFullFrame)
{
    FakeSnapshotCamera *camera = addCamera(SNAPSHOT_BYTES);
    camera->deferred = true;

    Requests clients;
    for (const char *width: { "100", "200", "300", "400", "500", "600" })
    {
        clients.push_back(std::make_unique<HttpTestRequest>());
        clients.back()->send(pictureRequest(CAMERA_ID, width));
    }

    while (camera->fireNext())
        ;
    ASSERT_TRUE(pumpUntil([&clients]()
                          {
        return allAnswered(clients);
    }));

    EXPECT_EQ("thumb:400", clients[3]->body());
    EXPECT_EQ(SNAPSHOT_BYTES, clients[4]->body());
    EXPECT_EQ(SNAPSHOT_BYTES, clients[5]->body());
    EXPECT_EQ(4u, resizer().decodesStarted());
}

TEST_F(JsonApiCameraSnapshotTest, TheFifthWidthLogsTheBusyFallbackOnce)
{
    FakeSnapshotCamera *camera = addCamera(SNAPSHOT_BYTES);
    camera->deferred = true;

    Requests clients;
    const std::string log = captureStdout([&]()
                                          {
        for (const char *width: { "100", "200", "300", "400", "500", "600" })
        {
            clients.push_back(std::make_unique<HttpTestRequest>());
            clients.back()->send(pictureRequest(CAMERA_ID, width));
        }
    });

    EXPECT_EQ(1u, countOf(log, BUSY_LINE)) << log;
    expectNoCameraIdentity(log);

    while (camera->fireNext())
        ;
    EXPECT_TRUE(pumpUntil([&clients]()
                          {
        return allAnswered(clients);
    }));
}

TEST_F(JsonApiCameraSnapshotTest, ResizerRunsTwoQueuesSixThenRefuses)
{
    useGate();

    const size_t accepted = PictureResizer::maxRunning + PictureResizer::maxPending;
    auto source = std::make_shared<const std::string>("frame");

    size_t finished = 0;
    for (size_t i = 0; i < accepted; i++)
    {
        EXPECT_TRUE(resizer().submit(source, 100, [&finished](JpegThumbnail::Result result)
                                     {
            EXPECT_EQ("thumb:100", result.data);
            finished++;
        }));
    }

    EXPECT_FALSE(resizer().submit(source, 100, [](JpegThumbnail::Result)
                                  {
        ADD_FAILURE() << "A refused job must not be called";
    }));

    ASSERT_TRUE(pumpUntil([this]()
                          {
        return gate.entered() == 2;
    }));
    pumpFor(30);
    EXPECT_EQ(2, gate.entered());
    EXPECT_EQ(0u, finished);

    gate.open();
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return finished == accepted;
    }));
    EXPECT_EQ(accepted, resizer().decodesStarted());
    EXPECT_TRUE(resizer().idle());
}

TEST_F(JsonApiCameraSnapshotTest, SaturatedResizerAnswersTheFullFrame)
{
    addCamera(SNAPSHOT_BYTES);
    useGate();
    saturateResizer();

    const std::string log = captureStdout([]()
                                          {
        HttpTestRequest req;
        req.send(pictureRequest(CAMERA_ID, "236"));

        ASSERT_EQ(1u, req.count());
        EXPECT_EQ(SNAPSHOT_BYTES, req.body());
    });

    EXPECT_EQ(1u, countOf(log, BUSY_LINE)) << log;
    expectNoCameraIdentity(log);
}

/*******************************************************************************
 * Every fallback is the full frame and one log line, without any identity.
 ******************************************************************************/

TEST_F(JsonApiCameraSnapshotTest, FallbacksSendTheFrameAndLogOnce)
{
    addCamera(SNAPSHOT_BYTES);

    const struct
    {
        const char *name;
        PictureResizer::Decoder decoder;
        const char *line;
    } cases[] = {
        { "undecodable",
          [](const std::string &, int)
          {
        return JpegThumbnail::Result{ JpegThumbnail::Status::Failed, {} };
         },
          UNDECODABLE_LINE },
        { "no decoder",
          [](const std::string &, int)
          {
        return JpegThumbnail::Result{ JpegThumbnail::Status::Unavailable, {} };
         },
          NO_DECODER_LINE },
      { "exception on the worker", [](const std::string &, int) -> JpegThumbnail::Result
        {
        throw std::runtime_error("decoder");
       },
        UNDECODABLE_LINE },
        };

    for (const auto &fallback: cases)
    {
        SCOPED_TRACE(fallback.name);
        resizer().resetForTest(fallback.decoder);

        const std::string log = captureStdout([]()
                                              {
            for (int i = 0; i < 2; i++)
            {
                HttpTestRequest req;
                req.send(pictureRequest(CAMERA_ID, "236"));

                ASSERT_TRUE(pumpUntil([&req]()
                                      {
                    return req.count() == 1;
                }));
                EXPECT_EQ(SNAPSHOT_BYTES, req.body());
            }
        });

        EXPECT_EQ(1u, countOf(log, fallback.line)) << log;
        expectNoCameraIdentity(log);
    }
}

//A frame narrower than asked is the normal answer, not a degradation.
TEST_F(JsonApiCameraSnapshotTest, UnchangedFrameLogsNothing)
{
    addCamera(SNAPSHOT_BYTES);
    resizer().resetForTest([](const std::string &, int)
                           {
        return JpegThumbnail::Result{ JpegThumbnail::Status::Unchanged, {} };
    });

    const std::string log = captureStdout([]()
                                          {
        HttpTestRequest req;
        req.send(pictureRequest(CAMERA_ID, "236"));

        ASSERT_TRUE(pumpUntil([&req]()
                              {
            return req.count() == 1;
        }));
        EXPECT_EQ(SNAPSHOT_BYTES, req.body());
    });

    EXPECT_EQ(0u, countOf(log, "full resolution")) << log;
}

TEST_F(JsonApiCameraSnapshotTest, EmptyDownloadWithAWidthDecodesNothing)
{
    addCamera(std::string());

    HttpTestRequest req;
    req.send(pictureRequest(CAMERA_ID, "236"));

    ASSERT_EQ(1u, req.count());
    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_EQ("image/jpeg", req.header("Content-Type"));
    EXPECT_EQ(0u, resizer().decodesStarted());
}

/*******************************************************************************
 * Whoever leaves during a decode: the result lands on a worker's way back.
 ******************************************************************************/

TEST_F(JsonApiCameraSnapshotTest, ClientGoneDuringTheDecodeLeavesTheOtherServed)
{
    addCamera(SNAPSHOT_BYTES);
    useGate();

    Requests clients = sendPictures(2, "236");
    ASSERT_TRUE(pumpUntil([this]()
                          {
        return gate.entered() == 1;
    }));

    clients.erase(clients.begin());

    gate.open();
    ASSERT_TRUE(pumpUntil([&clients]()
                          {
        return allAnswered(clients);
    }));
    EXPECT_EQ("thumb:236", clients[0]->body());
}

TEST_F(JsonApiCameraSnapshotTest, CameraDeletedDuringTheDecodeIsIgnored)
{
    FakeSnapshotCamera *camera = addCamera(SNAPSHOT_BYTES);
    useGate();

    HttpTestRequest req;
    req.send(pictureRequest(CAMERA_ID, "236"));
    ASSERT_TRUE(pumpUntil([this]()
                          {
        return gate.entered() == 1;
    }));

    ASSERT_TRUE(deleteIO(camera));

    gate.open();
    ASSERT_TRUE(pumpUntil([]()
                          {
        return resizer().idle();
    }));
    EXPECT_EQ(0u, req.count());
}

TEST_F(JsonApiCameraSnapshotTest, FrameArrivingAfterTheCameraIsDeletedDecodesNothing)
{
    FakeSnapshotCamera *camera = addCamera(SNAPSHOT_BYTES);
    camera->deferred = true;

    HttpTestRequest req;
    req.send(pictureRequest(CAMERA_ID, "236"));

    std::function<void()> lateAnswer = camera->takeNext();
    ASSERT_TRUE(bool(lateAnswer));
    ASSERT_TRUE(deleteIO(camera));

    lateAnswer();

    EXPECT_EQ(0u, resizer().decodesStarted());
    EXPECT_EQ(0u, req.count());
}

/*******************************************************************************
 * The real decoder, end to end.
 ******************************************************************************/

#ifdef HAVE_LIBJPEG

TEST_F(JsonApiCameraSnapshotTest, RealFrameIsResized)
{
    resizer().resetForTest();
    addCamera(JpegTestFrames::solid(1600, 900));

    HttpTestRequest req;
    req.send(pictureRequest(CAMERA_ID, "320"));
    ASSERT_TRUE(pumpUntil([&req]()
                          {
        return req.count() == 1;
    }));

    const JpegTestFrames::Decoded picture = JpegTestFrames::decode(req.body());
    EXPECT_EQ(320, picture.width);
    EXPECT_EQ(180, picture.height);
}

TEST_F(JsonApiCameraSnapshotTest, RealTruncatedFrameIsSentAsItCame)
{
    resizer().resetForTest();
    std::string frame = JpegTestFrames::solid(1600, 900);
    frame.resize(frame.size() / 2);
    addCamera(frame);

    const std::string log = captureStdout([&frame]()
                                          {
        HttpTestRequest req;
        req.send(pictureRequest(CAMERA_ID, "320"));

        ASSERT_TRUE(pumpUntil([&req]()
                              {
            return req.count() == 1;
        }));
        EXPECT_EQ(frame, req.body());
    });

    EXPECT_EQ(1u, countOf(log, UNDECODABLE_LINE)) << log;
    expectNoCameraIdentity(log);
}

#endif

/*******************************************************************************
 * get_camera_pic still spawns calaos_picture: its one process per request is
 * bounded, and it tells a frame it could not resize from a failure.
 ******************************************************************************/

void JsonApiCameraSnapshotTest::expectPictureLimit(size_t limit)
{
    FakePictureTool tool("exec sleep 30\n");
    ASSERT_TRUE(tool.isReady());

    Requests running;
    for (size_t i = 0; i < limit; i++)
    {
        running.push_back(std::make_unique<HttpTestRequest>());
        running.back()->send(cameraPicRequest("320"));
    }
    ASSERT_TRUE(pumpUntil([&]()
    {
        return tool.runs().size() == limit;
    }));

    HttpTestRequest refused;
    refused.send(cameraPicRequest("320"));

    ASSERT_EQ(1u, refused.count());
    const Json expected = Json{ { "error_str", "too many pictures in progress" },
                                { "success", "false" } };
    EXPECT_JSON_EQ(expected, refused.body());
    EXPECT_EQ(limit, tool.runs().size());

    //A client that leaves gives its place back
    running.pop_back();

    HttpTestRequest next;
    next.send(cameraPicRequest("320"));
    EXPECT_TRUE(pumpUntil([&]()
    {
        return tool.runs().size() == limit + 1;
    }));
    EXPECT_EQ(0u, next.count());
}

TEST_F(JsonApiCameraSnapshotTest, CameraPicRunsSixProcessesAtMostByDefault)
{
    addCamera(SNAPSHOT_BYTES);
    expectPictureLimit(6);
}

TEST_F(JsonApiCameraSnapshotTest, CameraPicRunsAsManyProcessesAsConfigured)
{
    addCamera(SNAPSHOT_BYTES);
    ASSERT_TRUE(Utils::set_config_option("max_picture_processes", "2"));
    expectPictureLimit(2);
}

TEST_F(JsonApiCameraSnapshotTest, CameraPicLimitAboveTheDefaultIsHonoured)
{
    addCamera(SNAPSHOT_BYTES);
    ASSERT_TRUE(Utils::set_config_option("max_picture_processes", "8"));
    expectPictureLimit(8);
}

TEST_F(JsonApiCameraSnapshotTest, CameraPicInvalidLimitFallsBackToSixAndWarnsOnce)
{
    addCamera(SNAPSHOT_BYTES);

    for (const char *bad: { "0", "-3", "lots", "4x", "1001", " " })
    {
        ASSERT_TRUE(Utils::set_config_option("max_picture_processes", bad));

        const std::string log = captureStdout([this]()
        {
            expectPictureLimit(6);
        });

        //Two refusals-worth of requests ran: the warning is not repeated
        EXPECT_EQ(1u, countOf(log, "max_picture_processes is not a whole number")) << bad << "\n"
                                                                                   << log;
        expectNoCameraIdentity(log);
    }
}

TEST_F(JsonApiCameraSnapshotTest, CameraPicGetsTheUrlOfTheCamera)
{
    addCamera(SNAPSHOT_BYTES);
    FakePictureTool tool("printf frame > \"$2\"\n");
    ASSERT_TRUE(tool.isReady());

    HttpTestRequest req;
    req.send(cameraPicRequest("320"));
    ASSERT_TRUE(pumpUntil([&req]()
                          {
        return req.count() == 1;
    }));

    const Json expected = Json{ { "contenttype", "image/jpeg" },
                                { "data", "ZnJhbWU=" },
                                { "encoding", "base64" },
                                { "success", "true" } };
    EXPECT_JSON_EQ(expected, req.body());
    EXPECT_EQ(std::vector<std::string>{ CAMERA_URL }, tool.runs());
}

TEST_F(JsonApiCameraSnapshotTest, CameraPicUnresizedFrameIsASuccessLoggedOnce)
{
    addCamera(SNAPSHOT_BYTES);
    FakePictureTool tool("printf frame > \"$2\"\nexit 3\n");
    ASSERT_TRUE(tool.isReady());

    const std::string log = captureStdout([]()
                                          {
        for (int i = 0; i < 2; i++)
        {
            HttpTestRequest req;
            req.send(cameraPicRequest("320"));
            ASSERT_TRUE(pumpUntil([&req]()
                                  {
                return req.count() == 1;
            }));

            const Json expected = Json{ { "contenttype", "image/jpeg" },
                                        { "data", "ZnJhbWU=" },
                                        { "encoding", "base64" },
                                        { "success", "true" } };
            EXPECT_JSON_EQ(expected, req.body());
        }
    });

    EXPECT_EQ(1u, countOf(log, NO_CONVERT_LINE)) << log;
    expectNoCameraIdentity(log);
}

//More failures in a row than places: a failed run has to give its place back.
TEST_F(JsonApiCameraSnapshotTest, CameraPicFailureAnswersAndFreesItsPlace)
{
    addCamera(SNAPSHOT_BYTES);
    FakePictureTool tool("exit 1\n");
    ASSERT_TRUE(tool.isReady());

    for (int i = 0; i < 5; i++)
    {
        HttpTestRequest req;
        req.send(cameraPicRequest("320"));
        ASSERT_TRUE(pumpUntil([&req]()
                              {
            return req.count() == 1;
        }));

        const Json expected = Json{ { "error_str", "unable to load data from url" },
                                    { "success", "false" } };
        EXPECT_JSON_EQ(expected, req.body());
    }
}

/*******************************************************************************
 * Album covers go through the same helper as the camera pictures.
 ******************************************************************************/

TEST_F(JsonApiCameraSnapshotTest, GetCoverRunsTheToolOnTheCoverUrl)
{
    addPlayer(COVER_URL);
    FakePictureTool tool("printf frame > \"$2\"\n");
    ASSERT_TRUE(tool.isReady());

    HttpTestRequest req;
    req.send(coverRequest());
    ASSERT_TRUE(pumpUntil([&req]()
                          {
        return req.count() == 1;
    }));

    const Json expected = Json{ { "contenttype", "image/jpeg" },
                                { "data", "ZnJhbWU=" },
                                { "encoding", "base64" },
                                { "success", "true" } };
    EXPECT_JSON_EQ(expected, req.body());
    EXPECT_EQ(std::vector<std::string>{ COVER_URL }, tool.runs());
}

TEST_F(JsonApiCameraSnapshotTest, GetCoverWithoutUrlRunsNothing)
{
    addPlayer("");
    FakePictureTool tool("printf frame > \"$2\"\n");
    ASSERT_TRUE(tool.isReady());

    HttpTestRequest req;
    req.send(coverRequest());

    ASSERT_EQ(1u, req.count());
    const Json expected = Json{ { "error_str", "unable to get url" },
                                { "success", "false" } };
    EXPECT_JSON_EQ(expected, req.body());
    EXPECT_TRUE(tool.runs().empty());
}

TEST_F(JsonApiCameraSnapshotTest, AudioGetCoverAnswersTheFileAsAJpeg)
{
    addPlayer(COVER_URL);
    FakePictureTool tool("printf frame > \"$2\"\n");
    ASSERT_TRUE(tool.isReady());

    HttpTestRequest req;
    req.send(audioCoverRequest());
    ASSERT_TRUE(pumpUntil([&req]()
                          {
        return req.count() == 1;
    }));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_EQ("image/jpeg", req.header("Content-Type"));
    EXPECT_EQ("frame", req.body());
    EXPECT_EQ(std::vector<std::string>{ COVER_URL }, tool.runs());
}

TEST_F(JsonApiCameraSnapshotTest, AudioGetCoverWithoutUrlRunsNothing)
{
    addPlayer("");
    FakePictureTool tool("printf frame > \"$2\"\n");
    ASSERT_TRUE(tool.isReady());

    HttpTestRequest req;
    req.send(audioCoverRequest());

    ASSERT_EQ(1u, req.count());
    const Json expected = Json{ { "error_str", "unable to get url" },
                                { "success", "false" } };
    EXPECT_JSON_EQ(expected, req.body());
    EXPECT_TRUE(tool.runs().empty());
}

TEST_F(JsonApiCameraSnapshotTest, AudioGetCoverFailureAnswersTheError)
{
    addPlayer(COVER_URL);
    FakePictureTool tool("exit 1\n");
    ASSERT_TRUE(tool.isReady());

    HttpTestRequest req;
    req.send(audioCoverRequest());
    ASSERT_TRUE(pumpUntil([&req]()
                          {
        return req.count() == 1;
    }));

    const Json expected = Json{ { "error_str", "unable to load data from url" },
                                { "success", "false" } };
    EXPECT_JSON_EQ(expected, req.body());
}

TEST_F(JsonApiCameraSnapshotTest, CameraPicMissingToolAnswersAndFreesItsPlace)
{
    addCamera(SNAPSHOT_BYTES);
    FakePictureTool tool("exit 0\n");
    ASSERT_TRUE(tool.isReady());
    tool.uninstall();

    Requests clients;
    for (int i = 0; i < 5; i++)
    {
        clients.push_back(std::make_unique<HttpTestRequest>());
        clients.back()->send(cameraPicRequest("320"));

        ASSERT_EQ(1u, clients.back()->count());
        const Json expected = Json{ { "error_str", "unable to load data from url" },
                                    { "success", "false" } };
        EXPECT_JSON_EQ(expected, clients.back()->body());
    }
}
