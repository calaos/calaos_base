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
//-----------------------------------------------------------------------------
#ifndef S_CAMOUTPUT_H
#define S_CAMOUTPUT_H
//-----------------------------------------------------------------------------
#include <Output.h>
#include <Calaos.h>
#include <sstream>
//-----------------------------------------------------------------------------
namespace Calaos
{
//-----------------------------------------------------------------------------
class IPCam;
//-----------------------------------------------------------------------------
class CamOutput: public Output
{
        private:
                std::string answer;
                IPCam *cam;

        public:
                CamOutput(Params &p, IPCam *_cam);
                ~CamOutput();

                virtual DATA_TYPE get_type() { return TSTRING; }

                virtual bool set_value(std::string val);
                virtual std::string get_value_string();

                IPCam *get_cam() { return cam; }
};
//-----------------------------------------------------------------------------
}
//-----------------------------------------------------------------------------
#endif
