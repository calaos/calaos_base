/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
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
#ifndef S_ServerInfo_H
#define S_ServerInfo_H

#include "config.h"
#include "json.hpp"

#include <string>
#include <vector>

namespace Calaos
{

/* What `get_server_info` tells a client. The capability list is the single
 * place to extend when a change alters what a client may do: a client treats
 * a missing name, or a server that does not answer, as an older server.
 */
namespace ServerInfo
{

//Bump only for an incompatible change of the wire protocol.
constexpr int ProtocolVersion = 1;

//Keep in step with the capability table of docs/08_http_api.md (get_server_info).
inline const std::vector<std::string> &capabilities()
{
    static const std::vector<std::string> caps = {
        "camera_token",                //WS camera_token + &camera_token= on camera/get_picture
        "admin_relay",                 //WS admin_unlock, admin_set, admin
        "admin_events",                //WS admin_event pushes
        "console",                     //WS /api/console tunnel
        "camera_width",                //optional width on camera/get_picture
        "change_cred_closes_sessions", //change_cred ends the other household sessions, consoles and get_video streams
    };
    return caps;
}

//Every value is a string, like the rest of the wire (docs/08_http_api.md).
inline nlohmann::json toJson()
{
    return nlohmann::json{ { "version", PACKAGE_VERSION },
                           { "protocol", std::to_string(ProtocolVersion) },
                           { "capabilities", capabilities() } };
}

} //namespace ServerInfo
} //namespace Calaos

#endif
