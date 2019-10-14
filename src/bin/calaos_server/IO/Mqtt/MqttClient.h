#ifndef __MQTT_CLIENT_H__
#define __MQTT_CLIENT_H__

#include <unordered_map>

#include <mosquittopp.h>
#include "Params.h"
#include "Utils.h"
#include "IODoc.h"


class MqttClient : public mosqpp::mosquittopp
{
public:
	MqttClient(const Params &p);
	~MqttClient();

    void subscribeTopic(const string topic, sigc::slot<void> callback);
    void publishTopic(const string topic, const string payload);

	void on_connect(int rc);
	void on_message(const struct mosquitto_message *message);
	void on_subcribe(int mid, int qos_count, const int *granted_qos);
    void on_error();
    void on_log(int level, const char *str);

    string getValueJson(string path, string payload);
    string getValue(const Params &params, bool &err);
    double getValueDouble(const Params &params, bool &err);
    static void commonDoc(IODoc *ioDoc);
    void   setValue(const Params &params, bool val);
    void  setValueString(const Params &params, string val);
    void  setValueInt(const Params &params, int val);

private:
    std::unordered_map<string, std::vector<sigc::slot<void>>> subscribeCb;
    std::unordered_map<string, struct mosquitto_message*> messages;
    bool connected = false;
};


#endif // __MQTT_CLIENT_H__
