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

//The request body NotifManager posts to push.calaos.fr (gorush). The apps in
//the stores parse what gorush builds from it, so the legacy entries are pinned
//byte for byte.

#include <gtest/gtest.h>

#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>

#include "HistLogger.h"
#include "NotifManager.h"
#include "libuvw.h"

using namespace Calaos;

namespace
{

PushToken token(const char *t, int hw)
{
    return PushToken(string(t), hw);
}

const char *IOS_ENTRY =
    R"({"message":"Door open","platform":1,"tokens":["ios-1"],"topic":"fr.calaos.CalaosMobile"})";

const char *IOS_PIC_ENTRY =
    R"({"data":{"event_uuid":"ev-42"},"message":"Door open","mutable-content":true,)"
    R"("mutable_content":true,"platform":1,"tokens":["ios-1"],"topic":"fr.calaos.CalaosMobile"})";

const char *ANDROID_ENTRY =
    R"({"data":{"body":"Door open","message":"Door open","title":"Calaos"},"message":"Door open",)"
    R"("notification":{"body":"Door open","sound":"default","title":"Calaos"},"platform":2,)"
    R"("priority":"high","sound":"default","title":"Calaos","tokens":["and-1","and-2"]})";

const char *ANDROID_PIC_ENTRY =
    R"({"data":{"body":"Door open","event_uuid":"ev-42","message":"Door open","title":"Calaos"},)"
    R"("message":"Door open","notification":{"body":"Door open","sound":"default","title":"Calaos"},)"
    R"("platform":2,"priority":"high","sound":"default","title":"Calaos","tokens":["and-1","and-2"]})";

string body(const std::vector<string> &entries)
{
    string s = R"({"notifications":[)";
    for (size_t i = 0; i < entries.size(); i++)
        s += (i ? "," : "") + entries[i];
    return s + "]}";
}

} //namespace

TEST(PushPayloadTest, NoTokenGivesNoBody)
{
    EXPECT_EQ("", NotifManager::buildPushBody({}, "Door open", "", false));
    EXPECT_EQ("", NotifManager::buildPushBody({ token("x", HistLogger::PUSH_HW_NONE) },
                                              "Door open", "", false));
}

TEST(PushPayloadTest, LegacyAndroidBodyIsPinned)
{
    EXPECT_EQ(body({ ANDROID_ENTRY }),
              NotifManager::buildPushBody({ token("and-1", HistLogger::PUSH_HW_ANDROID),
                                            token("and-2", HistLogger::PUSH_HW_ANDROID) },
                                          "Door open", "", false));
}

TEST(PushPayloadTest, LegacyAndroidBodyWithPictureIsPinned)
{
    EXPECT_EQ(body({ ANDROID_PIC_ENTRY }),
              NotifManager::buildPushBody({ token("and-1", HistLogger::PUSH_HW_ANDROID),
                                            token("and-2", HistLogger::PUSH_HW_ANDROID) },
                                          "Door open", "ev-42", false));
}

TEST(PushPayloadTest, IosBodyIsPinned)
{
    EXPECT_EQ(body({ IOS_ENTRY }),
              NotifManager::buildPushBody({ token("ios-1", HistLogger::PUSH_HW_IOS) },
                                          "Door open", "", false));
    EXPECT_EQ(body({ IOS_PIC_ENTRY }),
              NotifManager::buildPushBody({ token("ios-1", HistLogger::PUSH_HW_IOS) },
                                          "Door open", "ev-42", false));
}

TEST(PushPayloadTest, IosDevelopmentFlag)
{
    EXPECT_EQ(body({ R"({"development":true,"message":"Door open","platform":1,"tokens":["ios-1"],)"
                     R"("topic":"fr.calaos.CalaosMobile"})" }),
              NotifManager::buildPushBody({ token("ios-1", HistLogger::PUSH_HW_IOS) },
                                          "Door open", "", true));
}

TEST(PushPayloadTest, IosEntryComesBeforeAndroid)
{
    EXPECT_EQ(body({ IOS_PIC_ENTRY, ANDROID_PIC_ENTRY }),
              NotifManager::buildPushBody({ token("and-1", HistLogger::PUSH_HW_ANDROID),
                                            token("ios-1", HistLogger::PUSH_HW_IOS),
                                            token("and-2", HistLogger::PUSH_HW_ANDROID) },
                                          "Door open", "ev-42", false));
}

TEST(PushPayloadTest, EmptyMessageFallsBackToDefault)
{
    EXPECT_EQ(body({ R"({"message":"Calaos Notification","platform":1,"tokens":["ios-1"],)"
                     R"("topic":"fr.calaos.CalaosMobile"})" }),
              NotifManager::buildPushBody({ token("ios-1", HistLogger::PUSH_HW_IOS) },
                                          "", "", false));
}

TEST(PushPayloadTest, MessageIsAsciiEscapedAndInvalidUtf8Replaced)
{
    EXPECT_EQ(body({ "{\"message\":\"\\u00e9t\\ufffd\",\"platform\":1,\"tokens\":[\"ios-1\"],"
                     R"("topic":"fr.calaos.CalaosMobile"})" }),
              NotifManager::buildPushBody({ token("ios-1", HistLogger::PUSH_HW_IOS) },
                                          "\xc3\xa9t\xff", "", false));
}

namespace
{

PushToken dataToken(const char *t)
{
    return PushToken(string(t), HistLogger::PUSH_HW_ANDROID, HistLogger::PUSH_FORMAT_DATA);
}

const char *ANDROID_DATA_ENTRY =
    R"({"android":{"priority":"high"},"data":{"body":"Door open","message":"Door open","title":"Calaos"},)"
    R"("platform":2,"tokens":["data-1"]})";

const char *ANDROID_DATA_PIC_ENTRY =
    R"({"android":{"priority":"high"},"data":{"body":"Door open","event_uuid":"ev-42","message":"Door open",)"
    R"("title":"Calaos"},"platform":2,"tokens":["data-1"]})";

} //namespace

TEST(PushPayloadTest, DataTokenGetsADataOnlyEntry)
{
    EXPECT_EQ(body({ ANDROID_DATA_ENTRY }),
              NotifManager::buildPushBody({ dataToken("data-1") }, "Door open", "", false));
    EXPECT_EQ(body({ ANDROID_DATA_PIC_ENTRY }),
              NotifManager::buildPushBody({ dataToken("data-1") }, "Door open", "ev-42", false));
}

TEST(PushPayloadTest, IosIgnoresTheDataFormat)
{
    EXPECT_EQ(body({ IOS_PIC_ENTRY }),
              NotifManager::buildPushBody({ PushToken(string("ios-1"), HistLogger::PUSH_HW_IOS,
                                                      HistLogger::PUSH_FORMAT_DATA) },
                                          "Door open", "ev-42", false));
}

TEST(PushPayloadTest, MixedTokensGetOneEntryPerFormat)
{
    EXPECT_EQ(body({ IOS_PIC_ENTRY, ANDROID_PIC_ENTRY, ANDROID_DATA_PIC_ENTRY }),
              NotifManager::buildPushBody({ dataToken("data-1"),
                                            token("and-1", HistLogger::PUSH_HW_ANDROID),
                                            token("ios-1", HistLogger::PUSH_HW_IOS),
                                            token("and-2", HistLogger::PUSH_HW_ANDROID) },
                                          "Door open", "ev-42", false));
}

namespace
{

//sendPushNotification() reads the tokens through the HistLogger singleton: an
//empty database of its own, so nothing is ever posted from here.
class EmptyHistory: public ::testing::Environment
{
public:
    void SetUp() override
    {
        char tmpl[] = "/tmp/calaos_push1_payload_XXXXXX";
        ASSERT_NE(nullptr, ::mkdtemp(tmpl));
        base = tmpl;
        const std::string cfg = base + "/config";
        const std::string cache = base + "/cache";
        ASSERT_EQ(0, ::mkdir(cfg.c_str(), 0700));
        ASSERT_EQ(0, ::mkdir(cache.c_str(), 0700));
        Utils::initConfigOptions(const_cast<char *>(cfg.c_str()),
                                 const_cast<char *>(cache.c_str()), true);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(base, ec);
    }

private:
    std::string base;
};

const auto *env = ::testing::AddGlobalTestEnvironment(new EmptyHistory);

} //namespace

TEST(PushPayloadTest, SendWithoutAnyTokenPostsNothing)
{
    auto sent = std::make_shared<bool>(false);
    NotifManager::Instance().sendPushNotification("Door open", "", [sent]() { *sent = true; });

    //The worker answers in order: once this one is back, the send has its tokens.
    auto done = std::make_shared<bool>(false);
    HistLogger::Instance().getPushTokens([done](bool, string, const vector<PushToken> &) { *done = true; });
    auto loop = uvw::Loop::getDefault();
    for (int i = 0; i < 400 && !*done; i++)
    {
        loop->run<uvw::Loop::Mode::NOWAIT>();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    for (int i = 0; i < 10; i++)
        loop->run<uvw::Loop::Mode::NOWAIT>();

    ASSERT_TRUE(*done);
    EXPECT_FALSE(*sent);
}
