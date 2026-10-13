#include <gtest/gtest.h>

#include <sys/wait.h>
#include <set>

#include "ReservedPort.h"

using calaos_test::ReservedPort;

namespace
{

void ephemeralRange(int &lo, int &hi)
{
    lo = 32768;
    hi = 60999;
    if (FILE *f = fopen("/proc/sys/net/ipv4/ip_local_port_range", "r"))
    {
        if (fscanf(f, "%d %d", &lo, &hi) != 2)
        {
            lo = 32768;
            hi = 60999;
        }
        fclose(f);
    }
}

//What the suite did before: let the kernel pick, then give the number back.
int releasedKernelPort(int type)
{
    const int fd = socket(AF_INET, type, 0);
    sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a));
    socklen_t len = sizeof(a);
    getsockname(fd, reinterpret_cast<sockaddr *>(&a), &len);
    close(fd);
    return ntohs(a.sin_port);
}

} //namespace

TEST(ReservedPort, KernelPickedPortsLiveInThePoolOtherTestsDrawFrom)
{
    int lo, hi;
    ephemeralRange(lo, hi);

    for (int i = 0; i < 20; i++)
    {
        const int p = releasedKernelPort(SOCK_STREAM);
        EXPECT_TRUE(p >= lo && p <= hi)
            << "the premise of the old helper no longer holds: " << p;
    }
}

TEST(ReservedPort, ReservedPortIsOutsideTheEphemeralPool)
{
    int lo, hi;
    ephemeralRange(lo, hi);

    for (int i = 0; i < 50; i++)
    {
        const int p = ReservedPort::reserve(i % 2 ? SOCK_DGRAM : SOCK_STREAM);
        ASSERT_GT(p, 0);
        EXPECT_TRUE(p < lo || p > hi) << "port " << p << " is in " << lo << "-" << hi;
    }
}

TEST(ReservedPort, ReservationsNeverRepeat)
{
    std::set<int> seen;
    for (int i = 0; i < 100; i++)
    {
        const int p = ReservedPort::reserve(SOCK_STREAM);
        ASSERT_GT(p, 0);
        EXPECT_TRUE(seen.insert(p).second) << "port " << p << " handed out twice";
    }
}

TEST(ReservedPort, AReservedPortStaysClaimedAgainstAnotherProcess)
{
    const int p = ReservedPort::reserve(SOCK_DGRAM);
    ASSERT_GT(p, 0);
    EXPECT_TRUE(ReservedPort::isClaimed(p));

    const pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0)
        _exit(ReservedPort::isClaimed(p) ? 0 : 1);

    int status = 0;
    waitpid(pid, &status, 0);
    EXPECT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

TEST(ReservedPort, NobodyDrawingFromThePoolCanLandOnAReservedPort)
{
    const int p = ReservedPort::reserve(SOCK_STREAM);
    ASSERT_GT(p, 0);

    for (int i = 0; i < 2000; i++)
    {
        const int fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        ASSERT_EQ(0, bind(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a)));
        socklen_t len = sizeof(a);
        getsockname(fd, reinterpret_cast<sockaddr *>(&a), &len);
        close(fd);
        ASSERT_NE(p, (int)ntohs(a.sin_port));
    }
}
