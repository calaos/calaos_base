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
 * a long pump fires HttpClient's read timeout Timer) is avoided here: no case
 * pumps the loop, and the deferred cases fire the parked callback by hand.
 *
 * ON THE CAMERA: the reference house of the harness carries two StandardMjpeg
 * cameras, whose downloadSnapshot() really talks to the network. A local fake
 * is used instead, in two modes - immediate (the answer comes back inside
 * processApi()) and deferred (the test parks the callback and decides when, or
 * whether, it fires). Deferred is how a real camera behaves: IPCam holds the
 * callback in a member (IPCam.cpp:130) and hands it back when the HTTP GET to
 * the camera completes, which can be long after the client hung up.
 *
 * IO ids are prefixed t317d_ and used nowhere else: Config's IO state cache is
 * process wide and never cleared (see CalaosCoreFixture.h).
 */

#include "JsonApiCharacterization.h"

#include "IPCam.h"
#include "ListeRoom.h"
#include "Utils.h"

#include <deque>
#include <functional>
#include <string>

using namespace CalaosTest;
using namespace Calaos;

namespace
{

const char *const CAMERA_ID = "t317d_camera";

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

}

class JsonApiCameraSnapshotTest: public JsonApiCharacterizationTest
{
protected:
    void SetUp() override
    {
        JsonApiCharacterizationTest::SetUp();

        forgetIOState(CAMERA_ID);

        loadConfig();
    }

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

/* KNOWN DIVERGENCE, pinned as it is: processCamera() has no else branch
 * (JsonApiHandlerHttp.cpp:909-999). A known camera with an unknown "type"
 * answers NOTHING AT ALL - same shape of silence as autoscenario. Silence is
 * observable behaviour, so it is asserted.
 */
TEST_F(JsonApiCameraSnapshotTest, UnknownTypeAnswersNothingAtAll)
{
    FakeSnapshotCamera *camera = addCamera(SNAPSHOT_BYTES);

    HttpTestRequest req;
    req.send(authenticated(Json{
                               { "action", "camera" },
                               { "type", "t317d_not_a_type" },
                               { "id", CAMERA_ID },
                           }));

    EXPECT_EQ(0u, req.count());
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
