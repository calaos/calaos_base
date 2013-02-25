/*
 * mbus.h - general header file for mbus library
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
 * $Id: mbus.h,v 1.1.1.1 2003/09/16 08:33:28 kapyar Exp $
 */

#ifndef _MBUS_H
#define _MBUS_H

#include "mbus_conf.h"

#define LIBMBUS_VERSION "0.1.1"

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef LIBMBUS

#ifndef DEBUG
/* Uncommenting this line forces debug output */
/* #define DEBUG */
#endif

#ifdef DEBUG
#define DBG(a, b, c) fprintf(stderr, \
                     "libmbus: " c " (file %s, line %d)\n", a, b)
#else
#define DBG(a, b, c) do {} while (0) /* do nothing */
#endif

#endif

/*
 * Macros for access header OpenMODBUS/TCP packet
 *   p - pointer to the packet,
 *   d - displacement in the header
 */
#define MBUS_HDR(p, d) (*(p + d))

/*
 * OpenMODBUS/TCP packet structure
 */
#define MBUS_TRANS_ID_H 0    /* transaction ID high byte */
#define MBUS_TRANS_ID_L 1    /* transaction ID low byte */
#define MBUS_PROTO_ID_H 2    /* protocol ID high byte */
#define MBUS_PROTO_ID_L 3    /* protocol ID low byte */
#define MBUS_LENGTH_H   4    /* length field high byte */
#define MBUS_LENGTH_L   5    /* length field low byte */
#define MBUS_UNIT_ID    6    /* unit identifier */
#define MBUS_FCODE      7    /* function code */
#define MBUS_DATA       8    /* MODBUS data */
#define MBUS_HDR_LEN    6    /* OpenMODBUS/TCP header length */
#define MBUS_DATA_LEN   254  /* OpenMODBUS/TCP data length */

/* flag values */
#define MBUS_FL_FREE_ON_EXIT 0x01 /* free structure on exit */
#define MBUS_FL_IS_INITIALIZED 0x1eac /* is structure initialized */

typedef struct mbus_t
{
  int sd; /* link socket descriptor */
  char host_addr[MBUS_HOSTNAME_LENGTH + 1]; /* mbusd server hostname */
  mbus_uword host_port; /* mbusd server IP port */
  mbus_uword host_flags; /* host connection flags */
  int timeout;      /* socket timeout value, seconds */
  int flags; /* internal flags */
  int is_initialized; /* initialization flag */
  mbus_ubyte ex_code; /* last query exception code */
  mbus_ubyte buf[MBUS_HDR_LEN + MBUS_DATA_LEN];
} mbus_struct;

char *mbus_version(mbus_struct *mbus);
mbus_struct *mbus_init(mbus_struct *mbus);
int mbus_connect(mbus_struct *mbus, const char *host_addr,
                 mbus_uword host_port, mbus_uword flags);
int mbus_reconnect(mbus_struct *mbus);
int mbus_close(mbus_struct *mbus);
mbus_ubyte mbus_excode(mbus_struct *mbus);
int mbus_connected(mbus_struct *mbus);
int mbus_free(mbus_struct *mbus);

int mbus_cmd_read_coil_status(mbus_struct *mbus, mbus_ubyte slave_addr,
      mbus_uword coils_addr, mbus_uword coils_num, mbus_ubyte *coils_data);
int mbus_cmd_read_holding_registers(mbus_struct *mbus,
      mbus_ubyte slave_addr, mbus_uword start_addr,
      mbus_uword points_num, mbus_uword *data);
int mbus_cmd_force_single_coil(mbus_struct *mbus,
      mbus_ubyte slave_addr, mbus_uword coil_addr, mbus_uword data);
int mbus_cmd_preset_single_register(mbus_struct *mbus,
      mbus_ubyte slave_addr, mbus_uword register_addr,
      mbus_uword preset_data);
int mbus_cmd_diagnostics(mbus_struct *mbus,
      mbus_ubyte address, mbus_uword subfunction, mbus_uword *data);
int mbus_cmd_force_multiple_coils(mbus_struct *mbus, mbus_ubyte slave_addr,
      mbus_uword coils_addr, mbus_uword coils_num, mbus_ubyte *coils_data);
int mbus_cmd_report_slave_id(mbus_struct *mbus,
      mbus_ubyte slave_addr, mbus_ubyte *data_count, mbus_ubyte *slave_data);
int mbus_cmd_preset_multiple_registers(mbus_struct *mbus,
      mbus_ubyte slave_addr, mbus_uword start_addr,
      mbus_uword points_num, mbus_uword *data);

#ifdef __cplusplus
}
#endif

#endif /* _MBUS_H */
