#include <json.hpp>

#include "Utils.h"
#include "IOFactory.h"
#include "MqttCtrl.h"
#include "Prefix.h"

using namespace Calaos;

MqttCtrl::MqttCtrl(const Params &params)
{
    string host = "127.0.0.1";
    int port = 1883;
    string username = "";
    string password = "";

    cDebugDom("mqtt") << "New MQTT external process " << host << ":" << port;
    process = new ExternProcServer("mqtt");
    exe = Prefix::Instance().binDirectoryGet() + "/calaos_mqtt";

    json_t *root = json_object();
    json_object_set_new(root, "host", json_string(params["host"].c_str()));
    json_object_set_new(root, "port", json_string(params["port"].c_str()));
    json_object_set_new(root, "keepalive", json_string(params["keepalive"].c_str()));

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
        for(auto cb : subscribeCb[p["topic"]])
        {
            cb();
        }
        json_decref(jroot);
    });

    process->startProcess(exe, "mqtt", arg);

}

MqttCtrl::~MqttCtrl()
{
}

void MqttCtrl::subscribeTopic(const string topic, sigc::slot<void> callback)
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


string MqttCtrl::getValueJson(string path, string payload)
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
        for (auto it = tokens.begin();it != tokens.end();it++)
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
            value = parent.get<bool>()?"true":"false";
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
    return getValueJson(params["path"], payload);
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
        replace_str(data, "__##VALUE##__", to_string((int)(val * 2.55)));
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

    //user, password, keepalive
}
