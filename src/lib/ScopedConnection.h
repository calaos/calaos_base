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
#ifndef CALAOS_SCOPED_CONNECTION_H
#define CALAOS_SCOPED_CONNECTION_H

#include <sigc++/sigc++.h>

/* A sigc::connection that is cut when its holder dies.
 *
 * For an object that connects to a signal of something that outlives it (a
 * controller shared by all the IOs of a host): sigc::trackable only covers
 * sigc::mem_fun, never a lambda capturing `this`. Harmless when the signal is
 * already gone, sigc empties the connection when its slot is destroyed. */
class ScopedConnection
{
public:
    ScopedConnection() = default;
    ScopedConnection(const ScopedConnection &) = delete;
    ScopedConnection &operator=(const ScopedConnection &) = delete;
    ~ScopedConnection() { conn.disconnect(); }

    ScopedConnection &operator=(const sigc::connection &c)
    {
        conn.disconnect();
        conn = c;
        return *this;
    }

    bool connected() const { return conn.connected(); }

private:
    sigc::connection conn;
};

#endif
