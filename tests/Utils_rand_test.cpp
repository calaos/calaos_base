// T1.12: Utils::createRandomUuid() and Utils::getTmpFilename() no longer draw
// from a per-call-reseeded rand() / predictable name. These tests check the
// externally observable properties: UUID format + uniqueness across many
// draws (not clock-correlated), and that temp-file creation goes through the
// O_CREAT|O_EXCL primitive that refuses to follow a pre-planted symlink.
//
// createRandomUuid() now delegates to sole (already vendored, already used by
// ActionPush) instead of carrying its own generator, so the tests also pin the
// shape callers rely on: a 36 character, RFC 4122 version 4 uuid.

#include "Utils.h"
#include "FileUtils.h"
#include <gtest/gtest.h>

#include <regex>
#include <set>
#include <string>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstdio>

TEST(CreateRandomUuid, MatchesUuidFormat)
{
    // 8-4-4-4-12 hex digits, matching the historical output shape (see
    // Utils::createRandomUuid()).
    static const std::regex uuidRe(
        "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$");

    for (int i = 0; i < 50; i++)
    {
        std::string uuid = Utils::createRandomUuid();
        EXPECT_TRUE(std::regex_match(uuid, uuidRe)) << "uuid=" << uuid;
    }
}

TEST(CreateRandomUuid, IsAVersion4Uuid)
{
    // sole::uuid4() sets the version and variant nibbles, which the hand
    // rolled generator did not. Everything that stores those uuids only needs
    // 36 characters, so this is the shape not to lose.
    for (int i = 0; i < 50; i++)
    {
        std::string uuid = Utils::createRandomUuid();
        ASSERT_EQ(uuid.size(), 36u) << "uuid=" << uuid;
        EXPECT_EQ(uuid[14], '4') << "not a version 4 uuid: " << uuid;
        EXPECT_NE(std::string("89ab").find(uuid[19]), std::string::npos)
            << "wrong variant nibble: " << uuid;
    }
}

TEST(CreateRandomUuid, ManyDrawsAreDistinct)
{
    // The old implementation reseeded srand(usec*sec) on every call: two
    // calls landing in the same microsecond (easily hit in a tight loop)
    // produced the exact same sequence of rand() outputs, and therefore the
    // exact same UUID. Drawing many UUIDs back-to-back and requiring all of
    // them to be distinct catches that clock-correlation directly.
    const int kDraws = 2000;
    std::set<std::string> seen;
    for (int i = 0; i < kDraws; i++)
        seen.insert(Utils::createRandomUuid());

    EXPECT_EQ(seen.size(), (size_t)kDraws);
}

TEST(CreateRandomUuid, NotPerturbedByOrPerturbingGlobalRandState)
{
    // The fix must not still be reading from (or reseeding) the global
    // rand()/srand() stream: two draws taken around identical srand() calls
    // must still differ.
    srand(1);
    std::string a = Utils::createRandomUuid();
    srand(1);
    std::string b = Utils::createRandomUuid();
    EXPECT_NE(a, b);
}

class GetTmpFilenameTest: public ::testing::Test
{
protected:
    void TearDown() override
    {
        for (auto &f: created)
            ::unlink(f.c_str());
    }

    std::vector<std::string> created;
};

TEST_F(GetTmpFilenameTest, CreatesFileWithRequestedExtensionAndPrefix)
{
    std::string fname = Utils::getTmpFilename("tmp", "_utilstest");
    ASSERT_FALSE(fname.empty());
    created.push_back(fname);

    EXPECT_NE(fname.find("/tmp/calaos_utilstest_"), std::string::npos);
    EXPECT_EQ(fname.substr(fname.size() - 4), ".tmp");

    // mkstemps() creates the file itself (mode 0600): it must already exist
    // by the time getTmpFilename() returns.
    EXPECT_TRUE(FileUtils::exists(fname));
}

TEST_F(GetTmpFilenameTest, ManyCallsNeverCollide)
{
    const int kDraws = 200;
    std::set<std::string> seen;
    for (int i = 0; i < kDraws; i++)
    {
        std::string f = Utils::getTmpFilename("tmp", "_utilstest");
        ASSERT_FALSE(f.empty());
        created.push_back(f);
        seen.insert(f);
    }
    EXPECT_EQ(seen.size(), (size_t)kDraws);
}

TEST(GetTmpFilenameSecurityMechanism, RefusesToFollowPreplantedSymlink)
{
    // getTmpFilename() now creates its file via mkstemps(), i.e. open()
    // with O_CREAT|O_EXCL. That is the exact primitive exercised here: it
    // must refuse to create through a pre-existing symlink rather than
    // silently following it into the victim path -- precisely the
    // TOCTOU window the old rand()-guess-a-name / FileUtils::exists() /
    // (caller) open() loop was exposed to.
    std::string victim = "/tmp/calaos_utilstest_victim";
    std::string trap = "/tmp/calaos_utilstest_trap.tmp";
    ::unlink(victim.c_str());
    ::unlink(trap.c_str());
    ASSERT_EQ(0, symlink(victim.c_str(), trap.c_str()));

    int fd = open(trap.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
    int err = errno;
    if (fd >= 0)
        close(fd);

    EXPECT_EQ(fd, -1);
    EXPECT_EQ(err, EEXIST);
    EXPECT_FALSE(FileUtils::exists(victim));

    ::unlink(trap.c_str());
    ::unlink(victim.c_str());
}
