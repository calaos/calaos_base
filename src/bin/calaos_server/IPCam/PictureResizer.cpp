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
#include "PictureResizer.h"

#include "Utils.h"
#include "libuvw.h"

using namespace Calaos;

PictureResizer &PictureResizer::Instance()
{
    static PictureResizer instance;
    return instance;
}

bool PictureResizer::submit(std::shared_ptr<const std::string> source, int width, Done done)
{
    Job job{ std::move(source), width, std::move(done) };

    if (running < maxRunning)
    {
        start(std::move(job));
        return true;
    }

    if (pending.size() >= maxPending)
        return false;

    pending.push_back(std::move(job));
    return true;
}

void PictureResizer::start(Job job)
{
    running++;
    started++;

    auto result = std::make_shared<JpegThumbnail::Result>();

    // The request holds the frame and the decoder by value: nothing on the
    // worker reaches back to this object. A throw there would end the process.
    auto task = [source = job.source, width = job.width, decode = decoder, result]()
    {
        try
        {
            *result = decode(*source, width);
        }
        catch (...)
        {
            *result = {};
        }
    };

    // ErrorEvent means cancelled: result stays Failed, the caller falls back.
    auto finished = [this, done = std::move(job.done), result](const auto &, auto &)
    {
        running--;
        if (!pending.empty())
        {
            Job next = std::move(pending.front());
            pending.pop_front();
            start(std::move(next));
        }
        done(std::move(*result));
    };

    auto request = uvw::Loop::getDefault()->resource<uvw::WorkReq>(task);
    request->once<uvw::WorkEvent>(finished);
    request->once<uvw::ErrorEvent>(finished);
    request->queue();
}

void PictureResizer::reportFallback(Fallback reason)
{
    if (!reported.insert(reason).second)
        return;

    switch (reason)
    {
    case Fallback::NoDecoder:
        cWarningDom("ipcam") << "Built without libjpeg: camera pictures are sent in full resolution";
        break;
    case Fallback::Undecodable:
        cWarningDom("ipcam") << "Camera frame cannot be resized: sent in full resolution";
        break;
    case Fallback::Busy:
        cWarningDom("ipcam") << "Too many pictures to resize: sent in full resolution";
        break;
    case Fallback::NoConvert:
        cWarningDom("ipcam") << "calaos_picture cannot resize (ImageMagick): sent in full resolution";
        break;
    }
}

void PictureResizer::resetForTest(Decoder substitute)
{
    decoder = substitute ? std::move(substitute) : JpegThumbnail::resize;
    started = 0;
    reported.clear();
}
