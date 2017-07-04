/*** LICENCE ***************************************************************************************/
/*
  xPPLib - Simple class to manage xPL & xAP protocol

  This file is part of xPPLib.

    xPPLib is free software : you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    xPPLib is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with xPPLib.  If not, see <http://www.gnu.org/licenses/>.
*/
/***************************************************************************************************/

#include "StringTools/StringTools.h"
#include "SchemaControl.h"

namespace xPL
{

using namespace std;

/****************************************************************************************************/
/*** Schema control.basic                                                                           */
const vector<string> SchemaControlBasic::m_TypeString({"manual", "unset", "balance", "flag", "infrared", "input", "macro", "mute", "output", "variable", "periodic", "scheduled", "slider", "timer"});
const vector<string> SchemaControlBasic::m_FlagSet({"set", "clear", "neutral"});

SchemaControlBasic::SchemaControlBasic() : SchemaObject(ISchema::cmnd, "control", "basic")
{
    m_deviceType = controlType::manual;
}

SchemaControlBasic::SchemaControlBasic(string name, controlType type) : SchemaObject(ISchema::cmnd, "control", "basic")
{
    SetDeviceName(name);
    SetDeviceType(type);
}

SchemaControlBasic::SchemaControlBasic(string name, controlType type, string value) : SchemaObject(ISchema::cmnd, "control", "basic")
{
    SetDeviceName(name);
    SetDeviceType(type);
    SetCurrent(value);
}

SchemaControlBasic::~SchemaControlBasic()
{
}

void SchemaControlBasic::SetDeviceName(string name)
{
    SetValue("device", name);
}

void SchemaControlBasic::SetDeviceType(controlType type)
{
    SetValue("type", m_TypeString[type]);
    m_deviceType = type;
}

void SchemaControlBasic::SetDeviceType(string deviceType)
{
    controlType type;

    type = ToDeviceType(deviceType);
    if(type != controlType::manual)
    {
        SetDeviceType(type);
        return;
    }
    SetValue("type", deviceType);
    m_deviceType = controlType::manual;
}

SchemaControlBasic::controlType SchemaControlBasic::ToDeviceType(string type)
{
    unsigned int i=0;
    vector<string>::const_iterator it;


    for(i=0; i<SchemaControlBasic::m_TypeString.size(); i++)
        if(StringTools::IsEqualCaseInsensitive(m_TypeString[i],type)==true) return (controlType)i;

    return controlType::manual;
}

void SchemaControlBasic::SetFlag(flagSet value)
{
    SetCurrent(m_FlagSet[value]);
}

void SchemaControlBasic::SetCurrent(string current)
{
    if(m_deviceType!=manual)
        SetValue("current", StringTools::ToLower(current));
    else
        SetValue("current", current);
}

string SchemaControlBasic::GetCurrent()
{
    return GetValue("current");
}

void SchemaControlBasic::Check()
{
    int ivalue;
    string svalue;


    if(GetValue("device")=="") throw SchemaControlBasic::Exception(0x0100, "SchemaControlBasic::Check() : Device name mandatory");
    svalue = GetCurrent();

    switch(m_deviceType)
    {
        case manual :
            return;
        case unset :
            throw SchemaControlBasic::Exception(0x0101, "SchemaControlBasic::Check() : Device type mandatory");
            break;
        case balance :
            ivalue = GetValue<int>("current");
            if((ivalue<-100)||(ivalue>100)) throw SchemaControlBasic::Exception(0x0102, "SchemaControlBasic::Check() : Balance value out of limit");
            break;
        case flag :
            if(svalue=="set") return;
            if(svalue=="clear") return;
            if(svalue=="neutral") return;
            throw SchemaControlBasic::Exception(0x0103, "SchemaControlBasic::Check() : Flag value unknown");
        case infrared :
            if(svalue=="send") return;
            if(svalue=="enable_rx") return;
            if(svalue=="disable_rx") return;
            if(svalue=="enable_tx") return;
            if(svalue=="disable_tx") return;
            if(svalue=="sendx") return;
            throw SchemaControlBasic::Exception(0x0104, "SchemaControlBasic::Check() : Infrared value unknown");
        case input :
            if(svalue=="enable") return;
            if(svalue=="disable") return;
            throw SchemaControlBasic::Exception(0x0105, "SchemaControlBasic::Check() : Input value unknown");
        case macro :
            if(svalue=="enable") return;
            if(svalue=="disable") return;
            if(svalue=="do") return;
            throw SchemaControlBasic::Exception(0x0105, "SchemaControlBasic::Check() : Macro value unknown");
        case mute :
            if(svalue=="yes") return;
            if(svalue=="no") return;
            throw SchemaControlBasic::Exception(0x0106, "SchemaControlBasic::Check() : Mute value unknown");
        case output :
            if(svalue=="enable") return;
            if(svalue=="disable") return;
            if(svalue=="high") return;
            if(svalue=="low") return;
            if(svalue=="toggle") return;
            if(svalue=="pulse") return;
            throw SchemaControlBasic::Exception(0x0107, "SchemaControlBasic::Check() : Output value unknown");
        case variable :
            ivalue = GetValue<int>("current");
            if((ivalue>=0)&&(ivalue<=255)) return;
            if(svalue=="inc") return;
            if(svalue=="dec") return;
            throw SchemaControlBasic::Exception(0x0108, "SchemaControlBasic::Check() : Variable value unknown");
        case periodic :
            if(svalue=="started") return;
            if(svalue=="enable") return;
            if(svalue=="disable") return;
            throw SchemaControlBasic::Exception(0x0109, "SchemaControlBasic::Check() : Periodic value unknown");
        case scheduled :
            if(svalue=="started") return;
            if(svalue=="enable") return;
            if(svalue=="disable") return;
            throw SchemaControlBasic::Exception(0x0110, "SchemaControlBasic::Check() : Scheduled value unknown");
        case slider :
            ivalue = GetValue<int>("current");
            if((ivalue>=-255)&&(ivalue<=255)) return;
            throw SchemaControlBasic::Exception(0x0111, "SchemaControlBasic::Check() : Slider value out of limit");
        case timer :
            if(svalue=="went off") return;
            if(svalue=="start") return;
            if(svalue=="stop") return;
            if(svalue=="halt") return;
            if(svalue=="resume") return;
            throw SchemaControlBasic::Exception(0x0112, "SchemaControlBasic::Check() : Timer value unknown");

        default :
            throw SchemaControlBasic::Exception(0x0120, "SchemaControlBasic::Check() : Type unknown");
    }
}

}
