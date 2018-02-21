/******************************************************************************
 **  Copyright (c) 2006-2018, Calaos. All Rights Reserved.
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
#ifndef ThreadedQueue_H
#define ThreadedQueue_H

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <utility>

template <typename T>
class ThreadedQueue
{
public:
    ~ThreadedQueue()
    {
        invalidate();
    }

    bool tryPop(T &item)
    {
        std::lock_guard<std::mutex> lock{mutex};

        if (queue.empty() || !valid)
        {
            return false;
        }

        item = std::move(queue.front());

        return true;
    }

    bool waitPop(T &item)
    {
        std::unique_lock<std::mutex> lock{mutex};

        cond.wait(lock, [this]()
        {
            return !queue.empty() || !valid;
        });

        if (!valid)
            return false;

        item = std::move(queue.front());
        queue.pop();

        return true;
    }

    void push(T &item)
    {
        std::lock_guard<std::mutex> lock{mutex};
        queue.push(std::move(item));
        cond.notify_one();
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lock{mutex};
        return queue.empty();
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock{mutex};
        while (!queue.empty())
            queue.pop();

        cond.notify_all();
    }

    void invalidate()
    {
        std::lock_guard<std::mutex> lock{mutex};
        valid = false;
        cond.notify_all();
    }

    bool isValid() const
    {
        std::lock_guard<std::mutex> lock{mutex};
        return valid;
    }

private:
    std::atomic_bool valid{true};
    mutable std::mutex mutex;
    std::queue<T> queue;
    std::condition_variable cond;

};

#endif
