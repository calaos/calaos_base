/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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

    cDebugDom("wago") << "WagoCtrl::~WagoCtrl(): Ok";
}

bool WagoCtrl::getBit(unsigned char mot, int pos)
{
    return ((mot >> pos) & 0x01);
}

void WagoCtrl::setBit(unsigned char &mot, int pos, bool val)
{
    if (val)
        mot = mot | (0x01 << pos);
    else
    {
        if (getBit(mot, pos))
            mot = mot ^ (0x01 << pos);
    }
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

    int data_size = nb / 8 + nb % 8;
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

    mbus_ubyte *data = new mbus_ubyte[nb / 8 + nb % 8];
    memset(data, '\0', nb/8);

    for (int i = 0;i < nb;i++)
        setBit(*data, i, values[i]);

    int ret = mbus_cmd_force_multiple_coils(mbus, 1, (mbus_uword)address, (mbus_uword)nb, data);

    delete[] data;

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

    mbus_uword *data = new mbus_uword[nb];
    for (int i = 0;i < nb;i++)
        data[i] = values[i];

    int ret = mbus_cmd_preset_multiple_registers(mbus, 1, (mbus_uword)address, (mbus_uword)nb, data);

    delete[] data;

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
