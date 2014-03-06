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
#ifndef S_CONDITIONOUTPUT_H
#define S_CONDITIONOUTPUT_H

#include <Calaos.h>
#include <Condition.h>
#include <Output.h>

namespace Calaos
{

class ConditionOutput: public Condition
{
        protected:
                Output *output;
                string params;
                string ops;
                //this is used to do the condition test
                //based on another output
                string params_var;

                bool eval(bool val1, std::string oper, bool val2);
                bool eval(double val1, std::string oper, double val2);
                bool eval(std::string val1, std::string oper, std::string val2);
                bool eval(Output *out, std::string oper, std::string val);

        public:
                ConditionOutput();
                ~ConditionOutput();

                virtual bool Evaluate();

                void setOutput(Output *p) { output = p; }
                Output *getOutput() { return output; }

                string get_params() { return params; }
                string get_operator() { return ops; }
                string get_params_var() { return params_var; }
                void set_param(string p) { params = p; }
                void set_operator(string p) { ops = p; }
                void set_param_var(string p) { params_var = p; }

                bool LoadFromXml(TiXmlElement *node);
                bool SaveToXml(TiXmlElement *node);
};

}
#endif
