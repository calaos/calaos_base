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

namespace
{

const char *const SCREEN = "otat_screen";
const char *const HW = "otat-board";
const char *const FW_VERSION = "2.0.0";
const char *const FW_SHA = "36ea3c1c8b147613e8b00b3f97b2d63814c459bb87dc3f1198afb29505da5103";

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
        << "\"}";
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

OtaFirmwareManager &ota()
{
    static bool initialised = false;
    if (!initialised)
    {
        Utils::set_config_option("ota_enabled", "true");
        Utils::set_config_option("ota_firmware_path", firmwareDir());
        OtaFirmwareManager::Instance().init();
        initialised = true;
    }
    return OtaFirmwareManager::Instance();
}

} //namespace

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
    EXPECT_FALSE(connectPanel());
    EXPECT_TRUE(offers.empty());
}
