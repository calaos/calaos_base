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
#ifndef S_OUTPUT_H
#define S_OUTPUT_H

#include <Calaos.h>
#include <IOBase.h>

namespace Calaos
{

class Output: public IOBase
{
protected:
    typedef sigc::signal<void, std::string> type_signal_output;
    type_signal_output signal_output;
    type_signal_output::iterator iter_output;

public:
    Output(Params &p);
    virtual ~Output();

    virtual bool set_value(bool val) { return false; }
    virtual bool set_value(double val)  { return false; }
    virtual bool set_value(std::string val)  { return false; }

    //used to retreive the last state command of the TSTRING output
    virtual std::string get_command_string() { return ""; }

    //used to get a better condition value in ConditionOutput rules
    //like if shutter is open or is light on
    //Note: this is only used for TSTRING outputs
    virtual bool check_condition_value(std::string cvalue, bool equal) { return false; }

    virtual void EmitSignalOutput();

    virtual bool LoadFromXml(TiXmlElement *node)
    { return false; }
    virtual bool SaveToXml(TiXmlElement *node);
};

}
#endif
