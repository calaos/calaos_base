/******************************************************************************
 **  Copyright (c) 2006-2026, Calaos. All Rights Reserved.
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
#ifndef S_PictureResizer_H
#define S_PictureResizer_H

#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <set>
#include <string>

#include "JpegThumbnail.h"

namespace Calaos
{

/* Runs thumbnail jobs on the libuv thread pool, away from the main loop. Every
 * method belongs to the loop thread; only the decoder runs on a worker. */
class PictureResizer
{
public:
    // The pool is shared with getaddrinfo: a burst of frames must not starve it.
    static constexpr size_t maxRunning = 2;
    static constexpr size_t maxPending = 6;

    enum class Fallback
    {
        NoDecoder,
        Undecodable,
        Busy,
        NoConvert,
    };

    using Decoder = std::function<JpegThumbnail::Result(const std::string &, int)>;
    using Done = std::function<void(JpegThumbnail::Result)>;

    static PictureResizer &Instance();

    // False when saturated: done is not called, the caller falls back.
    bool submit(std::shared_ptr<const std::string> source, int width, Done done);

    // One line per reason for the life of the process, never an URL or an id:
    // the condition lasts and a client polls, so a line per frame would flood.
    void reportFallback(Fallback reason);

    size_t decodesStarted() const { return started; }
    bool idle() const { return running == 0 && pending.empty(); }

    // Substitutes the decoder (null restores it) and forgets past reports.
    void resetForTest(Decoder decoder = nullptr);

private:
    struct Job
    {
        std::shared_ptr<const std::string> source;
        int width;
        Done done;
    };

    PictureResizer() = default;

    void start(Job job);

    Decoder decoder = JpegThumbnail::resize;
    std::deque<Job> pending;
    size_t running = 0;
    size_t started = 0;
    std::set<Fallback> reported;
};

} //namespace Calaos

#endif
