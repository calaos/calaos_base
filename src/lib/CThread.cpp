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
#include <CThread.h>

CThread::CThread():
    th(0),
    started(false),
    stopped(false)
{
}

CThread::~CThread()
{
    stopped = true;
    End();
}

static void *CThreadProc(void *p)
{
    CThread *pThis = (CThread *)p;
    pThis->started = true;

#ifdef IPHONE_APP
    pThis->ThreadProc_objc();
#else
    pThis->ThreadProc();
#endif

    pthread_exit(0);
}

void CThread::ThreadProc()
{
    return;
}

#ifdef IPHONE_APP
void CThread::ThreadProc_objc()
{
    call_thread_with_objcpool(this);
}
#endif

void CThread::Start()
{
    if (th)
        End();

    if (pthread_create(&th, NULL, CThreadProc, (void *)this) != 0)
        cErrorDom("threads")
                << "CThread::Start(), pthread_create() error...";
}

void CThread::KillThread()
{
    if(th)
        pthread_kill(th, SIGKILL);
    th = 0;
}

void CThread::End()
{
    stopped = true;
    if (th)
    {
        cDebugDom("threads")
                << "CThread::End(), pthread_join() waiting for thread to finish his job..."
                   ;
        pthread_join(th, NULL);
    }

    started = false;
    th = 0;
}
