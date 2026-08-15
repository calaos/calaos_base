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
#ifndef CALAOS_CONSTANTS_H
#define CALAOS_CONSTANTS_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

//-----------------------------------------------------------------------------
// Some common defines
//-----------------------------------------------------------------------------
#define PREFIX_CONFIG_PATH      ETC_DIR"/calaos"
#define ETC_CONFIG_PATH         "/etc/calaos"
#define HOME_CONFIG_PATH        ".config/calaos"
#define HOME_CACHE_PATH         ".cache/calaos"

#define LOCAL_CONFIG            "local_config.xml"
#define IO_CONFIG               "io.xml"
#define RULES_CONFIG            "rules.xml"
#define WIDGET_CONFIG           "widgets.xml"

#define DEFAULT_URL             "http://update.calaos.fr/fwupdate.xml"
#define CALAOS_NETWORK_URL      "https://www.calaos.fr/calaos_network"
#define CALAOS_WEBSITE_URL      "http://www.calaos.fr"
#define CALAOS_CONTACT_EMAIL    "contact@calaos.fr"
#define CALAOS_COPYRIGHT_TEXT   "Copyright (c) 2006-2026, Calaos. All Rights Reserved."
#define ZONETAB                 "/usr/share/zoneinfo/zone.tab"
#define CURRENT_ZONE            "/etc/timezone"
#define LOCALTIME               "/etc/localtime"
#define ZONEPATH                "/usr/share/zoneinfo/"

// The size of the window. For now The Calaos touchscreen gui is only designed
// to fit a screen of 1024x768 pixels.
#define WIDTH   1024
#define HEIGHT   768
//-----------------------------------------------------------------------------
#define PI 3.14159265358979323846
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
#define RED "\x1b[31;01m"
#define DARKRED "\x1b[31;06m"
#define RESET "\x1b[0m"
#define GREEN "\x1b[32;06m"
#define YELLOW "\x1b[33;06m"

#define WAGO_LISTEN_PORT        4646
#define BCAST_UDP_PORT          4545
#define JSONAPI_PORT            5454

#define WAGO_KNX_START_ADDRESS          6144
#define WAGO_841_START_ADDRESS          4096

#endif
