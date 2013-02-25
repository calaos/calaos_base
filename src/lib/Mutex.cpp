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
#include <Mutex.h>

Mutex::Mutex(bool locked)
{
        pthread_mutex_init(&mutex, NULL);
        pthread_cond_init(&condition, NULL);

        if (locked)
                lock();
}

Mutex::Mutex(pthread_mutex_t &m)
{
        mutex = m;
}

pthread_mutex_t &Mutex::get_mutex()
{
        return mutex;
}

Mutex::~Mutex()
{
        pthread_mutex_destroy(&mutex);
        pthread_cond_destroy(&condition);
}

bool Mutex::lock()
{
        if (pthread_mutex_lock(&mutex) == 0)
                return true;
        else
                return false;
}

bool Mutex::try_lock()
{
        if (pthread_mutex_trylock(&mutex) == EBUSY)
                return false;
        else
                return true;
}

bool Mutex::unlock()
{
        if (pthread_mutex_unlock(&mutex) == 0)
                return true;
        else
                return false;
}

bool Mutex::condition_wait()
{
        if (pthread_cond_wait(&condition, &mutex) == 0)
                return true;
        else
                return false;
}

bool Mutex::condition_wake(bool all)
{
        if (all)
        {
                if (pthread_cond_broadcast(&condition) == 0)
                        return true;
                else
                        return false;
        }
        else
        {
                if (pthread_cond_signal(&condition) == 0)
                        return true;
                else
                        return false;
        }
}
