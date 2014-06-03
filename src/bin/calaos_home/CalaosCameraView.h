#ifndef CALAOSCAMERAVIEW_H
#define CALAOSCAMERAVIEW_H

#include "EvasSmart.h"
#include <Ecore.h>
#include <Ecore_Con.h>
#include <vector>

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

    friend Eina_Bool _url_data_cb(void *data, int type, void *event_info);
    friend Eina_Bool _url_complete_cb(void *data, int type, void *event_info);

public:
    CalaosCameraView(Evas *evas);
    virtual ~CalaosCameraView();

    void setCameraUrl(string url);

    virtual void SmartMove(int x, int y);
    virtual void SmartResize(int w, int h);
    virtual void SmartShow();
    virtual void SmartHide();
    virtual void SmartColorSet(int r, int g, int b, int a);
    virtual void SmartClipSet(Evas_Object *clip);
    virtual void SmartClipUnset();
};

#endif // CALAOSCAMERAVIEW_H
