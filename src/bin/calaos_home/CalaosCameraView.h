#ifndef CALAOSCAMERAVIEW_H
#define CALAOSCAMERAVIEW_H

#include <Utils.h>
#include "EvasSmart.h"
#include <Ecore.h>
#include <Ecore_Con.h>
#include <Params.h>

using namespace std;

class CalaosCameraView: public EvasSmart
{
private:
    Evas_Object *clip = nullptr;
    Evas_Object *camImage = nullptr;

    string cameraUrl;

    Ecore_Con_Url *ecurl = nullptr;

    Ecore_Event_Handler *handler_data = nullptr;
    Ecore_Event_Handler *handler_complete = nullptr;

    vector<unsigned char> buffer;
    Params headers;

    bool formatDetected;
    bool single_frame;
    bool format_error;
    string boundary;
    long int nextContentLength;
    long int nextDataStart;
    long int scanpos;

    void processData();
    void requestCompleted();
    bool readEnd(int pos, int &lineend, int &nextstart);

    friend Eina_Bool _url_data_cb(void *data, int type, void *event_info);
    friend Eina_Bool _url_complete_cb(void *data, int type, void *event_info);

public:
    CalaosCameraView(Evas *evas);
    virtual ~CalaosCameraView();

    void setCameraUrl(string url);

    void play();
    void stop();

    virtual void SmartMove(int x, int y);
    virtual void SmartResize(int w, int h);
    virtual void SmartShow();
    virtual void SmartHide();
    virtual void SmartColorSet(int r, int g, int b, int a);
    virtual void SmartClipSet(Evas_Object *clip);
    virtual void SmartClipUnset();
};

#endif // CALAOSCAMERAVIEW_H
