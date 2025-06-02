#ifndef __MQTT_CTRL_H__
#define __MQTT_CTRL_H__

#include <unordered_map>

#include "Params.h"
#include "Utils.h"
#include "IODoc.h"

#include "Calaos.h"
#include "ExternProc.h"
#include "IOBase.h"

class MqttCtrl : public sigc::trackable
{
public:
	MqttCtrl(const Params &p);
	~MqttCtrl();

    static void commonDoc(IODoc *ioDoc);

    typedef sigc::slot<void, string, string> MsgReceivedSignal;

    void subscribeTopic(const string topic, MsgReceivedSignal callback);
    void publishTopic(const string topic, const string payload);

    string getValueJson(const Params &params,string path, string payload);
    string getValue(const Params &params, bool &err, string topic_param, string path_param = "path");
    double getValueDouble(const Params &params, bool &err);
    ColorValue getValueColor(const Params &params, bool &err);

    void setValue(const Params &params, bool val);
    void setValueString(const Params &params, string val);
    void setValueInt(const Params &params, int val, string dataParam = "data");
    void setValueColor(const Params &params, ColorValue val);

    bool topicMatchesSubscription(string subscription, string topic);

    //register all special topics that are used to status updates (battery, online, etc.)
    //It's only used for sensors that have topics sets for battery, online, etc.
    void subscribeStatusTopics(Calaos::IOBase *io);

private:
    ExternProcServer *process;
    string exe;

    unordered_map<string, vector<MsgReceivedSignal>> subscribeCb;
    unordered_map<string, string> messages;

    bool connected = false;
};


#endif // __MQTT_CTRL_H__
