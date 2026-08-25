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
#ifndef S_WAGO_BITS_H
#define S_WAGO_BITS_H

#include <cstring>
#include <vector>

/*
 * T3.30 - the modbus multiple-write buffer, in one place.
 *
 * WagoCtrl::write_multiple_bits() and write_multiple_words() cannot be reached
 * from a test: they return on `if (!is_connected())` before their body, and
 * getting past that means a live modbus socket to a PLC. The part of them that
 * can be wrong on its own - sizing a coil buffer, deciding which byte a bit
 * belongs to, and deciding whether a count may be trusted - lives here so that
 * tests/WagoBits_test.cpp can execute the SHIPPED code instead of a copy of it.
 *
 * This header deliberately depends on nothing: <vector> and <cstring>, no
 * Utils.h, no mbus.h. mbus_ubyte is `unsigned char` and mbus_uword is
 * `unsigned short` (libmbus/mbus_conf.h), which is also Utils::UWord, so the
 * vectors below hand their data() straight to libmbus.
 *
 * THE THREE THINGS IT EXISTS TO GET RIGHT (FINDINGS.md F-WAGO-2):
 *
 *   (a) A COUNT IS NOT A SIZE. `nb` arrives from the wire message and `values`
 *       from the decoded array; they are two independent facts. packBits() and
 *       copyValues() REFUSE a count they cannot honour rather than truncating:
 *       a short write is indistinguishable, on the wire and at the relays,
 *       from a complete one. Reading values[i] past the end used to be a
 *       SIGSEGV, not a quiet mistake - an empty vector<bool> has a null
 *       _M_start.
 *
 *   (b) THE BYTE AND THE OFFSET ARE SPLIT IN EXACTLY ONE PLACE, setBufferBit()
 *       below. The previous shape - a function taking a reference to one byte
 *       plus a position, called as setBit(*data, i, v) - wrote every bit of
 *       every multiple-write into data[0], and formed shift counts of 32 and
 *       above, which is undefined. With `bit % 8` no shift count above 7 can
 *       be built, whatever nb is.
 *
 *   (c) THE BUFFER IS SIZED (nb + 7) / 8 AND FULLY WRITTEN. The old
 *       `nb / 8 + nb % 8` over-allocated (nb = 15 gave 8 bytes for 2) and the
 *       memset next to it covered only nb / 8 of them, so the last byte
 *       travelled to the PLC holding whatever was on the heap.
 */

namespace WagoBits
{

/* Bytes needed to carry nb coil bits. This is the size read_bits() must use
 * too: the two directions of the same wire have to agree. */
inline int coilBufferSize(int nb)
{
    if (nb <= 0) return 0;
    return (nb + 7) / 8;
}

/* Is a count from the wire honourable against what was actually delivered?
 * A modbus multiple-write carries at least one item, and never more items than
 * the caller handed over. Zero is also the only count for which the pointer
 * given to libmbus could be null. */
inline bool countIsWritable(int nb, size_t available)
{
    return nb > 0 && (size_t)nb <= available;
}

/* Set or clear one bit of a coil buffer, addressed by its ABSOLUTE index.
 * The byte/offset split happens here and nowhere else. */
inline void setBufferBit(unsigned char *buf, int bit, bool val)
{
    unsigned char &byte = buf[bit / 8];
    const unsigned char mask = (unsigned char)(0x01u << (bit % 8));

    if (val)
        byte = (unsigned char)(byte | mask);
    else
        byte = (unsigned char)(byte & (unsigned char)(~mask));
}

/* Pack the first nb entries of values into a modbus coil buffer.
 * Returns false and leaves `out` untouched when nb cannot be honoured. */
inline bool packBits(int nb, const std::vector<bool> &values,
                     std::vector<unsigned char> &out)
{
    if (!countIsWritable(nb, values.size()))
        return false;

    /* assign(), not resize(): every byte handed to the PLC is written here,
     * including the last one of a partial byte. */
    out.assign((size_t)coilBufferSize(nb), 0);

    for (int i = 0; i < nb; i++)
        setBufferBit(&out[0], i, values[i]);

    return true;
}

/* The register twin: no packing, the same refusal.
 * Returns false and leaves `out` untouched when nb cannot be honoured. */
template<typename T>
inline bool copyValues(int nb, const std::vector<T> &values,
                       std::vector<T> &out)
{
    if (!countIsWritable(nb, values.size()))
        return false;

    out.assign(values.begin(), values.begin() + nb);

    return true;
}

} //namespace WagoBits

#endif
