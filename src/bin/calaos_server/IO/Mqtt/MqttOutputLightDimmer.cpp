/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
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
#include "MqttOutputLightDimmer.h"
#include "MqttBrokersList.h"
#include "IOFactory.h"
#include <AnalogIO.h>
#include <ExpressionEvaluator.h>

using namespace Calaos;

REGISTER_IO(MqttOutputLightDimmer)

MqttOutputLightDimmer::MqttOutputLightDimmer(Params &p):
    OutputLightDimmer(p)
{
    // We use real state for this IO: only emit change when the value really changes
    useRealState = true;

    // Define IO documentation
    ioDoc->friendlyNameSet("MqttOutputLightDimmer");
    ioDoc->descriptionSet(_("Control lights through mqtt broker"));
    MqttCtrl::commonDoc(ioDoc);

    ioDoc->paramAdd("data", _("The data sent when publishing to topic. The __##VALUE##__ contained in data is substituted "
                              "with the state (integer value) to be sent."),
                    IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("in_expr", _("Use a mathematical expression to convert the raw value into a percent between 0 and 100. The variable `x` is replaced with the raw value. For example, if you want to convert a brightness of 0-254 to a percentage, you can use `x / 2.54`."),
                 IODoc::TYPE_STRING, false);
    ioDoc->paramAdd("out_expr", _("Use a mathematical expression to convert the value of the percentage into a raw value. The variable `x` is replaced with the percent value. For example, if you want to convert a percent value of 0-100 to a brightness of 0-254, you can use `x * 2.54`."),
                 IODoc::TYPE_STRING, false);

    ctrl = MqttBrokersList::Instance().get_ctrl(get_params());
    ctrl->subscribeTopic(get_param("topic_sub"), [=](string, string)
    {
        readValue();
    });

    ctrl->subscribeStatusTopics(this);

    cInfoDom("output") << "MqttOutputLightDimmer::MqttOutputLightDimmer()";
}

double MqttOutputLightDimmer::convertValue(const Params &params, string direction, double dvalue)
{
    string expr = direction + "_expr";

    if (params.Exists(expr) &&
        ExpressionEvaluator::isExpressionValid(params[expr]))
    {
        bool failed = false;
        double v = ExpressionEvaluator::calculateExpression(params[expr], dvalue, failed);
        if (failed)
        {
            cWarningDom("mqtt") << "Failed to calculate expression: " << params[expr];
            return dvalue; // Return original value on failure
        }

        cDebugDom("mqtt") << "Calculated value from expression (" << dvalue << "): " << v;
        return v;
    }

    return dvalue; // Return original value on failure
}

void MqttOutputLightDimmer::readValue()
{
    bool err;
    auto newValue = ctrl->getValueDouble(get_params(), err);

    if (!err && newValue != value)
    {
        value = convertValue(get_params(), "in", newValue);
        EmitSignalIO();
        emitChange();
    }
}

bool MqttOutputLightDimmer::set_value_real(int val)
{
    val = convertValue(get_params(), "out", val);
    ctrl->setValueInt(get_params(), val);
    return true;
}
