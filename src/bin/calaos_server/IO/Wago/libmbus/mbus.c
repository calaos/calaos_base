/*
 * mbus.c - general purpose libmbus functions
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
 * $Id: mbus.c,v 1.1.1.1 2003/09/16 08:33:28 kapyar Exp $
 */

#include "mbus.h"

extern int mbus_sock_create_client(const char *server_addr,
             mbus_uword server_port, int blkmode);

#define MBUS_CHECK_INIT(p) \
   do { if ((p)->is_initialized != MBUS_FL_IS_INITIALIZED) \
        return -1; } while (0);

/*
 * Get library version info
 *
 * Returns: pointer to library version internal string.
 */
char *
mbus_version
(
  mbus_struct *mbus /* pointer to MODBUS structure */
)
{
  mbus = mbus; /* prevent compiler warning */
  return ("\n libmbus version " LIBMBUS_VERSION "\n"
          " Copyright (c) 2003 Victor Antonovich\n");
}

/*
 * Initialization of MBUS structure
 *
 * Returns: Pointer to structure (NULL in case of error).
 *
 * Note: This function must be called before any call to
 *       the library just once for each MBUS stucture
 */
mbus_struct *
mbus_init
(
  mbus_struct *mbus /* Pointer to structure to be initialized
                       (if NULL - the memory will be allocated
                       for the structure before initializing) */
)
{
  if (mbus == NULL)
  { /* mbus structure yet isn't exist, create it */
    if ((mbus =
           (mbus_struct *)malloc(sizeof(mbus_struct))) != NULL)
    { /* initialize created structure */
      memset((void *)mbus, 0, sizeof(mbus_struct));
      mbus->flags |= MBUS_FL_FREE_ON_EXIT;
    }
    else return NULL;
  }
  else
    mbus->flags = 0; /* zero all flags */
  mbus->sd = -1;
  mbus->timeout = MBUS_SOCK_TIMEOUT;
  mbus->is_initialized = MBUS_FL_IS_INITIALIZED;
  /* initialized successfully */
  return mbus;
}

/*
 * Connect to mbusd server
 *
 * Returns: 0 if successfully connected;
 *         -1 in case of error.
 */
int
mbus_connect
(
  mbus_struct *mbus,     /* Pointer to MBUS structure */
  const char *host_addr, /* mbusd server host address (DNS or canonical form) */
  mbus_uword host_port,  /* mbusd IP port (1-65535) */
  mbus_uword flags       /* Additional flags (isn't used in this version) */
)
{
  MBUS_CHECK_INIT(mbus);
  if (mbus->sd >= 0)
    return mbus_reconnect(mbus);
  (void)strncpy(mbus->host_addr, host_addr, MBUS_HOSTNAME_LENGTH);
  mbus->host_port = host_port;
  mbus->flags = flags;
  mbus->sd =
    mbus_sock_create_client(mbus->host_addr, mbus->host_port, 0);
  return (mbus->sd == -1) ? -1 : 0;
}

/*
 * Reconnect to mbusd server
 *
 * Returns: 0 if successfully reconnected;
 *         -1 in case of error.
 */
int
mbus_reconnect
(
  mbus_struct *mbus /* Pointer to MBUS structure */
)
{
  MBUS_CHECK_INIT(mbus);
  if (mbus->sd < 0)
    return -1; /* not connected yet */
  close(mbus->sd);
  mbus->sd =
    mbus_sock_create_client(mbus->host_addr, mbus->host_port, 0);
  return (mbus->sd == -1) ? -1 : 0;
}

/*
 * Disconnect from mbusd server
 *
 * Returns: 0 if connection successfully closed;
 *         -1 in case of error.
 */
int
mbus_close
(
  mbus_struct *mbus /* Pointer to MBUS structure */
)
{
  MBUS_CHECK_INIT(mbus);
  if (mbus->sd >= 0)
    close(mbus->sd);
  mbus->sd = -1;
  return 0;
}

int mbus_free(mbus_struct *mbus)
{
        MBUS_CHECK_INIT(mbus);
        if (mbus->flags & MBUS_FL_FREE_ON_EXIT)
                free((void *)mbus);
        return 0;
}

/*
 * Get last request exception code
 *
 * Returns: Exception code (0-255).
 */
mbus_ubyte
mbus_excode
(
  mbus_struct *mbus /* Pointer to MBUS structure */
)
{
  MBUS_CHECK_INIT(mbus);
  return mbus->ex_code;
}

/*
 * Get connection status code
 *
 * Returns: -1 if structure is not initialized;
 *           0 if connection is not established;
 *           1 if successfully connected.
 */
int
mbus_connected
(
  mbus_struct *mbus /* Pointer to MBUS structure */
)
{
  MBUS_CHECK_INIT(mbus);
  return (mbus->sd >= 0) ? 1 : 0;
}
