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
#ifndef S_CONFIGTUI_H
#define S_CONFIGTUI_H

#include <string>

/* Facade of the full screen configuration browser.
 * ================================================
 *
 * This header must never include CppTui.h: cpptui.hpp is 18k lines and costs
 * about +10 s of compile time and +650 MB of compiler memory per translation
 * unit. calaos_config.cpp only needs the entry point below, and pays nothing
 * for the widgets.
 */

namespace Calaos
{

//Mirrors ConfigCli::ColorMode, so that this header stays free of any include
enum class TuiColorMode { Auto, Always, Never };

/* Runs the browser until the user leaves it and returns the exit code of
 * calaos_config: 0 when it ran, 1 when the terminal cannot host it.
 *
 * configFile is the local_config.xml to work on; empty means the one Utils
 * resolved. The caller has already checked that stdin and stdout are a
 * terminal, resolved --config/--cache and set the locale up.
 *
 * The terminal is restored on every way out of this call: normal exit,
 * exception, and SIGTERM/SIGINT/SIGHUP.
 */
int runConfigTui(const std::string &configFile, TuiColorMode color);

}

#endif /* S_CONFIGTUI_H */
