#include <json.hpp>

#include "Utils.h"
#include "IOFactory.h"
#include "MqttCtrl.h"
#include "MqttWire.h"
#include "Prefix.h"
#include "Params.h"
#include "ExpressionEvaluator.h"

using namespace Calaos;

MqttCtrl::MqttCtrl(const Params &params)
{
    string host, port, keepalive;

    //use default parameters if not set from config
    MqttWire::resolveBroker(params, host, port, keepalive);

    cDebugDom("mqtt") << "New MQTT external process " << host << ":" << port;
    process = new ExternProcServer("mqtt");
    exe = Prefix::Instance().binDirectoryGet() + "/calaos_mqtt";

    string arg = MqttWire::encodeConfig(params);

    process->processExited.connect([=]()
    {
        //restart process when stopped
        cWarningDom("process") << "process exited, restarting...";
        process->startProcess(exe, "mqtt", arg);
    });

    process->messageReceived.connect([=](const string &msg)
    {
        Params p;

        if (!MqttWire::decodeMessage(msg, p))
        {
            cWarningDom("mqtt") << "Error parsing json: " << msg;
            return;
        }

        cDebugDom("mqtt") << "Topic :  " << p["topic"] << " payload : " << p["payload"];

        // Set or replace the message
        storeMessage(p["topic"], p["payload"]);
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
    });

    process->startProcess(exe, "mqtt", arg);
}

MqttCtrl::~MqttCtrl()
{
}

void MqttCtrl::storeMessage(const string &topic, const string &payload)
{
    //T3.35b. Extracted so that the only way a message enters this object has a
    //name, and so that the error-flag suite can drive getValue() without a
    //broker. Behaviour identical to the assignment it replaces.
    messages[topic] = payload;
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
    process->sendMessage(MqttWire::encodeMessage(topic, payload));
}

string MqttCtrl::getValueJson(const Params &params, string path, string payload)
{
    bool err = false;
    return getValueJson(params, path, payload, err);
}

string MqttCtrl::getValueJson(const Params &params, string path, string payload, bool &err)
{
    string value;

    err = false;

    // If path is empty, treat payload as direct raw value
    if (path.empty())
    {
        cDebugDom("mqtt") << "Path is empty, returning raw payload value";
        value = payload;

        cDebugDom("mqtt") << "Returning value: " << value;
        return value;
    }

    // Original JSON parsing logic for non-empty paths.
    // Non throwing form: the payload comes from a third party device.
    const Json root = Json::parse(payload, nullptr, /*allow_exceptions=*/false);

    if (root.is_discarded())
    {
        err = true;
        cWarning() << "Error parsing " << payload;
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
                /* T3.35, corrected by T3.35b. A well formed index token is
                 * "[n]" - an opening bracket AND a closing one. erase() and
                 * pop_back() below strip the first and the last character
                 * UNCONDITIONALLY, so what has to be checked here is the FORM.
                 * A guard on the LENGTH alone (the T3.35 shape, `val.size() <
                 * 2`) covered only half of it and the message it printed was
                 * not true of the code that printed it:
                 *
                 *  - a lone '[' was EMPTIED by erase(), pop_back() then
                 *    underflowed the size_t length of the string, and the read
                 *    that followed escaped getValueJson() as a std::bad_alloc.
                 *    Nothing caught it anywhere up to main() and calaos_server
                 *    terminated. There is no remote vector - a `path` is only
                 *    ever written by calaos_installer - but a typo was enough
                 *    to bring the server down. A length guard does stop that.
                 *
                 *  - "[5" and "[12" are two characters or more, so a length
                 *    guard let them straight through; pop_back() then ate a
                 *    DIGIT and the parser answered element 0 and element 1,
                 *    SILENTLY, with a value nothing distinguishes from a
                 *    correct reading. That is worse than the empty string the
                 *    same typo produces everywhere else in this parser.
                 */
                if (val.size() < 2 || val.back() != ']')
                {
                    err = true;
                    cWarning() << "Error in path " << path << ", malformed array index " << *it
                               << " : an array index must be written [n], as in weather/[0]/description";
                    return string();
                }

                // Remove first and last char
                val.erase(0, 1);
                val.pop_back();

                int idx = 0;

                try
                {
                    /* T3.35b. The index is DECIDED here, on both branches,
                     * instead of being left to whatever Utils::from_string()
                     * happens to leave behind. The two failing shapes do not
                     * behave the same way and that asymmetry was the trap:
                     * on a BLANK string - the token "[]" - the stream sentry
                     * fails before num_get ever runs, so the destination is
                     * NOT written and the index was read UNINITIALISED; on a
                     * non blank string that does not parse - "[zz]" - the
                     * sentry succeeds and C++11 num_get stores 0. from_string()
                     * cannot even be interrogated about it: it returns
                     * iss.eof(), which is TRUE for the blank string.
                     *
                     * The two cases are made to agree, deliberately, on
                     * element 0: T3.29 froze that value and a real
                     * configuration may lean on it. What does not stay is the
                     * SILENCE - reading element 0 because the index was
                     * unreadable is precisely the case a user cannot diagnose.
                     *
                     * Everything that touches the index sits inside this try,
                     * so it can only ever fail through the one error path this
                     * branch already has.
                     *
                     * T3.35c. THE TEST IS "DOES IT CARRY A DIGIT", NOT "DOES
                     * from_string() COMPLAIN". from_string() returns
                     * iss.eof(), and a stream that consumed only whitespace -
                     * or only a sign - DID reach its end, so it reports
                     * SUCCESS on "[ ]", "[\t]", "[+]" and "[-]". On the two
                     * blank ones it does not write the destination either,
                     * which is how the index was still being read UNASSIGNED
                     * after T3.35b: `val.empty()` catches "[]" and nothing
                     * else. find_first_of() closes the whole family in one
                     * test, and it subsumes val.empty() - an empty string has
                     * no digit - so no sub-condition here is dead.
                     *
                     * WHY NOT "every character must be a digit"
                     * (find_first_not_of): it would test the WRONG thing
                     * three ways. An empty string has no NON-digit either, so
                     * "[]" would walk back through unguarded; "[+2]" would
                     * stop resolving; and "[-1]" would be answered "is not a
                     * number", which is false about -1. The sign is NOT
                     * rejected here, deliberately: a signed or padded token
                     * carries a number and goes on to at(), which refuses a
                     * negative index as out of range - a different message
                     * for a different mistake.
                     *
                     * INVARIANT this establishes, and the reason the
                     * `int idx = 0` above is now GENUINELY dead - it is kept
                     * as a belt, it is no longer the value anything reads:
                     * when the guard passes, val holds at least one digit, so the
                     * stream sentry succeeds, so num_get RUNS - and C++11
                     * num_get always stores something (the value, 0 on a
                     * failed parse, or the clamped limit on overflow). When
                     * the guard trips, idx = 0 is assigned. Every path into
                     * parent.at(idx) therefore writes idx first.
                     */
                    if (val.find_first_of("0123456789") == string::npos ||
                        !Utils::from_string(val, idx))
                    {
                        idx = 0;
                        cWarning() << "Error in path " << path << ", array index " << *it
                                   << " is not a number : reading element 0";
                    }

                    parent = parent.at(idx);
                }
                catch (const std::exception &e)
                {
                    err = true;
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
                    err = true;
                    cWarning() << "Error in path " << path << ", subpath not found " << *it << " : " << e.what();

                    /* T3.35 - the "option C" of T3.29 section 5.6. NO FALSE
                     * POSITIVE IS POSSIBLE HERE, by construction and not by
                     * heuristic: this catch is only ever entered when
                     * parent.at(val) has ALREADY thrown. A payload whose key
                     * really is spelled "action[0]" - a real, measured
                     * Zigbee2MQTT shape - has RESOLVED and never reaches this
                     * line. That is exactly what separates it from teaching
                     * the parser to also split a glued index, which T3.29
                     * implemented, measured and rejected because it shadowed
                     * such a key silently.
                     *
                     * Honest about its reach: cWarning() is not a filtered
                     * domain, so this does go to the calaos_server log by
                     * default - but the person who made the typo is sitting in
                     * calaos_installer.
                     */
                    if (val.find('[') != string::npos)
                    {
                        string suggestion = val;
                        suggestion.insert(suggestion.find('['), "/");
                        cWarning() << "Error in path " << path << ", did you mean " << suggestion
                                   << " ? array indices are their own path segment, not glued to "
                                      "the key that precedes them";
                    }

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
    else
    {
        //T3.35b. A non empty path that splits into no token at all ("///"):
        //nothing was resolved, so nothing is reported as resolved. The two
        //container MARKERS above keep err false on purpose - the path DID
        //resolve there, onto a container, and that frozen behaviour is what
        //APathStoppingOnAContainerReturnsAMarker pins.
        err = true;
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
    //T3.35b. `err` used to be cleared HERE, unconditionally, and getValueJson()
    //was then free to fail: `battery_path = "["` crossed the caller's
    //`if (!err)` with an empty value in hand. The flag now comes from the
    //parser itself.
    return getValueJson(params, params[path_param], payload, err);
}

double MqttCtrl::getValueDouble(const Params &params, bool &err)
{
    double val = 0;
    string value;
    err = true;

    value = getValue(params, err, "topic_sub");
    if (err)
        return val;

    if (Utils::is_of_type<double>(value) && !value.empty())
        Utils::from_string(value, val);
    else
        //T3.35b. This branch used to leave `err` at whatever getValue() had
        //set - false - so a value that is not a number was reported as a
        //successful reading of 0.
        err = true;

    return val;
}

ColorValue MqttCtrl::getValueColor(const Params &params, bool &err)
{
    /* T3.35b. Three corrections, all the same defect seen from three sides.
     *
     *  - x, y and b are INITIALISED. They were not, and no error flag could
     *    have saved them: `err` was CLEARED by any ONE of the three reads
     *    succeeding, so a device whose path_x is mistyped and whose path_y is
     *    not left err false and handed fromXYBrightness() an uninitialised x.
     *    That is true with or without T3.25, which is why this site is closed
     *    here and not left to it.
     *  - `err` is now the AND of the three reads, not their OR. All three
     *    parameters are declared MANDATORY in the ioDoc of MqttOutputLightRGB;
     *    a colour built out of two of them is not a colour, and
     *    MqttOutputLightRGB::readValue() reads err as "ignore this update",
     *    which is the right answer.
     *  - each value is checked for being a NUMBER, not merely for having been
     *    read: a path landing on an object answers the marker "object{}".
     */
    string value;
    double x = 0, y = 0;
    int b = 0;
    bool e = true;

    err = true;

    value = getValue(params, e, "topic_sub", "path_x");
    if (e || value.empty() || !Utils::is_of_type<double>(value))
        return {};
    Utils::from_string(value, x);

    value = getValue(params, e, "topic_sub", "path_y");
    if (e || value.empty() || !Utils::is_of_type<double>(value))
        return {};
    Utils::from_string(value, y);

    value = getValue(params, e, "topic_sub", "path_brightness");
    if (e || value.empty() || !Utils::is_of_type<int>(value))
        return {};
    Utils::from_string(value, b);

    err = false;

    return ColorValue::fromXYBrightness(x, y, b / 255.0);
}

bool MqttCtrl::readStatusNumber(const string &value, double &out)
{
    //T3.35b. Utils::from_string() does not write its destination on a blank
    //string - its stream sentry fails before num_get runs - and returns
    //iss.eof(), which is TRUE for that very string. Its return code therefore
    //cannot be used to detect the case; the string has to be checked first.
    if (value.empty() || !Utils::is_of_type<double>(value))
        return false;

    Utils::from_string(value, out);
    return true;
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
        //T3.35b. Initialised at their declaration: a coeff_a that EXISTS but
        //is not a number left Utils::from_string() with nothing to say and the
        //multiplication below read an uninitialised double.
        double coeff_a = 1.0, coeff_b = 0.0;
        if (params.Exists("coeff_a"))
            Utils::from_string(params["coeff_a"], coeff_a);

        if (params.Exists("coeff_b"))
            Utils::from_string(params["coeff_b"], coeff_b);


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

    ioDoc->paramAdd("path", _("The path where to find the value in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. Array indices are their own path segment, written between square brackets: for example weather/[0]/description reads the description value of the first element of the weather array. if payload is simple json, just try to use the key of the value you want to read, for example : {\"temperature\":14.23} use \"temperature\" as path"), IODoc::TYPE_STRING, true);

    ioDoc->paramAdd("battery_topic", _("The topic on witch to publish the battery status of the sensor. If not set, no battery status will be reported."), IODoc::TYPE_STRING, false);
    ioDoc->paramAdd("battery_path", _("The path where to find the battery status in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. Array indices are their own path segment, written between square brackets: for example weather/[0]/description reads the description value of the first element of the weather array. If payload is simple json object, just try to use the key of the value you want to read, for example : {\"battery\":90} use \"battery\" as path. When this path is set, and the level drops below 30%. The battery reported should be in percent. Use `battery_expr` to adjust if required."), IODoc::TYPE_STRING, false);
    ioDoc->paramAdd("battery_expr", _("If the battery value is not directly available in the payload, you can use this parameter to calculate the battery value from the payload. The value will be calculated as any valid mathematic expression. In the expression, the variable x is replaced with the raw value from path. If not set, the battery value will be read directly from the path. Example: \"x * 100 / 255\""), IODoc::TYPE_STRING, false);

    ioDoc->paramAdd("connected_status_topic", _("The topic on witch to publish the connected status of the sensor. If not set, no connected status will be reported."), IODoc::TYPE_STRING, false);
    ioDoc->paramAdd("connected_status_path", _("The path where to find the connected status in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. Array indices are their own path segment, written between square brackets: for example weather/[0]/description reads the description value of the first element of the weather array. If payload is simple json, just try to use the key of the value you want to read, for example : {\"connected\":true} use \"connected\" as path. The value should be a boolean. Use connected_status_expr to convert to a boolean"), IODoc::TYPE_STRING, false);
    ioDoc->paramAdd("connected_status_expr", _("If the connected status value is not directly available in the payload, you can use this parameter to convert the value from the path to a boolean. The value will be calculated as any valid mathematic expression. In the expression, the variable `value` is replaced with the raw value from path. If not set, the connected status will be read directly from the path. Example: \"value == 'connected'\" or \"value > 30 and value < 150\""), IODoc::TYPE_STRING, false);

    ioDoc->paramAdd("wireless_signal_topic", _("The topic on witch to publish the wireless signal strength of the sensor. If not set, no wireless signal strength will be reported."), IODoc::TYPE_STRING, false);
    ioDoc->paramAdd("wireless_signal_path", _("The path where to find the wireless signal strength in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. Array indices are their own path segment, written between square brackets: for example weather/[0]/description reads the description value of the first element of the weather array. If payload is simple json, just try to use the key of the value you want to read, for example : {\"signal\": 70} use \"signal\" as path. The value should be a number in percent. Use `wireless_signal_expr` to adjust if required."), IODoc::TYPE_STRING, false);
    ioDoc->paramAdd("wireless_signal_expr", _("If the wireless signal value is not directly available in the payload, you can use this parameter to calculate the wireless signal value from the payload. The value will be calculated as any valid mathematic expression. In the expression, the variable x is replaced with the raw value from path. If not set, the wireless signal value will be read directly from the path. Example: \"x * 100 / 255\""), IODoc::TYPE_STRING, false);

    ioDoc->paramAdd("uptime_topic", _("The topic on witch to publish the uptime of the sensor. If not set, no uptime will be reported."), IODoc::TYPE_STRING, false);
    ioDoc->paramAdd("uptime_path", _("The path where to find the uptime of the sensor in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. Array indices are their own path segment, written between square brackets: for example weather/[0]/description reads the description value of the first element of the weather array. If payload is simple json, just try to use the key of the value you want to read, for example : {\"uptime\": 3600} use \"uptime\" as path. The value should be a number in seconds. Use `uptime_expr` to adjust if required."), IODoc::TYPE_STRING, false);
    ioDoc->paramAdd("uptime_expr", _("If the uptime value is not directly available in the payload, you can use this parameter to calculate the uptime value from the payload. The value will be calculated as any valid mathematic expression. In the expression, the variable x is replaced with the raw value from path. If not set, the uptime value will be read directly from the path. Example: \"x * 100 / 255\""), IODoc::TYPE_STRING, false);

    ioDoc->paramAdd("ip_address_topic", _("The topic on witch to publish the IP address of the sensor. If not set, no IP address will be reported."), IODoc::TYPE_STRING, false);
    ioDoc->paramAdd("ip_address_path", _("The path where to find the IP address of the sensor in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. Array indices are their own path segment, written between square brackets: for example weather/[0]/description reads the description value of the first element of the weather array. If payload is simple json, just try to use the key of the value you want to read, for example : {\"ip_address\": \"192.168.1.156\"} use \"ip_address\" as path. The value should be a string with the IP address."), IODoc::TYPE_STRING, false);

    ioDoc->paramAdd("wifi_ssid_topic", _("The topic on witch to publish the WiFi SSID of the sensor. If not set, no WiFi SSID will be reported."), IODoc::TYPE_STRING, false);
    ioDoc->paramAdd("wifi_ssid_path", _("The path where to find the WiFi SSID where the sensor is connected in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. Array indices are their own path segment, written between square brackets: for example weather/[0]/description reads the description value of the first element of the weather array. If payload is simple json, just try to use the key of the value you want to read, for example : {\"wifi_ssid\": \"MyWifi\"} use \"wifi_ssid\" as path. The value should be a string with the WiFi SSID."), IODoc::TYPE_STRING, false);

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
                /* T3.35b. `double rawValue;` fed straight to
                 * Utils::from_string(), which does NOT write its destination
                 * when the string is blank. Before T3.35 a `battery_path`
                 * of "[" killed the server; after it the parser returned an
                 * empty string, getValue() reported success anyway, and this
                 * block published an INDETERMINATE battery - a lie the
                 * user reads in the interface and their rules act on, where
                 * the crash at least announced itself. getValue() is honest
                 * now, so `!err` no longer lets it in; this guard is the
                 * second lock, and it also catches the value that resolves but
                 * is not a number (a marker like "object{}", a string field).
                 */
                double rawValue = 0;
                if (!readStatusNumber(v, rawValue))
                {
                    cWarningDom("mqtt") << "Battery value \"" << v << "\" read from path "
                                        << params["battery_path"] << " is not a number, ignoring";
                    return;
                }

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
                /* T3.35b. `double rawValue;` fed straight to
                 * Utils::from_string(), which does NOT write its destination
                 * when the string is blank. Before T3.35 a `wireless_signal_path`
                 * of "[" killed the server; after it the parser returned an
                 * empty string, getValue() reported success anyway, and this
                 * block published an INDETERMINATE wireless signal - a lie the
                 * user reads in the interface and their rules act on, where
                 * the crash at least announced itself. getValue() is honest
                 * now, so `!err` no longer lets it in; this guard is the
                 * second lock, and it also catches the value that resolves but
                 * is not a number (a marker like "object{}", a string field).
                 */
                double rawValue = 0;
                if (!readStatusNumber(v, rawValue))
                {
                    cWarningDom("mqtt") << "Wireless signal value \"" << v << "\" read from path "
                                        << params["wireless_signal_path"] << " is not a number, ignoring";
                    return;
                }

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
                /* T3.35b. `double rawValue;` fed straight to
                 * Utils::from_string(), which does NOT write its destination
                 * when the string is blank. Before T3.35 a `uptime_path`
                 * of "[" killed the server; after it the parser returned an
                 * empty string, getValue() reported success anyway, and this
                 * block published an INDETERMINATE uptime - a lie the
                 * user reads in the interface and their rules act on, where
                 * the crash at least announced itself. getValue() is honest
                 * now, so `!err` no longer lets it in; this guard is the
                 * second lock, and it also catches the value that resolves but
                 * is not a number (a marker like "object{}", a string field).
                 */
                double rawValue = 0;
                if (!readStatusNumber(v, rawValue))
                {
                    cWarningDom("mqtt") << "Uptime value \"" << v << "\" read from path "
                                        << params["uptime_path"] << " is not a number, ignoring";
                    return;
                }

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
