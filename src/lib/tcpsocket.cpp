//
//    libsocketcpp - a C++ class/library to allow easy TCP and UDP functionatliy
//    Copyright (C) 2001 Garrett Grice
//
//    This library is free software; you can redistribute it and/or
//    modify it under the terms of the GNU Lesser General Public
//    License as published by the Free Software Foundation; either
//    version 2 of the License, or (at your option) any later version.
//
//    This library is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//    Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public
//    License along with this library; if not, write to the Free Software
//    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
//    see the file "AUTHORS" for contact information.

#include <tcpsocket.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sstream>

// On MacosX MSG_NOSIGNAL is not defined, redefine it here, and use the setsockopt(SO_SIGPIPE) when initializing the socket
#if !defined(MSG_NOSIGNAL)
#  define MSG_NOSIGNAL 0
#endif

namespace
{

/* select() restarted on EINTR, which the server gets routinely (SIGCHLD from
 * every spawned process). The callers used to test `!select(...)`, so a -1
 * return was read as "an fd is ready" and they went on inspecting an fd_set
 * that select() leaves unspecified after a failure.
 * `readfds` (and the timeout, which Linux rewrites with the time left) is
 * restored before each retry, so an interruption restarts the wait with the
 * full timeout instead of a truncated or garbage one: simpler than tracking a
 * deadline, and erring on the side of waiting a bit longer rather than
 * reporting a timeout that did not happen.
 * Returns the number of ready fds, 0 on timeout, -1 on a real error (errno).
 */
int selectRetryEintr(int nfds, fd_set *readfds, struct timeval *timeout)
{
    fd_set savedFds = *readfds;
    struct timeval savedTimeout;

    if (timeout)
        savedTimeout = *timeout;

    for (;;)
    {
        int ret = select(nfds, readfds, NULL, NULL, timeout);

        if (ret >= 0)
            return ret;

        if (errno != EINTR)
            return -1;

        *readfds = savedFds;
        if (timeout)
            *timeout = savedTimeout;
    }
}

//Highest of the watched descriptors, +1, as select() expects it
int selectNfds(int fd, int fdpipe)
{
    return ((fdpipe > fd) ? fdpipe : fd) + 1;
}

}

TCPSocket::TCPSocket()
{
    newfd = 0;
    sockfd = 0;
    connected = false;

    cDebugDom("socket") << "new";
}

TCPSocket::~TCPSocket()
{
    cDebugDom("socket") << "deleted fd=" << Utils::to_string((!newfd)?sockfd:newfd);
}

bool TCPSocket::Create()
{
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        cErrorDom("network") << "socket(AF_INET, SOCK_STREAM): " << strerror(errno);
        return false;
    }

#if defined(SO_NOSIGPIPE)
    // Mac OS X does not have the MSG_NOSIGNAL flag when calling sendo, but we can use this socket option instead
    if (sockfd > 0)
    {
        int set_option = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_NOSIGPIPE, &set_option,
                       sizeof(set_option)))
        {
            cErrorDom("network") << "setsockopt: " << strerror(errno);
            return false;
        }
    }
#endif  // SO_NOSIGPIPE

    cDebugDom("socket") << "fd=" << Utils::to_string((!newfd)?sockfd:newfd);

    return true;
};

void TCPSocket::SetReuse()
{
    int r, reuse = 1;

    cDebugDom("socket") << "fd=" << Utils::to_string((!newfd)?sockfd:newfd);

    r = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (int *) &reuse, sizeof(reuse));
    if (r == -1)
        cErrorDom("network") << "setsockopt: SO_REUSEADDR: " << strerror(errno);
}


bool TCPSocket::Create(char nType)
{
    cDebugDom("socket") << "fd=" << Utils::to_string((!newfd)?sockfd:newfd);

    if (nType == TCPSocketTCP)
        return Create();
    if (nType == TCPSocketUDP)
    {
        sockfd = socket(AF_INET, SOCK_DGRAM, 0);

        if (sockfd == -1)
        {
            cErrorDom("network") << "socket(AF_INET, SOCK_DGRAM): " << strerror(errno);
            return false;
        }

        return true;
    }
    return false;
}

bool TCPSocket::Connect(int nPort, char *Hostname)
{
    hostent *Host;
    void *res;

    cDebugDom("socket") <<  nPort << ", " << Hostname << "fd=" << Utils::to_string((!newfd)?sockfd:newfd);

    if ((Host = gethostbyname(Hostname)) == NULL)
    {
        cErrorDom("network") << "gethostbyname(" << Hostname << "): " << hstrerror(h_errno);
        return false;
    }

    INetAddress.sin_family = AF_INET;
    INetAddress.sin_port = htons(nPort);
    INetAddress.sin_addr = *((struct in_addr *) Host->h_addr);
    res = memset(&(INetAddress.sin_zero), '\0', 8);
    if (!res) cCriticalDom("network") << "Failed to set memory !";

    if (connect(sockfd, (struct sockaddr *) &INetAddress, sizeof(struct sockaddr)) == -1)
    {
        cErrorDom("network") << "connect error : " << strerror(errno);
        return false;
    }

    connected = true;
    return true;
}

int TCPSocket::Broadcast(const void *msg, int len, int bport)
{
    cDebugDom("socket") << "fd=" << Utils::to_string((!newfd)?sockfd:newfd);

    int on = 1;
    struct sockaddr_in sin;
    if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, (char *) &on, sizeof(on)) < 0)
    {
        cErrorDom("network") << "setsockopt(SO_BROADCAST): " << strerror(errno);
        return -1;
    }

    bzero(&sin, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(bport);
    sin.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    int ret = sendto(sockfd, msg, len, MSG_NOSIGNAL, (struct sockaddr *) &sin, sizeof(sin));
    if (ret < 0)
    {
        cErrorDom("network") << "sendto: " << strerror(errno);
        return -1;
    }

    return ret;
}

int TCPSocket::SendTo(const void *msg, int len, int bport, std::string host)
{
    cDebugDom("socket") << "fd=" << Utils::to_string((!newfd)?sockfd:newfd);

    struct sockaddr_in sin;

    bzero(&sin, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(bport);
    if (host == "")
        sin.sin_addr.s_addr = from.sin_addr.s_addr;
    else
    {
        struct hostent *hostInfo;
        hostInfo = gethostbyname(host.c_str());
        if (hostInfo == NULL)
        {
            cErrorDom("network") << "gethostbyname(" << host << "): " << hstrerror(h_errno);
            return -1;
        }
        sin.sin_family = hostInfo->h_addrtype;
        memcpy((char *) &sin.sin_addr.s_addr, hostInfo->h_addr_list[0], hostInfo->h_length);
    }

    int ret = sendto(sockfd, msg, len, MSG_NOSIGNAL, (struct sockaddr *) &sin, sizeof(sin));
    if (ret < 0)
    {
        cErrorDom("network") << "sendto: " << strerror(errno);
        return -1;
    }

    return ret;
}

int TCPSocket::SendTo(std::string msg)
{
    cDebugDom("socket") << "fd=" << Utils::to_string((!newfd)?sockfd:newfd);

    int ret = sendto(sockfd, msg.c_str(), msg.length(), MSG_NOSIGNAL, (struct sockaddr*)&from, sizeof(from));
    if (ret < 0)
    {
        cErrorDom("network") << "sendto: " << strerror(errno);
        return -1;
    }

    return ret;
}

int TCPSocket::RecvFrom(char *msg, int msize, int timeout)
{
    //timeout stuff
    fd_set events;
    struct timeval tv;

    cDebugDom("socket") << "fd=" << Utils::to_string((!newfd)?sockfd:newfd);

    if (timeout > 0)
    {
        FD_ZERO(&events);
        FD_SET(sockfd, &events);

        tv.tv_sec = timeout / 1000;
        tv.tv_usec = (timeout - tv.tv_sec * 1000) * 1000;

        int ready = selectRetryEintr(sockfd + 1, &events, &tv);

        if (ready < 0)
        {
            cErrorDom("network") << "select: " << strerror(errno);
            return -1;
        }

        if (ready == 0)
        {
            //timeout !
            cDebugDom("network") << "Timeout!";
            return -1;
        }
    }

    int addr_in_size = sizeof(struct sockaddr_in);
    int nb = recvfrom(sockfd, msg, msize, MSG_NOSIGNAL, (struct sockaddr *) &from, (socklen_t *) & addr_in_size);
    if (nb < 0)
    {
        cErrorDom("network") << "recvfrom: " << strerror(errno);
        return -1;
    }
    return nb;
}

int TCPSocket::Send(const void *Message, int nLength, bool block)
{
    int Bytes_Sent;
    unsigned int flags = 0;

    cDebugDom("socket") << "fd=" << Utils::to_string((!newfd)?sockfd:newfd);

    if (!block)
        flags = MSG_DONTWAIT | MSG_NOSIGNAL;
    else
        flags = MSG_NOSIGNAL;

    if (newfd == 0)
        Bytes_Sent = send(sockfd, Message, nLength, flags);
    else
        Bytes_Sent = send(newfd, Message, nLength, flags);

    return Bytes_Sent;
}

bool TCPSocket::Send(std::string Message)
{
    if (Send(Message.c_str(), Message.length()) < 0)
        return false;

    return true;
}

int TCPSocket::Recv(void *Buffer, int nLength, bool block)
{
    int Bytes_Recv;
    unsigned int flags = 0;

    if (!block)
        flags = MSG_DONTWAIT;

    if (newfd == 0)
        Bytes_Recv = recv(sockfd, Buffer, nLength, flags);
    else
        Bytes_Recv = recv(newfd, Buffer, nLength, flags);

    return Bytes_Recv;
}

bool TCPSocket::Recv(string & Message, int timeout, int fdpipe)
{
    char buf[4096];
    int ret;

    cDebugDom("socket") << "fd=" << Utils::to_string((!newfd)?sockfd:newfd);

    //timeout stuff
    fd_set events;
    struct timeval tv;
    ssize_t res;

    while (Message.find("\n", 0) == Message.npos &&
           Message.find("\r", 0) == Message.npos)
    {
        if (timeout > 0)
        {
            int fd = (newfd == 0) ? sockfd : newfd;
            FD_ZERO(&events);
            FD_SET(fd, &events);
            if (fdpipe > 0) FD_SET(fdpipe, &events);

            tv.tv_sec = timeout / 1000;
            tv.tv_usec = (timeout - tv.tv_sec * 1000) * 1000;

            //fdpipe is watched too, it has to be counted in nfds: with a pipe
            //descriptor above the socket one, select() never looked at it and
            //the abort pipe could not interrupt the wait.
            int ready = selectRetryEintr(selectNfds(fd, fdpipe), &events, &tv);

            if (ready < 0)
            {
                cErrorDom("network") << "select: " << strerror(errno);
                return false;
            }

            if (ready == 0)
            {
                //timeout !
                cDebugDom("network") << "Timeout!";
                return false;
            }

            if (fdpipe > 0 && FD_ISSET(fdpipe, &events))
            {
                //the pipe tell us to stop receiving data
                char c;
                res = read(fdpipe, &c, 1);
                if (res <= 0) cCriticalDom("network") << "Failed to read the pipe !";
                return false;
            }
        }

        ret = Recv(&buf, sizeof(buf));

        if (ret <= 0)
        {
            cErrorDom("network") << strerror(errno);
            return false;
        }

        //Append exactly the bytes recv() returned. buf is not guaranteed to
        //be NUL-terminated (a full read fills all of it, so treating it as a
        //C-string reads past the array) and treating it as one truncates any
        //payload with an embedded NUL, silently dropping the remainder.
        Message.append(buf, ret);
    }

    return true;
}

bool TCPSocket::Close()
{
    int res;

    cDebugDom("socket") << "fd=" << Utils::to_string((!newfd)?sockfd:newfd);

    if (sockfd < 1) return false;

    if (newfd == 0)
        res = close(sockfd);
    else
        res = close(newfd);

    if (res == -1)
    {
        cErrorDom("network") << "close: " << strerror(errno);
        return false;
    }

    if (newfd == 0)
        sockfd = 0;
    else
        newfd = 0;

    return true;
}

bool TCPSocket::InboundClose()
{
    int res;
    res = close(newfd);

    cDebugDom("socket") << "fd=" << Utils::to_string((!newfd)?sockfd:newfd);

    if (res == -1)
        return false;

    newfd = 0;

    return true;
}

bool TCPSocket::Create(int nPort)
{
    void *res;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
        return false;

    cDebugDom("socket") << nPort << ", fd=" << Utils::to_string((!newfd)?sockfd:newfd);

    SetReuse();

    INetAddress.sin_family = AF_INET;
    INetAddress.sin_port = htons(nPort);
    INetAddress.sin_addr.s_addr = INADDR_ANY;
    res = memset(&(INetAddress.sin_zero), '\0', 8);
    if (!res) cCriticalDom("network") << "Failed to set memory !";

    if (::bind(sockfd, (struct sockaddr *) &INetAddress, sizeof(struct sockaddr)) == -1)
    {
        cErrorDom("network") << "bind: " << strerror(errno);
        return false;
    }

    return true;
}

bool TCPSocket::Create(int nPort, char nType)
{
    void *res;

    cDebugDom("socket") << nPort << ", " << nType << "fd=" << Utils::to_string((!newfd)?sockfd:newfd);

    if (nType == TCPSocketTCP)
    {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd == -1)
        {
            cErrorDom("network") << "socket(AF_INET, SOCK_STREAM): " << strerror(errno);
            return false;
        }
    }
    if (nType == TCPSocketUDP)
    {
        sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd == -1)
        {
            cErrorDom("network") << "socket(AF_INET, SOCK_DGRAM): " << strerror(errno);
            return false;
        }
    }

    SetReuse();

    INetAddress.sin_family = AF_INET;
    INetAddress.sin_port = htons(nPort);
    INetAddress.sin_addr.s_addr = INADDR_ANY;
    res = memset(&(INetAddress.sin_zero), '\0', 8);
    if (!res) cCriticalDom("network") << "Failed to set memory !";

    if (::bind(sockfd, (struct sockaddr *) &INetAddress, sizeof(struct sockaddr)) == -1)
    {
        cErrorDom("network") << "bind: " << strerror(errno);
        return false;
    }


    return true;

}

bool TCPSocket::Listen()
{
    cDebugDom("socket") << "fd=" << Utils::to_string((!newfd)?sockfd:newfd);

    if (listen(sockfd, 5) == -1)
    {
        cErrorDom("network") << "listen: " << strerror(errno);
        return false;
    }

    return true;
}

bool TCPSocket::Accept(int fdpipe)
{
    size_t sin_size = sizeof(struct sockaddr_in);

    cDebugDom("socket") << fdpipe << " fd=" << Utils::to_string((!newfd)?sockfd:newfd);

    fd_set events;
    FD_ZERO(&events);
    FD_SET(sockfd, &events);
    if (fdpipe > 0) FD_SET(fdpipe, &events);

    //Same as Recv(): fdpipe is watched, so it drives nfds when it is the
    //highest descriptor, and a -1 return is an error, not a ready descriptor
    if (selectRetryEintr(selectNfds(sockfd, fdpipe), &events, NULL) < 0)
    {
        cErrorDom("network") << "select: " << strerror(errno);
        return false;
    }

    if (fdpipe > 0 && FD_ISSET(fdpipe, &events))
    {
        //the pipe tell us to stop
        cDebugDom("network") << "Forced exit by pipe.";
        return false;
    }

    if ((newfd = accept(sockfd, (struct sockaddr *) &RemoteAddress, (socklen_t *)&sin_size)) == -1)
    {
        cErrorDom("network") << "accept: " << strerror(errno);
        return false;
    }
    return true;
}

char *TCPSocket::GetRemoteIP()
{
    return inet_ntoa(RemoteAddress.sin_addr);
}

char *TCPSocket::GetUDPRemoteIP()
{
    return inet_ntoa(from.sin_addr);
}

bool TCPSocket::Shutdown()
{
    int res;

    cDebugDom("socket") << "fd=" << Utils::to_string((!newfd)?sockfd:newfd);

    if (sockfd < 1) return false;

    if (newfd == 0)
        res = shutdown(sockfd, SHUT_RDWR);
    else
        return false;

    if (res == -1)
    {
        cErrorDom("network") << "shutdown: " << strerror(errno);
        return false;
    }

    return true;
}

std::string TCPSocket::GetLocalIP(std::string intf)
{
    std::string ip;
    struct ifreq ifr;

    int skfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (skfd < 0)
    {
        cErrorDom("network") << "can't open socket! " << strerror(errno);
        return "";
    }

    strncpy(ifr.ifr_name, intf.c_str(), IFNAMSIZ);

    if (ioctl(skfd, SIOCGIFFLAGS, &ifr) < 0)
    {
        close(skfd);
        return "";
    }

    if (ioctl(skfd, SIOCGIFADDR, &ifr) < 0)
    {
        close(skfd);
        return "";
    }

    ip = inet_ntoa(*(struct in_addr *) &(((struct sockaddr_in *) &ifr.ifr_addr)->sin_addr.s_addr));

    close(skfd);

    return ip;
}

#define SYSCLASSNET     "/sys/class/net"
vector<string> TCPSocket::getAllInterfaces()
{
    return FileUtils::listDir(SYSCLASSNET);
}

std::string TCPSocket::GetLocalIPFor(std::string ip_search)
{
    string ip;

    //check if the string is a correct ip address
    struct sockaddr_in sa;
    int result = inet_pton(AF_INET, ip_search.c_str(), &(sa.sin_addr));
    if (result == 0) //not an ip address
    {
        cWarningDom("network") << ip_search << " is not a valid ip address";
        //Get the first interface ip address
        vector<string> intf = TCPSocket::getAllInterfaces();
        if (intf.size() > 0)
        {
            ip = TCPSocket::GetLocalIP(intf[0]);
            cDebugDom("network") << "Using local ip address: " << ip << " for ip: " << ip_search;
            return ip;
        }

        return std::string();
    }

    int sock = socket ( AF_INET, SOCK_DGRAM, 0);

    if(sock < 0)
    {
        cErrorDom("network") << "Can't create socket";
        return ip;
    }

    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = inet_addr(ip_search.c_str());
    serv.sin_port = htons(80);

    connect(sock, (const struct sockaddr *)&serv, sizeof(serv));

    struct sockaddr_in name;
    socklen_t namelen = sizeof(name);
    getsockname(sock, (struct sockaddr *) &name, &namelen);

    char buffer[100];
    memset(&buffer, 0, 100);
    const char* p = inet_ntop(AF_INET, &name.sin_addr, buffer, 100);

    if (p)
    {
        ip = buffer;
    }
    else
    {
        cErrorDom("network") << "Can't get IP: " << strerror(errno);
    }

    cDebugDom("network") << "Using local ip address: " << ip << " for ip: " << ip_search;
    close(sock);

    return ip;
}

bool TCPSocket::GetMacAddr(std::string intf, unsigned char *mac)
{
#ifndef SIOCGIFHWADDR
    return false;
#else
    int sock;
    struct ifreq ifr;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        cErrorDom("network") << "can't open socket! " << strerror(errno);

        return false;
    }

    strncpy(ifr.ifr_name, intf.c_str(), IFNAMSIZ);

    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0)
    {
        close(sock);
        return false;
    }

    if (ioctl(sock, SIOCGIFHWADDR, &ifr) < 0)
    {
        close(sock);
        return false;
    }

    memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);

    close(sock);

    return true;
#endif
}
