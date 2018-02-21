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

#ifndef WEBINPUTANALOG_H
#define WEBINPUTANALOG_H

#include "Calaos.h"
#include "OutputAnalog.h"
#include "WebDocBase.h"

namespace Calaos
{

class WebOutputAnalog : public OutputAnalog
{
protected:
    virtual void readValue();
    virtual void set_value_real(double val);

    WebDocBase docBase;

public:
    WebOutputAnalog(Params &p);
    virtual ~WebOutputAnalog();
};

}

#endif // WEBINPUTANALOG_H
