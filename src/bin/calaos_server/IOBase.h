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
#ifndef S_IOBASE_H
#define S_IOBASE_H

#include "Calaos.h"
#include "EventManager.h"
#include "IODoc.h"

namespace Calaos
{

class AutoScenario;

class IOBase
{
private:
    //we store all params here
    Params param;

    bool auto_sc_mark;
    AutoScenario *ascenario = nullptr;

protected:
    IODoc *ioDoc;

public:
    IOBase(Params &p):
        param(p),
        auto_sc_mark(false)
    {
        ioDoc = new IODoc();
        ioDoc->paramAdd("id", "Unique id indentifying the Input/Output in calaos-server", "string", true);
        if (!param.Exists("enabled"))
            param.Add("enabled", "true");
        ioDoc->paramAdd("enabled", "Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.", "bool", false);
    }
    virtual ~IOBase() { /* nothing */ }

    virtual DATA_TYPE get_type() = 0;

    virtual bool get_value_bool() { return false; }
    virtual map<string, bool> get_all_values_bool() { map<string, bool> m; return m; }

    virtual double get_value_double() { return 0.0; }
    virtual map<string, double> get_all_values_double() { map<string, double> m; return m; }

    virtual std::string get_value_string() { return ""; }
    virtual map<string, string> get_all_values_string() { map<string, string> m; return m; }

    virtual map<string, string> query_param(string key) { map<string, string> m; return m; }

    virtual void set_param(std::string opt, std::string val)
    { param.Add(opt, val); }
    virtual std::string get_param(std::string opt)
    { return param[opt]; }
    virtual Params &get_params()
    { return param; }

    virtual bool LoadFromXml(TiXmlElement *node)
    { return false; }
    virtual bool SaveToXml(TiXmlElement *node)
    { return false; }

    bool isAutoScenario() { return auto_sc_mark; }
    void setAutoScenario(bool m) { auto_sc_mark = m; }

    void setAutoScenarioPtr(AutoScenario *sc) { ascenario = sc; }
    AutoScenario *getAutoScenarioPtr() { return ascenario; }

    bool isEnabled() { return param["enabled"] == "true"; }

    string genDocMd() const
    {
        if (ioDoc)
            return ioDoc->genDocMd();
        else
            return "";
    }
    json_t *genDocJson() const
    {
        if (ioDoc)
            return ioDoc->genDocJson();
        else
            return nullptr;
    }

};

}

#endif
