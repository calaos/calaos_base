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
#ifndef S_WAGOCTRL_H
#define S_WAGOCTRL_H

#include <Utils.h>
#include <mbus.h>

#include "WagoTypes.h"

class WagoCtrl
{
protected:
    std::string host;
    int port;

    mbus_struct *mbus;

    bool getBit(unsigned char mot, int pos);
    //T3.30 - setBit(unsigned char &mot, int pos, bool val) is GONE. It took a
    //reference to ONE byte plus a position, and every caller of a multiple
    //write handed it *data with pos running to nb - 1: every bit landed in
    //data[0], and pos >= 32 was an undefined shift. The byte/offset split now
    //happens in exactly one place, WagoBits::setBufferBit(). Do not bring the
    //old signature back: it invites the bug rather than allowing it.
public:
    WagoCtrl(std::string host, int port = 502);
    ~WagoCtrl();

    bool Connect();
    void Disconnect();
    bool is_connected();

    //bits
    /* T3.31 - address, count and payload each have a type of their own.
     * ⚠️ The LAST hop, from these bodies into libmbus, is NOT closed:
     * mbus_cmd_preset_single_register() and mbus_cmd_force_single_coil()
     * take two mbus_uword in a row. That is a cost decision about a vendored
     * library, not an impossibility - a one-field struct closes a
     * permutation in C too, measured. WagoCtrl.cpp unwraps with .v in
     * exactly one line per command. See docs/refactoring/T3.31.md section 7.5. */
    bool read_bits(WagoTypes::Address address, WagoTypes::Count nb, vector<bool> &values);
    bool write_single_bit(WagoTypes::Address address, WagoTypes::BitValue val);
    bool read_single_output_bit(WagoTypes::Address address);
    bool write_multiple_bits(WagoTypes::Address address, WagoTypes::Count nb, vector<bool> &values);

    //Words
    bool read_words(WagoTypes::Address address, WagoTypes::Count nb, vector<Utils::UWord> &values);
    bool write_single_word(WagoTypes::Address address, WagoTypes::WordValue val);
    bool write_multiple_words(WagoTypes::Address address, WagoTypes::Count nb, vector<Utils::UWord> &values);

    void set_host(std::string &h) { host = h; }
    std::string get_host() { return host; }
    void set_port(int p) { port = p; }
    int get_port() { return port; }
};

#endif
