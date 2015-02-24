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
#include "Milight.h"
#include "EcoreTimer.h"

using namespace Calaos;

Milight::Milight(string h, int p):
    host(h),
    port(p)
{
    udp_sender = ecore_con_server_connect(ECORE_CON_REMOTE_UDP,
                                          host.c_str(),
                                          port,
                                          this);
}

Milight::~Milight()
{
    DELETE_NULL_FUNC(ecore_con_server_del, udp_sender);
}

void Milight::sendCommand(uint8_t code, uint8_t param)
{
    uint8_t cmd[3] = { code, param, 0x55 };

    if (ecore_con_server_send(udp_sender, cmd, 3) == 0)
    {
        DELETE_NULL_FUNC(ecore_con_server_del, udp_sender);
        udp_sender = ecore_con_server_connect(ECORE_CON_REMOTE_UDP,
                                              host.c_str(),
                                              port,
                                              this);
        ecore_con_server_send(udp_sender, cmd, 3);
    }
}

void Milight::sendOnCommand(int zone)
{
    if (zone < 0 || zone > 4) return;
    uint8_t codes[5] = { 0x42, 0x45, 0x47, 0x49, 0x4B };

    sendCommand(codes[zone], 0x00);
}

void Milight::sendOffCommand(int zone)
{
    if (zone < 0 || zone > 4) return;
    uint8_t codes[5] = { 0x41, 0x46, 0x48, 0x4A, 0x4C };

    sendCommand(codes[zone], 0x00);
}

void Milight::sendWhiteCommand(int zone)
{
    if (zone < 0 || zone > 4) return;
    sendOnCommand(zone);

    EcoreTimer::singleShot(0.1, [=]()
    {
        uint8_t codes[5] = { 0xC2, 0xC5, 0xC7, 0xC9, 0xCB };

        sendCommand(codes[zone], 0x00);
    });
}

void Milight::sendDiscoCommand(int zone)
{
    sendOnCommand(zone);

    EcoreTimer::singleShot(0.1, [=]()
    {
        sendCommand(0x4D, 0x00);
    });
}

void Milight::sendDiscoDecCommand(int zone)
{
    sendOnCommand(zone);

    EcoreTimer::singleShot(0.1, [=]()
    {
        sendCommand(0x43, 0x00);
    });
}

void Milight::sendDiscoIncCommand(int zone)
{
    sendOnCommand(zone);

    EcoreTimer::singleShot(0.1, [=]()
    {
        sendCommand(0x44, 0x00);
    });
}

void Milight::sendBrightnessCommand(int zone, int brightness)
{
    if (brightness < 1 || brightness > 19)
        return;
    sendOnCommand(zone);

    EcoreTimer::singleShot(0.1, [=]()
    {
        uint8_t codes[19] = { 0x02, 0x03, 0x04, 0x05, 0x08,
                              0x09, 0x0A, 0x0B, 0x0D, 0x0E,
                              0x0F, 0x10, 0x12, 0x13, 0x14,
                              0x15, 0x17, 0x18, 0x19 };

        sendCommand(0x4E, codes[brightness - 1]);
    });
}

void Milight::sendColorCommand(int zone, int color)
{
    if (color < 0 || color > 255)
        return;
    sendOnCommand(zone);

    EcoreTimer::singleShot(0.1, [=]()
    {
        sendCommand(0x40, color);
    });
}

ushort Milight::calcMilightColor(const ColorValue &color)
{
    ushort mcolor = (256 + 176 - (int)(color.getHSLHue() / 360.0 * 255.0)) % 256;
    return mcolor + 0xFA;
}
