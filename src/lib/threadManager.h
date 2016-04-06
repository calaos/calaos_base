/******************************************************************************
 **  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
 **
 **  This file is part of Calaos.
 **
 **  Calaos is free software; you can redistribute it and/or modify
 **  it under the terms of the GNU General Public License as published by
 **  the Free Software Foundation; either version 3 of the License, or
 **  (at your option) any later version.
 **
 **  Calaos is distributed in the hope that it will be useful,
 **  but WITHOUT ANY WARRANTY; without even the implied warranty of
 **  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 **  GNU General Public License for more details.
 **
 **  You should have received a copy of the GNU General Public License
 **  along with Foobar; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/
#ifndef  THREADMANAGER_INC
#define  THREADMANAGER_INC

#include <vector>
#include <CThread.h>
#include <IPC.h>


class ThreadManager
{
private:
    std::vector<CThread*> threads;
    sigc::signal<void, std::string, std::string, void*, void*> signal;
    sigc::connection conn;

public:
    static ThreadManager& Instance();
    ThreadManager();
    void deleteThread(std::string source, std::string s, void*, void*);
    ~ThreadManager();
    void add(CThread *c);

};

#endif
