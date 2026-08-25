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

    /* T3.35b. `err` is now told the truth. It used to mean "a payload arrived
     * for this topic", never "a value came out of it": getValue() cleared it
     * unconditionally before handing over to getValueJson(), so every failure
     * of the path parser - and they all return an empty string - was reported
     * to the caller as a SUCCESS carrying "". Every caller in the tree
     * (MqttInputString, MqttInputSwitch, MqttOutputLight, MqttOutputShutter,
     * MqttOutputLightRGB, getValueDouble, and the six status topics of
     * subscribeStatusTopics) reads `err` as "ignore this update", so making it
     * honest can only turn an invented value into a skipped update.
     *
     * The four-argument getValueJson() is where the flag is decided; the three
     * argument form is the one the two characterization suites drive and it
     * simply discards the flag.
     */
    string getValueJson(const Params &params, string path, string payload, bool &err);
    string getValueJson(const Params &params, string path, string payload);
    string getValue(const Params &params, bool &err, string topic_param, string path_param = "path");
    double getValueDouble(const Params &params, bool &err);
    ColorValue getValueColor(const Params &params, bool &err);

    /* T3.35b. The single door through which a broker message enters this
     * object - the messageReceived handler calls it, and it is what lets a
     * test drive getValue()/getValueColor() and check what `err` says without
     * a broker, a subprocess or an event loop.
     */
    void storeMessage(const string &topic, const string &payload);

    /* T3.35b. Reads one numeric status value, and REFUSES it rather than
     * writing garbage. Shared by the battery, wireless_signal and uptime
     * status topics, which each declared a `double rawValue;` and fed it
     * straight to Utils::from_string() - which does not write its destination
     * when the string is blank. Returns false and leaves `out` untouched when
     * `value` is not a number.
     */
    static bool readStatusNumber(const string &value, double &out);

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
