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
#ifndef MutexH
#define MutexH

#include <pthread.h>
#include <errno.h>

// A way to synchronize some threads using mutex
/* The use of this class is pretty simple. You have just to
  create an object and use lock/unlock
*/
class Mutex
{
public:
    //Create a mutex. True will lock the mutex a creation
    Mutex(bool locked = false);

    //Open an already existing mutex
    Mutex(pthread_mutex_t &m);

    ~Mutex();

    //Return a mutex id
    pthread_mutex_t &get_mutex();

    //Locking/unlocking of the mutex
    bool lock();
    bool try_lock();
    bool unlock();

    //Condition locking
    bool condition_wait();
    bool condition_wake(bool all = false);

private:

    pthread_mutex_t mutex;
    pthread_cond_t condition;
};

#endif
