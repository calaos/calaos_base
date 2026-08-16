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

/* T3.17e - the WEBSOCKET TRANSPORT and the alive token it does not own.
 *
 * THE QUESTION THIS FILE ANSWERS
 * ------------------------------
 * JsonApiHandlerWS declares no lifetime token of its own (JsonApiHandlerWS.h,
 * no occurrence of `alive`). Every guard that protects it - buildJsonState()
 * (T2.15), the recursive playlist chain (T3.17a), and the audio/audio_db
 * chains of T3.17b/c - checks `apiAlive`, a member of the JsonApi BASE
 * sub-object (JsonApi.h:210). T3.17 assumed that this is enough for the
 * websocket transport because "there is only one object and one destruction".
 *
 * That assumption was only ever verified on JsonApiHandlerHttp, whose owner is
 * an HttpClient. The websocket handler's owner is a WebSocket, and WebSocket
 * DERIVES from HttpClient (WebSocket.h:35). The handler is created by
 * WebSocket::checkHandshakeRequest() (WebSocket.cpp:279) but deleted by
 * ~HttpClient() (HttpClient.cpp:162), i.e. by the BASE of the transport, after
 * ~WebSocket() has already run. Nothing in the tree pinned that chain, and no
 * existing test ever builds a real transport at all: WsTestSession of the E4.0a
 * harness constructs its handler with a NULL HttpClient and deletes the handler
 * directly, so it exercises `delete handler`, never `delete transport`.
 *
 * This file builds the real thing - a WebSocket on an unconnected uvw handle,
 * owning a JsonApiHandlerWS through HttpClient::jsonApi, wired exactly like
 * WebSocket.cpp:279 and :341-348 - and destroys the TRANSPORT while an answer
 * is in flight. Result of the audit: the inherited token is sufficient, and
 * these cases are what keeps it sufficient.
 *
 * WHAT A REGRESSION LOOKS LIKE HERE
 * ---------------------------------
 *   - ~HttpClient() stops deleting jsonApi, or a WebSocket-level owner is
 *     introduced: TransportDeathDestroysTheHandlerOnce fails (destructor count
 *     wrong, or the token still valid) and the in-flight cases start answering
 *     into a dead transport.
 *   - JsonApiHandlerWS gets a token of its own that shadows or diverges from
 *     the base one: the in-flight cases below fail, because the guards that
 *     actually run live in JsonApi.cpp and read apiAlive.
 *   - The playlist guard of T3.17a is removed: ClientGoneMidChain* become
 *     heap-use-after-free under ASan instead of silent no-ops.
 *   - A guard is dropped in too eagerly and swallows answers:
 *     LiveTransportGetsTheWholeAnswer fails.
 *
 * ON THE HARNESS: read JsonApiCharacterization.h first, in particular the
 * "TRAPS" section. Trap 2 (the live read Timer of HttpClient) applies here:
 * every transport built below is destroyed inside the case that built it, and
 * nothing pumps the loop long enough for a 30s timeout to fire.
 *
 * The oracle stays the semantic one: no serialized JSON is ever compared.
 *
 * IO ids are prefixed t317e_ and used nowhere else: Config's IO state cache is
 * process wide and never cleared (see CalaosCoreFixture.h).
 */

#include "JsonApiCharacterization.h"

#include "AudioPlayer.h"
#include "EventManager.h"
#include "ListeRoom.h"
#include "Utils.h"
#include "WebSocket.h"
#include "libuvw.h"

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace CalaosTest;
using namespace Calaos;

namespace
{

const char *const PLAYER_ID = "t317e_player";

//Incremented by ~ProbeHandler. Reset by the fixture before every case.
int handlerDestroyCount = 0;

/* The websocket handler the transport owns, with two test-only additions:
 * it counts its own destructions, and it hands out a weak_ptr on the alive
 * token of its JsonApi BASE sub-object - the very token every guard in
 * JsonApi.cpp checks. Nothing else is overridden, so the object the transport
 * owns behaves exactly like the JsonApiHandlerWS of WebSocket.cpp:279.
 */
class ProbeHandler: public JsonApiHandlerWS
{
public:
    explicit ProbeHandler(HttpClient *client): JsonApiHandlerWS(client)
    {
        //Login is a case of its own (E4.0e); every case here starts logged in.
        setAuthenticated(true);
    }

    ~ProbeHandler() override { handlerDestroyCount++; }

    //The token of the JsonApi base sub-object, NOT a token of this class.
    std::weak_ptr<bool> baseAliveToken() const { return apiAlive; }
};

/* A real WebSocket transport on an unconnected uvw::TcpHandle.
 *
 * Measured safe by E4.0a for HttpClient (see the harness header, point (b));
 * WebSocket adds only its own state machine reset() and a
 * websocketDisconnected connection (WebSocket.cpp:38-48), it touches no socket
 * at construction.
 *
 * The handler is installed into HttpClient::jsonApi and its sendData signal is
 * connected to a caller owned sink, mirroring WebSocket.cpp:276-348. The sink
 * MUST outlive the transport: after the transport dies the signal is gone with
 * it, so a message appended afterwards would mean the dead handler answered.
 */
class WsTransport: public WebSocket
{
public:
    WsTransport(const std::shared_ptr<uvw::TcpHandle> &h,
                std::vector<std::string> *sink):
        WebSocket(h),
        handle(h)
    {
        ProbeHandler *probe = new ProbeHandler(this);
        jsonApi = probe;            //HttpClient::jsonApi, deleted by ~HttpClient
        handler = probe;

        jsonApi->sendData.connect([sink](const std::string &data)
        {
            sink->push_back(data);
        });
    }

    //What WebSocket::processFrame() does with a complete text frame
    //(WebSocket.cpp:513).
    void feed(const Json &request) { jsonApi->processApi(request.dump(), Params()); }

    ProbeHandler *probeHandler() const { return handler; }

    std::shared_ptr<uvw::TcpHandle> handle;

private:
    ProbeHandler *handler = nullptr;
};

/* The production disconnection path: the whole transport is deleted, which
 * runs ~WebSocket() and then ~HttpClient(), and it is ~HttpClient() that does
 * `delete jsonApi` (HttpClient.cpp:162). ~HttpClient() never closes the uv
 * handle, so the test does it, then lets uv run the close callbacks.
 */
void destroyTransport(WsTransport *&transport)
{
    std::shared_ptr<uvw::TcpHandle> h = transport->handle;

    delete transport;
    transport = nullptr;

    if (h)
        h->close();
    pumpEventLoop(2);
}

/* An AudioPlayer whose playlist getters answer only when the test says so.
 * Same shape as the fake of JsonApiPlaylist_test.cpp (T3.17a), reduced to what
 * get_playlist needs: the connection object owns the callback and can fire it
 * long after the JsonApi - or here, after the whole transport - is gone.
 */
class FakePlaylistPlayer: public AudioPlayer
{
public:
    FakePlaylistPlayer(Params &p): AudioPlayer(p) {}

    bool deferred = false;
    int currentTrack = 0;
    int playlistSize = 0;

    //Indices get_playlist_item() was actually asked for, in order.
    std::vector<int> requestedItems;

    void get_playlist_current(AudioRequest_cb cb, AudioPlayerData = AudioPlayerData()) override
    {
        AudioPlayerData d;
        d.ivalue = currentTrack;
        answer(cb, d);
    }

    void get_playlist_size(AudioRequest_cb cb, AudioPlayerData = AudioPlayerData()) override
    {
        AudioPlayerData d;
        d.ivalue = playlistSize;
        answer(cb, d);
    }

    void get_playlist_item(int index, AudioRequest_cb cb, AudioPlayerData = AudioPlayerData()) override
    {
        requestedItems.push_back(index);
        answer(cb, itemData(index));
    }

    bool canPlaylist() override { return true; }

    static AudioPlayerData itemData(int index)
    {
        const std::string idx = Utils::to_string(index);
        AudioPlayerData d;
        d.params.Add("id", "track_" + idx);
        d.params.Add("title", "Title " + idx);
        d.params.Add("artist", "Artist " + idx);
        d.params.Add("album", "Album " + idx);
        d.params.Add("duration", Utils::to_string(100 + index));
        return d;
    }

    size_t pendingCount() const { return pending.size(); }

    bool fireNext()
    {
        if (pending.empty())
            return false;
        Pending p = pending.front();
        pending.pop_front();
        p.cb(p.data);
        return true;
    }

private:
    struct Pending
    {
        AudioRequest_cb cb;
        AudioPlayerData data;
    };
    std::deque<Pending> pending;

    void answer(const AudioRequest_cb &cb, const AudioPlayerData &d)
    {
        if (deferred)
            pending.push_back({ cb, d });
        else
            cb(d);
    }
};

//The expected items of a playlist of `count` tracks, built from the same
//source of truth the fake answers with.
Json expectedItems(int count)
{
    Json items = Json::array();
    for (int i = 0; i < count; i++)
    {
        const std::string idx = Utils::to_string(i);
        items.push_back(Json{
                            { "id", "track_" + idx },
                            { "title", "Title " + idx },
                            { "artist", "Artist " + idx },
                            { "album", "Album " + idx },
                            { "duration", Utils::to_string(100 + i) },
                        });
    }
    return items;
}

}

class JsonApiWsTransportTest: public JsonApiCharacterizationTest
{
protected:
    void SetUp() override
    {
        JsonApiCharacterizationTest::SetUp();

        forgetIOState(PLAYER_ID);
        handlerDestroyCount = 0;

        loadConfig();

        //Loading the config raises one EventIOAdded per IO. They stay in the
        //EventManager queue until something pumps the loop, and a case that
        //counts messages would otherwise trip over them. Drain them here,
        //while no transport is alive to receive them.
        pumpEventLoop();
    }

    static std::shared_ptr<uvw::TcpHandle> newHandle()
    {
        return uvw::Loop::getDefault()->resource<uvw::TcpHandle>();
    }

    static WsTransport *newTransport(std::vector<std::string> *sink)
    {
        return new WsTransport(newHandle(), sink);
    }

    static FakePlaylistPlayer *addPlayer(int size, int currentTrack = 0)
    {
        Params p;
        p.Add("id", PLAYER_ID);
        p.Add("name", "Fake playlist player");
        p.Add("type", "FakePlaylistPlayer");

        FakePlaylistPlayer *player = new FakePlaylistPlayer(p);
        player->playlistSize = size;
        player->currentTrack = currentTrack;

        firstRoom()->AddIO(player);
        ListeRoom::Instance().addIOHash(player);
        return player;
    }

    static Json getPlaylistRequest()
    {
        return Json{
            { "msg", "get_playlist" },
            { "msg_id", "1" },
            { "data", {{ "id", PLAYER_ID }} },
        };
    }

    //`data` member of the last message of a sink, or a null Json.
    static Json lastData(const std::vector<std::string> &sink)
    {
        if (sink.empty())
            return Json();

        const Json env = asJsonDocument(sink.back());
        if (!env.is_object())
            return Json();

        auto it = env.find("data");
        if (it == env.end())
            return Json();

        return *it;
    }
};

/*******************************************************************************
 * THE OWNERSHIP CHAIN
 *
 * The one fact every guard in JsonApi.cpp depends on: killing the websocket
 * transport kills the handler, exactly once, and with it the apiAlive token of
 * its JsonApi base sub-object.
 ******************************************************************************/

TEST_F(JsonApiWsTransportTest, TransportDeathDestroysTheHandlerOnce)
{
    std::vector<std::string> sent;
    WsTransport *transport = newTransport(&sent);

    ProbeHandler *handler = transport->probeHandler();
    ASSERT_TRUE(handler != nullptr);

    //The token is the one of the JsonApi base sub-object, and it is alive as
    //long as the transport is.
    std::weak_ptr<bool> token = handler->baseAliveToken();
    EXPECT_FALSE(token.expired());
    EXPECT_EQ(0, handlerDestroyCount);

    destroyTransport(transport);

    //Deleted by ~HttpClient(), the BASE of WebSocket - once, not twice.
    EXPECT_EQ(1, handlerDestroyCount);
    //...and that is what makes every `alive.expired()` in JsonApi.cpp true.
    EXPECT_TRUE(token.expired());
}

/* Two live transports have two independent tokens: killing one must not blind
 * the other. This is the counterpart of the case above - a token accidentally
 * made static or shared would pass it and fail here.
 */
TEST_F(JsonApiWsTransportTest, TwoTransportsHaveIndependentTokens)
{
    std::vector<std::string> sentA;
    std::vector<std::string> sentB;

    WsTransport *a = newTransport(&sentA);
    WsTransport *b = newTransport(&sentB);

    std::weak_ptr<bool> tokenA = a->probeHandler()->baseAliveToken();
    std::weak_ptr<bool> tokenB = b->probeHandler()->baseAliveToken();

    destroyTransport(a);

    EXPECT_TRUE(tokenA.expired());
    EXPECT_FALSE(tokenB.expired());
    EXPECT_EQ(1, handlerDestroyCount);

    destroyTransport(b);

    EXPECT_TRUE(tokenB.expired());
    EXPECT_EQ(2, handlerDestroyCount);
}

/*******************************************************************************
 * AN ANSWER IN FLIGHT WHEN THE TRANSPORT DIES
 *
 * get_playlist is guarded in JsonApi.cpp (T3.17a) with the INHERITED token.
 * These cases prove the guard fires when the death comes from the websocket
 * transport, not from a direct `delete handler`.
 ******************************************************************************/

TEST_F(JsonApiWsTransportTest, ClientGoneMidChainOnTheRealTransportIsIgnored)
{
    FakePlaylistPlayer *player = addPlayer(3, 0);
    player->deferred = true;

    std::vector<std::string> sent;
    WsTransport *transport = newTransport(&sent);
    transport->feed(getPlaylistRequest());

    ASSERT_EQ(1u, player->pendingCount());
    ASSERT_EQ(0u, sent.size());

    //The client disconnects: the WHOLE transport goes, and it is ~HttpClient()
    //that deletes the handler.
    destroyTransport(transport);

    //Drain everything the player connection still holds. Without the inherited
    //token this walks the recursion and ends on sendJson() of a freed
    //JsonApiHandlerWS - ASan reports a heap-use-after-free.
    int fired = 0;
    while (player->fireNext())
        fired++;

    EXPECT_EQ(1, fired);                        //the in-flight answer, and nothing chained
    EXPECT_TRUE(player->requestedItems.empty());
    EXPECT_EQ(0u, sent.size());                 //no message came out of a dead transport
}

/* Same, but the transport dies in the MIDDLE of the recursion. The guard has
 * to be on every stage, and every stage has to see the base token die with the
 * derived transport.
 */
TEST_F(JsonApiWsTransportTest, ClientGoneMidRecursionOnTheRealTransportIsIgnored)
{
    FakePlaylistPlayer *player = addPlayer(5, 0);
    player->deferred = true;

    std::vector<std::string> sent;
    WsTransport *transport = newTransport(&sent);
    transport->feed(getPlaylistRequest());

    ASSERT_TRUE(player->fireNext());        //current track
    ASSERT_TRUE(player->fireNext());        //playlist size -> item 0 in flight
    ASSERT_TRUE(player->fireNext());        //item 0        -> item 1 in flight
    ASSERT_EQ(std::vector<int>({ 0, 1 }), player->requestedItems);
    ASSERT_EQ(1u, player->pendingCount());
    ASSERT_EQ(0u, sent.size());

    destroyTransport(transport);

    int fired = 0;
    while (player->fireNext())
        fired++;

    EXPECT_EQ(1, fired);
    EXPECT_EQ(std::vector<int>({ 0, 1 }), player->requestedItems);
    EXPECT_EQ(0u, sent.size());
}

/*******************************************************************************
 * NO SWALLOWED ANSWER
 *
 * The invariant that costs more than the use-after-free if it breaks: a client
 * that stays connected gets its WHOLE answer. A guard checked against the
 * wrong object, or a token invalidated too early, would make this case fail
 * while all the ones above stay green.
 ******************************************************************************/

TEST_F(JsonApiWsTransportTest, LiveTransportGetsTheWholeAnswer)
{
    const int SIZE = 4;
    FakePlaylistPlayer *player = addPlayer(SIZE, 2);
    player->deferred = true;

    std::vector<std::string> sent;
    WsTransport *transport = newTransport(&sent);
    transport->feed(getPlaylistRequest());

    ASSERT_TRUE(player->fireNext());        //current track
    ASSERT_TRUE(player->fireNext());        //playlist size

    for (int i = 0; i < SIZE; i++)
    {
        EXPECT_EQ(0u, sent.size()) << "answered before the last track, at stage " << i;
        ASSERT_TRUE(player->fireNext());
    }

    ASSERT_EQ(1u, sent.size());

    std::vector<int> allIndices;
    for (int i = 0; i < SIZE; i++)
        allIndices.push_back(i);
    EXPECT_EQ(allIndices, player->requestedItems);

    const Json data = lastData(sent);
    ASSERT_TRUE(data.is_object());
    EXPECT_JSON_EQ(Json("2"), data.value("current_track", Json()));
    EXPECT_JSON_EQ(Json("4"), data.value("count", Json()));
    EXPECT_JSON_EQ(expectedItems(SIZE), data.value("items", Json()));

    destroyTransport(transport);
}

/*******************************************************************************
 * THE EVENT PUSH - the one asynchronous path that belongs to the websocket
 * handler alone, and the only one that is NOT protected by a token.
 *
 * JsonApiHandlerWS subscribes to EventManager::newEvent in its constructor
 * (JsonApiHandlerWS.cpp:37). Events are not delivered synchronously: they queue
 * into a libuv idler (EventManager.cpp:46-54), so a session can perfectly die
 * between the event being raised and the idler running - exactly the in-flight
 * situation the tokens exist for.
 *
 * It is safe for a different reason, and these two cases pin it: the connection
 * is dropped by ~JsonApiHandlerWS (evcon.disconnect(), :42) AND by sigc::trackable,
 * which JsonApi derives from. Both run as part of the `delete jsonApi` of
 * ~HttpClient. Adding a token here would be redundant.
 ******************************************************************************/

TEST_F(JsonApiWsTransportTest, LiveTransportReceivesEvents)
{
    std::vector<std::string> sent;
    WsTransport *transport = newTransport(&sent);

    EventManager::create(CalaosEvent::EventIOChanged,
                         Params({{ "id", ID_BOOL_IN }}), false);
    pumpEventLoop();

    ASSERT_EQ(1u, sent.size());
    const Json env = asJsonDocument(sent.back());
    ASSERT_TRUE(env.is_object());
    EXPECT_JSON_EQ(Json("event"), env.value("msg", Json()));

    destroyTransport(transport);
}

TEST_F(JsonApiWsTransportTest, EventRaisedBeforeTheTransportDiesIsNeverDelivered)
{
    std::vector<std::string> sent;
    WsTransport *transport = newTransport(&sent);

    //Raised, queued in the idler, NOT delivered yet.
    EventManager::create(CalaosEvent::EventIOChanged,
                         Params({{ "id", ID_BOOL_IN }}), false);
    ASSERT_EQ(0u, sent.size());

    //The client disconnects before the idler ever runs. destroyTransport()
    //pumps the loop, so the queued event is dispatched with the handler
    //already freed: without the disconnect this is a use-after-free on
    //JsonApiHandlerWS::handleEvents().
    destroyTransport(transport);
    pumpEventLoop();

    EXPECT_EQ(0u, sent.size());
}
