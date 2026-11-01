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
#include <gtest/gtest.h>

#include "JpegThumbnail.h"
#include "core/JpegTestFrames.h"

using JpegThumbnail::Status;

#ifndef HAVE_LIBJPEG

TEST(JpegThumbnail, WithoutLibjpegEveryFrameIsUnavailable)
{
    EXPECT_EQ(JpegThumbnail::resize("anything", 100).status, Status::Unavailable);
}

#else

#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <utility>

using namespace JpegTestFrames;

namespace
{

const unsigned char *middle(const Decoded &image)
{
    return image.at(image.width / 2, image.height / 2);
}

long peakRssKiB()
{
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss;
}

Options progressive()
{
    Options options;
    options.progressive = true;
    return options;
}

Options plainGreyScans()
{
    Options options;
    options.color = JCS_GRAYSCALE;
    options.plainScans = true;
    return options;
}

}

TEST(JpegThumbnail, KeepsTheRatio)
{
    auto result = JpegThumbnail::resize(solid(640, 360), 100);

    ASSERT_EQ(result.status, Status::Resized);
    Decoded image = decode(result.data);
    EXPECT_EQ(image.width, 100);
    EXPECT_EQ(image.height, 56);
}

TEST(JpegThumbnail, KeepsColoursInOrder)
{
    std::string red = encode(800, 600, [](int, int, unsigned char *rgb)
    {
        rgb[0] = 220;
        rgb[1] = 10;
        rgb[2] = 10;
    });

    auto result = JpegThumbnail::resize(red, 64);

    ASSERT_EQ(result.status, Status::Resized);
    Decoded image = decode(result.data);
    ASSERT_EQ(image.components, 3);
    EXPECT_NEAR(middle(image)[0], 220, 12);
    EXPECT_NEAR(middle(image)[1], 10, 12);
    EXPECT_NEAR(middle(image)[2], 10, 12);
}

TEST(JpegThumbnail, KeepsWhereThingsAre)
{
    std::string halves = encode(1600, 800, [](int x, int, unsigned char *rgb)
    {
        rgb[0] = rgb[1] = rgb[2] = x < 800 ? 0 : 255;
    });

    auto result = JpegThumbnail::resize(halves, 137);

    ASSERT_EQ(result.status, Status::Resized);
    Decoded image = decode(result.data);
    ASSERT_EQ(image.width, 137);
    EXPECT_LT(image.at(10, image.height / 2)[0], 20);
    EXPECT_GT(image.at(image.width - 10, image.height / 2)[0], 235);
}

TEST(JpegThumbnail, ReadsAColourProgressiveFrame)
{
    auto result = JpegThumbnail::resize(solid(1280, 720, progressive()), 320);

    ASSERT_EQ(result.status, Status::Resized);
    EXPECT_EQ(decode(result.data).width, 320);
}

TEST(JpegThumbnail, ReadsAGreyFrame)
{
    Options grey;
    grey.color = JCS_GRAYSCALE;
    auto result = JpegThumbnail::resize(solid(640, 480, grey), 100);

    ASSERT_EQ(result.status, Status::Resized);
    EXPECT_EQ(decode(result.data).width, 100);
}

TEST(JpegThumbnail, NeverEnlarges)
{
    std::string frame = solid(320, 240);

    EXPECT_EQ(JpegThumbnail::resize(frame, 320).status, Status::Unchanged);
    EXPECT_EQ(JpegThumbnail::resize(frame, 321).status, Status::Unchanged);
    EXPECT_EQ(JpegThumbnail::resize(frame, 10000).status, Status::Unchanged);
}

TEST(JpegThumbnail, OneColumnNarrowerIsResized)
{
    auto result = JpegThumbnail::resize(solid(321, 241), 320);

    ASSERT_EQ(result.status, Status::Resized);
    EXPECT_EQ(decode(result.data).width, 320);
}

TEST(JpegThumbnail, ThumbnailBiggerThanTheFrameIsDropped)
{
    Options tiny;
    tiny.quality = 10;
    tiny.optimize = true;
    tiny.color = JCS_GRAYSCALE;
    std::string frame = solid(640, 480, tiny);

    auto result = JpegThumbnail::resize(frame, 636);

    EXPECT_EQ(result.status, Status::Unchanged);
    EXPECT_TRUE(result.data.empty());
}

TEST(JpegThumbnail, ExtremeRatiosKeepAtLeastOneRow)
{
    auto wide = JpegThumbnail::resize(solid(2000, 1), 100);

    ASSERT_EQ(wide.status, Status::Resized);
    EXPECT_EQ(decode(wide.data).height, 1);

    auto tall = JpegThumbnail::resize(solid(200, 3000), 100);

    ASSERT_EQ(tall.status, Status::Resized);
    EXPECT_EQ(decode(tall.data).height, 1500);
}

TEST(JpegThumbnail, ScalerBoundaries)
{
    auto eighth = JpegThumbnail::resize(solid(800, 600), 100);

    ASSERT_EQ(eighth.status, Status::Resized);
    EXPECT_EQ(decode(eighth.data).width, 100);

    auto single = JpegThumbnail::resize(solid(1600, 900), 1);

    ASSERT_EQ(single.status, Status::Resized);
    Decoded pixel = decode(single.data);
    EXPECT_EQ(pixel.width, 1);
    EXPECT_EQ(pixel.height, 1);
}

TEST(JpegThumbnail, BadInputsFail)
{
    std::string frame = solid(320, 240);

    EXPECT_EQ(JpegThumbnail::resize(frame, 0).status, Status::Failed);
    EXPECT_EQ(JpegThumbnail::resize(frame, -1).status, Status::Failed);
    EXPECT_EQ(JpegThumbnail::resize("", 100).status, Status::Failed);
    EXPECT_EQ(JpegThumbnail::resize("not a jpeg at all", 100).status, Status::Failed);
}

TEST(JpegThumbnail, TruncatedFrameFails)
{
    std::string frame = solid(640, 480);

    EXPECT_EQ(JpegThumbnail::resize(frame.substr(0, frame.size() / 2), 100).status, Status::Failed);
    EXPECT_EQ(JpegThumbnail::resize(frame.substr(0, 20), 100).status, Status::Failed);
}

TEST(JpegThumbnail, TruncatedProgressiveFrameFails)
{
    std::string frame = solid(640, 480, progressive());

    EXPECT_EQ(JpegThumbnail::resize(frame.substr(0, frame.size() / 2), 100).status, Status::Failed);
}

TEST(JpegThumbnail, ExtremeAnnouncedDimensionsFail)
{
    std::string frame = solid(320, 240);

    EXPECT_EQ(JpegThumbnail::resize(withDimensions(frame, 65500, 65500), 100).status, Status::Failed);
    EXPECT_EQ(JpegThumbnail::resize(withDimensions(frame, 1, 65500), 1).status, Status::Failed);
    EXPECT_EQ(JpegThumbnail::resize(withDimensions(frame, 16385, 1), 100).status, Status::Failed);
    EXPECT_EQ(JpegThumbnail::resize(withDimensions(frame, 16384, 16384), 100).status, Status::Failed);
}

TEST(JpegThumbnail, ZeroHeightFails)
{
    EXPECT_EQ(JpegThumbnail::resize(withDimensions(solid(320, 240), 320, 0), 100).status, Status::Failed);
}

TEST(JpegThumbnail, AnnouncedProgressiveFrameCannotAllocateItsCoefficients)
{
    // Inside the pixel limit, but holding its coefficients takes 192 MB.
    std::string hostile = withDimensions(solid(320, 240, progressive()), 8000, 8000);

    long before = peakRssKiB();
    auto result = JpegThumbnail::resize(hostile, 100);

    EXPECT_EQ(result.status, Status::Failed);
    EXPECT_LT(peakRssKiB() - before, 64 * 1024);
}

TEST(JpegThumbnail, RepeatedScansAreBounded)
{
    std::string frame = solid(320, 240, plainGreyScans());

    EXPECT_EQ(JpegThumbnail::resize(withRepeatedFirstScan(frame, 8), 100).status, Status::Resized);
    EXPECT_EQ(JpegThumbnail::resize(withRepeatedFirstScan(frame, 70), 100).status, Status::Failed);
}

TEST(JpegThumbnail, FourComponentFrameFails)
{
    Options cmyk;
    cmyk.color = JCS_CMYK;

    EXPECT_EQ(JpegThumbnail::resize(solid(320, 240, cmyk), 100).status, Status::Failed);
}

TEST(JpegThumbnail, EveryTruncationOfAValidFrameStaysControlled)
{
    std::string frame = solid(128, 96, progressive());

    for (size_t size = 0; size < frame.size(); size += 7)
    {
        Status status = JpegThumbnail::resize(frame.substr(0, size), 32).status;
        EXPECT_TRUE(status == Status::Failed || status == Status::Unchanged) << "at " << size;
    }
}

namespace
{

struct Cost
{
    double cpuMs = 0;
    double rssMiB = 0;
};

Cost costOf(pid_t child, int iterations)
{
    int status = 0;
    rusage usage{};
    wait4(child, &status, 0, &usage);
    EXPECT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    Cost cost;
    double seconds = usage.ru_utime.tv_sec + usage.ru_stime.tv_sec +
                     (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1e6;
    cost.cpuMs = seconds * 1000 / iterations;
    cost.rssMiB = usage.ru_maxrss / 1024.0;
    return cost;
}

Cost measureResizer(const std::string &frame, int width, int iterations)
{
    pid_t child = fork();
    if (child == 0)
    {
        for (int i = 0; i < iterations; i++)
            if (JpegThumbnail::resize(frame, width).status != Status::Resized)
                _exit(1);
        _exit(0);
    }
    return costOf(child, std::max(iterations, 1));
}

Cost measureScript(const char *script, const std::string &path, int width, int iterations)
{
    const std::string url = "file://" + path;
    const std::string output = path + ".out";
    const std::string size = std::to_string(width);

    pid_t child = fork();
    if (child == 0)
    {
        if (!freopen("/dev/null", "w", stdout) || !freopen("/dev/null", "w", stderr))
            _exit(1);
        for (int i = 0; i < iterations; i++)
        {
            pid_t run = fork();
            if (run == 0)
            {
                execl("/bin/bash", "bash", script, url.c_str(), output.c_str(), "-w", size.c_str(), nullptr);
                _exit(127);
            }
            int status = 0;
            if (waitpid(run, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
                _exit(1);
        }
        _exit(0);
    }
    Cost cost = costOf(child, iterations);
    unlink(output.c_str());
    return cost;
}

void report(const char *what, int width, const Cost &cost)
{
    std::printf("%-22s width %4d: %8.1f ms CPU, %6.1f MiB peak, %7.1f img/s per core\n",
                what, width, cost.cpuMs, cost.rssMiB, 1000 / cost.cpuMs);
}

}

/* Run by hand: GTEST_ARGS=--gtest_also_run_disabled_tests bin/test <worktree>
 * core/JpegThumbnail_test:JpegThumbnail.DISABLED_Measure. Picks up the script
 * to compare with from calaos_picture.before in the working directory. */
TEST(JpegThumbnail, DISABLED_Measure)
{
    char directory[] = "/tmp/calaos_jpeg_measure_XXXXXX";
    ASSERT_NE(mkdtemp(directory), nullptr);

    for (const auto &[name, size] : {std::pair<const char *, const char *>{"4512", "4512x2512"},
                                     std::pair<const char *, const char *>{"1920", "1920x1080"}})
    {
        const std::string path = std::string(directory) + "/synth-" + name + ".jpg";
        const std::string generate = std::string("convert -size ") + size +
            " -seed 42 plasma:fractal -blur 0x1.2 -interlace none -sampling-factor 4:2:0 -strip -quality 50 " + path;
        ASSERT_EQ(std::system(generate.c_str()), 0);

        std::ifstream file(path, std::ios::binary);
        std::stringstream content;
        content << file.rdbuf();
        const std::string frame = content.str();

        std::printf("%s: %ld bytes\n", size, long(frame.size()));
        report("resizer, nothing done", 0, measureResizer(frame, 236, 0));

        for (int width : {236, 640})
        {
            const int iterations = 50;
            auto result = JpegThumbnail::resize(frame, width);
            ASSERT_EQ(result.status, Status::Resized);

            report("JpegThumbnail", width, measureResizer(frame, width, iterations));
            std::printf("  thumbnail: %zu bytes\n", result.data.size());

            if (access("calaos_picture.before", R_OK) == 0)
                report("calaos_picture before", width, measureScript("calaos_picture.before", path, width, 10));
            report("calaos_picture", width, measureScript(CALAOS_PICTURE_SCRIPT, path, width, 10));
        }

        unlink(path.c_str());
    }

    rmdir(directory);
}

#endif
