#include <iostream>
#include <EvasSmart.h>
#include <Elementary.h>
#include "CalaosCameraView.h"
#include <Ecore_Con.h>


EAPI_MAIN int elm_main(int argc, char **argv)
{
    Evas_Object *win = NULL;

    ecore_init();
    ecore_con_init();
    ecore_con_url_init();

    elm_policy_set(ELM_POLICY_QUIT, ELM_POLICY_QUIT_LAST_WINDOW_CLOSED);

    /* Create an win, associate it with a canvas and */
    /* turn it visible on WM (Window Manager).       */
    win = elm_win_util_standard_add("Greetings", "Hello, World!");
    elm_win_autodel_set(win, EINA_TRUE);

    CalaosCameraView *cam = new CalaosCameraView(evas_object_evas_get(win));
    Evas_Object *o = cam->getSmartObject();

    //cam->setCameraUrl("http://test:test@10.7.0.102/image.jpg");
    //cam->setCameraUrl("http://192.168.0.47/axis-cgi/jpg/image.cgi");
    //cam->setCameraUrl("http://192.168.0.47/axis-cgi/mjpg/video.cgi");
    cam->setCameraUrl("http://192.168.0.37:8080/video");
    //cam->setCameraUrl("http://192.168.0.47");
    //cam->setCameraUrl("http://192.168.0.13/video.mjpg");
    //cam->setCameraUrl("http://192.168.0.13/image.jpg");

    CalaosCameraView *cam2 = new CalaosCameraView(evas_object_evas_get(win));
    Evas_Object *o2 = cam2->getSmartObject();
    cam2->setCameraUrl("http://192.168.0.47/axis-cgi/mjpg/video.cgi");

    cam->play();
    cam2->play();

    evas_object_resize(o, 640, 480);
    evas_object_move(o, 0, 0);
    evas_object_show(o);

    evas_object_resize(o2, 640, 480);
    evas_object_move(o2, 640, 0);
    evas_object_show(o2);

    evas_object_resize(win, 640*2, 480);
    evas_object_show(win);

    elm_run();
    elm_shutdown();

    return 0;
}
ELM_MAIN()
