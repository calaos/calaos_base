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

//T3.14: a static destructor that logs must not touch anything the atexit chain
//has already destroyed.
//
//The pattern cannot be reproduced inside a gtest binary: the interesting
//moment is AFTER main() returns, when there is no test left to assert
//anything. It is therefore reproduced in StaticLogShutdown_helper, a real
//program built next to this one, which this test runs and whose output it
//inspects. The helper is described at length in its own file.
//
//Before the fix the helper printed a TinyXML/pugixml parse error on a config
//path made of freed heap bytes, and, under AddressSanitizer, died on a
//heap-use-after-free in Utils::calaosLogger(). Both show up here as extra
//output and, for the second one, as a non zero exit status.

#include <gtest/gtest.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using std::string;

static const char *VALID_CONFIG =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
    "<calaos:config xmlns:calaos=\"http://www.calaos.fr\">\n"
    "<calaos:option name=\"show_cursor\" value=\"true\" />\n"
    "</calaos:config>\n";

class StaticLogShutdownTest: public ::testing::Test
{
protected:
    virtual void SetUp()
    {
        char tmpl[] = "/tmp/calaos_shutdowntest_XXXXXX";
        char *d = mkdtemp(tmpl);
        ASSERT_NE(d, nullptr);
        base = d;

        confDir = base + "/config";
        ASSERT_EQ(::mkdir(confDir.c_str(), 0755), 0);

        std::ofstream f((confDir + "/local_config.xml").c_str());
        f << VALID_CONFIG;
        f.close();

        helper = findHelper();
    }

    virtual void TearDown()
    {
        string cmd = "rm -rf '" + base + "'";
        if (::system(cmd.c_str()) != 0)
            std::cerr << "warning: could not clean " << base << std::endl;
    }

    //The helper is a check_PROGRAM, it is built in this very directory. The
    //test harness exports abs_top_builddir (AM_TESTS_ENVIRONMENT), which also
    //covers a run from somewhere else.
    static string findHelper()
    {
        const char *builddir = getenv("abs_top_builddir");

        if (builddir)
        {
            string p = string(builddir) + "/tests/StaticLogShutdown_helper";
            if (::access(p.c_str(), X_OK) == 0)
                return p;
        }

        string local = "./StaticLogShutdown_helper";
        if (::access(local.c_str(), X_OK) == 0)
            return local;

        return string();
    }

    //Run the helper with its own config directory, return everything it wrote
    //on stdout and stderr, including what came out of the atexit chain.
    string runHelper(int &exitStatus)
    {
        string out = base + "/output.txt";
        string cmd = "CALAOS_CONFIG='" + confDir + "' HOME='" + base + "' '" + helper + "' > '" + out + "' 2>&1";

        exitStatus = ::system(cmd.c_str());

        std::ifstream f(out.c_str());
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    static size_t countOf(const string &haystack, const string &needle)
    {
        size_t n = 0;
        size_t pos = haystack.find(needle);

        while (pos != string::npos)
        {
            n++;
            pos = haystack.find(needle, pos + needle.size());
        }

        return n;
    }

    string base;
    string confDir;
    string helper;
};

//The destructor of a translation unit static must still reach a live logger.
//It is the exact shape of ~WagoMapManager -> ~WagoMap -> ~ExternProcServer in
//calaos_server, the chain AddressSanitizer caught reading a destroyed
//unordered_map in Utils::calaosLogger().
TEST_F(StaticLogShutdownTest, StaticDestructorStillReachesTheLogger)
{
    ASSERT_FALSE(helper.empty()) << "StaticLogShutdown_helper was not found";

    int status = -1;
    string output = runHelper(status);

    ASSERT_TRUE(WIFEXITED(status)) << "the helper did not exit normally, output:\n" << output;
    EXPECT_EQ(WEXITSTATUS(status), 0) << "output:\n" << output;

    EXPECT_EQ(countOf(output, "T3_14_MAIN_MARKER"), 1u) << "output:\n" << output;
    EXPECT_EQ(countOf(output, "T3_14_SHUTDOWN_MARKER"), 1u) << "the log emitted from a static destructor was lost, output:\n" << output;
}

//Nothing else may be printed at shutdown. The parse error on a corrupted path
//is what the level cache produced when it re-ran its initialisation from the
//atexit chain and read local_config.xml through an already destroyed path
//static.
TEST_F(StaticLogShutdownTest, NoParasiteOutputAtShutdown)
{
    ASSERT_FALSE(helper.empty()) << "StaticLogShutdown_helper was not found";

    int status = -1;
    string output = runHelper(status);

    EXPECT_EQ(output.find("Parse error"), string::npos) << "output:\n" << output;
    EXPECT_EQ(output.find("XML parsing"), string::npos) << "output:\n" << output;
    //The config file is never named in a normal run: seeing it means the
    //logger went back to local_config.xml from a static destructor.
    EXPECT_EQ(output.find("local_config.xml"), string::npos) << "output:\n" << output;

    //Two log lines and nothing else
    size_t lines = 0;
    for (size_t i = 0; i < output.size(); i++)
    {
        if (output[i] == '\n')
            lines++;
    }
    EXPECT_EQ(lines, 2u) << "output:\n" << output;
}
