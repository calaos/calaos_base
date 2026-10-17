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

/* An upgrade that ships calaos-server and a RemoteUI firmware together: the
 * firmware postinst asks for a rescan, the server offers the image, the
 * panels start downloading, and the same transaction then restarts the
 * server under them. These cases measure the gate that keeps the offer back
 * while the server is not settled, and the deferred offer that reaches the
 * panels once it is, without them having to reconnect.
 */

#include "JsonApiCharacterization.h"
#include "IO/RemoteUI/RemoteUI.h"
#include "RemoteUIWebSocketHandler.h"
#include "RemoteUIManager.h"
#include "OtaFirmwareManager.h"
#include "HttpClient.h"
#include "ListeRoom.h"
#include "libuvw.h"
#include <chrono>
#include <fstream>
#include <sys/stat.h>
#include <thread>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

const char *const SCREEN = "otat_screen";
const char *const HW = "otat-board";
const char *const FW_VERSION = "2.0.0";
const char *const FW_SHA = "36ea3c1c8b147613e8b00b3f97b2d63814c459bb87dc3f1198afb29505da5103";
//Long enough for a loaded runner to make the checks inside a hold
const uint64_t STARTUP_HOLD_MS = 1000;
const uint64_t PACKAGE_HOLD_MS = 1500;

class PanelClient: public HttpClient
{
public:
    explicit PanelClient(const std::shared_ptr<uvw::TcpHandle> &h):
        HttpClient(h),
        handle(h)
    {
    }

    std::shared_ptr<uvw::TcpHandle> handle;
};

class PanelHandler: public RemoteUIWebSocketHandler
{
public:
    explicit PanelHandler(HttpClient *c):
        RemoteUIWebSocketHandler(c)
    {
    }

    void attach(RemoteUI *ui)
    {
        authenticated_remote_ui = ui;
        setAuthenticated(true);
    }
};

std::string house()
{
    std::string screen =
        "    <calaos:remote_ui type=\"RemoteUI\" id=\"" + std::string(SCREEN) +
        "\" name=\"Screen\" enabled=\"true\" visible=\"true\" device_type=\"" + HW + "\""
                                                                                     " grid_w=\"3\" grid_h=\"3\">\n"
                                                                                     "      <calaos:pages>\n        <calaos:page name=\"p1\">\n"
                                                                                     "        </calaos:page>\n      </calaos:pages>\n    </calaos:remote_ui>\n";
    return ioXmlDocument(roomXml("Salon", "salon", screen));
}

std::string firmwareDir()
{
    static std::string dir;
    if (!dir.empty())
        return dir;

    char tmpl[] = "/tmp/calaos_otatiming_XXXXXX";
    dir = ::mkdtemp(tmpl);
    const std::string board = dir + "/" + HW;
    ::mkdir(board.c_str(), 0700);
    std::ofstream(board + "/fw.bin") << "calaos-test-firmware";
    std::ofstream(board + "/manifest.json")
        << "{\"schema_version\": 1, \"hardware_id\": \"" << HW << "\", \"version\": \""
        << FW_VERSION << "\", \"firmware_file\": \"fw.bin\", \"checksum_sha256\": \"" << FW_SHA
        << "\", \"name\": \"Test panel\", \"release_notes\": \"notes\"}";
    return dir;
}

void pumpUntil(int ms, const std::function<bool()> &done)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end && !done())
    {
        pumpEventLoop(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

bool &offersAllowedRightAfterInit()
{
    static bool allowed = true;
    return allowed;
}

OtaFirmwareManager &ota()
{
    static bool initialised = false;
    if (!initialised)
    {
        Utils::set_config_option("ota_enabled", "true");
        Utils::set_config_option("ota_firmware_path", firmwareDir());
        OtaFirmwareManager::Instance().setHoldDelays(STARTUP_HOLD_MS, PACKAGE_HOLD_MS);
        OtaFirmwareManager::Instance().init();
        offersAllowedRightAfterInit() = OtaFirmwareManager::Instance().offersAllowed();
        initialised = true;
    }
    return OtaFirmwareManager::Instance();
}

} //namespace

TEST(OtaOfferGate, OpensWhenTheLongestHoldEnds)
{
    OtaOfferGate g;
    EXPECT_TRUE(g.offersAllowed(0));
    EXPECT_EQ(g.msUntilAllowed(0), 0u);

    g.holdFor(1000, 500);
    EXPECT_FALSE(g.offersAllowed(1499));
    EXPECT_EQ(g.msUntilAllowed(1200), 300u);
    EXPECT_TRUE(g.offersAllowed(1500));

    //A shorter hold never shortens a longer one
    g.holdFor(1000, 2000);
    g.holdFor(1100, 10);
    EXPECT_FALSE(g.offersAllowed(2999));
    EXPECT_TRUE(g.offersAllowed(3000));
    EXPECT_EQ(g.msUntilAllowed(5000), 0u);
}

class OtaOfferTimingTest: public JsonApiCharacterizationTest
{
protected:
    void SetUp() override
    {
        JsonApiCharacterizationTest::SetUp();
        loadConfig(house(), rulesXmlDocument(""));
        ota();

        screen = dynamic_cast<RemoteUI *>(ListeRoom::Instance().get_io(SCREEN));
        ASSERT_NE(screen, nullptr);
        screen->set_param("device_version", "1.0.0");

        auto tcp = uvw::Loop::getDefault()->resource<uvw::TcpHandle>();
        client = std::make_shared<PanelClient>(tcp);
        handler.reset(new PanelHandler(client.get()));
        handler->sendData.connect([this](const std::string &d)
                                  {
            Json env = Json::parse(d, nullptr, false);
            if (env.is_object() && env.value("msg", "") == "remote_ui_fw_update_available")
                offers.push_back(env.at("data"));
        });
        handler->attach(screen);
        RemoteUIManager::Instance().addWebSocketHandler(SCREEN, handler.get());
    }

    void TearDown() override
    {
        RemoteUIManager::Instance().removeWebSocketHandler(SCREEN, handler.get());
        handler.reset();
        if (client && client->handle)
            client->handle->close();
        client.reset();
        pumpEventLoop(2);
        JsonApiCharacterizationTest::TearDown();
    }

    //What the server does when the panel connects
    bool connectPanel()
    {
        return ota().checkDeviceForUpdate(handler.get(), HW, screen->get_param("device_version"));
    }

    RemoteUI *screen = nullptr;
    std::shared_ptr<PanelClient> client;
    std::unique_ptr<PanelHandler> handler;
    std::vector<Json> offers;
};

TEST_F(OtaOfferTimingTest, AFreshlyStartedServerDoesNotOfferAtOnce)
{
    //An upgrade restarts the server: a panel reconnecting at once must not
    //start a download the server may not be there to finish
    EXPECT_FALSE(offersAllowedRightAfterInit());
    ota().serverStarted();
    EXPECT_FALSE(connectPanel());
    EXPECT_TRUE(offers.empty());
}

TEST_F(OtaOfferTimingTest, AFreshlyStartedServerHoldsTheOfferThenSendsItUnasked)
{
    ota().serverStarted();
    ASSERT_FALSE(ota().offersAllowed());

    EXPECT_FALSE(connectPanel());
    EXPECT_TRUE(offers.empty());
    EXPECT_TRUE(ota().deferredNotifyPending());

    //No reconnection: the deferred offer reaches the connected panel
    pumpUntil(10000, [this]()
              {
        return !offers.empty();
    });
    ASSERT_EQ(offers.size(), 1u);
    EXPECT_EQ(offers[0].value("version", ""), FW_VERSION);
    EXPECT_EQ(offers[0].value("checksum_sha256", ""), FW_SHA);
    EXPECT_EQ(offers[0].value("download_url", ""), std::string("/api/v3/ota/firmware/") + HW + "/download");
    EXPECT_EQ(offers[0].value("name", ""), "Test panel");
    EXPECT_EQ(offers[0].value("release_notes", ""), "notes");
    EXPECT_FALSE(ota().deferredNotifyPending());
}

TEST_F(OtaOfferTimingTest, AFirmwarePackageHoldsTheOfferUntilTheTransactionSettles)
{
    pumpUntil(10000, [this]()
              {
        return ota().offersAllowed();
    });
    ASSERT_TRUE(ota().offersAllowed());

    const auto before = std::chrono::steady_clock::now();
    ota().rescanFromPackage();
    EXPECT_FALSE(ota().offersAllowed());
    //Served at once, offered later
    EXPECT_NE(ota().getFirmware(HW), nullptr);
    EXPECT_FALSE(connectPanel());

    pumpUntil(10000, [this]()
              {
        return !offers.empty();
    });
    const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - before)
                            .count();
    ASSERT_EQ(offers.size(), 1u);
    EXPECT_GE(waited, static_cast<long>(PACKAGE_HOLD_MS));
}

TEST_F(OtaOfferTimingTest, ASecondPackageMovesTheOfferLater)
{
    pumpUntil(10000, [this]()
              {
        return ota().offersAllowed();
    });
    const auto first = std::chrono::steady_clock::now();
    ota().rescanFromPackage();
    EXPECT_FALSE(connectPanel());
    pumpUntil(1000, []()
              {
        return false;
    });
    ota().rescanFromPackage();

    //The first hold has ended, the second one holds
    pumpUntil(10000, [first]()
              {
        return std::chrono::steady_clock::now() - first >=
               std::chrono::milliseconds(PACKAGE_HOLD_MS + 200);
    });
    EXPECT_TRUE(offers.empty());
    EXPECT_FALSE(ota().offersAllowed());

    pumpUntil(10000, [this]()
              {
        return !offers.empty();
    });
    EXPECT_EQ(offers.size(), 1u);
}

TEST_F(OtaOfferTimingTest, ASettledServerOffersAtOnceAndNotToAnUpToDatePanel)
{
    pumpUntil(10000, [this]()
              {
        return ota().offersAllowed();
    });
    ASSERT_TRUE(ota().offersAllowed());

    EXPECT_TRUE(connectPanel());
    EXPECT_EQ(offers.size(), 1u);

    screen->set_param("device_version", FW_VERSION);
    offers.clear();
    EXPECT_FALSE(connectPanel());
    EXPECT_FALSE(ota().deferredNotifyPending());
    EXPECT_TRUE(ota().buildOffer(HW, FW_VERSION).is_null());
    EXPECT_TRUE(ota().buildOffer("", "1.0.0").is_null());
    EXPECT_TRUE(ota().buildOffer("unknown-board", "1.0.0").is_null());
    EXPECT_FALSE(ota().checkDeviceForUpdate(nullptr, HW, "1.0.0"));
    EXPECT_TRUE(offers.empty());
}
