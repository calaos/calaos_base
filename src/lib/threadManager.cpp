/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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
#include <threadManager.h>

ThreadManager& ThreadManager::Instance()
{
    static ThreadManager tm;
    return tm;
}

ThreadManager::~ThreadManager()
{
    if(threads.size()!=0)
    {
        std::string s;

        std::vector<CThread* >::iterator iter;
        for (iter = threads.begin();iter!=threads.end();iter++)
            s+=Utils::to_string(*iter)+", ";

        cErrorDom("threads") << "These threads are not terminated : " << s;
    }

    IPC::Instance().DeleteHandler(signal);
    conn.disconnect();
}

ThreadManager::ThreadManager()
{
    conn = signal.connect(sigc::mem_fun(*this, &ThreadManager::deleteThread));
    IPC::Instance().AddHandler("threads","deleteThread",signal,NULL);
}

void ThreadManager::add(CThread* t)
{
    threads.push_back(t);
    cInfoDom("threads") << "Add new thread : " << t;
}

void ThreadManager::deleteThread(std::string source, std::string s, void* listener_data, void* t)
{
    CThread* c = (CThread*) t;

    std::vector<CThread* >::iterator iter;
    for (iter = threads.begin();iter!=threads.end();iter++)
    {
        if (*iter == t) break;
    }

    if(iter==threads.end())
    {
        cErrorDom("threads") << "Try to delete the thread " << t << " but it doesn't exists in the threads list";
        return ;
    }

    threads.erase(iter);

    cInfoDom("threads") << "Deleting thread : " << c;

    delete c;
}
