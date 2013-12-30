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
#include <Eina.h>
#include <Ecore_File.h>

// On MacosX MSG_NOSIGNAL is not defined, redefine it here, and use the setsockopt(SO_SIGPIPE) when initializing the socket
#if !defined(MSG_NOSIGNAL)
#  define MSG_NOSIGNAL 0
#endif

TCPSocket::TCPSocket()
{
        newfd = 0;
        sockfd = 0;
        connected = false;

        Utils::logger("socket") << Priority::DEBUG << "TCPSocket::TCPSocket new (" << this << ")" << log4cpp::eol;
}

TCPSocket::~TCPSocket()
{
        Utils::logger("socket") << Priority::DEBUG << "TCPSocket::~TCPSocket (" << this << ") deleted fd=" << Utils::to_string((!newfd)?sockfd:newfd) << log4cpp::eol;
}

bool TCPSocket::Create()
{
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd == -1)
        {
                Utils::logger("network") << Priority::ERROR << "socket(AF_INET, SOCK_STREAM): " << strerror(errno) << log4cpp::eol;
                return false;
        }

#if defined(SO_NOSIGPIPE)
	// Mac OS X does not have the MSG_NOSIGNAL flag when calling sendo, but we can use this socket option instead
	if (socket > 0)
	{
	        int set_option = 1;
	        if (setsockopt(sockfd, SOL_SOCKET, SO_NOSIGPIPE, &set_option,
			       sizeof(set_option)))
	        {
	                Utils::logger("network") << Priority::ERROR << "setsockopt: " << strerror(errno) << log4cpp::eol;
	                return false;
	        }
	}
#endif  // SO_NOSIGPIPE

        Utils::logger("socket") << Priority::DEBUG << "TCPSocket::Create(" << this << "), fd=" << Utils::to_string((!newfd)?sockfd:newfd) << log4cpp::eol;

        return true;
};

void TCPSocket::SetReuse()
{
        int r, reuse = 1;

        Utils::logger("socket") << Priority::DEBUG << "TCPSocket::SetReuse(" << this << "), fd=" << Utils::to_string((!newfd)?sockfd:newfd) << log4cpp::eol;

        r = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (int *) &reuse, sizeof(reuse));
        if (r == -1)
                Utils::logger("network") << Priority::ERROR << "setsockopt: SO_REUSEADDR: " << strerror(errno) << log4cpp::eol;
}


bool TCPSocket::Create(char nType)
{
        Utils::logger("socket") << Priority::DEBUG << "TCPSocket::Create(" << nType << ", " << this << "), fd=" << Utils::to_string((!newfd)?sockfd:newfd) << log4cpp::eol;

        if (nType == TCP)
                return Create();
        if (nType == UDP)
        {
                sockfd = socket(AF_INET, SOCK_DGRAM, 0);

                if (sockfd == -1)
                {
                        Utils::logger("network") << Priority::ERROR << "socket(AF_INET, SOCK_DGRAM): " << strerror(errno) << log4cpp::eol;
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

        Utils::logger("socket") << Priority::DEBUG << "TCPSocket::Connect(" << this << ", " << nPort << ", " << Hostname << "), fd=" << Utils::to_string((!newfd)?sockfd:newfd) << log4cpp::eol;

        if ((Host = gethostbyname(Hostname)) == NULL)
        {
                Utils::logger("network") << Priority::ERROR << "gethostbyname(" << Hostname << "): " << hstrerror(h_errno) << log4cpp::eol;
                return false;
        }

        INetAddress.sin_family = AF_INET;
        INetAddress.sin_port = htons(nPort);
        INetAddress.sin_addr = *((struct in_addr *) Host->h_addr);
        res = memset(&(INetAddress.sin_zero), '\0', 8);
        if (!res) Utils::logger("network") << Priority::CRIT << "Failed to set memory !" << log4cpp::eol;

        if (connect(sockfd, (struct sockaddr *) &INetAddress, sizeof(struct sockaddr)) == -1)
        {
                Utils::logger("network") << Priority::ERROR << "TCPSocket::Connect(): " << strerror(errno) << log4cpp::eol;
                return false;
        }

        connected = true;
        return true;
}

int TCPSocket::Broadcast(const void *msg, int len, int bport)
{
        Utils::logger("socket") << Priority::DEBUG << "TCPSocket::Broadcast(" << this << "), fd=" << Utils::to_string((!newfd)?sockfd:newfd) << log4cpp::eol;

        int on = 1;
        struct sockaddr_in sin;
        if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, (char *) &on, sizeof(on)) < 0)
        {
                Utils::logger("network") << Priority::ERROR << "TCPSocket::Broadcast(): setsockopt(SO_BROADCAST): " << strerror(errno) << log4cpp::eol;
                return -1;
        }

        bzero(&sin, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_port = htons(bport);
        sin.sin_addr.s_addr = htonl(INADDR_BROADCAST);

        int ret = sendto(sockfd, msg, len, MSG_NOSIGNAL, (struct sockaddr *) &sin, sizeof(sin));
        if (ret < 0)
        {
                Utils::logger("network") << Priority::ERROR << "TCPSocket::Broadcast(): sendto: " << strerror(errno) << log4cpp::eol;
                return -1;
        }

        return ret;
}

int TCPSocket::SendTo(const void *msg, int len, int bport, std::string host)
{
        Utils::logger("socket") << Priority::DEBUG << "TCPSocket::SendTo(" << this << "), fd=" << Utils::to_string((!newfd)?sockfd:newfd) << log4cpp::eol;

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
                        Utils::logger("network") << Priority::ERROR << "gethostbyname(" << host << "): " << hstrerror(h_errno) << log4cpp::eol;
                        return -1;
                }
                sin.sin_family = hostInfo->h_addrtype;
                memcpy((char *) &sin.sin_addr.s_addr, hostInfo->h_addr_list[0], hostInfo->h_length);
        }

        int ret = sendto(sockfd, msg, len, MSG_NOSIGNAL, (struct sockaddr *) &sin, sizeof(sin));
        if (ret < 0)
        {
                Utils::logger("network") << Priority::ERROR << "TCPSocket::SendTo(), sendto: " << strerror(errno) << log4cpp::eol;
                return -1;
        }

        return ret;
}

int TCPSocket::SendTo(std::string msg)
{
        Utils::logger("socket") << Priority::DEBUG << "TCPSocket::SendTo(" << this << "), fd=" << Utils::to_string((!newfd)?sockfd:newfd) << log4cpp::eol;

        int ret = sendto(sockfd, msg.c_str(), msg.length(), MSG_NOSIGNAL, (struct sockaddr*)&from, sizeof(from));
        if (ret < 0)
        {
                Utils::logger("network") << Priority::ERROR << "TCPSocket::SendTo(), sendto: " << strerror(errno) << log4cpp::eol;
                return -1;
        }

        return ret;
}

int TCPSocket::RecvFrom(char *msg, int msize, int timeout)
{
        //timeout stuff
        fd_set events;
        struct timeval tv;

        Utils::logger("socket") << Priority::DEBUG << "TCPSocket::RecvFrom(" << this << "), fd=" << Utils::to_string((!newfd)?sockfd:newfd) << log4cpp::eol;

        if (timeout > 0)
        {
                FD_ZERO(&events);
                FD_SET(sockfd, &events);

                tv.tv_sec = timeout / 1000;
                tv.tv_usec = (timeout - tv.tv_sec * 1000) * 1000;

                if (!select(sockfd + 1, &events, NULL, NULL, &tv))
                {
                        //timeout !
                        Utils::logger("network") << Priority::DEBUG << "TCPSocket::RecvFrom(): Timeout!" << log4cpp::eol;
                        return -1;
                }
        }

        int addr_in_size = sizeof(struct sockaddr_in);
        int nb = recvfrom(sockfd, msg, msize, MSG_NOSIGNAL, (struct sockaddr *) &from, (socklen_t *) & addr_in_size);
        if (nb < 0)
        {
                Utils::logger("network") << Priority::ERROR << "TCPSocket::RecvFrom(), recvfrom: " << strerror(errno) << log4cpp::eol;
                return -1;
        }
        return nb;
}

int TCPSocket::Send(const void *Message, int nLength, bool block)
{
        int Bytes_Sent;
        unsigned int flags = 0;

        Utils::logger("socket") << Priority::DEBUG << "TCPSocket::Send(" << this << "), fd=" << Utils::to_string((!newfd)?sockfd:newfd) << log4cpp::eol;

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

        Utils::logger("socket") << Priority::DEBUG << "TCPSocket::Recv(" << this << "), fd=" << Utils::to_string((!newfd)?sockfd:newfd) << log4cpp::eol;

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

                        if (!select(fd + 1, &events, NULL, NULL, &tv))
                        {
                                //timeout !
                                Utils::logger("network") << Priority::DEBUG << "TCPSocket::Recv(): Timeout!" << log4cpp::eol;
                                return false;
                        }

                        if (fdpipe > 0 && FD_ISSET(fdpipe, &events))
                        {
                                //the pipe tell us to stop receiving data
                                char c;
                                res = read(fdpipe, &c, 1);
                                if (res <= 0) Utils::logger("network") << Priority::CRIT << "Failed to read the pipe !" << log4cpp::eol;
                                return false;
                        }
                }

                memset(buf, '\0', sizeof(buf));
                ret = Recv(&buf, sizeof(buf));

                if (ret <= 0)
                {
                        Utils::logger("network") << Priority::ERROR << "TCPSocket::Recv(): " << strerror(errno) << log4cpp::eol;
                        return false;
                }

                Message += buf;
        }

        return true;
}

bool TCPSocket::Close()
{
        int res;

        Utils::logger("socket") << Priority::DEBUG << "TCPSocket::Close(" << this << "), fd=" << Utils::to_string((!newfd)?sockfd:newfd) << log4cpp::eol;

        if (sockfd < 1) return false;

        if (newfd == 0)
                res = close(sockfd);
        else
                res = close(newfd);

        if (res == -1)
        {
                Utils::logger("network") << Priority::ERROR << "TCPSocket::Close(), close: " << strerror(errno) << log4cpp::eol;
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

        Utils::logger("socket") << Priority::DEBUG << "TCPSocket::InboundClose(" << this << "), fd=" << Utils::to_string((!newfd)?sockfd:newfd) << log4cpp::eol;

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

        Utils::logger("socket") << Priority::DEBUG << "TCPSocket::Create(" << nPort << ", " << this << "), fd=" << Utils::to_string((!newfd)?sockfd:newfd) << log4cpp::eol;

        SetReuse();

        INetAddress.sin_family = AF_INET;
        INetAddress.sin_port = htons(nPort);
        INetAddress.sin_addr.s_addr = INADDR_ANY;
        res = memset(&(INetAddress.sin_zero), '\0', 8);
        if (!res) Utils::logger("network") << Priority::CRIT << "Failed to set memory !" << log4cpp::eol;

        if (::bind(sockfd, (struct sockaddr *) &INetAddress, sizeof(struct sockaddr)) == -1)
        {
                Utils::logger("network") << Priority::ERROR << "TCPSocket::Create(), bind: " << strerror(errno) << log4cpp::eol;
                return false;
        }

        return true;
}

bool TCPSocket::Create(int nPort, char nType)
{
        void *res;

        Utils::logger("socket") << Priority::DEBUG << "TCPSocket::Create(" << nPort << ", " << nType << ", " << this << "), fd=" << Utils::to_string((!newfd)?sockfd:newfd) << log4cpp::eol;

        if (nType == TCP)
        {
                sockfd = socket(AF_INET, SOCK_STREAM, 0);
                if (sockfd == -1)
                {
                        Utils::logger("network") << Priority::ERROR << "socket(AF_INET, SOCK_STREAM): " << strerror(errno) << log4cpp::eol;
                        return false;
                }
        }
        if (nType == UDP)
        {
                sockfd = socket(AF_INET, SOCK_DGRAM, 0);
                if (sockfd == -1)
                {
                        Utils::logger("network") << Priority::ERROR << "socket(AF_INET, SOCK_DGRAM): " << strerror(errno) << log4cpp::eol;
                        return false;
                }
        }

        SetReuse();

        INetAddress.sin_family = AF_INET;
        INetAddress.sin_port = htons(nPort);
        INetAddress.sin_addr.s_addr = INADDR_ANY;
        res = memset(&(INetAddress.sin_zero), '\0', 8);
        if (!res) Utils::logger("network") << Priority::CRIT << "Failed to set memory !" << log4cpp::eol;

        if (::bind(sockfd, (struct sockaddr *) &INetAddress, sizeof(struct sockaddr)) == -1)
        {
                Utils::logger("network") << Priority::ERROR << "TCPSocket::Create(), bind: " << strerror(errno) << log4cpp::eol;
                return false;
        }


        return true;

}

bool TCPSocket::Listen()
{
        Utils::logger("socket") << Priority::DEBUG << "TCPSocket::Listen(" << this << "), fd=" << Utils::to_string((!newfd)?sockfd:newfd) << log4cpp::eol;

        if (listen(sockfd, 5) == -1)
        {
                Utils::logger("network") << Priority::ERROR << "TCPSocket::Listen(), listen: " << strerror(errno) << log4cpp::eol;
                return false;
        }

        return true;
}

bool TCPSocket::Accept(int fdpipe)
{
        size_t sin_size = sizeof(struct sockaddr_in);

        Utils::logger("socket") << Priority::DEBUG << "TCPSocket::Accept(" << fdpipe << ", " << this << "), fd=" << Utils::to_string((!newfd)?sockfd:newfd) << log4cpp::eol;

        fd_set events;
        FD_ZERO(&events);
        FD_SET(sockfd, &events);
        if (fdpipe > 0) FD_SET(fdpipe, &events);

        if (!select(sockfd + 1, &events, NULL, NULL, NULL))
        {
                Utils::logger("network") << Priority::DEBUG << "TCPSocket::Accept(): Terminating." << log4cpp::eol;
                return false;
        }

        if (fdpipe > 0 && FD_ISSET(fdpipe, &events))
        {
                //the pipe tell us to stop
                Utils::logger("network") << Priority::DEBUG << "TCPSocket::Accept(): Forced exit by pipe." << log4cpp::eol;
                return false;
        }

        if ((newfd = accept(sockfd, (struct sockaddr *) &RemoteAddress, (socklen_t *)&sin_size)) == -1)
        {
                Utils::logger("network") << Priority::ERROR << "TCPSocket::Accept(), accept: " << strerror(errno) << log4cpp::eol;
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

        Utils::logger("socket") << Priority::DEBUG << "TCPSocket::Shutdown(" << this << "), fd=" << Utils::to_string((!newfd)?sockfd:newfd) << log4cpp::eol;

        if (sockfd < 1) return false;

        if (newfd == 0)
                res = shutdown(sockfd, SHUT_RDWR);
        else
                return false;

        if (res == -1)
        {
                Utils::logger("network") << Priority::ERROR << "TCPSocket::Shutdown(), shutdown: " << strerror(errno) << log4cpp::eol;
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
                Utils::logger("network") << Priority::ERROR << "TCPSocket::GetLocalIP(): can't open socket! " << strerror(errno) << log4cpp::eol;
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
        vector<string> ret;
        Eina_Iterator *it = eina_file_ls(SYSCLASSNET);

        const char *f_name;
        EINA_ITERATOR_FOREACH(it, f_name)
        {
                ret.push_back(ecore_file_file_get(f_name));
                eina_stringshare_del(f_name);
        }
        eina_iterator_free(it);

        return ret;
}

std::string TCPSocket::GetLocalIPFor(std::string ip_search)
{
        bool found_ip = false;
        string ip;
        vector<string> intf = TCPSocket::getAllInterfaces();

        /* This is not the correct way to check. We need a way to look
         * for the conresponding net interface for the given ip address.
         * We need to look for a way to get the right route for that ip
         * address. And if nothing is found, try getting the default route.
         * link: http://stackoverflow.com/questions/15668653/how-to-find-the-default-networking-interface-in-linux
         */

        //check if the string is a correct ip address
        struct sockaddr_in sa;
        int result = inet_pton(AF_INET, ip_search.c_str(), &(sa.sin_addr));
        if (result == 0) //not an ip address
        {
                Utils::logger("network") << Priority::INFO << ip_search << " is not a valid ip address" << log4cpp::eol;
                //Get the first interface ip address
                if (intf.size() > 0)
                {
                        ip = TCPSocket::GetLocalIP(intf[0]);
                        Utils::logger("network") << Priority::INFO << "Using local ip address: " << ip << log4cpp::eol;
                        return ip;
                }

                return std::string();
        }

        for (uint j = 0;j < intf.size() && !found_ip;j++)
        {
                ip = TCPSocket::GetLocalIP(intf[j]);

                if (ip == "") continue;
                vector<string> splitter, splitter2;
                Utils::split(ip, splitter, ".", 4);
                Utils::split(ip_search, splitter2, ".", 4);
                if (splitter[0] == splitter2[0] &&
                    splitter[1] == splitter2[1] &&
                    splitter[2] == splitter2[2])
                        found_ip = true;
        }

        Utils::logger("network") << Priority::INFO << "Using local ip address: " << ip << log4cpp::eol;

        if (found_ip)
                return ip;
        return std::string();
}

bool TCPSocket::GetMacAddr(std::string intf, unsigned char *mac)
{
#ifndef SIOCGIFADDR
        return false;
#else
        int sock;
        struct ifreq ifr;

        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0)
        {
                Utils::logger("network") << Priority::ERROR <<
                                "TCPSocket::GetMacAddr(): can't open socket! "
                                << strerror(errno)
                                << log4cpp::eol;

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
