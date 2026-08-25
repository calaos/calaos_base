/*
 * mbus_cmd.c - MODBUS commands routines
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
 * $Id: mbus_cmd.c,v 1.1.1.1 2003/09/16 08:33:33 kapyar Exp $
 */

#include "mbus.h"

#define MBUS_BYTE_WR(p, b) \
          (*(p++) = (mbus_ubyte)(b))
#define MBUS_BYTE_RD(p) \
          ((mbus_ubyte)*(p++))
#define MBUS_WORD_WR(p, w) \
          do \
          { MBUS_BYTE_WR(p, ((w) >> 8) & 0xff); \
            MBUS_BYTE_WR(p, ((w) & 0xff)); } \
          while (0)
#define MBUS_WORD_RD(p, w) \
          do \
          { w = ((mbus_uword)*(p++) << 8) & 0xff00; \
            w |= *(p++) & 0xff; } \
          while (0)

#define MBUS_CHECK_ADDR(a, m) \
   if ((m && !(a)) || (a) > MBUS_ADDR_MAX) return -1

#define MBUS_FC_READCOILSTATUS 1
#define MBUS_FC_READINPUTSTATUS 2
#define MBUS_FC_READHOLDINGREGISTERS 3
#define MBUS_FC_READINPUTREGISTERS 4
#define MBUS_FC_FORCESINGLECOIL 5
#define MBUS_FC_PRESETSINGLEREGISTER 6
#define MBUS_FC_READEXCEPTIONSTATUS 7
#define MBUS_FC_DIAGNOSTICS 8
#define MBUS_FC_PROGRAM484 9
#define MBUS_FC_POLL484 10
#define MBUS_FC_FETCHCOMMEVENTCTR 11
#define MBUS_FC_FETCHCOMMEVENTLOG 12
#define MBUS_FC_PROGRAMCONTROLLER 13
#define MBUS_FC_POLLCONTROLLER 14
#define MBUS_FC_FORCEMULTIPLECOILS 15
#define MBUS_FC_PRESETMULTIPLEREGISTERS 16
#define MBUS_FC_REPORTSLAVEID 17

extern int mbus_rqst(mbus_struct *mbus, mbus_ubyte len);
extern void dump(mbus_ubyte *buf, mbus_ubyte len);

/* internal functions prototypes */
int mbus_cmd_check_response(mbus_struct *mbus,
      mbus_ubyte slave_addr, mbus_ubyte funct_code);
int mbus_cmd_nodata(mbus_struct *mbus,
      mbus_ubyte slave_addr, mbus_ubyte funct_code);
int mbus_cmd_addr_wdata(mbus_struct *mbus,
      mbus_ubyte slave_addr, mbus_ubyte funct_code,
      mbus_uword addr, mbus_uword data);
int mbus_cmd_addr_mdata(mbus_struct *mbus,
      mbus_ubyte slave_addr, mbus_ubyte funct_code,
      mbus_uword addr, mbus_ubyte *data,
      mbus_ubyte data_size, mbus_word data_count);
int mbus_cmd_bytecount_ok(mbus_struct *mbus,
      int byte_count, int expected);

/*
 * A byte count taken from a RESPONSE is a claim made by the peer, bounded by
 * 255 and by nothing the caller allocated - and MODBUS/TCP is unauthenticated.
 * Two facts are demanded, because neither implies the other: the count is the
 * one the request implies (`expected` < 0 when the request states none), and
 * it fits inside the body actually received (2040 coils legitimately need 255
 * bytes, of which only 251 fit). A contradicting count is refused, not
 * clamped: clamping would hand the caller bits that never arrived, and every
 * caller here already treats -1 as "keep the previous value".
 *
 * Returns: 1 if the byte count may be trusted, 0 if it may not.
 */
int
mbus_cmd_bytecount_ok
(
  mbus_struct *mbus, /* Pointer to MBUS structure, holding the response */
  int byte_count,    /* Byte count announced by the response */
  int expected       /* Byte count the request implies, or -1 if it states none */
)
{
  /* Bytes of the response body not yet consumed: the length announced in the
   * header, less the unit id, the function code and the byte count itself. */
  int body_left = (int)MBUS_HDR(mbus->buf, MBUS_LENGTH_L) - 3;

  if (byte_count <= 0)
    return 0; /* not a count */
  if (expected >= 0 && byte_count != expected)
    return 0; /* not the count that was asked for */
  if (byte_count > body_left)
    return 0; /* not inside the frame that arrived */
  return 1;
}

/*
 * Check MODBUS response for errors and exception
 *
 * Returns: 1 if exception code returned,
 *          0 if response received successfully,
 *         -1 in case of error.
 */
int
mbus_cmd_check_response
(
  mbus_struct *mbus,     /* Pointer to MBUS structure */
  mbus_ubyte slave_addr, /* MODBUS device address (1-247) */
  mbus_ubyte funct_code  /* Function code (1-255) */
)
{
  mbus_ubyte resp_fc;
  mbus_ubyte *bufptr = mbus->buf + MBUS_HDR_LEN;
  if (MBUS_BYTE_RD(bufptr) != slave_addr)
    return -1; /* error, returning */
  if ((resp_fc =
         MBUS_BYTE_RD(bufptr)) != funct_code)
  { /* checking for exception */
    if (resp_fc == (funct_code | 0x80))
    { /* report about exception */
      mbus->ex_code = MBUS_BYTE_RD(bufptr);
      return 1;
    }
    else return -1; /* response is incorrect */
  };
  mbus->ex_code = 0;
  return 0;
}

/*
 * Make OpenMODBUS request witout data field
 *
 * Returns: 1 if exception code returned,
 *          0 if response received successfully,
 *         -1 in case of error.
 */
int
mbus_cmd_nodata
(
  mbus_struct *mbus,     /* Pointer to MBUS structure */
  mbus_ubyte slave_addr, /* MODBUS device address (0-247) */
  mbus_ubyte funct_code  /* Function code (1-255) */
)
{
  mbus_ubyte *bufptr = mbus->buf + MBUS_HDR_LEN;

  /* make body request */
  MBUS_BYTE_WR(bufptr, slave_addr);
  MBUS_BYTE_WR(bufptr, funct_code);

  /* send request to the server and receive response */
  return (mbus_rqst(mbus, 2) < 0) ?
    -1 : mbus_cmd_check_response(mbus, slave_addr, funct_code);
}

/*
 * Make OpenMODBUS request with address and one word data
 *
 * Returns: 1 if exception code returned,
 *          0 if response received successfully,
 *         -1 in case of error.
 */
int
mbus_cmd_addr_wdata
(
  mbus_struct *mbus,     /* Pointer to MBUS structure */
  mbus_ubyte slave_addr, /* MODBUS device address (0-247) */
  mbus_ubyte funct_code, /* Function code (1-255) */
  mbus_uword addr,       /* Data address (0-65535) */
  mbus_uword data        /* Query data (0-65535) */
)
{
  mbus_ubyte *bufptr = mbus->buf + MBUS_HDR_LEN;

  /* make body request */
  MBUS_BYTE_WR(bufptr, slave_addr);
  MBUS_BYTE_WR(bufptr, funct_code);
  MBUS_WORD_WR(bufptr, addr);
  MBUS_WORD_WR(bufptr, data);

  /* send request to the server and receive response */
  return (mbus_rqst(mbus, 6) < 0) ?
    -1 : mbus_cmd_check_response(mbus, slave_addr, funct_code);
}

/*
 * Make OpenMODBUS request with address and multiple word data
 *
 * Returns: 1 if exception code returned,
 *          0 if response received successfully,
 *         -1 in case of error.
 */
int
mbus_cmd_addr_mdata
(
  mbus_struct *mbus,     /* Pointer to MBUS structure */
  mbus_ubyte slave_addr, /* MODBUS device address (0-247) */
  mbus_ubyte funct_code, /* Function code (1-255) */
  mbus_uword addr,       /* Data address (0-65535) */
  mbus_ubyte *data,      /* Pointer to the query data */
  mbus_ubyte data_size,  /* 0 for coils, non-zero for registers */
  mbus_word data_count   /* Number of data values */
)
{
  mbus_ubyte byte_count, counter;
  mbus_ubyte *bufptr = mbus->buf + MBUS_HDR_LEN;
  mbus_uword word_buf;

  /* make request body */
  MBUS_BYTE_WR(bufptr, slave_addr);
  MBUS_BYTE_WR(bufptr, funct_code);
  MBUS_WORD_WR(bufptr, addr);
  MBUS_WORD_WR(bufptr, data_count);
  if (data_size)
    /* each data value is 16 bit length */
    byte_count = (mbus_ubyte)((data_count * 2) & 0xff);
  else
    /* each byte consist from max. eigth coils */
    byte_count = (mbus_ubyte)((data_count + 7) >> 3);
  MBUS_BYTE_WR(bufptr, byte_count);
  /* copy data to request */
  counter = byte_count;
  if (data_size)
  { /* 16-bit wide data */
    counter >>= 1;
    while (counter--)
    { /* copy data to buffer */
      word_buf = *((mbus_uword *)data);
      MBUS_WORD_WR(bufptr, word_buf);
      data += 2;
    }
  }
  else
    /* 8-bit wide data */
    while (counter--)
      MBUS_BYTE_WR(bufptr, *data++);

  /* send request to the server and receive response */
  return (mbus_rqst(mbus, 7 + byte_count) < 0) ?
    -1 : mbus_cmd_check_response(mbus, slave_addr, funct_code);
}

/*
 * MODBUS command - Function Code 01 (Read Coil Status)
 *
 * Returns: 1 if exception code returned,
 *          0 if response received successfully,
 *         -1 in case of error.
 */
int
mbus_cmd_read_coil_status
(
  mbus_struct *mbus,     /* Pointer to MBUS structure */
  mbus_ubyte slave_addr, /* MODBUS device address (1-247) */
  mbus_uword coils_addr, /* Coils address (0-65535) */
  mbus_uword coils_num,  /* Number of coils to read (0-65535) */
  mbus_ubyte *coils_data /* Pointer to readed data buffer */
)
{
  int rc, expected;
  mbus_ubyte *bufptr = mbus->buf + MBUS_DATA, byte_count;

  MBUS_CHECK_ADDR(slave_addr, 1);
  /* make request */
  if ((rc = mbus_cmd_addr_wdata(mbus, slave_addr,
                                MBUS_FC_READCOILSTATUS,
                                coils_addr, coils_num)) != 0)
    return rc;

  /* take bytecount */
  byte_count = MBUS_BYTE_RD(bufptr);
  /* One byte per eight coils asked for and nothing else: `coils_data` was
   * sized on coils_num, never on this response. Above 255 the request has no
   * representable answer at all. */
  expected = ((int)coils_num + 7) / 8;
  if (expected > 255 || !mbus_cmd_bytecount_ok(mbus, (int)byte_count, expected))
    return -1; /* invalid value of bytecount */
  /* copy received data */
  while (byte_count--)
    MBUS_BYTE_WR(coils_data, *bufptr++);
  return 0;
}

/*
 * MODBUS command - Function Code 03 (Read Holding Registers)
 *
 * Returns: 1 if exception code returned,
 *          0 if response received successfully,
 *         -1 in case of error.
 */
int
mbus_cmd_read_holding_registers
(
  mbus_struct *mbus,     /* Pointer to MBUS structure */
  mbus_ubyte slave_addr, /* MODBUS device address (1-247) */
  mbus_uword start_addr, /* Starting register address (0-65535) */
  mbus_uword points_num, /* Quantity of registers to read (1-65535) */
  mbus_uword *data       /* Pointer to readed data buffer */
)
{
  int rc, expected;
  mbus_ubyte *bufptr = mbus->buf + MBUS_DATA, byte_count, data_count;
  mbus_uword word_buf;

  MBUS_CHECK_ADDR(slave_addr, 1);
  /* make request */
  if ((rc = mbus_cmd_addr_wdata(mbus, slave_addr,
                                MBUS_FC_READHOLDINGREGISTERS,
                                start_addr, points_num)) != 0)
    return rc;

  /* take bytecount */
  byte_count = MBUS_BYTE_RD(bufptr);
  /* Two bytes per register asked for. The RAW count is what must be confronted
   * with the request, not the halved one the copy loop uses: 7 / 2 == 3, so a
   * check written on the halved value accepts an odd byte count no slave can
   * produce and reads a byte that is not there. */
  expected = (int)points_num * 2;
  if (expected > 255 || !mbus_cmd_bytecount_ok(mbus, (int)byte_count, expected))
    return -1; /* invalid value of bytecount */
  data_count = (mbus_ubyte)(byte_count / 2);
  /* copy received data */
  while (data_count--)
  {
    MBUS_WORD_RD(bufptr, word_buf);
    *data++ = word_buf;
  }
  return 0;
}

/*
 * MODBUS command - Function Code 05 (Force Single Coil)
 *
 * Returns: 1 if exception code returned,
 *          0 if response received successfully,
 *         -1 in case of error.
 */
int
mbus_cmd_force_single_coil
(
  mbus_struct *mbus,     /* Pointer to MBUS structure */
  mbus_ubyte slave_addr, /* MODBUS device address (0-247) */
  mbus_uword coil_addr,  /* Coil address (0-65535) */
  mbus_uword data        /* Force data */
)
{
  int rc;
  mbus_uword resp_data;
  mbus_ubyte *bufptr = mbus->buf + MBUS_DATA;

  MBUS_CHECK_ADDR(slave_addr, 0);
  /* make request */
  if((rc = mbus_cmd_addr_wdata(mbus, slave_addr,
                               MBUS_FC_FORCESINGLECOIL,
                               coil_addr, data)) != 0)
    return rc;

  /* check response coil address */
  MBUS_WORD_RD(bufptr, resp_data);
  if (resp_data!= coil_addr)
    return -1; /* coil_address is incorrect */
  /* check response data */
  MBUS_WORD_RD(bufptr, resp_data);
  return (resp_data == data) ? 0 : -1;
}

/*
 * MODBUS command - Function Code 06 (Preset Single Register)
 *
 * Returns: 1 if exception code returned,
 *          0 if response received successfully,
 *         -1 in case of error.
 */
int
mbus_cmd_preset_single_register
(
  mbus_struct *mbus,        /* Pointer to MBUS structure */
  mbus_ubyte slave_addr,    /* MODBUS device address (0-247) */
  mbus_uword register_addr, /* Starting register address (0-65535) */
  mbus_uword preset_data    /* Register preset data (0-65535) */
)
{
  int rc;
  mbus_uword resp_data;
  mbus_ubyte *bufptr = mbus->buf + MBUS_DATA;

  MBUS_CHECK_ADDR(slave_addr, 0);
  /* make request */
  if((rc = mbus_cmd_addr_wdata(mbus, slave_addr,
                               MBUS_FC_PRESETSINGLEREGISTER,
                               register_addr, preset_data)) != 0)
    return rc;

  /* check response register address */
  MBUS_WORD_RD(bufptr, resp_data);
  if (resp_data!= register_addr)
    return -1; /* register address is incorrect */
  /* check response data */
  MBUS_WORD_RD(bufptr, resp_data);
  return (resp_data == preset_data) ? 0 : -1;
}

/*
 * MODBUS command - Function Code 08 (Diagnostics)
 *
 * Returns: 1 if exception code returned,
 *          0 if response received successfully,
 *         -1 in case of error.
 */
int
mbus_cmd_diagnostics
(
  mbus_struct *mbus,      /* Pointer to MBUS structure */
  mbus_ubyte slave_addr,  /* MODBUS device address (1-247) */
  mbus_uword subfunction, /* Subfunction code (0-65535) */
  mbus_uword *data        /* Pointer to the query data
                            (replaced by response data in case of success) */
)
{
  int rc;
  mbus_uword resp_subfunction;
  mbus_ubyte *bufptr = mbus->buf + MBUS_DATA;

  MBUS_CHECK_ADDR(slave_addr, 1);
  /* make request */
  if((rc = mbus_cmd_addr_wdata(mbus, slave_addr,
                               MBUS_FC_DIAGNOSTICS,
                               subfunction, *data)) != 0)
    return rc;

  /* check response subfunction code */
  MBUS_WORD_RD(bufptr, resp_subfunction);
  if (resp_subfunction != subfunction)
    return -1; /* subfunction code is incorrect */
  /* read response data */
  MBUS_WORD_RD(bufptr, *data);
  return 0;
}

/*
 * MODBUS command - Function Code 15 (Force Multiple Coils)
 *
 * Returns: 1 if exception code returned,
 *          0 if response received successfully,
 *         -1 in case of error.
 */
int
mbus_cmd_force_multiple_coils
(
  mbus_struct *mbus,     /* Pointer to MBUS structure */
  mbus_ubyte slave_addr, /* MODBUS device address (0-247) */
  mbus_uword coils_addr, /* Coils address (0-65535) */
  mbus_uword coils_num,  /* Number of coils to force (0-65535) */
  mbus_ubyte *coils_data /* Pointer to coils force data */
)
{
  int rc;
  mbus_uword resp_data;
  mbus_ubyte *bufptr = mbus->buf + MBUS_DATA;

  MBUS_CHECK_ADDR(slave_addr, 0);
  /* make request */
  if((rc = mbus_cmd_addr_mdata(mbus,
       slave_addr, MBUS_FC_FORCEMULTIPLECOILS,
       coils_addr, coils_data,
       0, coils_num)) != 0)
    return rc;

  /* check response coil address */
  MBUS_WORD_RD(bufptr, resp_data);
  if (resp_data!= coils_addr)
    return -1; /* coil_address is incorrect */
  /* check response quantity */
  MBUS_WORD_RD(bufptr, resp_data);
  return (resp_data == coils_num) ? 0 : -1;
}

/*
 * MODBUS command - Function Code 16 (Preset Multiple Registers)
 *
 * Returns: 1 if exception code returned,
 *          0 if response received successfully,
 *         -1 in case of error.
 */
int
mbus_cmd_preset_multiple_registers
(
  mbus_struct *mbus,     /* Pointer to MBUS structure */
  mbus_ubyte slave_addr, /* MODBUS device address (0-247) */
  mbus_uword start_addr, /* Registers starting address (0-65535) */
  mbus_uword points_num, /* Number of registers to preset (0-65535) */
  mbus_uword *data       /* Pointer to registers preset data */
)
{
  int rc;
  mbus_uword resp_data;
  mbus_ubyte *bufptr = mbus->buf + MBUS_DATA;

  MBUS_CHECK_ADDR(slave_addr, 0);
  /* make request */
  if((rc = mbus_cmd_addr_mdata(mbus, slave_addr,
                               MBUS_FC_PRESETMULTIPLEREGISTERS,
                               start_addr, (mbus_ubyte *)data,
                               1, points_num)) != 0)
    return rc;

  /* check response registers starting address */
  MBUS_WORD_RD(bufptr, resp_data);
  if (resp_data != start_addr)
    return -1; /*  registers address is incorrect */
  /* check response quantity */
  MBUS_WORD_RD(bufptr, resp_data);
  return (resp_data == points_num) ? 0 : -1;
}

/*
 * MODBUS command - Function Code 17 (Report Slave ID)
 * Returns: 1 if exception code returned,
 *          0 if response received successfully,
 *         -1 in case of error.
 */
int
mbus_cmd_report_slave_id
(
  mbus_struct *mbus,      /* Pointer to MBUS structure */
  mbus_ubyte slave_addr,  /* MODBUS device address (1-247) */
  mbus_ubyte *data_count, /* Readed slave ID data count */
  mbus_ubyte *slave_data  /* Pointer to readed slave ID data buffer */
)
{
  int rc;
  mbus_ubyte byte_count;
  mbus_ubyte *bufptr = mbus->buf + MBUS_DATA;

  MBUS_CHECK_ADDR(slave_addr, 1);
  /* make request */
  if((rc = mbus_cmd_nodata(mbus, slave_addr,
                           MBUS_FC_REPORTSLAVEID)) != 0)
    return rc;

  /* take bytecount */
  byte_count = MBUS_BYTE_RD(bufptr);
  /* No quantity in the request, so the frame that arrived is the only bound -
   * hence `expected` = -1. That bounds the READ only; the size of `slave_data`
   * stays a caller contract, written down in mbus.h. */
  if (!mbus_cmd_bytecount_ok(mbus, (int)byte_count, -1))
    return -1; /* invalid value of bytecount */
  *data_count = byte_count;
  /* copy received data */
  while (byte_count--)
    MBUS_BYTE_WR(slave_data, *bufptr++);
  return 0;
}
