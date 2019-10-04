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

    if (p.Exists("broker"))
        broker = p["broker"];
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
        cDebugDom("mqtt") << "Error connecting to host : " << broker;
        break;
    case MOSQ_ERR_SUCCESS:
        loop_start();
        break;
    default:
        cDebugDom("mqtt") << "Error connecting : " << strerror(res);
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
    auto v = subscribeCb[topic];
    v.push_back(callback);
    subscribeCb[topic] = v;

    cDebugDom("mqtt") << "Subscribing to topic " << topic;

    // mosquitto subscribe call
    subscribe(NULL, topic.c_str());
}

void MqttClient::on_connect(int rc)
{
    cDebugDom("mqtt") << "Connected with code"  << rc;

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


string MqttClient::getValue(const Params &params)
{
    string type = params["type"];


    if (!params.Exists("topic") || !params.Exists("path"))
    {
        cDebugDom("mqtt") << "Topic or path does not exists";
        return "";
    }

    const struct mosquitto_message *m = messages[params["topic"]];

    if (!m)
    {
        cDebugDom("mqtt") << "No message received for topic " << params["topic"] << " yet";
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
    value = getValue(params);

    if (Utils::is_of_type<double>(value) && !value.empty())
    {
        Utils::from_string(value, val);
        err = false;
    }

    return val;
}
