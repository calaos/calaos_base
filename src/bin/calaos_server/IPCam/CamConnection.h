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
#ifndef S_CamConnection_H
#define S_CamConnection_H

#include <Calaos.h>
#include <CThread.h>
#include <tcpsocket.h>
#include <CamManager.h>
#include <IPCam.h>
#include <curl/curl.h>

namespace Calaos
{

class CamConnection: public CThread
{
protected:

    TCPSocket socket;
    bool end_conn; //fin de connexion
    bool login;
    bool quit;

    char *pict_buffer;
    unsigned long int pict_size;

    void ProcessRequest(string &request);

public:
    CamConnection(TCPSocket socket); //socket to read from
    ~CamConnection();

    virtual void ThreadProc(); //redefined

    bool get_end() { return end_conn; }

    void Clean();

    //Lib CURL stuff
    char *get_buffer() { return pict_buffer; }
    void set_buffer(char *b) { pict_buffer = b; }
    unsigned long int get_size() { return pict_size; }
    void set_size(unsigned long int s) { pict_size = s; }
};

}
#endif
