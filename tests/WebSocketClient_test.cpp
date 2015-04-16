#include "Utils.h"
#include "EcoreTimer.h"
#include "WebSocketClient.h"

WebSocketClient *wsclient = nullptr;
int testCount = 0;
int currentTest = 0;
int argNumber = 0;
int argCount = 0;

void startNextTest()
{
    currentTest++;

    cout << "currentTest:" << currentTest <<
            " - argNumber:" << argNumber << " > argCount:" << argCount << endl;

    if (((argCount > 0) && currentTest - argNumber > argCount - 1) ||
        ((argCount == 0) && currentTest > testCount))
    {
        //do not delete in the slot
        WebSocketClient *w = wsclient;
        EcoreTimer::singleShot(0, [=]() { delete w; });

        wsclient = new WebSocketClient();
        wsclient->websocketDisconnected.connect([]()
        {
            cout << "Reports updated." << endl;
            ecore_main_loop_quit();
        });
        string uri = "ws://127.0.0.1:9001/updateReports?agent=CalaosWebSocketClient";
        wsclient->openConnection(uri);

        return;
    }

    cout << "Running test: " << currentTest << endl;

    string uri = "ws://127.0.0.1:9001/runCase?case=" + Utils::to_string(currentTest) + "&agent=CalaosWebSocketClient";
    wsclient->openConnection(uri);
}

void textMessageReceived(const string &msg)
{
    wsclient->sendTextMessage(msg);
}

void binMessageReceived(const string &msg)
{
    wsclient->sendBinaryMessage(msg);
}

void onConnected()
{
    cout << "---------------------------------------------------------"  << endl;
    cout << "Running test " << currentTest << "/" << testCount << endl;
    cout << "---------------------------------------------------------"  << endl;
}

void onDisconnected()
{
    startNextTest();
}

int main(int argc, char **argv)
{
    InitEinaLog("test");

    eina_init();
    ecore_init();
    ecore_con_init();

    if (argc > 1)
    {
        Utils::from_string(argv[1], argNumber);
        currentTest = argNumber - 1;
    }
    if (argc > 2)
        Utils::from_string(argv[2], argCount);

    EcoreTimer::singleShot(0.1, []()
    {
        cout << "Start websocket client" << endl;
        wsclient = new WebSocketClient();
        wsclient->textMessageReceived.connect([](const string &msg)
        {
            if (Utils::is_of_type<int>(msg))
                Utils::from_string(msg, testCount);
            cout << "Test count: " << testCount << endl;
        });
        wsclient->websocketDisconnected.connect([]()
        {
            //do not delete in the slot
            WebSocketClient *w = wsclient;
            EcoreTimer::singleShot(0, [=]() { delete w; });

            wsclient = new WebSocketClient();
            wsclient->websocketConnected.connect(sigc::ptr_fun(onConnected));
            wsclient->websocketDisconnected.connect(sigc::ptr_fun(onDisconnected));
            wsclient->textMessageReceived.connect(sigc::ptr_fun(textMessageReceived));
            wsclient->binaryMessageReceived.connect(sigc::ptr_fun(binMessageReceived));

            EcoreTimer::singleShot(0.1, sigc::ptr_fun(startNextTest));
        });
        wsclient->openConnection("ws://127.0.0.1:9001/getCaseCount");
    });

    ecore_main_loop_begin();

    delete wsclient;

    return 0;
}

