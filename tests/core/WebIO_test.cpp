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

/*
 * Tests for the thin Web IO subclasses (T3.2d).
 *
 * The seven Web IO types share their constructor skeleton through the
 * WebIOBase/WebInputBase mixins of WebDocBase.h. What must not change:
 *
 *  - every type stays registered in IOFactory under its XML name and is
 *    instantiable from an io.xml document,
 *  - the ioDoc parameters coming from WebDocBase are preserved per type:
 *    url/file_type/path for the "GET" flavor, url/data/data_type for the
 *    "POST" flavor, both for WebOutputString, plus raw_value for
 *    WebOutputLightRGB (--gendoc content),
 *  - a local file:// url still feeds the inputs synchronously at creation
 *    time (WebCtrl::Add fires the downloaded-callback in line for local
 *    files - the reason why the polling registration must stay the last
 *    statement of the concrete constructors, see WebDocBase.h),
 *  - the create/delete cycle unregisters the WebCtrl callback (no dangling
 *    callback into a deleted IO).
 */

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "CalaosCoreFixture.h"
#include "IOBase.h"
#include "IODoc.h"
#include "libuvw.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

/* E4.1k: genDocJson() answers a nlohmann Json instead of a json_t*. The walk
 * below is the same walk, name for name: value() keeps the tolerance of the
 * jansson version (a missing "parameters" gave a NULL that json_array_foreach
 * simply skipped, it never threw), and only string "name" fields are kept, as
 * before. No assertion of this file moved. */
std::set<std::string> docParamNames(IOBase *io)
{
    std::set<std::string> names;

    Json doc = io->getDoc()->genDocJson();
    for (const Json &val: doc.value("parameters", Json::array()))
    {
        if (val.is_object() && val.contains("name") && val.at("name").is_string())
            names.insert(val.at("name").get<std::string>());
    }

    return names;
}

std::string webIoXml(const std::string &elem, const std::string &type,
                     const std::string &id, const std::string &extraAttributes)
{
    return "    <calaos:" + elem + " type=\"" + type + "\" id=\"" + id +
           "\" name=\"" + id + "\" enabled=\"true\" visible=\"true\" " +
           extraAttributes + " />\n";
}

/* HTTP on the loopback: every request gets the current answer, then the
 * connection closes. */
class AnswerServer
{
public:
    AnswerServer()
    {
        listenFd = socket(AF_INET, SOCK_STREAM, 0);
        int on = 1;
        setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(listenFd, (struct sockaddr *)&addr, sizeof(addr)) == 0 && listen(listenFd, 4) == 0)
        {
            socklen_t len = sizeof(addr);
            if (getsockname(listenFd, (struct sockaddr *)&addr, &len) == 0)
                port = ntohs(addr.sin_port);
        }
        th = std::thread([this]()
                         {
            run();
        });
    }

    ~AnswerServer()
    {
        stop = true;
        th.join();
        close(listenFd);
    }

    void answer(const std::string &status, const std::string &body)
    {
        std::lock_guard<std::mutex> l(mutex);
        statusLine = status;
        content = body;
    }

    std::string url() const { return "http://127.0.0.1:" + std::to_string(port) + "/doc.json"; }

    std::atomic<int> served{ 0 };
    int port = 0;

private:
    void run()
    {
        while (!stop)
        {
            struct pollfd p = { listenFd, POLLIN, 0 };
            if (poll(&p, 1, 20) <= 0)
                continue;
            const int fd = accept(listenFd, nullptr, nullptr);
            if (fd < 0)
                continue;
            char buf[2048];
            struct pollfd r = { fd, POLLIN, 0 };
            if (poll(&r, 1, 500) > 0)
                (void)recv(fd, buf, sizeof(buf), 0);
            std::string reply;
            {
                std::lock_guard<std::mutex> l(mutex);
                reply = "HTTP/1.1 " + statusLine + "\r\nContent-Type: application/json\r\n" +
                        "Content-Length: " + std::to_string(content.size()) +
                        "\r\nConnection: close\r\n\r\n" + content;
            }
            (void)send(fd, reply.data(), reply.size(), MSG_NOSIGNAL);
            close(fd);
            served++;
        }
    }

    int listenFd = -1;
    std::atomic<bool> stop{ false };
    std::thread th;
    std::mutex mutex;
    std::string statusLine = "200 OK";
    std::string content;
};

bool pumpUntil(const std::function<bool()> &done, int ms = 5000)
{
    auto loop = uvw::Loop::getDefault();
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!done())
    {
        if (std::chrono::steady_clock::now() > end)
            return false;
        loop->run<uvw::Loop::Mode::NOWAIT>();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

} // namespace

class WebIOTest: public CoreFixture {};

/* All seven Web types instantiate from an io.xml document, and the inputs
 * read their value from a local json file synchronously at creation. */
TEST_F(WebIOTest, AllWebTypesInstantiateFromIoXml)
{
    const std::string jsonFile = cacheDir() + "/webio_doc.json";
    {
        std::ofstream f(jsonFile);
        f << "{\"temp\": 21.5, \"msg\": \"hello\", \"num\": 42.5}";
    }

    const std::string url = "file://" + jsonFile;
    const std::string getAttrs =
        "url=\"" + url + "\" file_type=\"json\" period=\"10\" ";

    const std::string ios =
        webIoXml("input", "WebInputAnalog", "web_ana", getAttrs + "path=\"num\"") +
        webIoXml("input", "WebInputTemp", "web_temp", getAttrs + "path=\"temp\"") +
        webIoXml("input", "WebInputString", "web_str", getAttrs + "path=\"msg\"") +
        webIoXml("output", "WebOutputAnalog", "web_oana", getAttrs + "path=\"num\"") +
        webIoXml("output", "WebOutputLight", "web_light", "url=\"" + url + "\"") +
        webIoXml("output", "WebOutputLightRGB", "web_rgb", "url=\"" + url + "\"") +
        webIoXml("output", "WebOutputString", "web_ostr", getAttrs + "path=\"msg\"");

    loadConfig(ioXmlDocument(roomXml(ROOM_NAME, ROOM_TYPE, ios)),
               rulesXmlDocument(""));

    const char *const ids[] = {"web_ana", "web_temp", "web_str", "web_oana",
                               "web_light", "web_rgb", "web_ostr"};
    for (const char *id: ids)
        EXPECT_NE(io(id), nullptr) << id << " was not instantiated";

    //The local file url makes WebCtrl fire the download callback in line
    //during the constructor, so the values are already there.
    ASSERT_NE(io("web_ana"), nullptr);
    EXPECT_DOUBLE_EQ(io("web_ana")->get_value_double(), 42.5);
    ASSERT_NE(io("web_temp"), nullptr);
    EXPECT_DOUBLE_EQ(io("web_temp")->get_value_double(), 21.5);
    //Read again on each period, rewritten or not.
    EXPECT_FALSE(io("web_temp")->datesEveryReading());
    ASSERT_NE(io("web_str"), nullptr);
    EXPECT_EQ(io("web_str")->get_value_string(), "hello");

    //Delete cycle: the destructor must unregister the WebCtrl callback.
    EXPECT_TRUE(deleteIO(io("web_ana")));
    EXPECT_EQ(io("web_ana"), nullptr);
}

/* The WebDocBase ioDoc parameters are preserved per type (--gendoc). */
TEST_F(WebIOTest, IoDocParametersPreserved)
{
    loadConfig();
    Room *room = firstRoom();
    ASSERT_NE(room, nullptr);

    auto make = [&](const char *type, const char *id) -> IOBase *
    {
        return createIO({{ "type", type }, { "id", id }, { "name", id }}, room);
    };

    //GET flavor: url/file_type/path, no POST params
    for (const char *type: {"WebInputAnalog", "WebInputTemp", "WebInputString"})
    {
        IOBase *in = make(type, (std::string("doc_") + type).c_str());
        ASSERT_NE(in, nullptr) << type;
        auto n = docParamNames(in);
        EXPECT_TRUE(n.count("url")) << type;
        EXPECT_TRUE(n.count("file_type")) << type;
        EXPECT_TRUE(n.count("path")) << type;
        EXPECT_FALSE(n.count("data")) << type;
        EXPECT_FALSE(n.count("data_type")) << type;
    }

    IOBase *oana = make("WebOutputAnalog", "doc_oana");
    ASSERT_NE(oana, nullptr);
    auto n = docParamNames(oana);
    EXPECT_TRUE(n.count("url"));
    EXPECT_TRUE(n.count("file_type"));
    EXPECT_TRUE(n.count("path"));
    EXPECT_FALSE(n.count("data"));

    //POST flavor: url/data/data_type, no GET params
    for (const char *type: {"WebOutputLight", "WebOutputLightRGB"})
    {
        IOBase *out = make(type, (std::string("doc_") + type).c_str());
        ASSERT_NE(out, nullptr) << type;
        n = docParamNames(out);
        EXPECT_TRUE(n.count("url")) << type;
        EXPECT_TRUE(n.count("data")) << type;
        EXPECT_TRUE(n.count("data_type")) << type;
        EXPECT_FALSE(n.count("file_type")) << type;
        EXPECT_FALSE(n.count("path")) << type;
    }

    //WebOutputLightRGB documents its extra raw_value parameter
    IOBase *rgb = io("doc_WebOutputLightRGB");
    ASSERT_NE(rgb, nullptr);
    EXPECT_TRUE(docParamNames(rgb).count("raw_value"));

    //WebOutputString documents both flavors
    IOBase *ostr = make("WebOutputString", "doc_ostr");
    ASSERT_NE(ostr, nullptr);
    n = docParamNames(ostr);
    for (const char *paramName: {"url", "file_type", "path", "data", "data_type"})
        EXPECT_TRUE(n.count(paramName)) << paramName;
}

namespace
{

void writeWebDoc(const std::string &file, double num)
{
    std::ofstream f(file);
    f << "{\"num\": " << num << "}";
}

Params webInput(const std::string &id, const std::string &url)
{
    return { { "type", "WebInputAnalog" },
             { "id", id },
             { "name", id },
             { "url", url },
             { "file_type", "json" },
             { "path", "num" },
             { "period", "10" },
             { "enabled", "true" },
             { "visible", "false" } };
}

} //namespace

/* A WebCtrl is shared by every IO of a url, and its callbacks used to be
 * removed by path: deleting one of two inputs on the same path unregistered
 * the OTHER one and left the deleted IO's callback behind. Adding an input
 * downloads a local file in line, which runs every callback. */
TEST_F(WebIOTest, DeletingAWebInputKeepsTheOtherInputOfItsPathPolled)
{
    loadConfig();
    const std::string file = cacheDir() + "/webio_fb4_shared.json";
    const std::string url = "file://" + file;
    writeWebDoc(file, 1);

    IOBase *kept = createIO(webInput("web_fb4_kept", url));
    IOBase *gone = createIO(webInput("web_fb4_gone", url));
    ASSERT_NE(kept, nullptr);
    ASSERT_NE(gone, nullptr);
    ASSERT_TRUE(deleteIO(gone));

    writeWebDoc(file, 2);
    IOBase *trigger = createIO(webInput("web_fb4_trigger", url));
    ASSERT_NE(trigger, nullptr);

    EXPECT_DOUBLE_EQ(kept->get_value_double(), 2)
        << "the surviving input is no longer polled";
}

/* The url and path of an IO can be changed at runtime (set_param from the
 * JSON API or Lua): its callback must still be removed from the WebCtrl it
 * was registered on. */
TEST_F(WebIOTest, AWebInputWhoseUrlChangedIsStillUnregistered)
{
    loadConfig();
    const std::string file = cacheDir() + "/webio_fb4_moved.json";
    const std::string url = "file://" + file;
    writeWebDoc(file, 1);

    IOBase *moved = createIO(webInput("web_fb4_moved", url));
    ASSERT_NE(moved, nullptr);
    moved->set_param("url", "file://" + cacheDir() + "/webio_fb4_elsewhere.json");
    moved->set_param("path", "elsewhere");
    ASSERT_TRUE(deleteIO(moved));

    IOBase *trigger = createIO(webInput("web_fb4_moved_trigger", url));
    ASSERT_NE(trigger, nullptr);
    EXPECT_DOUBLE_EQ(trigger->get_value_double(), 1);
}

//WebCtrl refuses a zero period: nothing is registered, nothing to remove.
TEST_F(WebIOTest, AWebInputWithAZeroPeriodIsNeverPolled)
{
    loadConfig();
    const std::string file = cacheDir() + "/webio_fb4_zero.json";
    const std::string url = "file://" + file;
    writeWebDoc(file, 3);

    Params p = webInput("web_fb4_zero", url);
    p.Add("period", "0");
    IOBase *zero = createIO(p);
    ASSERT_NE(zero, nullptr);
    EXPECT_NE(zero->get_value_double(), 3);
    ASSERT_TRUE(deleteIO(zero));

    IOBase *trigger = createIO(webInput("web_fb4_zero_trigger", url));
    ASSERT_NE(trigger, nullptr);
    EXPECT_DOUBLE_EQ(trigger->get_value_double(), 3);
}

/* A download repeating the value an input holds is still a reading
 * (IOBase::lastReading()): a thermostat on a steady web sensor must not see
 * it as silent. A failed download is not one. */
TEST_F(WebIOTest, ARepeatedDownloadIsStillAReading)
{
    AnswerServer server;
    ASSERT_NE(server.port, 0);
    server.answer("200 OK", "{\"temp\": 21.5}");

    const std::string attrs = "url=\"" + server.url() + "\" file_type=\"json\" period=\"50\" ";
    loadConfig(ioXmlDocument(roomXml(ROOM_NAME, ROOM_TYPE,
                                     webIoXml("input", "WebInputTemp", "web_fresh_temp",
                                              attrs + "path=\"temp\""))),
               rulesXmlDocument(""));
    IOBase *in = io("web_fresh_temp");
    ASSERT_NE(in, nullptr);
    EXPECT_TRUE(in->datesEveryReading());

    ASSERT_TRUE(pumpUntil([&]()
                          {
        return in->lastReading().has_value();
    }));
    EXPECT_DOUBLE_EQ(in->get_value_double(), 21.5);
    const auto first = *in->lastReading();
    EXPECT_TRUE(pumpUntil([&]()
                          {
        return *in->lastReading() > first;
    })) << "a download of the same value was not taken as a reading";
    EXPECT_DOUBLE_EQ(in->get_value_double(), 21.5);

    //Once the downloads that may have started before the switch are over.
    server.answer("500 Internal Server Error", "{}");
    const int switched = server.served;
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return server.served >= switched + 2;
    }));
    pumpUntil([]()
              {
        return false;
    },
              100);
    const auto failed = *in->lastReading();
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return server.served >= switched + 5;
    }));
    pumpUntil([]()
              {
        return false;
    },
              100);
    EXPECT_EQ(*in->lastReading(), failed);
}
