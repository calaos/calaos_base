#include "FileUtils.h"
#include <gtest/gtest.h>

#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

// Test fixture: builds a temporary directory tree:
//   <root>/index.html
//   <root>/sub/page.html
//   <outside>/secret.txt   (sibling of root, must never be reachable)
class FileUtilsSafePathTest: public ::testing::Test
{
protected:
    virtual void SetUp()
    {
        char tmpl[] = "/tmp/calaos_futest_XXXXXX";
        char *d = mkdtemp(tmpl);
        ASSERT_NE(d, nullptr);
        base = d;

        root = base + "/root";
        outside = base + "/outside";
        ::mkdir(root.c_str(), 0755);
        ::mkdir(outside.c_str(), 0755);
        ::mkdir((root + "/sub").c_str(), 0755);

        writeFile(root + "/index.html", "ok");
        writeFile(root + "/sub/page.html", "ok");
        writeFile(outside + "/secret.txt", "secret");
    }

    virtual void TearDown()
    {
        // Best-effort cleanup; ignore errors.
        ::unlink((root + "/index.html").c_str());
        ::unlink((root + "/sub/page.html").c_str());
        ::unlink((outside + "/secret.txt").c_str());
        ::unlink((root + "/link_outside").c_str());
        ::unlink((root + "/link_inside").c_str());
        ::rmdir((root + "/sub").c_str());
        ::rmdir(root.c_str());
        ::rmdir(outside.c_str());
        ::rmdir(base.c_str());
    }

    static void writeFile(const std::string &path, const std::string &content)
    {
        std::ofstream f(path.c_str());
        f << content;
    }

    std::string base;
    std::string root;
    std::string outside;
};

TEST_F(FileUtilsSafePathTest, AllowsValidSubPath)
{
    std::string out;
    EXPECT_TRUE(FileUtils::resolveSafePath(root, "index.html", out));
    EXPECT_EQ(out, root + "/index.html");

    EXPECT_TRUE(FileUtils::resolveSafePath(root, "sub/page.html", out));
    EXPECT_EQ(out, root + "/sub/page.html");
}

TEST_F(FileUtilsSafePathTest, RejectsParentSegment)
{
    std::string out;
    EXPECT_FALSE(FileUtils::resolveSafePath(root, "../outside/secret.txt", out));
    EXPECT_FALSE(FileUtils::resolveSafePath(root, "..", out));
    EXPECT_FALSE(FileUtils::resolveSafePath(root, "sub/../../outside/secret.txt", out));
    EXPECT_FALSE(FileUtils::resolveSafePath(root, "sub/..", out));
    EXPECT_TRUE(out.empty());
}

TEST_F(FileUtilsSafePathTest, RejectsAbsolutePath)
{
    std::string out;
    EXPECT_FALSE(FileUtils::resolveSafePath(root, "/etc/passwd", out));
    EXPECT_FALSE(FileUtils::resolveSafePath(root, "/", out));
}

TEST_F(FileUtilsSafePathTest, RejectsNulByte)
{
    std::string out;
    std::string evil = std::string("index.html") + '\0' + "/etc/passwd";
    EXPECT_FALSE(FileUtils::resolveSafePath(root, evil, out));
}

TEST_F(FileUtilsSafePathTest, RejectsMissingFile)
{
    std::string out;
    EXPECT_FALSE(FileUtils::resolveSafePath(root, "does_not_exist.html", out));
}

TEST_F(FileUtilsSafePathTest, RejectsMissingRoot)
{
    std::string out;
    EXPECT_FALSE(FileUtils::resolveSafePath(base + "/no_such_root", "index.html", out));
    EXPECT_FALSE(FileUtils::resolveSafePath("", "index.html", out));
}

TEST_F(FileUtilsSafePathTest, RejectsSymlinkEscapingRoot)
{
    // root/link_outside -> outside/secret.txt
    ASSERT_EQ(0, ::symlink((outside + "/secret.txt").c_str(),
                           (root + "/link_outside").c_str()));
    std::string out;
    EXPECT_FALSE(FileUtils::resolveSafePath(root, "link_outside", out));
}

TEST_F(FileUtilsSafePathTest, AcceptsSymlinkInsideRoot)
{
    // root/link_inside -> root/index.html
    ASSERT_EQ(0, ::symlink((root + "/index.html").c_str(),
                           (root + "/link_inside").c_str()));
    std::string out;
    EXPECT_TRUE(FileUtils::resolveSafePath(root, "link_inside", out));
    EXPECT_EQ(out, root + "/index.html");
}

//Under a wide umask, which is the case the mode must not depend on
TEST_F(FileUtilsSafePathTest, AnOwnerOnlyCopyIs0600WhateverItReplaces)
{
    const std::string fresh = root + "/copy_fresh";
    const std::string existing = root + "/copy_existing";
    writeFile(existing, "old");
    ASSERT_EQ(0, ::chmod(existing.c_str(), 0644));

    const mode_t previous = ::umask(0);
    const bool freshCopied = FileUtils::copyFile(root + "/index.html", fresh, true);
    const bool existingCopied = FileUtils::copyFile(root + "/index.html", existing, true);
    ::umask(previous);
    ASSERT_TRUE(freshCopied);
    ASSERT_TRUE(existingCopied);

    struct stat st;
    ASSERT_EQ(0, ::stat(fresh.c_str(), &st));
    EXPECT_EQ(0600u, st.st_mode & 07777);
    EXPECT_EQ(2, st.st_size);
    ASSERT_EQ(0, ::stat(existing.c_str(), &st));
    EXPECT_EQ(0600u, st.st_mode & 07777);
    EXPECT_EQ(2, st.st_size);

    ::unlink(fresh.c_str());
    ::unlink(existing.c_str());
}

TEST_F(FileUtilsSafePathTest, APlainCopyStillFollowsTheUmask)
{
    const std::string dest = root + "/copy_plain";

    const mode_t previous = ::umask(0022);
    const bool copied = FileUtils::copyFile(root + "/index.html", dest);
    ::umask(previous);
    ASSERT_TRUE(copied);

    struct stat st;
    ASSERT_EQ(0, ::stat(dest.c_str(), &st));
    EXPECT_EQ(0644u, st.st_mode & 07777);

    ::unlink(dest.c_str());
}

TEST_F(FileUtilsSafePathTest, RootWithTrailingSlash)
{
    std::string out;
    EXPECT_TRUE(FileUtils::resolveSafePath(root + "/", "index.html", out));
    EXPECT_EQ(out, root + "/index.html");
}
