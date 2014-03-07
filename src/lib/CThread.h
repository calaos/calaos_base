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
#ifndef CThreadH
#define CThreadH

#include <Utils.h>

// A way to run a class in a different thread
/*
        The use of this class is pretty simple. You have just to
        inherit from it, and redefine the ThreadProc() member function.
*/
class CThread
{
protected:
    pthread_t th;

public:
    CThread();
    virtual ~CThread();

    bool started;
    bool stopped;

    // Start the thread
    /*
                        Create the thread and run it.
                */
    void Start();
    // Wait for the end of the thread
    void End();
    // Try to stop the running thread
    void KillThread();

    // the threaded member function
    /*
                        This function run in a new thread after a call to Start().
                */
    virtual void ThreadProc();

#ifdef IPHONE_APP
    /* used in obj-c to encapsulate the thread in a NSReleasePool */
    virtual void ThreadProc_objc();
#endif
};

#endif
