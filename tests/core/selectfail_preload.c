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

/*
 * Makes select() answer EBADF inside a SHIPPED sidecar, so that the exit
 * status its parent collects can be measured on a main loop that really
 * failed.
 *
 * No sidecar but calaos_mqtt registers a descriptor of its own, so the only
 * descriptor their select() watches is the socket to the server - and the
 * server has no way to hand it a closed one. Without this interposition the
 * failing branch of ExternProcClient::run() is unreachable from outside the
 * process, which is what left it untested.
 *
 * The injection ANNOUNCES itself on stderr: an interposition that silently
 * fails to load would otherwise leave a sidecar that simply keeps running,
 * and a case cannot tell that apart from one that measured nothing.
 */

#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

typedef int (*select_fn)(int, fd_set *, fd_set *, fd_set *, struct timeval *);

static const char kMarker[] = "calaos-selectfail: EBADF injected\n";

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
           struct timeval *timeout)
{
    static select_fn real = NULL;
    static long allowed = -1;
    static long seen = 0;

    if (allowed < 0)
    {
        const char *v = getenv("CALAOS_SELECT_FAIL_AFTER");
        allowed = v? strtol(v, NULL, 10): 0;
    }

    if (seen++ >= allowed)
    {
        ssize_t ignored = write(2, kMarker, sizeof(kMarker) - 1);
        (void)ignored;
        errno = EBADF;
        return -1;
    }

    if (!real)
        real = (select_fn)dlsym(RTLD_NEXT, "select");
    if (!real)
    {
        errno = ENOSYS;
        return -1;
    }

    return real(nfds, readfds, writefds, exceptfds, timeout);
}
