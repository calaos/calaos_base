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
#ifndef S_JpegThumbnail_H
#define S_JpegThumbnail_H

#include <string>

/* Pure function, no logging: it runs on a thread pool thread, over bytes that
 * come straight from a camera and are not trusted. */
namespace JpegThumbnail
{

enum class Status
{
    Resized,     // data holds the thumbnail
    Unchanged,   // no gain: the source is not wider, or is already smaller
    Failed,      // not a JPEG we accept: malformed, truncated, too big
    Unavailable, // built without libjpeg
};

struct Result
{
    Status status = Status::Failed;
    std::string data;
};

/* Fit the source into `width` columns, keeping its ratio. Never upscales. */
Result resize(const std::string &jpeg, int width);

}

#endif
