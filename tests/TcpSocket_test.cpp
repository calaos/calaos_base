// T1.12: TCPSocket::Recv(string&, ...) used to append its 4096-byte read
// buffer as a NUL-terminated C-string instead of using the returned recv()
// length: a payload with an embedded NUL got truncated (dropping the rest of
// the message and often hanging, since the terminator the loop is waiting
// for arrived after the NUL), and a full 4096-byte read with no NUL at all
// made it read past the end of the stack buffer. These tests exercise both
// over a real loopback TCP connection.

#include "tcpsocket.h"
#include <gtest/gtest.h>

#include <thread>
#include <chrono>
#include <string>
#include <csignal>
#include <cstring>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

namespace
{

// Binds an ephemeral port (nPort=0) and returns the port the kernel picked,
// so parallel test runs never collide on a fixed port number.
int bindEphemeral(TCPSocket &server)
{
    if (!server.Create(0))
        return -1;
    if (!server.Listen())
        return -1;

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if (getsockname(server.get_sockfd(), (struct sockaddr *)&addr, &len) != 0)
        return -1;

    return ntohs(addr.sin_port);
}

} // namespace

TEST(TcpSocketRecv, EmbeddedNulIsNotTruncated)
{
    TCPSocket server;
    int port = bindEphemeral(server);
    ASSERT_GT(port, 0);

    bool acceptOk = false;
    std::thread acceptThread([&]() { acceptOk = server.Accept(); });

    TCPSocket client;
    ASSERT_TRUE(client.Create());
    char host[] = "127.0.0.1";
    ASSERT_TRUE(client.Connect(port, host));

    acceptThread.join();
    ASSERT_TRUE(acceptOk);

    // A payload with an embedded NUL byte before the terminating '\n'. The
    // old string(buf)/`Message += buf` code stops at the NUL, silently
    // dropping everything after it.
    std::string payload;
    payload += "before";
    payload += '\0';
    payload += "after\n";

    ASSERT_TRUE(client.Send(payload));

    std::string received;
    ASSERT_TRUE(server.Recv(received, 2000));

    EXPECT_EQ(received, payload);
    EXPECT_EQ(received.size(), payload.size());

    server.Close();
    client.Close();
}

TEST(TcpSocketRecv, FullBufferReadHasNoTrailingGarbage)
{
    TCPSocket server;
    int port = bindEphemeral(server);
    ASSERT_GT(port, 0);

    bool acceptOk = false;
    std::thread acceptThread([&]() { acceptOk = server.Accept(); });

    TCPSocket client;
    ASSERT_TRUE(client.Create());
    char host[] = "127.0.0.1";
    ASSERT_TRUE(client.Connect(port, host));

    acceptThread.join();
    ASSERT_TRUE(acceptOk);

    // Larger than the 4096-byte internal read buffer and containing no NUL
    // byte at all: the first Recv() iteration fills the whole stack buffer
    // with non-NUL data. The old string(buf) construction had no in-bounds
    // terminator to stop at and read past the array.
    std::string payload(5000, 'A');
    payload += '\n';

    ASSERT_TRUE(client.Send(payload));

    std::string received;
    ASSERT_TRUE(server.Recv(received, 2000));

    EXPECT_EQ(received, payload);
    EXPECT_EQ(received.size(), payload.size());

    server.Close();
    client.Close();
}

// The abort pipe of Recv()/Accept() is watched by select() but was not counted
// in its nfds: with a pipe descriptor above the socket one -- which is what
// happens as soon as the pipe is created after the socket, i.e. always -- the
// pipe bit was outside of the range select() looks at, and writing to the pipe
// never woke the wait up. The Recv() below only returns before its timeout if
// the pipe is really watched.
// The same select() calls also treated -1 as "a descriptor is ready", so an
// EINTR (SIGCHLD is routine in the server) sent them reading an fd_set that
// select() leaves unspecified on failure.

namespace
{

// A connected socket pair whose descriptors are guaranteed to sit *below* the
// abort pipe: the pipe is created last, so it gets the higher fd, which is the
// nfds bug.
struct AbortPipe
{
    int fds[2] = { -1, -1 };

    AbortPipe() { pipe(fds); }
    ~AbortPipe()
    {
        if (fds[0] >= 0) ::close(fds[0]);
        if (fds[1] >= 0) ::close(fds[1]);
    }

    void wake() { ssize_t r = ::write(fds[1], "s", 1); (void)r; }
};

} // namespace

TEST(TcpSocketRecv, AbortPipeAboveTheSocketFdStopsTheWait)
{
    TCPSocket server;
    int port = bindEphemeral(server);
    ASSERT_GT(port, 0);

    bool acceptOk = false;
    std::thread acceptThread([&]() { acceptOk = server.Accept(); });

    TCPSocket client;
    ASSERT_TRUE(client.Create());
    char host[] = "127.0.0.1";
    ASSERT_TRUE(client.Connect(port, host));

    acceptThread.join();
    ASSERT_TRUE(acceptOk);

    AbortPipe abort;
    ASSERT_GE(abort.fds[0], 0);
    ASSERT_GT(abort.fds[0], server.get_sockfd())
        << "the pipe has to be the highest descriptor for this test to mean anything";

    // Nothing is ever sent on the socket: only the pipe can end the wait before
    // the (long) timeout expires.
    abort.wake();

    auto start = std::chrono::steady_clock::now();
    std::string received;
    bool ret = server.Recv(received, 10000, abort.fds[0]);
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(ret) << "Recv() must report the abort, not a message";
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 5000)
        << "the abort pipe did not wake select() up";
    EXPECT_TRUE(received.empty());

    server.Close();
    client.Close();
}

// A signal delivered while Recv() waits (SIGCHLD when a spawned process exits)
// makes select() fail with EINTR. It has to be retried, not read as "the socket
// is ready" (the recv() that followed then failed) nor as an abort.
TEST(TcpSocketRecv, InterruptedSelectIsRetried)
{
    static volatile sig_atomic_t signalCount = 0;
    struct sigaction sa, old;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = [](int) { signalCount = signalCount + 1; };
    // No SA_RESTART on purpose: this is what the server gets from libuv's
    // SIGCHLD handling, and what select() reports as EINTR.
    ASSERT_EQ(0, sigaction(SIGUSR1, &sa, &old));

    TCPSocket server;
    int port = bindEphemeral(server);
    ASSERT_GT(port, 0);

    bool acceptOk = false;
    std::thread acceptThread([&]() { acceptOk = server.Accept(); });

    TCPSocket client;
    ASSERT_TRUE(client.Create());
    char host[] = "127.0.0.1";
    ASSERT_TRUE(client.Connect(port, host));

    acceptThread.join();
    ASSERT_TRUE(acceptOk);

    pthread_t receiver = pthread_self();

    // Interrupt the wait a few times, then let the real message through
    std::thread signaler([&]()
    {
        for (int i = 0;i < 3;i++)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            pthread_kill(receiver, SIGUSR1);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        client.Send(std::string("interrupted\n"));
    });

    std::string received;
    bool ret = server.Recv(received, 10000);

    signaler.join();
    sigaction(SIGUSR1, &old, NULL);

    EXPECT_GT(signalCount, 0) << "the wait was never interrupted, the test proves nothing";
    EXPECT_TRUE(ret) << "an interrupted select() was not retried";
    EXPECT_EQ(received, "interrupted\n");

    server.Close();
    client.Close();
}
