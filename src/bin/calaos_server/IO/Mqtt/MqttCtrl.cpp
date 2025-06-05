#include <json.hpp>

#include "Utils.h"
#include "IOFactory.h"
#include "MqttCtrl.h"
#include "Prefix.h"
#include "Params.h"
#include "ExpressionEvaluator.h"

using namespace Calaos;

MqttCtrl::MqttCtrl(const Params &params)
{
    string host = "127.0.0.1";
    string port = "1883";
    string keepalive = "120";

    //use default parameters if not set from config
    if (params.Exists("host") && !params["host"].empty())
        host = params["host"];
    if (params.Exists("port") && !params["port"].empty())
        port = params["port"];
    if (params.Exists("keepalive") && !params["keepalive"].empty())
        keepalive = params["keepalive"];

    cDebugDom("mqtt") << "New MQTT external process " << host << ":" << port;
    process = new ExternProcServer("mqtt");
    exe = Prefix::Instance().binDirectoryGet() + "/calaos_mqtt";

    json_t *root = json_object();
    json_object_set_new(root, "host", json_string(host.c_str()));
    json_object_set_new(root, "port", json_string(port.c_str()));
    json_object_set_new(root, "keepalive", json_string(keepalive.c_str()));

    if (params.Exists("user") && params.Exists("password"))
    {
        json_object_set_new(root, "user", json_string(params["user"].c_str()));
        json_object_set_new(root, "password", json_string(params["password"].c_str()));
    }

    string arg = jansson_to_string(root);

    process->processExited.connect([=]()
    {
        //restart process when stopped
        cWarningDom("process") << "process exited, restarting...";
        process->startProcess(exe, "mqtt", arg);
    });

    process->messageReceived.connect([=](const string &msg)
    {
        json_error_t jerr;
        json_t *jroot = json_loads(msg.c_str(), 0, &jerr);

        if (!jroot)
        {
            cWarningDom("mqtt") << "Error parsing json: " << jerr.text;
            if (jroot)
                json_decref(jroot);
            return;
        }
        Params p;
        jansson_decode_object(jroot, p);

        cDebugDom("mqtt") << "Topic :  " << p["topic"] << " payload : " << p["payload"];

        // Set or replace the message
        messages[p["topic"]] = p["payload"];
        for (auto& it: subscribeCb)
        {
            if (topicMatchesSubscription(it.first, p["topic"]))
            {
                cDebugDom("mqtt") << "New message received on topic " << msg;

                // Call the all registered callbacks for this topic
                for (auto &callback : it.second)
                {
                    callback(p["topic"], p["payload"]);
                }
            }
        }

        json_decref(jroot); });

    process->startProcess(exe, "mqtt", arg);
}

MqttCtrl::~MqttCtrl()
{
}

void MqttCtrl::subscribeTopic(const string topic, MsgReceivedSignal callback)
{
    // subscribeCb contains a map of list of callbacks, register this callback to the key relative of this topic
    cDebugDom("mqtt") << "subscribeTopic : " << topic;
    if (topic == "")
    {
        cErrorDom("mqtt") << "Topic is empty !";
        return;
    }

    auto v = subscribeCb[topic];
    v.push_back(callback);
    subscribeCb[topic] = v;
}

void MqttCtrl::publishTopic(const string topic, const string payload)
{
    string message;

    json_t *jroot = json_object();
    json_object_set_new(jroot, "topic", json_string(topic.c_str()));
    json_object_set_new(jroot, "payload", json_string(payload.c_str()));

    process->sendMessage(jansson_to_string(jroot));
    json_decref(jroot);
}

string MqttCtrl::getValueJson(const Params &params, string path, string payload)
{
    string value;

    // If path is empty, treat payload as direct raw value
    if (path.empty())
    {
        cDebugDom("mqtt") << "Path is empty, returning raw payload value";
        value = payload;

        cDebugDom("mqtt") << "Returning value: " << value;
        return value;
    }

    // Original JSON parsing logic for non-empty paths
    Json root;
    try
    {
        root = Json::parse(payload);
    }
    catch (const std::exception &e)
    {
        cWarning() << "Error parsing " << payload << ":" << e.what();
        return string();
    }

    vector<string> tokens;
    Utils::split(path, tokens, "/");

    if (!tokens.empty())
    {
        Json parent = root;
        for (auto it = tokens.begin(); it != tokens.end(); it++)
        {
            string val = *it;

            // Test if the token is an array index
            // if it's the case, it must be something like [x]
            if (val[0] == '[')
            {
                int idx;
                // Remove first and last char
                val.erase(0, 1);
                val.pop_back();
                // Read array index
                Utils::from_string(val, idx);

                try
                {
                    parent = parent.at(idx);
                }
                catch (const std::exception &e)
                {
                    cWarning() << "Error in path " << path << ", index not found " << *it << " : " << e.what();
                    return string();
                }
            }
            else
            {
                // Token is a normal object name
                try
                {
                    parent = parent.at(val);
                }
                catch (const std::exception &e)
                {
                    cWarning() << "Error in path " << path << ", subpath not found " << *it << " : " << e.what();
                    return string();
                }
            }
        }

        if (parent.is_null())
            value = "null";
        else if (parent.is_boolean())
            value = parent.get<bool>() ? "true" : "false";
        else if (parent.is_number())
            value = Utils::to_string(parent.get<double>());
        else if (parent.is_string())
            value = parent.get<string>();
        else if (parent.is_object())
        {
            cWarning() << "Error, path returns an object, not a value";
            value = "object{}";
        }
        else if (parent.is_array())
        {
            cWarning() << "Error, path returns an array, not a value";
            value = "array[]";
        }
    }

    return value;
}

string MqttCtrl::getValue(const Params &params, bool &err, string topic_param, string path_param)
{
    string type = params["type"];

    if (!params.Exists(topic_param))
    {
        cDebugDom("mqtt") << "Topic does not exists \"" << topic_param << "\" in params";
        err = true;
        return "";
    }

    string payload = messages[params[topic_param]];

    if (payload.empty())
    {
        cDebugDom("mqtt") << "No message received for topic " << params[topic_param] << " yet";
        err = true;
        return "";
    }
    err = false;
    return getValueJson(params, params[path_param], payload);
}

double MqttCtrl::getValueDouble(const Params &params, bool &err)
{
    double val = 0;
    string value;
    err = true;

    value = getValue(params, err, "topic_sub");

    if (Utils::is_of_type<double>(value) && !value.empty())
    {
        Utils::from_string(value, val);
        err = false;
    }

    return val;
}

ColorValue MqttCtrl::getValueColor(const Params &params, bool &err)
{
    string value;
    double x, y;
    int b;
    err = true;

    value = getValue(params, err, "topic_sub", "path_x");

    if (Utils::is_of_type<double>(value) && !value.empty())
    {
        Utils::from_string(value, x);
        err = false;
    }

    value = getValue(params, err, "topic_sub", "path_y");

    if (Utils::is_of_type<double>(value) && !value.empty())
    {
        Utils::from_string(value, y);
        err = false;
    }

    value = getValue(params, err, "topic_sub", "path_brightness");

    if (Utils::is_of_type<int>(value) && !value.empty())
    {
        Utils::from_string(value, b);
        err = false;
    }

    if (err)
        return {};

    return ColorValue::fromXYBrightness(x, y, b / 255.0);
}

void MqttCtrl::setValueString(const Params &params, string val)
{
    string data;
    string topic = params["topic_pub"];

    if (params.Exists("data"))
    {
        data = params["data"];
        replace_str(data, "__##VALUE##__", val);
    }
    else
    {
        cErrorDom("mqtt") << "No data provided in configuration IO";
        return;
    }

    cDebugDom("mqtt") << "Publish " << data << " on topic" << topic;

    publishTopic(topic, data);
}

void MqttCtrl::setValue(const Params &params, bool val)
{
    string on_value = "on";
    string off_value = "off";
    string topic = params["topic_pub"];
    string data;

    if (params.Exists("on_value"))
        on_value = params["on_value"];

    if (params.Exists("off_value"))
        off_value = params["off_value"];

    if (params.Exists("data"))
    {
        data = params["data"];
        replace_str(data, "__##VALUE##__", val ? on_value : off_value);
    }
    else
    {
        cErrorDom("mqtt") << "No data provided in configuration IO";
        return;
    }

    cDebugDom("mqtt") << "Publish " << data << " on topic" << topic;

    publishTopic(topic, data);
}

void MqttCtrl::setValueInt(const Params &params, int val, string dataParam)
{
    string topic = params["topic_pub"];
    string data;

    if (params.Exists(dataParam))
    {
        data = params[dataParam];
        double coeff_a, coeff_b;
        if (params.Exists("coeff_a"))
            Utils::from_string(params["coeff_a"], coeff_a);
        else
            coeff_a = 1.0;

        if (params.Exists("coeff_b"))
            Utils::from_string(params["coeff_b"], coeff_b);
        else
            coeff_b = 0.0;


        replace_str(data, "__##VALUE##__", Utils::to_string((int)(val * coeff_a + coeff_b)));
    }
    else
    {
        cErrorDom("mqtt") << "No data provided in configuration IO";
        return;
    }

    cDebugDom("mqtt") << "Publish " << data << " on topic" << topic;

    publishTopic(topic, data);
}

void MqttCtrl::setValueColor(const Params &params, ColorValue val)
{
    string data;
    string topic = params["topic_pub"];

    if (params.Exists("data"))
    {
        data = params["data"];
        replace_str(data, "__##VALUE_R##__", Utils::to_string(val.getRed()));
        replace_str(data, "__##VALUE_G##__", Utils::to_string(val.getGreen()));
        replace_str(data, "__##VALUE_B##__", Utils::to_string(val.getBlue()));
        double x, y, b;
        val.toXYBrightness(x, y, b);
        replace_str(data, "__##VALUE_X##__", Utils::to_string(x));
        replace_str(data, "__##VALUE_Y##__", Utils::to_string(y));
        replace_str(data, "__##VALUE_BRIGHTNESS##__", Utils::to_string(b * 255.0));
        replace_str(data, "__##VALUE_HEX##__", val.toString());
    }
    else
    {
        cErrorDom("mqtt") << "No data provided in configuration IO";
        return;
    }

    cDebugDom("mqtt") << "Publish " << data << " on topic" << topic;

    publishTopic(topic, data);
}

void MqttCtrl::commonDoc(IODoc *ioDoc)
{
    ioDoc->paramAdd("host", _("IP address of the mqtt broker to connect to. Default value is 127.0.0.1."), IODoc::TYPE_STRING, false, "127.0.0.1");
    ioDoc->paramAdd("port", _("TCP port of the mqtt broker. Default value is 1883"), IODoc::TYPE_INT, false, "1883");
    ioDoc->paramAdd("keepalive", _("keepalive timeout in seconds. Time between two mqtt PING."), IODoc::TYPE_INT, false, "120");

    ioDoc->paramAdd("password", _("Password to use for authentication with mqtt broker. User must be defined in that case."), IODoc::TYPE_STRING, false);
    ioDoc->paramAdd("user", _("User to use for authentication with mqtt broker. Password must be defined in that case."), IODoc::TYPE_STRING, false);

    ioDoc->paramAdd("topic_pub", _("Topic on witch to publish."), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("topic_sub", _("Topic on witch to subscribe."), IODoc::TYPE_STRING, true);

    ioDoc->paramAdd("path", _("The path where to find the value in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. for example weather[0]/description, try to read the description value of the 1 element of the array of the weather object. if payload is simple json, just try to use the key of the value you want to read, for example : {\"temperature\":14.23} use \"temperature\" as path"), IODoc::TYPE_STRING, true);

    ioDoc->paramAdd("battery_topic", _("The topic on witch to publish the battery status of the sensor. If not set, no battery status will be reported."), IODoc::TYPE_STRING, false);
    ioDoc->paramAdd("battery_path", _("The path where to find the battery status in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. for example weather[0]/description, try to read the description value of the 1 element of the array of the weather object. If payload is simple json object, just try to use the key of the value you want to read, for example : {\"battery\":90} use \"battery\" as path. When this path is set, and the level drops below 30%. The battery reported should be in percent. Use `battery_expr` to adjust if required."), IODoc::TYPE_STRING, false);
    ioDoc->paramAdd("battery_expr", _("If the battery value is not directly available in the payload, you can use this parameter to calculate the battery value from the payload. The value will be calculated as any valid mathematic expression. In the expression, the variable x is replaced with the raw value from path. If not set, the battery value will be read directly from the path. Example: \"x * 100 / 255\""), IODoc::TYPE_STRING, false);

    ioDoc->paramAdd("connected_status_topic", _("The topic on witch to publish the connected status of the sensor. If not set, no connected status will be reported."), IODoc::TYPE_STRING, false);
    ioDoc->paramAdd("connected_status_path", _("The path where to find the connected status in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. for example weather[0]/description, try to read the description value of the 1 element of the array of the weather object. If payload is simple json, just try to use the key of the value you want to read, for example : {\"connected\":true} use \"connected\" as path. The value should be a boolean. Use connected_status_expr to convert to a boolean"), IODoc::TYPE_STRING, false);
    ioDoc->paramAdd("connected_status_expr", _("If the connected status value is not directly available in the payload, you can use this parameter to convert the value from the path to a boolean. The value will be calculated as any valid mathematic expression. In the expression, the variable `value` is replaced with the raw value from path. If not set, the connected status will be read directly from the path. Example: \"value == 'connected'\" or \"value > 30 and value < 150\""), IODoc::TYPE_STRING, false);

    ioDoc->paramAdd("wireless_signal_topic", _("The topic on witch to publish the wireless signal strength of the sensor. If not set, no wireless signal strength will be reported."), IODoc::TYPE_STRING, false);
    ioDoc->paramAdd("wireless_signal_path", _("The path where to find the wireless signal strength in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. for example weather[0]/description, try to read the description value of the 1 element of the array of the weather object. If payload is simple json, just try to use the key of the value you want to read, for example : {\"signal\": 70} use \"signal\" as path. The value should be a number in percent. Use `wireless_signal_expr` to adjust if required."), IODoc::TYPE_STRING, false);
    ioDoc->paramAdd("wireless_signal_expr", _("If the wireless signal value is not directly available in the payload, you can use this parameter to calculate the wireless signal value from the payload. The value will be calculated as any valid mathematic expression. In the expression, the variable x is replaced with the raw value from path. If not set, the wireless signal value will be read directly from the path. Example: \"x * 100 / 255\""), IODoc::TYPE_STRING, false);

    ioDoc->paramAdd("uptime_topic", _("The topic on witch to publish the uptime of the sensor. If not set, no uptime will be reported."), IODoc::TYPE_STRING, false);
    ioDoc->paramAdd("uptime_path", _("The path where to find the uptime of the sensor in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. for example weather[0]/description, try to read the description value of the 1 element of the array of the weather object. If payload is simple json, just try to use the key of the value you want to read, for example : {\"uptime\": 3600} use \"uptime\" as path. The value should be a number in seconds. Use `uptime_expr` to adjust if required."), IODoc::TYPE_STRING, false);
    ioDoc->paramAdd("uptime_expr", _("If the uptime value is not directly available in the payload, you can use this parameter to calculate the uptime value from the payload. The value will be calculated as any valid mathematic expression. In the expression, the variable x is replaced with the raw value from path. If not set, the uptime value will be read directly from the path. Example: \"x * 100 / 255\""), IODoc::TYPE_STRING, false);

    ioDoc->paramAdd("ip_address_topic", _("The topic on witch to publish the IP address of the sensor. If not set, no IP address will be reported."), IODoc::TYPE_STRING, false);
    ioDoc->paramAdd("ip_address_path", _("The path where to find the IP address of the sensor in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. for example weather[0]/description, try to read the description value of the 1 element of the array of the weather object. If payload is simple json, just try to use the key of the value you want to read, for example : {\"ip_address\": \"192.168.1.156\"} use \"ip_address\" as path. The value should be a string with the IP address."), IODoc::TYPE_STRING, false);

    ioDoc->paramAdd("wifi_ssid_topic", _("The topic on witch to publish the WiFi SSID of the sensor. If not set, no WiFi SSID will be reported."), IODoc::TYPE_STRING, false);
    ioDoc->paramAdd("wifi_ssid_path", _("The path where to find the WiFi SSID where the sensor is connected in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. for example weather[0]/description, try to read the description value of the 1 element of the array of the weather object. If payload is simple json, just try to use the key of the value you want to read, for example : {\"wifi_ssid\": \"MyWifi\"} use \"wifi_ssid\" as path. The value should be a string with the WiFi SSID."), IODoc::TYPE_STRING, false);

    ioDoc->paramAdd("notif_battery", _("If set, a notification will be sent when the battery level drops below 30%. This is only used if the battery_topic is set."), IODoc::TYPE_BOOL, false, "true");
    ioDoc->paramAdd("notif_connected", _("If set, a notification will be sent when the connected status changes. This is only used if the connected_status_topic is set."), IODoc::TYPE_BOOL, false, "false");
}

// Does a topic match a subscription?
bool MqttCtrl::topicMatchesSubscription(string s, string t)
{
    const char *sub = s.c_str();
    const char *topic = t.c_str();
    size_t spos;
    bool result = false;

    if (!sub || !topic || sub[0] == 0 || topic[0] == 0)
    {
        return result;
    }

    if ((sub[0] == '$' && topic[0] != '$') || (topic[0] == '$' && sub[0] != '$'))
    {
        return result;
    }

    spos = 0;

    while (sub[0] != 0)
    {
        if (topic[0] == '+' || topic[0] == '#')
        {
            return result;
        }
        if (sub[0] != topic[0] || topic[0] == 0)
        { /* Check for wildcard matches */
            if (sub[0] == '+')
            {
                /* Check for bad "+foo" or "a/+foo" subscription */
                if (spos > 0 && sub[-1] != '/')
                {
                    return result;
                }
                /* Check for bad "foo+" or "foo+/a" subscription */
                if (sub[1] != 0 && sub[1] != '/')
                {
                    return result;
                }
                spos++;
                sub++;
                while (topic[0] != 0 && topic[0] != '/')
                {
                    if (topic[0] == '+' || topic[0] == '#')
                    {
                        return result;
                    }
                    topic++;
                }
                if (topic[0] == 0 && sub[0] == 0)
                {
                    result = true;
                    return result;
                }
            }
            else if (sub[0] == '#')
            {
                /* Check for bad "foo#" subscription */
                if (spos > 0 && sub[-1] != '/')
                {
                    return result;
                }
                /* Check for # not the final character of the sub, e.g. "#foo" */
                if (sub[1] != 0)
                {
                    return result;
                }
                else
                {
                    while (topic[0] != 0)
                    {
                        if (topic[0] == '+' || topic[0] == '#')
                        {
                            return result;
                        }
                        topic++;
                    }
                    result = true;
                    return result;
                }
            }
            else
            {
                /* Check for e.g. foo/bar matching foo/+/# */
                if (topic[0] == 0 && spos > 0 && sub[-1] == '+' && sub[0] == '/' && sub[1] == '#')
                {
                    result = true;
                    return result;
                }

                /* There is no match at this point, but is the sub invalid? */
                while (sub[0] != 0)
                {
                    if (sub[0] == '#' && sub[1] != 0)
                    {
                        return result;
                    }
                    spos++;
                    sub++;
                }

                /* Valid input, but no match */
                return result;
            }
        }
        else
        {
            /* sub[spos] == topic[tpos] */
            if (topic[1] == 0)
            {
                /* Check for e.g. foo matching foo/# */
                if (sub[1] == '/' && sub[2] == '#' && sub[3] == 0)
                {
                    result = true;
                    return result;
                }
            }
            spos++;
            sub++;
            topic++;
            if (sub[0] == 0 && topic[0] == 0)
            {
                result = true;
                return result;
            }
            else if (topic[0] == 0 && sub[0] == '+' && sub[1] == 0)
            {
                if (spos > 0 && sub[-1] != '/')
                {
                    return result;
                }
                spos++;
                sub++;
                result = true;
                return result;
            }
        }
    }
    if ((topic[0] != 0 || sub[0] != 0))
    {
        result = false;
    }
    while (topic[0] != 0)
    {
        if (topic[0] == '+' || topic[0] == '#')
        {
            return result;
        }
        topic++;
    }

    return result;
}

void MqttCtrl::subscribeStatusTopics(Calaos::IOBase *io)
{
    auto &params = io->get_params();

    // Subscribe to the status topics if they are defined
    if (params.Exists("battery_topic"))
    {
        subscribeTopic(params["battery_topic"], [=](string, string)
        {
            bool err;
            auto v = getValue(params, err, "battery_topic", "battery_path");
            if (!err)
            {
                double rawValue;
                Utils::from_string(v, rawValue);

                if (params.Exists("battery_expr") &&
                    ExpressionEvaluator::isExpressionValid(params["battery_expr"]))
                {
                    double dval = ExpressionEvaluator::calculateExpression(params["battery_expr"], rawValue, err);

                    if (!err)
                    {
                        cDebugDom("mqtt") << "Battery value calculated: " << dval;

                        io->setStatusInfo(IOBase::StatusType::BatteryLevel, dval);

                        EventManager::create(CalaosEvent::EventIOStatusChanged,
                                             io->get_param("id"),
                                             io->getStatusInfo());
                    }
                }
                else
                {
                    cDebugDom("mqtt") << "Battery value read directly from path: " << v;

                    io->setStatusInfo(IOBase::StatusType::BatteryLevel, rawValue);

                    EventManager::create(CalaosEvent::EventIOStatusChanged,
                                             io->get_param("id"),
                                             io->getStatusInfo());
                }
            }
        });
    }

    if (params.Exists("connected_status_topic"))
    {
        subscribeTopic(params["connected_status_topic"], [=](string, string)
        {
            bool err;
            auto v = getValue(params, err, "connected_status_topic", "connected_status_path");
            if (!err)
            {
                bool devconnected = false;

                if (params.Exists("connected_status_expr"))
                    devconnected = ExpressionEvaluator::evaluateExpressionBool(params["connected_status_expr"], v, err);
                else
                    err = true;

                if (!err)
                {
                    cDebugDom("mqtt") << "Connected status evaluated using \"" << params["connected_status_expr"] << "\" with value=" << v << " : " << devconnected;

                    io->setStatusInfo(IOBase::StatusType::Connected, devconnected? IOBase::StatusConnected::STATUS_CONNECTED : IOBase::StatusConnected::STATUS_DISCONNECTED);
                    EventManager::create(CalaosEvent::EventIOStatusChanged,
                                            io->get_param("id"),
                                            io->getStatusInfo());
                }
                else
                {
                    cDebugDom("mqtt") << "Connected status read directly from path: " << v;

                    devconnected = (v == "true" || v == "1" || v == "yes");
                    io->setStatusInfo(IOBase::StatusType::Connected, devconnected? IOBase::StatusConnected::STATUS_CONNECTED : IOBase::StatusConnected::STATUS_DISCONNECTED);
                    EventManager::create(CalaosEvent::EventIOStatusChanged,
                                             io->get_param("id"),
                                             io->getStatusInfo());
                }
            }
        });
    }

    if (params.Exists("wireless_signal_topic"))
    {
        subscribeTopic(params["wireless_signal_topic"], [=](string, string)
        {
            bool err;
            auto v = getValue(params, err, "wireless_signal_topic", "wireless_signal_path");
            if (!err)
            {
                double rawValue;
                Utils::from_string(v, rawValue);

                if (params.Exists("wireless_signal_expr") &&
                    ExpressionEvaluator::isExpressionValid(params["wireless_signal_expr"]))
                {
                    double dval = ExpressionEvaluator::calculateExpression(params["wireless_signal_expr"], rawValue, err);

                    if (!err)
                    {
                        cDebugDom("mqtt") << "Wireless signal value calculated: " << dval;

                        io->setStatusInfo(IOBase::StatusType::WirelessSignal, dval);
                        EventManager::create(CalaosEvent::EventIOStatusChanged,
                                             io->get_param("id"),
                                             io->getStatusInfo());
                    }
                }
                else
                {
                    cDebugDom("mqtt") << "Wireless signal value read directly from path: " << v;

                    io->setStatusInfo(IOBase::StatusType::WirelessSignal, rawValue);
                    EventManager::create(CalaosEvent::EventIOStatusChanged,
                                             io->get_param("id"),
                                             io->getStatusInfo());
                }
            }
        });
    }

    if (params.Exists("uptime_topic"))
    {
        subscribeTopic(params["uptime_topic"], [=](string, string)
        {
            bool err;
            auto v = getValue(params, err, "uptime_topic", "uptime_path");
            if (!err)
            {
                double rawValue;
                Utils::from_string(v, rawValue);

                if (params.Exists("uptime_expr") &&
                    ExpressionEvaluator::isExpressionValid(params["uptime_expr"]))
                {
                    double dval = ExpressionEvaluator::calculateExpression(params["uptime_expr"], rawValue, err);

                    if (!err)
                    {
                        cDebugDom("mqtt") << "Uptime value calculated: " << dval;

                        io->setStatusInfo(IOBase::StatusType::Uptime, static_cast<uint64_t>(dval));
                        EventManager::create(CalaosEvent::EventIOStatusChanged,
                                             io->get_param("id"),
                                             io->getStatusInfo());
                    }
                }
                else
                {
                    cDebugDom("mqtt") << "Uptime value read directly from path: " << v;

                    io->setStatusInfo(IOBase::StatusType::Uptime, static_cast<uint64_t>(rawValue));
                    EventManager::create(CalaosEvent::EventIOStatusChanged,
                                             io->get_param("id"),
                                             io->getStatusInfo());
                }
            }
        });
    }

    if (params.Exists("ip_address_topic"))
    {
        subscribeTopic(params["ip_address_topic"], [=](string, string)
        {
            bool err;
            auto v = getValue(params, err, "ip_address_topic", "ip_address_path");
            if (!err)
            {
                cDebugDom("mqtt") << "IP address read from path: " << v;

                io->setStatusInfo(IOBase::StatusType::IpAddress, v);
                EventManager::create(CalaosEvent::EventIOStatusChanged,
                                             io->get_param("id"),
                                             io->getStatusInfo());
            }
        });
    }

    if (params.Exists("wifi_ssid_topic"))
    {
        subscribeTopic(params["wifi_ssid_topic"], [=](string, string)
        {
            bool err;
            auto v = getValue(params, err, "wifi_ssid_topic", "wifi_ssid_path");
            if (!err)
            {
                cDebugDom("mqtt") << "WiFi SSID read from path: " << v;

                io->setStatusInfo(IOBase::StatusType::WifiSSID, v);
                EventManager::create(CalaosEvent::EventIOStatusChanged,
                                             io->get_param("id"),
                                             io->getStatusInfo());
            }
        });
    }
}
