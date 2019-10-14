#include <mosquittopp.h>
#include <json.hpp>

#include "Utils.h"
#include "IOFactory.h"
#include "MqttClient.h"

using namespace Calaos;

MqttClient::MqttClient(const Params &p) : mosquittopp("calaos")
{
    string broker = "127.0.0.1";
    int port = 1883;

    mosqpp::lib_init();

    if (p.Exists("host"))
        broker = p["host"];
    if (p.Exists("port"))
        Utils::from_string(p["port"], port);

    if (p.Exists("user") && p.Exists("password"))
        username_pw_set(p["user"].c_str(), p["password"].c_str());

    int keepalive = 120;
    if (p.Exists("keepalive"))
        Utils::from_string(p["keepalive"], keepalive);

    cDebugDom("mqtt") << "Connecting to broker " << broker << ":" << port;

    int res = connect_async(broker.c_str(), port, keepalive);

    switch (res)
    {
    case MOSQ_ERR_INVAL:
        cErrorDom("mqtt") << "Error connecting to host : " << broker;
        break;
    case MOSQ_ERR_SUCCESS:
        loop_start();
        break;
    default:
        cErrorDom("mqtt") << "Error connecting : " << strerror(res);
        break;
    }
}

MqttClient::~MqttClient()
{
    for (auto m : messages)
        free(m.second);
}

void MqttClient::subscribeTopic(const string topic, sigc::slot<void> callback)
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

    if (connected)
    {
        cDebugDom("mqtt") << "Subscribing to topic " << topic;
        // mosquitto subscribe call
        subscribe(NULL, topic.c_str());
    }
}

void MqttClient::publishTopic(const string topic, const string payload)
{
    publish(NULL, topic.c_str(), payload.size(), payload.c_str());
}


void MqttClient::on_connect(int rc)
{
    cDebugDom("mqtt") << "Connected with code "  << rc;

    if (!rc)
    {
         connected = true;
         for (auto t : subscribeCb)
         {
             cDebugDom("mqtt") << "Subscribing to topic " << t.first;
             subscribe(NULL, t.first.c_str());
         }
    }

}

void MqttClient::on_subcribe(int mid, int qos_count, const int *granted_qos)
{
    cDebugDom("mqtt") << "Subscription succeeded.";
}


void MqttClient::on_message(const struct mosquitto_message *message)
{
    cDebugDom("mqtt") << "New message received";

    struct mosquitto_message *m = (struct mosquitto_message*) calloc(sizeof(struct mosquitto_message), 1);

    // First copu the message
    mosquitto_message_copy(m, message);

    // If a message for this topic exists, free it
    if (messages.find(message->topic) != messages.end())
    {
        free(messages[message->topic]);
    }
    // Set or replace the message
    messages[message->topic] = m;

    // Call all callback registered for this topic
    for(auto cb : subscribeCb[m->topic])
    {
        cb();
    }
}

void MqttClient::on_log(int level, const char *str)
{
    cDebugDom("mqtt") << str;
}

void MqttClient::on_error()
{
    cErrorDom("mqtt") << "Error";
}

string MqttClient::getValueJson(string path, string payload)
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


string MqttClient::getValue(const Params &params, bool &err)
{
    string type = params["type"];
    err = false;
    if (!params.Exists("topic_sub") || !params.Exists("path"))
    {
        cDebugDom("mqtt") << "Topic or path does not exists" << params["topic_sub"] << " " << params["path"];
        err = true;
        return "";
    }

    const struct mosquitto_message *m = messages[params["topic_sub"]];

    if (!m)
    {
        // No message received for topic yet return error and empty value
        err = true;
        return "";
    }
    string payload((const char*)m->payload);
    return getValueJson(params["path"], payload);
}

double MqttClient::getValueDouble(const Params &params, bool &err)
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

void MqttClient::setValueString(const Params &params, string val)
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

    publish(NULL, topic.c_str(), data.size(), data.c_str());

}

void MqttClient::setValue(const Params &params, bool val)
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

    publish(NULL, topic.c_str(), data.size(), data.c_str());
}


void MqttClient::setValueInt(const Params &params, int val)
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

    publish(NULL, topic.c_str(), data.size(), data.c_str());
}

void MqttClient::commonDoc(IODoc *ioDoc)
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
