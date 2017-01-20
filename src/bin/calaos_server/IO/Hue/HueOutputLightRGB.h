/******************************************************************************
 **  Copyright (c) 2006-2017, Calaos. All Rights Reserved.
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
#ifndef HueOutputLightRGB_H
#define HueOutputLightRGB_H

#include "Timer.h"
#include "OutputLightRGB.h"

namespace Calaos
{

class HueOutputLightRGB : public OutputLightRGB
{
private:
    string m_host;
    string m_api;
    string m_idHue;
    Timer *m_timer;

    void setOff();
    void setColor(const ColorValue &c);

protected:
    virtual void setColorReal(const ColorValue &c, bool s);

public:
    HueOutputLightRGB(Params &p);
    ~HueOutputLightRGB();
};

}

#endif // HueOutputLightRGB_H
