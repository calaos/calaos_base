/*
 * mbus_conf.h - configuration header file for mbus library
 *
 * Copyright (c) 2003, Victor Antonovich (avmlink@vlink.ru)
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $Id: mbus_conf.h,v 1.1.1.1 2003/09/16 08:33:33 kapyar Exp $
 */
 
#ifndef _MBUS_CONF_H
#define _MBUS_CONF_H

#include <sys/types.h>

#ifdef LIBMBUS

#include <sys/socket.h> /* connect(), setsockopt(), socket() */
#include <netinet/in.h>
#include <arpa/inet.h>  /* inet_addr() */
#include <sys/time.h>   /* struct timeval */
#include <netdb.h>      /* gethostbyname() */
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>     /* read(), write(), select(), close() */
#include <string.h>     /* bzero(), strncpy() */
#include <stdlib.h>     /* free(), malloc() */
#include <stdio.h>      /* fprintf() */

#endif

/*
 * General types
 */
typedef unsigned long mbus_ulong;
typedef long mbus_long;
typedef unsigned short mbus_uword;
typedef short mbus_word;
typedef unsigned char mbus_ubyte;

/* Default socket timeout (in seconds) */
#define MBUS_SOCK_TIMEOUT 2

/* Host name maximum length */
#define MBUS_HOSTNAME_LENGTH 255

/* Maximum MODBUS address value */
#define MBUS_ADDR_MAX 247

#endif /* _MBUS_CONF_H */
