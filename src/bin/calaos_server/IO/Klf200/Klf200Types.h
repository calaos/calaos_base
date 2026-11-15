/******************************************************************************
 **  Copyright (c) 2026, Calaos. All Rights Reserved.
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
#ifndef S_KLF200_TYPES_H
#define S_KLF200_TYPES_H

#include <string>
#include <utility>

/*
 * Strong types for the three values that identify a VELUX node on the wire
 * between Klf200Ctrl and the calaos_klf200 external process. Same rationale as
 * IO/Reolink/ReolinkTypes.h: host and password are two adjacent strings on
 * every call that carries them, and a caller that swaps them compiles in
 * silence and sends the password in clear in the host field. With distinct
 * types the swap does not compile.
 */
namespace Klf200Types
{

struct Host
{
    std::string v;
    explicit Host(std::string s):
        v(std::move(s))
    {
    }
};

struct Password
{
    std::string v;
    explicit Password(std::string s):
        v(std::move(s))
    {
    }
};

struct NodeId
{
    int v;
    explicit NodeId(int n):
        v(n)
    {
    }
};

} //namespace Klf200Types

#endif
