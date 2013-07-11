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
//   see the file "AUTHORS" for contact information.

#ifndef _TCPSOCKET_H
#define _TCPSOCKET_H

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include <Utils.h>

#ifdef IPHONE_APP
#define MSG_NOSIGNAL 0
#endif

#define TCP 'T'
#define UDP 'U'


        class TCPSocket
        {

                public:

                        TCPSocket();
                        ~TCPSocket();

                        bool Create();
                        bool Create(char nType);
                        bool Create(int nPort);
                        bool Create(int nPort, char nType);

                        bool Listen();

                        bool Accept(int fdpipe = 0);

                        bool Connect(int nPort, char *Hostname);
                        bool Connected()
                        {
                                return connected;
                        }

                        int Send(const void *Message, int nLength, bool block = true);
                        bool Send(std::string Message);
                        int Recv(void *Buffer, int nLength, bool block = true);
                        bool Recv(std::string & Message, int timeout = 0, int fdpipe = 0);
                        int Broadcast(const void *msg, int len, int bport);
                        bool Broadcast(std::string msg, int port)
                        {
                                if (Broadcast(msg.c_str(), msg.length(), port) >= 0)
                                        return true;
                                else
                                        return false;
                        }
                        int SendTo(const void *msg, int len, int bport, std::string host = "");
                        int SendTo(std::string msg);
                        bool SendTo(std::string msg, int port)
                        {
                                if (SendTo(msg.c_str(), msg.length(), port) >= 0)
                                        return true;
                                else
                                        return false;
                        }
                        int RecvFrom(char *msg, int msize, int timeout = 0);

                        char *GetRemoteIP();
                        char *GetUDPRemoteIP();

                        bool Close();
                        bool Shutdown();
                        bool InboundClose();

                        void SetReuse();

                        static std::string GetLocalIP(std::string intf = "eth0");
                        static bool GetMacAddr(std::string intf, unsigned char *mac);

                        int get_sockfd() { return sockfd; }

                private:

                        int sockfd, newfd;
                        sockaddr_in INetAddress, RemoteAddress, from;

                        bool connected;
        };
#endif
