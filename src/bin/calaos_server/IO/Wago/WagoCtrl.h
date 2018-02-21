/******************************************************************************
 **  Copyright (c) 2006-2018, Calaos. All Rights Reserved.
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

class WagoCtrl
{
protected:
    std::string host;
    int port;

    mbus_struct *mbus;

    bool getBit(unsigned char mot, int pos);
    void setBit(unsigned char &mot, int pos, bool val);
public:
    WagoCtrl(std::string host, int port = 502);
    ~WagoCtrl();

    bool Connect();
    void Disconnect();
    bool is_connected();

    //bits
    bool read_bits(Utils::UWord address, int nb, vector<bool> &values);
    bool write_single_bit(Utils::UWord address, bool val);
    bool read_single_output_bit(Utils::UWord address);
    bool write_multiple_bits(Utils::UWord address, int nb, vector<bool> &values);

    //Words
    bool read_words(Utils::UWord address, int nb, vector<Utils::UWord> &values);
    bool write_single_word(Utils::UWord address, Utils::UWord val);
    bool write_multiple_words(Utils::UWord address, int nb, vector<Utils::UWord> &values);

    void set_host(std::string &h) { host = h; }
    std::string get_host() { return host; }
    void set_port(int p) { port = p; }
    int get_port() { return port; }
};

#endif
