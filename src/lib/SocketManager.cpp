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
#include <SocketManager.h>

using namespace CalaosNetwork;

SocketManager::SocketManager(): async_mutex(true)
{
        async_thread = new AsyncThread(this);
        async_thread->Start();
}

SocketManager::~SocketManager()
{
        async_thread->Clean();
        async_mutex.unlock();
        delete async_thread;
        for (uint i = 0;i < sockets.size();i++)
                delete sockets[i];
        for (uint i = 0;i < mutex.size();i++)
                delete mutex[i];
        for (uint i = 0;i < connect_mutex.size();i++)
                delete connect_mutex[i];
        for (uint i = 0;i < rpipes.size();i++)
        {
                close(rpipes[i]);
                close(wpipes[i]);
        }

        sockets.clear();
        hosts.clear();
        ports.clear();
        sock_types.clear();
        mutex.clear();
        rpipes.clear();
        wpipes.clear();
}

SocketManager &SocketManager::Instance()
{
        static SocketManager inst;

        return inst;
}

TCPSocket *SocketManager::Add(std::string host, int port, SOCKET_TYPE type)
{
        int res;
        TCPSocket *socket = new TCPSocket();

        sockets.push_back(socket);
        hosts.push_back(host);
        ports.push_back(port);
        sock_types.push_back(type);
        Mutex *m = new Mutex(false);
        mutex.push_back(m);
        m = new Mutex(false);
        connect_mutex.push_back(m);
        socket_error.push_back(true); //force the connection

        int pfd[2];
        res = pipe(pfd);
        rpipes.push_back(pfd[0]);
        wpipes.push_back(pfd[1]);

        return socket;
}

void SocketManager::Delete(int pos)
{
        vector<TCPSocket *>::iterator iter = sockets.begin();
        for (int i = 0;i < pos;iter++, i++) ;
        delete sockets[pos];
        sockets.erase(iter);

        vector<std::string>::iterator iter2 = hosts.begin();
        for (int i = 0;i < pos;iter2++, i++) ;
        hosts.erase(iter2);

        vector<int>::iterator iter3 = ports.begin();
        for (int i = 0;i < pos;iter3++, i++) ;
        ports.erase(iter3);

        vector<SOCKET_TYPE>::iterator iter4 = sock_types.begin();
        for (int i = 0;i < pos;iter4++, i++) ;
        sock_types.erase(iter4);

        vector<Mutex *>::iterator iter5 = mutex.begin();
        for (int i = 0;i < pos;iter5++, i++) ;
        delete mutex[pos];
        mutex.erase(iter5);

        vector<Mutex *>::iterator iter6 = connect_mutex.begin();
        for (int i = 0;i < pos;iter6++, i++) ;
        delete connect_mutex[pos];
        connect_mutex.erase(iter6);

        vector<bool>::iterator iter7 = socket_error.begin();
        for (int i = 0;i < pos;iter7++, i++) ;
        socket_error.erase(iter7);
}

bool SocketManager::SendAsync(TCPSocket *socket, std::string packet, bool lock)
{
        async_socket = socket;
        async_cmd = packet;
        async_lock = lock;
        async_answer = false;
        async_mutex.unlock();

        return true;
}

bool SocketManager::SendAsync(TCPSocket *socket, std::string packet, sigc::signal<void, std::string> &signal, bool lock)
{
        async_socket = socket;
        async_cmd = packet;
        async_lock = lock;
        async_answer = true;
        async_sig = signal;
        async_mutex.unlock();

        return true;
}

bool SocketManager::Recv(TCPSocket *socket, std::string &packet, int timeout, bool unlock)
{
        TCPSocket *s = NULL;
        int id = -1;

        for (uint i = 0;i < sockets.size();i++)
        {
                if (socket == sockets[i])
                {
                        s = sockets[i];
                        id = i;
                }
        }

        if (!s || id < 0) return false;
        if (!s->Connected() || socket_error[id])
        {
                Connect(id);
                socket_error[id] = false;
        }

        packet = ""; //clear the string
        bool ret = s->Recv(packet, timeout, rpipes[id]); //timeout -> 60s

        while( (packet[packet.length() - 1] == '\n' || packet[packet.length() - 1] == '\r')
                        && !packet.empty() )
                packet.erase(packet.length() - 1, 1);
        if (!ret) socket_error[id] = true; //we are not connected anymore, try to reconnect
        if (unlock) mutex[id]->unlock();

        return ret;
}

int SocketManager::Recv(TCPSocket *socket, void *data, int length, bool lock, bool unlock, int timeout)
{
        TCPSocket *s = NULL;
        int id = -1;

        for (uint i = 0;i < sockets.size();i++)
        {
                if (socket == sockets[i])
                {
                        s = sockets[i];
                        id = i;
                }
        }

        if (!s || id < 0) return false;
        if (lock) mutex[id]->lock();

        if (!s->Connected() || socket_error[id])
        {
                Connect(id);
                socket_error[id] = false;
        }

        int ret = s->Recv(data, length, timeout);
        if (ret <= 0) socket_error[id] = true; //we are not connected anymore, try to reconnect
        if (unlock) mutex[id]->unlock();

        return ret;
}

bool SocketManager::Send(TCPSocket *socket, std::string packet, bool lock)
{
        TCPSocket *s = NULL;
        int id = -1;

        for (uint i = 0;i < sockets.size();i++)
        {
                if (socket == sockets[i])
                {
                        s = sockets[i];
                        id = i;
                }
        }

        if (!s || id < 0) return false;
        if (lock) mutex[id]->lock();

        if (!s->Connected() || socket_error[id])
        {
                Connect(id);
                socket_error[id] = false;
        }

        packet = packet + "\r\n";
        bool ret = s->Send(packet);
        if (!ret)
        {
                //First try to connect, and resend data
                Connect(id);
                ret = s->Send(packet);
                if (!ret) socket_error[id] = true; //Something not working, leave with error
        }

        return ret;
}

int SocketManager::Send(TCPSocket *socket, const void *data, int length, bool lock, bool unlock)
{
        TCPSocket *s = NULL;
        int id = -1;

        for (uint i = 0;i < sockets.size();i++)
        {
                if (socket == sockets[i])
                {
                        s = sockets[i];
                        id = i;
                }
        }

        if (!s || id < 0) return false;
        if (lock) mutex[id]->lock();

        if (!s->Connected() || socket_error[id])
        {
                Connect(id);
                socket_error[id] = false;
        }

        int ret = s->Send(data, length);
        if (ret <= 0)
        {
                //First try to connect, and resend data
                Connect(id);
                ret = s->Send(data, length);
                if (ret <= 0) socket_error[id] = true; //Something not working, leave with error
        }
        if (unlock) mutex[id]->unlock();

        return ret;
}

void SocketManager::MutexLock(TCPSocket *socket)
{
        int id = -1;

        for (uint i = 0;i < sockets.size();i++)
                if (socket == sockets[i])
                        id = i;

        if (id < 0) return;

        mutex[id]->lock();
}

void SocketManager::MutexUnlock(TCPSocket *socket)
{
        int id = -1;

        for (uint i = 0;i < sockets.size();i++)
                if (socket == sockets[i])
                        id = i;

        if (id < 0) return;

        mutex[id]->unlock();
}

void SocketManager::Connect(int i)
{
        connect_mutex[i]->lock();

        //send something to the pipe, forcing to close the eventual blocking recv
        size_t res;
        int resi;
        char _c = 1;
        res = write(wpipes[i], &_c, 1);

        TCPSocket *socket = sockets[i];
        socket->Shutdown();
        char c;
        while (socket->Recv((void *)&c, 1) > 0) /* do... */ ;
        socket->Close();
        socket->Create();
        socket->Connect(ports[i], (char *)hosts[i].c_str());

        if (sock_types[i] == CALAOS)
        {
                //try to login
                string cmd = "login ";

		//Get username/password
		string username = Utils::get_config_option("calaos_user");
		string password = Utils::get_config_option("calaos_password");

		if (Utils::get_config_option("cn_user") != "" &&
		    Utils::get_config_option("cn_pass") != "")
		{
			username = Utils::get_config_option("cn_user");
			password = Utils::get_config_option("cn_pass");
		}

                cmd += Utils::url_encode(username) + " ";
                cmd += Utils::url_encode(password);
                cmd += "\r\n";
                if (socket->Send(cmd))
                {
                        string res;
                        if(!socket->Recv(res, 5000))
                        {
                                    IPC::Instance().SendEvent("wrongPassword", "wrongPassword", NULL);
                        }
                }
        }

        //recreate the pipe
        resi = close(wpipes[i]);
        close(rpipes[i]);
        int pfd[2];
        resi = pipe(pfd);
        rpipes[i] = pfd[0];
        wpipes[i] = pfd[1];

        connect_mutex[i]->unlock();
}

void SocketManager::Connect(TCPSocket *socket)
{
        int id = -1;

        for (uint i = 0;i < sockets.size();i++)
        {
                if (socket == sockets[i])
                {
                        id = i;
                }
        }

        if (id < 0) return ;

        Connect(id);
}

AsyncThread::~AsyncThread()
{
        Clean();

#ifndef IPHONE_APP
        //Wait 2s for the thread to stop
        //after 2s close all network connections
        double t = ecore_time_get();
        while (ecore_time_get() - t < 2.0)
        {
                if (end_mutex->try_lock())
                {
                        //thread has quit
                        break;
                }

                struct timespec t;
                t.tv_sec = 0;
                t.tv_nsec = 150000000; //150ms
                nanosleep(&t, NULL);
        }

        //reset connections
        if (ecore_time_get() - t > 1.0)
        {
                for(int i=0;i<SocketManager::Instance().get_size();i++)
                        SocketManager::Instance().Connect(i);
        }
#endif
	
        delete end_mutex;
        End(); //wait for the thread
}

void AsyncThread::ThreadProc()
{
        //if mutex is already locked, return imediatly because the
        //thread is being deleted
        if (!end_mutex->try_lock())
                return;

        //entering loop
        while(!quit)
        {
                sm->getMutex().lock();

                if (quit) return;

                string cmd, answer;
                TCPSocket *socket;
                bool lock;
                bool do_answer;
                sigc::signal<void, std::string> signal;
                sm->getAsyncCommand(socket, cmd, lock, signal, do_answer);

                SocketManager::Instance().Send(socket, cmd, lock);
                SocketManager::Instance().Recv(socket, answer);

                if (do_answer)
                {
                        signal.emit(answer);
                }

                if (quit) return;
        }

        end_mutex->unlock();
}
