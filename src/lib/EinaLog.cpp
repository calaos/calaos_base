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
#include "EinaLog.h"
#include "Utils.h"

namespace efl
{
namespace eina
{
namespace log
{

LogStream::~LogStream()
{
    //Only add function name when CALAOS_DEBUG is set
    if (!getenv("CALAOS_DEBUG"))
    {
        logData->function = string();
    }

    //Strip away file path and only keep filename
    vector<string> v;
    Utils::split(logData->file, v, "/");
    if (v.size() > 0)
        logData->file = v[v.size() - 1];

    if (logData->domain == -1 &&
            logData->level == EINA_LOG_LEVEL_UNKNOWN)
        std::cout << logData->file << ":" << logData->line << " "
                  << logData->function << ": "
                  << logData->stream.str()
                  << std::endl;
    else
        eina_log_print(logData->domain,
                       logData->level,
                       logData->file.c_str(),
                       logData->function.c_str(),
                       logData->line,
                       "%s",
                       logData->stream.str().c_str());

    delete logData;
}

}
}
}
