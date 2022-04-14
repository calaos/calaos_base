#include <json.hpp>

#include "Utils.h"
#include "IOFactory.h"
#include "MqttCtrl.h"
#include "Prefix.h"
#include "Params.h"

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
        cDebugDom("mqtt") << "New message received " << msg;
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
        for (auto& it: subscribeCb) {
            if (topicMatchesSubscription(it.first, p["topic"]))
        {
                auto cb = it.second[0];
                cb(p["topic"], p["payload"]);
        }
        }

        json_decref(jroot); });

    process->startProcess(exe, "mqtt", arg);
}

MqttCtrl::~MqttCtrl()
{
}

void MqttCtrl::subscribeTopic(const string topic, sigc::slot<void, string, string> callback)
{
    // subscribeCb contains a map of list of callbacks, register this callback to the key  relative of this topic
    cDebugDom("mqtt") << "Topic : " << topic;
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
                // Toke is a normal object name
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
        {
            double v;
            double coeff_a, coeff_b;
            if (params.Exists("coeff_a"))
                Utils::from_string(params["coeff_a"], coeff_a);
            else
                coeff_a = 1.0;

            if (params.Exists("coeff_b"))
                Utils::from_string(params["coeff_b"], coeff_b);
            else
                coeff_b = 0.0;

            v = (parent.get<double>() - coeff_b) / coeff_a;

            value = Utils::to_string(v);
        }
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
        cWarning() << "Error emtpy path not allowed";
    }

    return value;
}

string MqttCtrl::getValue(const Params &params, bool &err)
{
    string type = params["type"];

    if (!params.Exists("topic_sub") || !params.Exists("path"))
    {
        cDebugDom("mqtt") << "Topic or path does not exists" << params["topic_sub"] << " " << params["path"];
        err = true;
        return "";
    }

    string payload = messages[params["topic_sub"]];

    if (payload.empty())
    {
        cDebugDom("mqtt") << "No message received for topic " << params["topic_sub"] << " yet";
        err = true;
        return "";
    }
    err = false;
    return getValueJson(params, params["path"], payload);
}

double MqttCtrl::getValueDouble(const Params &params, bool &err)
{
    double val = 0;
    string value;
    err = true;

    value = getValue(params, err);

    if (Utils::is_of_type<double>(value) && !value.empty())
    {
        Utils::from_string(value, val);
        err = false;
    }

    return val;
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

void MqttCtrl::setValueInt(const Params &params, int val)
{
    string topic = params["topic_pub"];
    string data;

    if (params.Exists("data"))
    {
        data = params["data"];
        double coeff_a, coeff_b;
        if (params.Exists("coeff_a"))
            Utils::from_string(params["coeff_a"], coeff_a);
        else
            coeff_a = 1.0;

        if (params.Exists("coeff_b"))
            Utils::from_string(params["coeff_b"], coeff_b);
        else
            coeff_b = 0.0;


        replace_str(data, "__##VALUE##__", to_string((int)(val * coeff_a + coeff_b)));
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

    ioDoc->paramAdd("path", _("The path where to found the value in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. for example weather[0]/description, try to read the description value of the 1 element of the array of the weather object. if payload is somple json, just try to use the key of the value you want to read, for example : {\"temperature\":14.23} use \"temperature\" as path\n"), IODoc::TYPE_STRING, true);

    // user, password, keepalive
}

/* Does a topic match a subscription? */
/* Does a topic match a subscription? */
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
