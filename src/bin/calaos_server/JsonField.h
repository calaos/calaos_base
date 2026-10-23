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
#ifndef S_JsonField_H
#define S_JsonField_H

#include "Utils.h"

namespace JsonField
{

//The string member key of j, the default when j is not an object or the
//member is absent or not a string. The copies named jsonStringGet() in the
//JsonApi files have the same contract and can fold into this one.
inline std::string string(const Json &j, const char *key, const std::string &defaultValue = std::string())
{
    if (!j.is_object())
        return defaultValue;

    const Json::const_iterator it = j.find(key);
    if (it == j.cend() || !it->is_string())
        return defaultValue;

    return it->get<std::string>();
}

} //namespace JsonField

#endif
