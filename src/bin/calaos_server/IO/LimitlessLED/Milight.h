/******************************************************************************
 **  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
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
#ifndef Milight_H
#define Milight_H

#define DEFAULT_MILIGHT_PORT    8899

#include "Calaos.h"
#include <Ecore_Con.h>

namespace Calaos {

class Milight
{
public:

    Milight(std::string host, int port);
    ~Milight();

    void sendOnCommand(int zone);
    void sendOffCommand(int zone);
    void sendWhiteCommand(int zone);
    void sendDiscoCommand(int zone);
    void sendDiscoDecCommand(int zone);
    void sendDiscoIncCommand(int zone);
    /* brightness value between 1 and 19 */
    void sendBrightnessCommand(int zone, int brightness);
    /* color range between 0-255 */
    void sendColorCommand(int zone, ushort color);

    //convert HSL color to milight 0-255 value
    static ushort calcMilightColor(const ColorValue &color);

private:

    Ecore_Con_Server *udp_sender = nullptr;
    std::string host;
    int port = DEFAULT_MILIGHT_PORT;

    void sendCommand(uint8_t code, uint8_t param);

};

}

#endif
