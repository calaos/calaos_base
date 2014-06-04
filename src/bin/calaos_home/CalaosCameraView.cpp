#include "CalaosCameraView.h"
#include "EcoreTimer.h"

Eina_Bool _url_data_cb(void *data, int type, void *event_info)
{
    Ecore_Con_Event_Url_Data *url_data = reinterpret_cast<Ecore_Con_Event_Url_Data *>(event_info);
    CalaosCameraView *view = reinterpret_cast<CalaosCameraView *>(data);

    if (view != ecore_con_url_data_get(url_data->url_con))
        return false;

    view->buffer.reserve(view->buffer.size() + url_data->size);
    std::copy(url_data->data, url_data->data + url_data->size, std::back_inserter(view->buffer));

    if (view->headers.size() <= 0)
    {
        const Eina_List *headers, *l;
        void *str;
        headers = ecore_con_url_response_headers_get(url_data->url_con);

        EINA_LIST_FOREACH(headers, l, str)
        {
            if (!str) continue;
            string s((char *)str);

            vector<string> tokens;
            Utils::replace_str(s, "\n", "");
            Utils::replace_str(s, "\r", "");
            Utils::split(s, tokens, ":", 2);

            string key = tokens[0];
            Utils::trim_left(key, " ");
            Utils::trim_right(key, " ");

            string val = tokens[1];
            Utils::trim_left(val, " ");
            Utils::trim_right(val, " ");

            cDebugDom("camera") << "add key: \"" << key << "\" with value: \"" << val << "\"";
            view->headers.Add(key, val);

        }
    }

    view->processData();

    return true;
}

Eina_Bool _url_complete_cb(void *data, int type, void *event_info)
{
    Ecore_Con_Event_Url_Complete *url_complete = reinterpret_cast<Ecore_Con_Event_Url_Complete *>(event_info);
    CalaosCameraView *view = reinterpret_cast<CalaosCameraView *>(data);

    if (view != ecore_con_url_data_get(url_complete->url_con))
        return false;

    cDebugDom("camera") << "completed: " << view->buffer.size();

    view->requestCompleted();

    return true;
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
    evas_object_image_fill_set(camImage, 0, 0, w, h);
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
        cErrorDom("camera") << "Failed to create ecore_con_url!";
        return;
    }
    ecore_con_url_data_set(ecurl, this);
    ecore_con_url_ssl_verify_peer_set(ecurl, false);

    single_frame = false;
    buffer.clear();
    formatDetected = false;
    format_error = false;
    nextContentLength = -1;

    if (!ecore_con_url_get(ecurl))
    {
        cErrorDom("camera") << "Could not realize request!";
        ecore_con_url_free(ecurl);
        ecurl = nullptr;
    }
}

bool CalaosCameraView::readEnd(int pos, int &lineend, int &nextstart)
{
    bool foundr = false;

    for (int i = pos;i < buffer.size();i++)
    {
        if (buffer[i] == '\r')
            foundr = true;
        else if (buffer[i] == '\n')
        {
            lineend = i - (foundr?1:0);
            nextstart = i + 1;
            return true;
        }
    }

    return false;
}

void CalaosCameraView::processData()
{
    if (!formatDetected)
    {
        //first frame, we need to look for the boundary
        //and for content-type/content-length if present
        if (buffer.size() < 4)
            return; //need more data

        if (buffer[0] != '-' || buffer[1] != '-')
        {
            //try to detect of the data is directly the jpeg picture
            if (buffer[0] == 0xFF && buffer[1] == 0xD8)
            {
                cWarningDom("camera") << "Data seems to be a single frame.";
                single_frame = true;
            }
            else
            {
                cWarningDom("camera") << "Wrong start of frame, give up!";
                format_error = true;
            }

            formatDetected = true;
            return;
        }

        //search for the line end after the boundary to get the boundary text
        int end, next;
        if (!readEnd(2, end, next))
        {
            if (buffer.size() > 500)
            {
                cWarningDom("camera") << "Boundary not found, give up!";
                format_error = true;
            }

            return; //need more data;
        }

        //get boundary
        boundary = string((char *)&buffer[0], end);

        cDebugDom("camera") << "Found boundary \"" << boundary << "\"";

        int i = next;
        while (readEnd(next, end, next))
        {
            int len = end - i;

            if (len == 0)
            {
                //line is empty, data starts now
                nextDataStart = next;
                formatDetected = true;
                scanpos = 0;
                break;
            }

            if (len > 15)
            {
                string s((char *)&buffer[i], len);
                if (Utils::strStartsWith(s, "Content-Length", Utils::CaseInsensitive))
                {
                    Utils::from_string(s.substr(15), nextContentLength);
                    cDebugDom("camera") << "Found content length header: \"" << nextContentLength << "\"";
                    //nextContentLength = -1; //to test code without content-length header
                }
            }

            i = next;
        }

        if (!formatDetected)
        {
            cWarningDom("camera") << "Something is wrong in the data, give up!";
            format_error = true;
        }
    }

    if (formatDetected && !single_frame)
    {
        //we should be positionned at the start of data
        //small check to be sure
        if (!(buffer[nextDataStart] == 0xFF && buffer[nextDataStart + 1] == 0xD8))
        {
            cWarningDom("camera") << "Wrong image data.";
            format_error = true;

            EcoreTimer::singleShot(0, [=]()
            {
                cDebugDom("camera") << "Cancel stream";
                ecore_con_url_free(ecurl);
                ecurl = nullptr;
            });

            return;
        }

        if (nextContentLength >= 0)
        {
            //the content-length is known, fast path
            if (buffer.size() < nextContentLength + nextDataStart + 2)
                return; //need more data

            cDebugDom("camera") << "Set new frame";

            evas_object_image_memfile_set(camImage, &buffer[nextDataStart], nextContentLength, NULL, NULL);
            //evas_object_image_size_get(camImage, &w, &h);

            if (buffer[nextDataStart + nextContentLength] == '\r') //assume a \n always follows \r
                nextContentLength += 2;
            else if (buffer[nextDataStart + nextContentLength] == '\n')
                nextContentLength += 1;

            //remove unused data from buffer
            auto iter = buffer.begin();
            buffer.erase(iter, iter + (nextDataStart + nextContentLength));

            //reset for next frame
            nextContentLength = -1;
            formatDetected = false;
            nextDataStart = 0;
            scanpos = 0;
        }
        else
        {
            int i;
            cDebugDom("camera") << "scanpos: " << scanpos;
            scanpos = 0;
            for (i = nextDataStart + scanpos;
                 i < buffer.size() - boundary.length();i++)
            {
                if (buffer[i] == '-' && buffer[i + 1] == '-' &&
                    !boundary.compare(0, boundary.length(), (const char *)&buffer[i], boundary.length()))
                {
                    //boundary found
                    //check for newline between boundary and data
                    nextContentLength = i - nextDataStart;
                    /*if (buffer[i - 2] == '\r')
                        nextContentLength -= 2;
                    else if (buffer[i - 1] == '\n')
                        nextContentLength -= 1;*/

                    evas_object_image_memfile_set(camImage, &buffer[nextDataStart], nextContentLength, NULL, NULL);
                    //evas_object_image_size_get(camImage, &w, &h);

                    //remove unused data from buffer
                    auto iter = buffer.begin();
                    buffer.erase(iter, iter + (nextDataStart + nextContentLength));

                    //reset for next frame
                    nextContentLength = -1;
                    formatDetected = false;
                    nextDataStart = 0;
                    scanpos = 0;
                    return;
                }
            }

            scanpos += i;
        }
    }
}

void CalaosCameraView::requestCompleted()
{
    if (single_frame)
    {
        evas_object_image_memfile_set(camImage, &buffer[0], buffer.size(), NULL, NULL);

        Evas_Load_Error err = evas_object_image_load_error_get(camImage);
        if (err != EVAS_LOAD_ERROR_NONE)
            cErrorDom("camera") << "could not load image. error string is \"%s\"" << evas_load_error_str(err);
    }

    if (format_error)
        return;

    EcoreTimer::singleShot(0, [=]()
    {
        if (!single_frame)
            cWarningDom("camera") << "Restarting request to camera...";
        setCameraUrl(cameraUrl);
    });
}
