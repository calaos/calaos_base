/******************************************************************************
**  Copyright (c) 2006-2011, Calaos. All Rights Reserved.
**
**  This file is part of Calaos Home.
**
**  Calaos Home is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 3 of the License, or
**  (at your option) any later version.
**
**  Calaos Home is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Calaos; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#include "ActivityWebView.h"
#include "WebKitError.h"

#include <FileDownloader.h>
#include "ApplicationMain.h"

#ifdef HAVE_EWEBKIT
#include <EWebKit.h>
#endif

#define USER_AGENT              "Mozilla/5.0 (X11; U; Linux x86; fr) AppleWebKit/536.6 (KHTML, like Gecko, Safari/536.6) Calaos/2.0"
#define DEFAULT_BROWSER_URL     "https://127.0.0.1/calaos_bookmarks/"

static void _web_load_started(void *data, Evas_Object *obj, void *event_info)
{
    ActivityWebView *view = reinterpret_cast<ActivityWebView *>(data);
    if (!view) return;

    view->_webLoadStarted();
}

static void _web_load_progress(void *data, Evas_Object *obj, void *event_info)
{
    ActivityWebView *view = reinterpret_cast<ActivityWebView *>(data);
    if (!view) return;

    view->_webLoadProgress();
}

static void _web_load_finished(void *data, Evas_Object *obj, void *event_info)
{
    ActivityWebView *view = reinterpret_cast<ActivityWebView *>(data);
    if (!view) return;

    Elm_Web_Frame_Load_Error *error = NULL;

    if (event_info)
        error = reinterpret_cast<Elm_Web_Frame_Load_Error *>(event_info);

    view->_webLoadFinished(error);
}

static void _web_title_changed(void *data, Evas_Object *obj, void *event_info)
{
    ActivityWebView *view = reinterpret_cast<ActivityWebView *>(data);
    if (!view) return;

    view->_webTitleChanged();
}

static void _web_inputmethod_changed(void *data, Evas_Object *obj, void *event_info)
{
    ActivityWebView *view = reinterpret_cast<ActivityWebView *>(data);
    if (!view) return;

    bool enabled = (bool)event_info;

    view->_webInputMethodChanged(enabled);
}

ActivityWebView::ActivityWebView(Evas *_e, Evas_Object *_parent):
    ActivityView(_e, _parent, "calaos/page/web")
{
    setPartText("header.label", "Navigateur Web");

    keyboard = new KeyboardView(evas, parent);
    Swallow(keyboard, "keyboard");

    if (elm_need_web())
    {
        web = elm_web_add(parent);
        Swallow(web, "webkit");
        evas_object_show(web);
        elm_object_focus_set(web, true);

        elm_web_useragent_set(web, USER_AGENT);
        elm_web_inwin_mode_set(web, true);

        evas_object_smart_callback_add(web, "load,started", _web_load_started, this);
        evas_object_smart_callback_add(web, "load,progress", _web_load_progress, this);
        evas_object_smart_callback_add(web, "load,finished", _web_load_finished, this);
        evas_object_smart_callback_add(web, "load,error", _web_load_finished, this);
        evas_object_smart_callback_add(web, "title,changed", _web_title_changed, this);
        evas_object_smart_callback_add(web, "inputmethod,changed", _web_inputmethod_changed, this);

#ifdef HAVE_EWEBKIT
        Evas_Object *eview = elm_web_webkit_view_get(web);
        ewk_view_setting_enable_auto_resize_window_set(eview, false);
#endif

        goToCallback(DEFAULT_BROWSER_URL);
    }

    addCallback("button.*", "pressed", sigc::mem_fun(*this, &ActivityWebView::buttonCallback));
}

ActivityWebView::~ActivityWebView()
{
    DELETE_NULL(keyboard);
    DELETE_NULL_FUNC(evas_object_del, web);
}

void ActivityWebView::resetView()
{
}

void ActivityWebView::_webLoadStarted()
{
    EmitSignal("start,loading", "calaos");
}

void ActivityWebView::_webLoadProgress()
{
    setDragValue("progress.level", elm_web_load_progress_get(web), 0.0);

    string t = Utils::to_string((int)(elm_web_load_progress_get(web) * 100)) + " %";
    setPartText("progress.text", t);
}

void ActivityWebView::_webLoadFinished(Elm_Web_Frame_Load_Error *error)
{
    EmitSignal("hide,keyboard", "calaos");
    EmitSignal("stop,loading", "calaos");

    if (error && !error->is_cancellation)
    {
#ifdef HAVE_EWEBKIT
        string t = WEBKIT_ERROR_HTML;
        replace_str(t, "{FAILING_URL}", error->failing_url);
        replace_str(t, "{DESC}", error->description);

        Evas_Object *eview = elm_web_webkit_view_get(web);
        Evas_Object *frame = ewk_view_frame_main_get(eview);
        ewk_frame_contents_set(frame,
                               t.c_str(),
                               t.length(),
                               "text/html",
                               "UTF-8",
                               error->failing_url);
#endif
    }
}

void ActivityWebView::_webTitleChanged()
{
    const char *t = elm_web_title_get(web);

    if (t) setPartText("page.title", t);
}

void ActivityWebView::_webInputMethodChanged(bool en)
{
    if (en)
        EmitSignal("show,keyboard", "calaos");
    else
        EmitSignal("hide,keyboard", "calaos");
}

void ActivityWebView::buttonCallback(void *data, Evas_Object *edje_object, string emission, string source)
{
    if (source == "button.back")
        elm_web_back(web);
    else if (source == "button.forward")
        elm_web_forward(web);
    else if (source == "button.reload")
        elm_web_reload_full(web);
    else if (source == "button.stop")
        elm_web_stop(web);
    else if (source == "button.forward")
        elm_web_forward(web);
    else if (source == "button.home")
        goToCallback(DEFAULT_BROWSER_URL);
    else if (source == "button.go")
    {
        ApplicationMain::Instance().ShowKeyboard("Entrez une URL ci-dessous",
                                                 sigc::mem_fun(*this, &ActivityWebView::goToCallback),
                                                 false,
                                                 elm_web_uri_get(web));
    }
    else if (source == "button.bookmark")
    {
        string url = elm_web_uri_get(web);

        string _url = DEFAULT_BROWSER_URL;
        _url += "Bookmark.php?new=" + url_encode(url);
        _url += "&title=" + url_encode(elm_web_title_get(web));
        _url += "&thumb_file=/tmp/thumb.png";

        setDragValue("progress.level", 0.0, 0.0);
        setPartText("progress.text", "Saving...");
        EmitSignal("saving,bookmark", "calaos");

        FileDownloader *fdownloader = new FileDownloader(_url, "/dev/null", true);
        fdownloader->Start();
    }
}

void ActivityWebView::goToCallback(string text)
{
    string url;

    remove_tag(text, "<", ">");
    if (text.substr(0, 7) != "http://" && text.substr(0, 8) != "https://")
        url = "http://";

    url += text;

    elm_web_uri_set(web, url.c_str());
    elm_object_focus_set(web, true);
}
