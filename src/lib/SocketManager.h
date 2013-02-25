/******************************************************************************
**  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
**
**  This file is part of Calaos Home.
**
**  Calaos Home is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 3 of the License, or
**  (at your option) any later version.
**
**  Calaos Home is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Foobar; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#ifndef S_SOCKETMANAGER_H
#define S_SOCKETMANAGER_H

#include <Utils.h>
#include <tcpsocket.h>
#include <CThread.h>
#include <Mutex.h>
#include <IPC.h>
#ifndef IPHONE_APP
#include <Ecore.h>
#endif

namespace CalaosNetwork
{
        class SocketManager;

        class AsyncThread: public CThread
        {
                private:
                        bool quit;
                        SocketManager *sm;

                        Mutex *end_mutex;

                public:
                        AsyncThread(SocketManager *s): quit(false), sm(s)
                        {
                                end_mutex = new Mutex(false);
                        }
                        ~AsyncThread();

                        virtual void ThreadProc();

                        void Clean() { quit = true; }
        };

        class SocketManager
        {
                private:
                        SocketManager();

                        std::vector<TCPSocket *> sockets;
                        std::vector<std::string> hosts;
                        std::vector<int> ports;
                        std::vector<SOCKET_TYPE> sock_types;
                        std::vector<Mutex *> mutex; //each socket has its own mutex
                        std::vector<Mutex *> connect_mutex; //each socket has its own mutex for Connect()
                        std::vector<int> rpipes; //pipes for gracefull close of blocking recv
                        std::vector<int> wpipes; //pipes for gracefull close of blocking recv

                        bool stop_thread;

                        AsyncThread *async_thread;
                        Mutex async_mutex;
                        string async_cmd;
                        TCPSocket *async_socket;
                        bool async_lock;
                        bool async_answer;
                        sigc::signal<void, string> async_sig;

                        std::vector<bool> socket_error;

                public:
                        static SocketManager &Instance(); //Singleton
                        ~SocketManager();

                        TCPSocket *Add(std::string host, int port, SOCKET_TYPE type);
                        void Delete(int i);
                        int get_size() { return sockets.size(); } //return the number of sockets available
                        TCPSocket *get_socket(int i) { return sockets[i]; }

                        void Connect(int i);
                        void Connect(TCPSocket *socket);

                        bool Listen(TCPSocket *socket);

                        bool Recv(TCPSocket *socket, std::string &packet, int timeout = 0, bool unlock = true);
                        int Recv(TCPSocket *socket, void *data, int length, bool lock, bool unlock, int timeout = 0);
                        bool Send(TCPSocket *socket, std::string packet, bool lock = true);
                        int Send(TCPSocket *socket, const void *data, int length, bool lock, bool unlock);

                        //Send a simple command in a thread without returning result
                        // (usefull with basic action command)
                        bool SendAsync(TCPSocket *socket, std::string packet, bool lock = true);
                        bool SendAsync(TCPSocket *socket, std::string packet, sigc::signal<void, std::string> &signal, bool lock = true);

                        void MutexLock(TCPSocket *socket);
                        void MutexUnlock(TCPSocket *socket);

                        Mutex &getMutex() { return async_mutex; }
                        void getAsyncCommand(TCPSocket *&sock, string &cmd, bool &lock, sigc::signal<void, std::string> &signal, bool &answer)
                        { sock = async_socket; cmd = async_cmd; lock = async_lock; signal = async_sig; answer = async_answer; }

        };

}
#endif
