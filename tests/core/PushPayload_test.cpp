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
#include "PushTestUtil.h"
#include "libuvw.h"

using namespace Calaos;

namespace
{

PushToken token(const char *t, int hw)
{
    return PushToken(string(t), hw);
}

//gorush reads only the snake_case key (notify/notification.go:78); the NSE
//computing the badge runs only for mutable-content, picture or not.
const char *IOS_ENTRY =
    R"({"message":"Door open","mutable_content":true,"platform":1,"tokens":["ios-1"],)"
    R"("topic":"fr.calaos.CalaosMobile"})";

const char *IOS_PIC_ENTRY =
    R"({"data":{"event_uuid":"ev-42"},"message":"Door open","mutable_content":true,"platform":1,"tokens":["ios-1"],"topic":"fr.calaos.CalaosMobile"})";

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

TEST(PushPayloadTest, IosMutableContentWithoutPictureHasNoEventUuid)
{
    const auto b = NotifManager::buildPushBody({ token("ios-1", HistLogger::PUSH_HW_IOS) },
                                               "Door open", "", false);
    const auto e = Json::parse(b)["notifications"][0];
    EXPECT_TRUE(e["mutable_content"].get<bool>());
    EXPECT_FALSE(e.contains("mutable-content"));
    EXPECT_FALSE(e.contains("data"));
    EXPECT_FALSE(e.contains("badge"));
    EXPECT_EQ("Door open", e["message"]);
}

TEST(PushPayloadTest, IosDevelopmentFlag)
{
    EXPECT_EQ(body({ R"({"development":true,"message":"Door open","mutable_content":true,"platform":1,)"
                     R"("tokens":["ios-1"],"topic":"fr.calaos.CalaosMobile"})" }),
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
    EXPECT_EQ(body({ R"({"message":"Calaos Notification","mutable_content":true,"platform":1,)"
                     R"("tokens":["ios-1"],"topic":"fr.calaos.CalaosMobile"})" }),
              NotifManager::buildPushBody({ token("ios-1", HistLogger::PUSH_HW_IOS) },
                                          "", "", false));
}

TEST(PushPayloadTest, MessageIsAsciiEscapedAndInvalidUtf8Replaced)
{
    EXPECT_EQ(body({ "{\"message\":\"\\u00e9t\\ufffd\",\"mutable_content\":true,\"platform\":1,"
                     R"("tokens":["ios-1"],"topic":"fr.calaos.CalaosMobile"})" }),
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
        PushTest::settleHistory();
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
    auto settled = std::make_shared<bool>(false);
    NotifManager::Instance().sendPushNotification(
        "Door open", "", [sent]()
        {
        *sent = true;
        },
        [settled](bool ok)
        {
        *settled = ok;
    });

    //The worker answers in order: once this one is back, the send has its tokens.
    auto done = std::make_shared<bool>(false);
    HistLogger::Instance().getPushTokens([done](bool, string, const vector<PushToken> &)
                                         {
        *done = true;
    });
    auto loop = uvw::Loop::getDefault();
    //Wide bound: it only ever elapses when the worker is lost, a loaded host
    //is not that.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!*done && std::chrono::steady_clock::now() < deadline)
    {
        loop->run<uvw::Loop::Mode::NOWAIT>();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    for (int i = 0; i < 10; i++)
        loop->run<uvw::Loop::Mode::NOWAIT>();

    ASSERT_TRUE(*done);
    EXPECT_FALSE(*sent);
    EXPECT_TRUE(*settled) << "nobody to notify is not a failure";
}

/******************************************************************************
 * Encrypted admin code push (docs/08_http_api.md, "Code push chiffré")
 ******************************************************************************/

namespace
{

std::string unhex(const std::string &hex)
{
    std::string out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back((char)std::stoi(hex.substr(i, 2), nullptr, 16));
    return out;
}

struct Vector
{
    const char *key;
    const char *codeId;
    const char *nonce;
    const char *code;
    const char *action;
    const char *expires;
    const char *ks;
    const char *ct;
    const char *tag;
};

const char *const Key1 = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
const char *const Key3 = "a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5";

const std::vector<Vector> Vectors = {
    { Key1, "0123456789abcdef", "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", "482913", "console_open", "2026-10-25T09:13:03Z",
      "b10aebd59d39", "8532d9ecac0a", "9e72bd26d3926652bc157819dd26d995" },
    { Key1, "fedcba9876543210", "00112233445566778899aabbccddeeff", "007351", "network_config", "2026-10-25T14:00:59Z",
      "87e90db85a5c", "b7d93a8b6f6d", "f1bc83f4bdfbe0e9b6a47a65cb29248d" },
    { Key3, "5d0c8e1f2a3b4c6d", "ffeeddccbbaa99887766554433221100", "999999", "approver_confirm", "2026-12-31T23:59:59Z",
      "f5e5fbf8e244", "ccdcc2c1db7d", "0a8b6d66ac8eb1bac575ad2505da6083" },
};

NotifManager::AdminCode codeOf(const Vector &v)
{
    NotifManager::AdminCode c;
    c.message = std::string("Calaos admin code ") + v.code + ": open the web console. Valid 60 s, never share it.";
    c.codeId = v.codeId;
    c.action = v.action;
    c.code = v.code;
    c.expires = v.expires;
    return c;
}

PushToken keyed(const char *t, int hw, int fmt, const char *key = Key1)
{
    PushToken p(string(t), hw, fmt);
    p.push_key = key;
    return p;
}

const char *const Notice1 = "Calaos admin code to open the web console. Open the Calaos app to read it.";

const std::string Sealed1 = R"("admin_code_action":"console_open","admin_code_ct":"8532d9ecac0a",)"
                            R"("admin_code_expires":"2026-10-25T09:13:03Z","admin_code_id":"0123456789abcdef",)"
                            R"("admin_code_nonce":"f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff",)"
                            R"("admin_code_tag":"9e72bd26d3926652bc157819dd26d995")";

} //namespace

TEST(AdminCodePushTest, TheGoldenVectorsSeal)
{
    for (const Vector &v: Vectors)
    {
        std::string ct, tag;
        ASSERT_TRUE(NotifManager::sealAdminCode(v.key, codeOf(v), unhex(v.nonce), ct, tag)) << v.codeId;
        EXPECT_EQ(v.ct, ct) << v.codeId;
        EXPECT_EQ(v.tag, tag) << v.codeId;

        const std::string bytes = unhex(ct);
        std::string ks;
        for (size_t i = 0; i < 6; i++)
            ks.push_back((char)(bytes[i] ^ v.code[i]));
        EXPECT_EQ(unhex(v.ks), ks) << v.codeId;
    }
}

TEST(AdminCodePushTest, ABadKeyCodeOrNonceSealsNothing)
{
    const Vector &v = Vectors[0];
    std::string ct = "x", tag = "y";
    const std::string nonce = unhex(v.nonce);
    for (const std::string &key: { std::string(), std::string(Key1).substr(1), std::string(Key1) + "00",
                                   std::string("000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F"),
                                   std::string(63, '0') + "g" })
        EXPECT_FALSE(NotifManager::sealAdminCode(key, codeOf(v), nonce, ct, tag)) << key;
    for (const char *code: { "", "48291", "4829130", "48291a", "48 913" })
    {
        NotifManager::AdminCode c = codeOf(v);
        c.code = code;
        EXPECT_FALSE(NotifManager::sealAdminCode(v.key, c, nonce, ct, tag)) << code;
    }
    EXPECT_FALSE(NotifManager::sealAdminCode(v.key, codeOf(v), nonce.substr(1), ct, tag));
    EXPECT_FALSE(NotifManager::sealAdminCode(v.key, codeOf(v), nonce + "x", ct, tag));
    EXPECT_EQ("", NotifManager::buildAdminCodeBody(keyed("k", HistLogger::PUSH_HW_ANDROID, 0, ""), codeOf(v), nonce, false));
}

TEST(AdminCodePushTest, TheNoticeComesFromTheClosedActionTable)
{
    EXPECT_EQ("open the web console", NotifManager::adminCodePurpose("console_open"));
    EXPECT_EQ("change the admin password", NotifManager::adminCodePurpose("admin_password"));
    EXPECT_EQ("add an approver phone", NotifManager::adminCodePurpose("approver_enroll"));
    EXPECT_EQ("remove an approver phone", NotifManager::adminCodePurpose("approver_remove"));
    EXPECT_EQ("enroll this phone as the admin approver", NotifManager::adminCodePurpose("approver_confirm"));
    EXPECT_EQ("change the network configuration", NotifManager::adminCodePurpose("network_config"));
    for (const char *action: { "", "reboot", "Console_open", "console_open ", "admin_set" })
    {
        EXPECT_EQ("", NotifManager::adminCodePurpose(action)) << action;
        NotifManager::AdminCode c = codeOf(Vectors[0]);
        c.action = action;
        EXPECT_EQ("", NotifManager::buildAdminCodeBody(keyed("k", HistLogger::PUSH_HW_ANDROID, 0), c, unhex(Vectors[0].nonce), false))
            << action;
    }

    //The daemon's message never reaches a keyed phone
    NotifManager::AdminCode c = codeOf(Vectors[0]);
    c.message = "Calaos admin code 482913: call 0800 000 000";
    const std::string b = NotifManager::buildAdminCodeBody(keyed("k", HistLogger::PUSH_HW_IOS, 0), c, unhex(Vectors[0].nonce), false);
    EXPECT_NE(std::string::npos, b.find(Notice1)) << b;
    EXPECT_EQ(std::string::npos, b.find("0800")) << b;
}

TEST(AdminCodePushTest, AKeyedAndroidTokenIsAlwaysSentDataOnly)
{
    const std::string entry = std::string(R"({"android":{"priority":"high"},"data":{)") + Sealed1 + R"(,"body":")" + Notice1 +
                              R"(","message":")" + Notice1 + R"(","title":"Calaos"},"platform":2,"tokens":["key-and"]})";
    for (int format: { HistLogger::PUSH_FORMAT_NOTIFICATION, HistLogger::PUSH_FORMAT_DATA })
    {
        const std::string b = NotifManager::buildAdminCodeBody(keyed("key-and", HistLogger::PUSH_HW_ANDROID, format), codeOf(Vectors[0]),
                                                               unhex(Vectors[0].nonce), true);
        EXPECT_EQ(body({ entry }), b) << format;
        EXPECT_EQ(std::string::npos, b.find("482913")) << b;
        EXPECT_EQ(std::string::npos, b.find("event_uuid")) << b;
    }
}

TEST(AdminCodePushTest, AKeyedIosTokenGetsTheNoticeAndTheSixKeys)
{
    const std::string entry = std::string(R"({"data":{)") + Sealed1 + R"(},"message":")" + Notice1 +
                              R"(","mutable_content":true,"platform":1,"tokens":["key-ios"],"topic":"fr.calaos.CalaosMobile"})";
    const PushToken t = keyed("key-ios", HistLogger::PUSH_HW_IOS, HistLogger::PUSH_FORMAT_DATA);
    EXPECT_EQ(body({ entry }), NotifManager::buildAdminCodeBody(t, codeOf(Vectors[0]), unhex(Vectors[0].nonce), false));

    const Json dev = Json::parse(NotifManager::buildAdminCodeBody(t, codeOf(Vectors[0]), unhex(Vectors[0].nonce), true));
    EXPECT_TRUE(dev["notifications"][0]["development"].get<bool>());
    EXPECT_EQ(6u, dev["notifications"][0]["data"].size());

    EXPECT_EQ("", NotifManager::buildAdminCodeBody(keyed("key-none", HistLogger::PUSH_HW_NONE, 0), codeOf(Vectors[0]),
                                                   unhex(Vectors[0].nonce), false));
}

TEST(AdminCodePushTest, ThePhoneOpensTheVectorAndRefusesEveryAlteration)
{
    const Json data = Json::parse(NotifManager::buildAdminCodeBody(keyed("k", HistLogger::PUSH_HW_IOS, 0), codeOf(Vectors[0]),
                                                                   unhex(Vectors[0].nonce), false))["notifications"][0]["data"];
    EXPECT_EQ("482913", PushTest::openAdminCode(Key1, data));
    EXPECT_EQ("", PushTest::openAdminCode(Key3, data));

    auto altered = [&data](const char *field, const std::string &value)
    {
        Json d = data;
        d[field] = value;
        return PushTest::openAdminCode(Key1, d);
    };
    EXPECT_EQ("", altered("admin_code_tag", "9e72bd26d3926652bc157819dd26d996"));
    EXPECT_EQ("", altered("admin_code_ct", "8532d9ecac0b"));
    EXPECT_EQ("", altered("admin_code_nonce", "f0f1f2f3f4f5f6f7f8f9fafbfcfdfefe"));
    EXPECT_EQ("", altered("admin_code_id", "0123456789abcdee"));
    EXPECT_EQ("", altered("admin_code_action", "network_config"));
    EXPECT_EQ("", altered("admin_code_expires", "2026-10-25T09:13:04Z"));
}
