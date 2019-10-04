#ifndef __MQTT_CLIENT_H__
#define __MQTT_CLIENT_H__

#include <mosquittopp.h>
#include "Params.h"
#include "Utils.h"
#include <unordered_map>

class MqttClient : public mosqpp::mosquittopp
{
public:
	MqttClient(const Params &p);
	~MqttClient();

    std::vector<sigc::signal<void>> subscribeCb;
    std::unordered_map<string, struct mosquitto_message*> messages;
    void subscribeTopic(const string topic, sigc::slot<void> callback);

	void on_connect(int rc);
	void on_message(const struct mosquitto_message *message);
	void on_subcribe(int mid, int qos_count, const int *granted_qos);
    void on_error();
    void on_log(int level, const char *str);

    string getValueJson(string path, string payload);
    string getValue(const Params &params);
    double getValueDouble(const Params &params);
};


#endif // __MQTT_CLIENT_H__
