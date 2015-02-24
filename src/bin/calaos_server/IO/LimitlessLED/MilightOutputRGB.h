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
#ifndef MilightOutputRGB_H
#define MilightOutputRGB_H

#include "OutputLightRGB.h"
#include "Milight.h"

namespace Calaos
{

class MilightOutputRGB : public OutputLightRGB
{
private:
    int port = DEFAULT_MILIGHT_PORT;
    string host;
    int zone = 0;

    Milight *milight;

protected:
    virtual void setColorReal(const ColorValue &c, bool s);

public:
    MilightOutputRGB(Params &p);
    ~MilightOutputRGB();
};

}

#endif // MilightOutputRGB_H
