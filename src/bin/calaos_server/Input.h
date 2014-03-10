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
#ifndef S_INPUT_H
#define S_INPUT_H

#include <Calaos.h>
#include <IOBase.h>

namespace Calaos
{

class Input: public IOBase
{
protected:
    typedef sigc::signal<void, std::string> type_signal_input;
    type_signal_input signal_input;
    type_signal_input::iterator iter_input;

public:
    Input(Params &p);
    virtual ~Input();

    virtual void force_input_bool(bool val) { /* Do nothing */ }
    virtual void force_input_double(double val) { /* Do nothing */ }
    virtual void force_input_string(std::string val) { /* Do nothing */ }

    virtual void EmitSignalInput();
    virtual void hasChanged() { }

    virtual bool LoadFromXml(TiXmlElement *node)
    { return false; }
    virtual bool SaveToXml(TiXmlElement *node);
};

}
#endif
