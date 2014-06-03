#include "CalaosCameraView.h"

Eina_Bool _url_data_cb(void *data, int type, void *event_info)
{
    Ecore_Con_Event_Url_Data *url_data = reinterpret_cast<Ecore_Con_Event_Url_Data *>(event_info);
    CalaosCameraView *view = reinterpret_cast<CalaosCameraView *>(data);

    vector<unsigned char> buf;
    buf.reserve(url_data->size);
    std::copy(url_data->data, url_data->data + url_data->size, std::back_inserter(buf));

    view->buffer.insert(view->buffer.end(), buf.begin(), buf.end());
    cout << "read data: " << view->buffer.size() << endl;
}

Eina_Bool _url_complete_cb(void *data, int type, void *event_info)
{
    Ecore_Con_Event_Url_Complete *url_complete = reinterpret_cast<Ecore_Con_Event_Url_Complete *>(event_info);
    CalaosCameraView *view = reinterpret_cast<CalaosCameraView *>(data);

    cout << "completed: " << view->buffer.size() << endl;

    evas_object_image_memfile_set(view->camImage, &view->buffer[0], view->buffer.size(), NULL, NULL);

    Evas_Load_Error err = evas_object_image_load_error_get(view->camImage);
    if (err != EVAS_LOAD_ERROR_NONE)
        cout << "could not load image. error string is \"%s\"" << evas_load_error_str(err) << endl;
}

CalaosCameraView::CalaosCameraView(Evas *evas):
    EvasSmart(evas, "PageContentView")
{
    clip = evas_object_rectangle_add(evas);
    evas_object_color_set(clip, 255, 255, 255, 255);
    AddMemberObject(clip);

    camImage = evas_object_image_add(evas);
    evas_object_clip_set(camImage, clip);
    AddMemberObject(camImage);

    handler_data = ecore_event_handler_add(ECORE_CON_EVENT_URL_DATA, _url_data_cb, this);
    handler_complete = ecore_event_handler_add(ECORE_CON_EVENT_URL_COMPLETE, _url_complete_cb, this);
}

CalaosCameraView::~CalaosCameraView()
{
    if (ecurl)
        ecore_con_url_free(ecurl);

    DelMemberObject(clip);
    evas_object_del(clip);
    DelMemberObject(camImage);
    evas_object_del(camImage);

    ecore_event_handler_del(handler_data);
    ecore_event_handler_del(handler_complete);
}

void CalaosCameraView::SmartMove(int x, int y)
{
    evas_object_move(clip, x, y);
    evas_object_move(camImage, x, y);
}

void CalaosCameraView::SmartResize(int w, int h)
{
    evas_object_resize(clip, w, h);
    evas_object_resize(camImage, w, h);
}

void CalaosCameraView::SmartShow()
{
    evas_object_show(clip);
    evas_object_show(camImage);
}

void CalaosCameraView::SmartHide()
{
    evas_object_hide(clip);
    evas_object_hide(camImage);
}

void CalaosCameraView::SmartColorSet(int r, int g, int b, int a)
{
    evas_object_color_set(clip, r, g, b, a);
}

void CalaosCameraView::SmartClipSet(Evas_Object *_clip)
{
    evas_object_clip_set(clip, _clip);
}

void CalaosCameraView::SmartClipUnset()
{
    evas_object_clip_unset(clip);
}

void CalaosCameraView::setCameraUrl(string url)
{
    cameraUrl = url;

    if (ecurl)
        ecore_con_url_free(ecurl);

    ecurl = ecore_con_url_new(cameraUrl.c_str());
    if (!ecurl)
    {
        cout << "Failed to create ecore_con_url: " << ecurl << endl;
        return;
    }
    ecore_con_url_data_set(ecurl, this);
    ecore_con_url_ssl_verify_peer_set(ecurl, false);

    if (!ecore_con_url_get(ecurl))
    {
        cout << "could not realize request." << endl;
        ecore_con_url_free(ecurl);
        ecurl = nullptr;
    }
}
