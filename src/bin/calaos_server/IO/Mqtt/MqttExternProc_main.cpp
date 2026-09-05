/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
 **
 **  This file is part of Calaos.
 **
 **  Calaos is free software; you can redistribute it and/or modify
 **  it under the terms of the GNU General Public License as published by
 **  the Free Software Foundation; either version 3 of the License, or
 **  (at your option) any later version.
 **
 **  Calaos is distributed in the hope that it will be useful,
 **  but WITHOUT ANY WARRANTY; without even the implied warranty of
 **  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 **  GNU General Public License for more details.
 **
 **  You should have received a copy of the GNU General Public License
 **  along with Foobar; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/
#include "ExternProc.h"

#include <sys/select.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <unordered_map>

#include <mosquittopp.h>
#include "Params.h"
#include "Utils.h"
#include "MqttWire.h"

class MqttClient : public mosqpp::mosquittopp
{
public:
    MqttClient(const string &id);

    void on_connect(int rc);
	void on_message(const struct mosquitto_message *message);
    void messageRcv(sigc::slot<void, const struct mosquitto_message *> callback);
	void on_subcribe(int mid, int qos_count, const int *granted_qos);
    void on_error();
    void on_log(int level, const char *str);
    void publishTopic(const string topic, const string payload);

private:
    std::vector<sigc::slot<void, const struct mosquitto_message *>> subscribeCb;
    bool connected = false;
};

MqttClient::MqttClient(const string &id) : mosquittopp(id.c_str())
{
}

void MqttClient::messageRcv(sigc::slot<void, const struct mosquitto_message *> callback)
{
    cDebugDom("mqtt") << "On message";
    // subscribeCb contains a map of list of callbacks, register this callback to the key  relative of this topic
    subscribeCb.push_back(callback);

    if (connected)
    {
        cDebugDom("mqtt") << "Subscribing to topic #";
        // mosquitto subscribe call
        subscribe(NULL, "#");
    }
}

void MqttClient::publishTopic(const string topic, const string payload)
{
    publish(NULL, topic.c_str(), payload.size(), payload.c_str());
}

void MqttClient::on_connect(int rc)
{
    /* rc is a CONNACK code, not an errno: read with the errno table a broker
     * refusing the credentials answered "No such file or directory". And a
     * refused CONNACK is what a wrong password looks like, so it belongs to
     * the level a stock install prints. */
    if (rc)
    {
        cErrorDom("mqtt") << "The broker refused the connection : "
                          << mosqpp::connack_string(rc);
    }
    else
    {
        cDebugDom("mqtt") << "Connected with code " << rc << " : "
                          << mosqpp::connack_string(rc);
    }

    if (!rc)
    {
        connected = true;
        cDebugDom("mqtt") << "Subscribing to topic #";
        subscribe(NULL, "#");
    }
}

void MqttClient::on_subcribe(int mid, int qos_count, const int *granted_qos)
{
    cDebugDom("mqtt") << "Subscription succeeded.";
}

void MqttClient::on_message(const struct mosquitto_message *message)
{
    // Call all callback registered for this topic
    for(auto cb : subscribeCb)
    {
        cb(message);
    }
}

void MqttClient::on_log(int level, const char *str)
{
    // cDebugDom("mqtt") << str;
}

void MqttClient::on_error()
{
    cErrorDom("mqtt") << "Error";
}

/*
 * The two error tables of this file overlap without agreeing, and reading one
 * with the other is how a refused port came out as "Bad address": ::strerror()
 * answers EFAULT for 14, which is MOSQ_ERR_ERRNO. That one code is the one
 * that means "the cause is in errno", so it is the only one handed to the
 * errno table - every other code belongs to libmosquitto's own.
 */
static string brokerErrorText(int rc, int sysErrno)
{
    if (rc == MOSQ_ERR_ERRNO)
        return string(::strerror(sysErrno)) + " (errno " + Utils::to_string(sysErrno) + ")";
    return mosqpp::strerror(rc);
}

/*
 * The deadline is not a comfort margin: it is what a PARTIAL UPDATE needs. A
 * calaos_server older than this binary hands the configuration in the argv,
 * which this build no longer reads, so nothing ever arrives on the socket.
 * Without a deadline that is an idle process and a silent journal - with it,
 * one printed line per relaunch. Its value lives in MqttWire.h so that no copy
 * of it can drift away from the one the tests hold this binary to.
 */
static const int kConfigWaitMs = MqttWire::configWaitMs();

class MqttProcess: public ExternProcClient
{
public:

    //needs to be reimplemented
    virtual bool setup(int &argc, char **&argv);
    virtual int procMain();

    EXTERN_PROC_CLIENT_CTOR(MqttProcess)
    virtual ~MqttProcess();

protected:
    MqttClient *m_client = nullptr;

    /* The configuration arrives asynchronously, but every way it can fail
     * belongs to setup(): messageReceived() records the verdict here and
     * awaitConfiguration() turns it into the false that ends the process. */
    enum ConfigState { ConfigWaiting, ConfigApplied, ConfigRefused };
    ConfigState m_configState = ConfigWaiting;

    /* The descriptor libmosquitto is using, kept because appendFd() takes it
     * and only the caller can give it back: once the library closes it, a
     * select() on it answers EBADF, and the number can be handed to an
     * unrelated open() in between. */
    int m_brokerFd = -1;
    string m_broker;
    int m_exitCode = 0;

    bool awaitConfiguration();
    bool applyConfiguration(const Params &p);
    bool pumpBroker();
    void brokerLost(int rc, int sysErrno);

    //needs to be reimplemented
    virtual void readTimeout();
    virtual void messageReceived(const string &msg);
    virtual bool handleFdSet(int fd);

};

MqttProcess::~MqttProcess()
{
    delete m_client;
    mosqpp::lib_cleanup();
}

void MqttProcess::readTimeout()
{
    pumpBroker();
}

/*
 * Two halves, and neither is enough alone. Giving the descriptor back keeps
 * the loop from polling a number the library no longer owns; ending loudly is
 * what makes the failure reach an operator. Only giving it back would leave a
 * sidecar idling for ever: nothing reconnects here, the server does, and it
 * only knows how to do that by relaunching us.
 */
bool MqttProcess::pumpBroker()
{
    const int rc = m_client->loop(0, 1);
    const int sysErrno = errno;

    if (rc == MOSQ_ERR_SUCCESS)
        return true;

    brokerLost(rc, sysErrno);
    return false;
}

void MqttProcess::brokerLost(int rc, int sysErrno)
{
    if (m_brokerFd >= 0)
    {
        removeFd(m_brokerFd);
        m_brokerFd = -1;
    }

    cErrorDom("mqtt") << "Lost the connection to the broker " << m_broker
                      << " : " << brokerErrorText(rc, sysErrno);

    m_exitCode = 1;
    quitLoop();
}

void MqttProcess::messageReceived(const string &msg)
{
    Params p;

    /* Never the message itself, only its size. What travels here is either a
     * broker message or the broker configuration, and the configuration
     * carries the password - while calaos_server pipes this stdout straight
     * into its own journal. */
    if (!MqttWire::decodeMessage(msg, p))
    {
        if (m_configState == ConfigWaiting)
        {
            cError() << "Unable to parse the configuration sent by calaos_server ("
                     << msg.size() << " bytes)";
            m_configState = ConfigRefused;
        }
        else
        {
            cWarningDom("mqtt") << "Error parsing json message from calaos_server ("
                                << msg.size() << " bytes)";
        }
        return;
    }

    const bool isConfig = p.Exists("action") && p["action"] == MqttWire::configAction();

    if (m_configState == ConfigWaiting)
    {
        if (!isConfig)
        {
            cError() << "Expected the broker configuration as the first message, got "
                     << msg.size() << " bytes";
            m_configState = ConfigRefused;
            return;
        }

        m_configState = applyConfiguration(p)? ConfigApplied : ConfigRefused;
        return;
    }

    if (isConfig)
    {
        /* Applying it would move a live client to another broker under the
         * same client id, and leaving on it would give the backoff-less
         * relaunch of calaos_server a second way in. */
        cWarningDom("mqtt") << "A second broker configuration was received and ignored";
        return;
    }

    //the strings, not their c_str(): a payload may carry a NUL byte
    m_client->publishTopic(p["topic"], p["payload"]);
}

/*
 * Wait for the configuration, which is the first message calaos_server writes
 * on the socket.
 *
 * It used to be the first argument - where /proc/<pid>/cmdline, mode 444,
 * published the broker password to every account of the machine. Waiting for
 * it HERE rather than in the message handler is what keeps the failure on the
 * path it was already on: setup() answers false, main() returns 1, and the
 * server relaunches.
 */
bool MqttProcess::awaitConfiguration()
{
    const int fd = getSocketFd();
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(kConfigWaitMs);

    while (m_configState == ConfigWaiting)
    {
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

        if (now >= deadline)
        {
            cError() << "Gave up waiting for its configuration after "
                     << kConfigWaitMs << " ms. calaos_server sends it on the "
                        "socket as the first message; a server that still "
                        "passes it as an argument is older than this sidecar.";
            return false;
        }

        const long remain = static_cast<long>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());

        struct timeval tv;
        tv.tv_sec = remain / 1000;
        tv.tv_usec = (remain % 1000) * 1000;

        fd_set events;
        FD_ZERO(&events);
        FD_SET(fd, &events);

        const int ret = select(fd + 1, &events, NULL, NULL, &tv);

        if (ret < 0)
        {
            if (errno == EINTR)
                continue;
            cError() << "Error while waiting for its configuration: " << strerror(errno);
            return false;
        }

        if (ret > 0 && !processSocketRecv())
        {
            cError() << "calaos_server closed the connection before sending a configuration";
            return false;
        }
    }

    return m_configState == ConfigApplied;
}

bool MqttProcess::applyConfiguration(const Params &p)
{
    string host = "127.0.0.1";
    int port = 1883;
    int keepalive = 120;

    if (p.Exists("host"))
        host = p["host"];
    //T3.25: _keep, so that a key that is PRESENT AND EMPTY keeps the default
    //rather than collapsing to port 0 / keepalive 0, neither of which is a
    //usable value. Exists() does not protect against an empty value.
    if (p.Exists("port"))
        from_string_or_keep(p["port"], port);

    if (p.Exists("keepalive"))
        from_string_or_keep(p["keepalive"], keepalive);

    if (p.Exists("user") && p.Exists("password"))
    {
        m_client->username_pw_set(p["user"].c_str(), p["password"].c_str());
    }
    m_broker = host + ":" + Utils::to_string(port);
    cDebugDom("mqtt") << "Connecting to broker " << m_broker;

    const int res = m_client->connect_async(host.c_str(), port, keepalive);
    //before anything else can overwrite it, and MOSQ_ERR_ERRNO is the only
    //answer that carries its cause there rather than in its own code
    const int sysErrno = errno;

    switch (res)
    {
    case MOSQ_ERR_INVAL:
        cErrorDom("mqtt") << "Error connecting to host : " << host;
        return false;
    case MOSQ_ERR_SUCCESS:
        /* The TCP connect is only STARTED here. It can still fail afterwards,
         * inside the library, which then closes this very descriptor - see
         * brokerLost(). */
        m_brokerFd = m_client->socket();
        cInfoDom("mqtt") << "Connect to : " << host << "socket " << m_brokerFd;
        appendFd(m_brokerFd);
        return true;
    default:
        cErrorDom("mqtt") << "Error connecting to the broker " << m_broker
                          << " : " << brokerErrorText(res, sysErrno);
        return false;
    }
}

bool MqttProcess::setup(int &argc, char **&argv)
{
    cDebugDom("mqtt") << "Mqtt external process";

    /* This sidecar takes NO argument. The broker configuration used to be the
     * first one, password included, where any account of the machine could
     * read it back from /proc/<pid>/cmdline. An argument here means a
     * calaos_server older than this binary, handing the configuration over a
     * channel this build never reads - named rather than ignored, because the
     * alternative is a process waiting for a message nobody will send. */
    if (argc > 1)
    {
        cError() << "This sidecar takes no argument and was given " << (argc - 1)
                 << ": calaos_server is older than calaos_mqtt";
        return false;
    }

    if (!connectSocket())
    {
        cError() << "process cannot connect to calaos_server";
        return false;
    }

    mosqpp::lib_init();

    m_client = new MqttClient("calaos_" + Utils::createRandomUuid());

    if (!awaitConfiguration())
        return false;

    m_client->messageRcv([=](const struct mosquitto_message *m)
    {
        sendMessage(MqttWire::encodeMessage(m->topic?m->topic:"",
                                            MqttWire::payloadToString(m->payload, m->payloadlen)));
    });

    return true;
}

bool MqttProcess::handleFdSet(int fd)
{
    return pumpBroker();
}

int MqttProcess::procMain()
{
    const bool ok = run(200);

    if (m_exitCode != 0)
        return m_exitCode;

    return ok? 0: 1;
}

EXTERN_PROC_CLIENT_MAIN(MqttProcess)
