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
#include <WagoCtrl.h>
#include <WagoBits.h>

using namespace Utils;

WagoCtrl::WagoCtrl(std::string h, int p):
    host(h),
    port(p)
{
    mbus = mbus_init(NULL);
    if (host == "") host = "127.0.0.1";

    if (!mbus)
        cErrorDom("wago") << "WagoCtrl::WagoCtrl(" << host << ", " << port << "): Cant init modbus structure !";

    cInfoDom("wago") << "WagoCtrl::WagoCtrl(" << host << ", " << port << "): Ok";
}

WagoCtrl::~WagoCtrl()
{
    if (is_connected())
        Disconnect();
    if (mbus)
        mbus_free(mbus);
}

bool WagoCtrl::getBit(unsigned char mot, int pos)
{
    return ((mot >> pos) & 0x01);
}

bool WagoCtrl::Connect()
{
    if (is_connected()) mbus_close(mbus);
    if (mbus_connect(mbus, host.c_str(), (mbus_uword)port, 0))
    {
        cErrorDom("wago") << "WagoCtrl::Connect(): Can't connect...";
        return false;
    }
    else
    {
        cDebugDom("wago") << "WagoCtrl::Connect(): Ok";
        return true;
    }
}

void WagoCtrl::Disconnect()
{
    mbus_close(mbus);
    cDebugDom("wago") << "WagoCtrl::Disconnect(): Ok";
}

bool WagoCtrl::is_connected()
{
    if (mbus_connected(mbus) == 1)
        return true;
    else
        return false;
}

bool WagoCtrl::read_bits(UWord address, int nb, vector<bool> &values)
{
    if (!is_connected()) return false;

    //T3.30 - REFUSE THE COUNT BEFORE ALLOCATING ON IT. `nb` arrives from the
    //dispatcher out of an `int count;` that is never initialised (F-WAGO-7);
    //on a non-positive one coilBufferSize() answers 0, this allocates a valid
    //pointer to zero usable bytes, and mbus_cmd_read_coil_status() then copies
    //the response byte-count - an mbus_ubyte, so up to 255 bytes, bounded by
    //nothing passed here - straight into it. The two writes already refuse
    //nb <= 0 through WagoBits::countIsWritable(); both directions of the wire
    //now say the same thing.
    if (!WagoBits::countIsReadable(nb)) return false;

    //T3.30 - the size of a coil buffer is the ceiling of nb over eight, and
    //it now comes from one place. The expression that used to be written out
    //here over-allocated (nb = 15 asked for 8 bytes to hold 2); benign in this
    //direction because the memset below covers the whole allocation, but it is
    //the same expression write_multiple_bits() got wrong, so both ends of the
    //wire now agree through WagoBits::coilBufferSize().
    int data_size = WagoBits::coilBufferSize(nb);
    mbus_ubyte *data = new mbus_ubyte[data_size];
    memset(data, 0, sizeof(mbus_ubyte) * data_size);
    int ret = mbus_cmd_read_coil_status(mbus, 1, (mbus_uword)address, (mbus_uword)nb, data);

    for (int i = 0;i < nb;i++)
        values.push_back(getBit(*(data + i / 8), i % 8));

    delete[] data;

    if (ret != 0)
    {
        cErrorDom("wago") << "WagoCtrl::read_bits(): Error reading bits!";
        return false;
    }
    else
    {
        cDebugDom("wago") << "WagoCtrl::read_bits(" << address << "," << nb <<"): Ok";
        return true;
    }
}

bool WagoCtrl::write_single_bit(UWord address, bool val)
{
    if (!is_connected()) return false;

    mbus_uword data = 0x0000;

    if (val) data = 0xFF00;

    int ret = mbus_cmd_force_single_coil(mbus, 1, (mbus_uword)address, data);

    if (ret != 0)
    {
        cErrorDom("wago") << "WagoCtrl::write_single_bit(): Error writing single bit!";
        return false;
    }
    else
    {
        cDebugDom("wago") << "WagoCtrl::write_single_bit(" << address << ", " << (val?"true":"false") << "): Ok";
        return true;
    }
}

bool WagoCtrl::read_single_output_bit(UWord address)
{
    vector<bool> v;

    if (!read_bits(address + 0x200, 1, v))
        return false;

    if (!v.empty())
        return v[0];

    return false;
}

bool WagoCtrl::write_multiple_bits(UWord address, int nb, vector<bool> &values)
{
    if (!is_connected()) return false;

    //T3.30 - `nb` comes from the wire message and `values` from the decoded
    //array: two independent facts. packBits() refuses a count it cannot
    //honour instead of truncating - a short write is indistinguishable, at
    //the relays, from a complete one - and it is also what stops values[i]
    //from reading past the end, which on an empty vector was a SIGSEGV.
    //It packs bit i into byte i / 8 at offset i % 8, like read_bits() above,
    //and writes every byte of the buffer it sizes.
    vector<unsigned char> data;
    if (!WagoBits::packBits(nb, values, data))
    {
        cErrorDom("wago") << "WagoCtrl::write_multiple_bits(): refusing to write "
                          << nb << " bits from " << values.size() << " values!";
        return false;
    }

    int ret = mbus_cmd_force_multiple_coils(mbus, 1, (mbus_uword)address, (mbus_uword)nb, &data[0]);

    if (ret != 0)
    {
        cErrorDom("wago") << "WagoCtrl::write_multiple_bits(): Error writing multiple words... !";
        return false;
    }
    else
    {
        cDebugDom("wago") << "WagoCtrl::write_multiple_bits(): Ok";
        return true;
    }
}

bool WagoCtrl::read_words(UWord address, int nb, vector<UWord> &values)
{
    if (!is_connected()) return false;

    //T3.30 - the register twin of the guard in read_bits(), for the same
    //reason and against the same uninitialised `count`. The allocation below
    //throws for a negative nb and hands back ZERO usable words for nb == 0,
    //and mbus_cmd_read_holding_registers() then writes up to 127 words into
    //it - a count taken from the response, bounded by nothing passed here.
    if (!WagoBits::countIsReadable(nb)) return false;

    mbus_uword *data = new mbus_uword[nb];
    int ret = mbus_cmd_read_holding_registers(mbus, 1, (mbus_uword)address, (mbus_uword)nb, data);

    for (int i = 0;i < nb;i++)
        values.push_back(data[i]);

    delete[] data;

    if (ret != 0)
    {
        cErrorDom("wago") << "WagoCtrl::read_words(): Error reading words... !";
        return false;
    }
    else
    {
        cDebugDom("wago") << "WagoCtrl::read_words(): Ok";
        return true;
    }
}

bool WagoCtrl::write_single_word(UWord address, UWord val)
{
    if (!is_connected()) return false;

    int ret = mbus_cmd_preset_single_register(mbus, 1, (mbus_uword)address, val);

    if (ret != 0)
    {
        cErrorDom("wago") << "WagoCtrl::write_single_word(): Error writing single word... !";
        return false;
    }
    else
    {
        cDebugDom("wago") << "WagoCtrl::write_single_word(): Ok";
        return true;
    }
}

bool WagoCtrl::write_multiple_words(UWord address, int nb, vector<UWord> &values)
{
    if (!is_connected()) return false;

    //T3.30 - the twin of write_multiple_bits(): no bit packing, the same
    //refusal. UWord and mbus_uword are both `unsigned short`, so the vector
    //hands its buffer straight to libmbus.
    vector<UWord> data;
    if (!WagoBits::copyValues(nb, values, data))
    {
        cErrorDom("wago") << "WagoCtrl::write_multiple_words(): refusing to write "
                          << nb << " words from " << values.size() << " values!";
        return false;
    }

    int ret = mbus_cmd_preset_multiple_registers(mbus, 1, (mbus_uword)address, (mbus_uword)nb, &data[0]);

    if (ret != 0)
    {
        cErrorDom("wago") << "WagoCtrl::write_multiple_words(): Error writing multiple words... !";
        return false;
    }
    else
    {
        cDebugDom("wago") << "WagoCtrl::write_multiple_words(): Ok";
        return true;
    }
}
