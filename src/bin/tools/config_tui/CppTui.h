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
#ifndef S_CPPTUI_H
#define S_CPPTUI_H

/* The one and only place where the vendored cpp-tui header is included.
 * ====================================================================
 *
 * cpptui.hpp is 18k lines and produces 30 warnings with the warning flags of
 * the project (29 -Wshadow, 1 -Wunused-variable). They are silenced here, for
 * that header only, instead of weakening the flags of our own code.
 *
 * It also costs roughly +10 s of compile time and +650 MB of compiler memory
 * per translation unit that includes it, so only ConfigRow.cpp and
 * ConfigTui.cpp may include this file. ConfigTui.h, the facade the rest of
 * calaos_config talks to, deliberately does not.
 */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-variable"
#include <cpptui.hpp>
#pragma GCC diagnostic pop

#endif /* S_CPPTUI_H */
