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
#include <string>
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
