/*
 * mbus_rqst.c - request / response routines
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
 * $Id: mbus_rqst.c,v 1.1.1.1 2003/09/16 08:33:33 kapyar Exp $
 */

#include "mbus.h"

extern int mbus_sock_read(int sd, mbus_ubyte *buf, int len, int timeout);
extern int mbus_sock_write(int sd, mbus_ubyte *buf, int len, int timeout);

/* prototypes */
int mbus_rqst(mbus_struct *mbus, mbus_ubyte len);

/*
 * Description:
 *   Send request to mbusd server and receive response
 * Parameters:
 *   mbus - pointer to mbus structure (must be initialized first!);
 *   len - request length
 * Return:
 *    0 if response successfully received;
 *   -1 in case of error.
 */
int
mbus_rqst(mbus_struct *mbus, mbus_ubyte len)
{
  /* header fields initialization */
  memset((void *)mbus->buf, 0, MBUS_HDR_LEN);
  MBUS_HDR(mbus->buf, MBUS_LENGTH_L) = len;

  /* send request to the server */
  if (mbus_sock_write(mbus->sd, mbus->buf,
                      MBUS_HDR_LEN + len,
                      mbus->timeout) < (MBUS_HDR_LEN + len))
  {
    DBG(__FILE__, __LINE__,
        "mbus_sock_write(): can't write request");
    return -1;
  }

  /* receive response header from server */
  if (mbus_sock_read(mbus->sd, mbus->buf,
                     MBUS_HDR_LEN,
                     mbus->timeout) < MBUS_HDR_LEN)
  {
    DBG(__FILE__, __LINE__,
        "mbus_sock_read(): can't read response header");
    return -1;
  }

  /* receive response body from server */
  if (mbus_sock_read(mbus->sd,
                     mbus->buf + MBUS_HDR_LEN,
                     MBUS_HDR(mbus->buf, MBUS_LENGTH_L),
                     mbus->timeout) < MBUS_HDR(mbus->buf, MBUS_LENGTH_L))
  {
    DBG(__FILE__, __LINE__,
        "mbus_sock_read(): can't read response body");
    return -1;
  }

  /* response received successfully */
  return 0;
}
