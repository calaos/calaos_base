/******************************************************************************
**  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
**
**  This file is part of Calaos Home.
**
**  Calaos Home is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 3 of the License, or
**  (at your option) any later version.
**
**  Calaos Home is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Foobar; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#ifndef S_WOVolet_H
#define S_WOVolet_H

#include <OutputShutter.h>
#include <WagoMap.h>

namespace Calaos
{

class WOVolet : public OutputShutter
{
        private:
                int up_address, down_address;
                std::string host;
                int port;

                virtual void setOutputUp(bool enable);
                virtual void setOutputDown(bool enable);

                void readConfig();
                void WagoWriteCallback(bool status, UWord address, bool value);

        public:
                WOVolet(Params &p);
                ~WOVolet();
};

}
#endif
