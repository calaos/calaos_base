#ifndef CALAOS_TEST_RESERVED_PORT_H
#define CALAOS_TEST_RESERVED_PORT_H

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

namespace calaos_test
{

//A port that a forked server binds later and that no other process of the same
//`make check -j` can take in the meantime.
//
//Binding port 0 and closing the socket hands the number back to the ephemeral
//pool, which every connect() and bind(0) of a parallel test draws from: the
//server then finds it taken. So the number comes from below the ephemeral
//range, and is claimed by a flock() on a per-port file whose descriptor is
//deliberately never closed, so two parallel tests never pick the same one.
class ReservedPort
{
public:
    static const int kFirst = 20000;
    static const int kLast = 32000;

    static int reserve(int type, int family = AF_INET)
    {
        int lo = 32768, hi = 60999;
        if (FILE *f = fopen("/proc/sys/net/ipv4/ip_local_port_range", "r"))
        {
            if (fscanf(f, "%d %d", &lo, &hi) != 2)
            {
                lo = 32768;
                hi = 60999;
            }
            fclose(f);
        }

        const unsigned span = kLast - kFirst + 1;
        static unsigned counter = 0;
        const unsigned seed = (unsigned)getpid() * 2654435761u ^
                              (unsigned)time(nullptr) ^ (counter++ * 7919u);

        for (unsigned i = 0; i < span; i++)
        {
            const int port = kFirst + (int)((seed + i) % span);
            if (port >= lo && port <= hi)
                continue;
            if (claim(port) && bindable(port, type, family))
                return port;
        }
        return 0;
    }

    //False while some process holds the claim on `port`.
    static bool isClaimed(int port)
    {
        const int fd = open(lockPath(port).c_str(), O_RDWR | O_CLOEXEC);
        if (fd < 0)
            return false;
        const bool held = flock(fd, LOCK_EX | LOCK_NB) != 0;
        close(fd);
        return held;
    }

private:
    static std::string lockPath(int port)
    {
        const char *dir = getenv("TMPDIR");
        return std::string(dir && *dir ? dir : "/tmp") +
               "/calaos-test-port-" + std::to_string(port) + ".lock";
    }

    static bool claim(int port)
    {
        const int fd = open(lockPath(port).c_str(),
                            O_RDWR | O_CREAT | O_CLOEXEC, 0666);
        if (fd < 0)
            return false;
        if (flock(fd, LOCK_EX | LOCK_NB) != 0)
        {
            close(fd);
            return false;
        }
        return true;
    }

    static bool bindable(int port, int type, int family)
    {
        const int fd = socket(family, type, 0);
        if (fd < 0)
            return false;

        int rc;
        if (family == AF_INET6)
        {
            sockaddr_in6 a;
            memset(&a, 0, sizeof(a));
            a.sin6_family = AF_INET6;
            a.sin6_addr = in6addr_any;
            a.sin6_port = htons((uint16_t)port);
            rc = bind(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a));
        }
        else
        {
            sockaddr_in a;
            memset(&a, 0, sizeof(a));
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = htonl(INADDR_ANY);
            a.sin_port = htons((uint16_t)port);
            rc = bind(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a));
        }
        close(fd);
        return rc == 0;
    }
};

} //namespace calaos_test

#endif
