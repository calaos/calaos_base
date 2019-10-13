#ifndef __MQTT_CTRL_H__
#define __MQTT_CTRL_H__

#include <unordered_map>

#include <mosquittopp.h>
#include "Params.h"
#include "Utils.h"
#include "IODoc.h"

#include "Calaos.h"
#include "ExternProc.h"

class MqttCtrl : public sigc::trackable
{
public:
	MqttCtrl(const Params &p);
	~MqttCtrl();

    void subscribeTopic(const string topic, sigc::slot<void> callback);
    void publishTopic(const string topic, const string payload);

    string getValueJson(string path, string payload);
    string getValue(const Params &params, bool &err);
    double getValueDouble(const Params &params, bool &err);
    static void commonDoc(IODoc *ioDoc);
    void   setValue(const Params &params, bool val);
    void  setValueString(const Params &params, string val);
    void  setValueInt(const Params &params, int val);

private:
    ExternProcServer *process;
    string exe;

    unordered_map<string, vector<sigc::slot<void>>> subscribeCb;
    unordered_map<string, string> messages;
    bool connected = false;
};


#endif // __MQTT_CTRL_H__
