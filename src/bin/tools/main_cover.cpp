/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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
#include <stdio.h>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <Ecore.h>
#include <Evas.h>
#include <Ecore_Evas.h>
#include <Ecore_File.h>
#include <curl/curl.h>

using namespace std;

//Usefull utility functions
template<typename T>
bool is_of_type(const std::string &str)
{
    std::istringstream iss(str);
    T tmp;
    return (iss >> tmp) && (iss.eof());
}
template<typename T>
bool from_string(const std::string &str, T &dest)
{
    std::istringstream iss(str);
    iss >> dest;
    return iss.eof();
}
template<typename T>
std::string to_string( const T & Value )
{
    std::ostringstream oss;
    oss << Value;
    return oss.str();
}

typedef struct _ThumbSize
{
    int w, h;
    bool empty() { return w <= 0 || h <= 0; }
} ThumbSize;

struct CurlFile
{
    string filename;
    FILE *stream;
};

static size_t thumb_fwrite(void *buffer, size_t size, size_t nmemb, void *stream)
{
    struct CurlFile *out = (struct CurlFile *)stream;
    if(out && !out->stream)
    {
        out->stream = fopen(out->filename.c_str(), "wb");
        if(!out->stream)
            return -1; /* failure, can't open file to write */
    }
    return fwrite(buffer, size, nmemb, out->stream);
}

static int thumb_progress(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow)
{
    cout << "." << flush;

    return 0;
}

static void split(const string &str, vector<string> &tokens, const string &delimiters = " ", int max = 0);
static bool CreateThumb_Evas(string &input_file, string &output_file, ThumbSize &thumb_sizes);

int main (int argc, char **argv)
{
    if (argc < 3)
    {
        cout << "Calaos thumbnailer" << endl << "\tUsage:" << endl;
        cout << "\t\t" << argv[0] << " <image_file> <out_file> <size>" << endl << endl;
        cout << "\tWhere <image_file> is an image file supported by Evas. <image_file> can be an http link and then the file is downloaded first." << endl;
        cout << "\tAnd <size> is of the form: 123x123" << endl;
        cout << endl << "Thumb is saved in <out_file> in JPG format." << endl;
        cout << endl << "If <image_file> is an url the file is first downloaded." << endl;

        return 1;
    }

    //parse cmd line
    string input_file = argv[1];
    string output_file = argv[2];
    bool to_del = false;

    ThumbSize size = { 0, 0 };
    vector<string> token;

    string s;
    if (argc == 4) s = argv[3];
    split(s, token, "x", 2);
    if (token.size() != 2)
    {
        cout << "calaos_thumb: Can't parse size, using original image size" << endl;
    }
    else
    {
        from_string(token[0], size.w);
        from_string(token[1], size.h);
    }

    cout << "calaos_thumb: Using image source: " << input_file << endl;

    //Check if it's an url and download it
    if (input_file.compare(0, 7, "http://") == 0 ||
        input_file.compare(0, 8, "https://") == 0)
    {
        cout << "calaos_thumb: Input image is an URL, starting download: ." << flush;

        CURL *curl;
        CURLcode res;
        CurlFile cfile;
        cfile.filename = output_file + "_tmp";
        cfile.stream = NULL;

        to_del = true;

        curl = curl_easy_init();
        if(curl)
        {
            curl_easy_setopt(curl, CURLOPT_URL, input_file.c_str());
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, false);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, false);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, true);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, true);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, thumb_fwrite);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &cfile);
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, false);
            curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, thumb_progress);

            res = curl_easy_perform(curl);

            /* always cleanup */
            curl_easy_cleanup(curl);

            if(CURLE_OK != res)
            {
                /* we failed */
                cout << " Failed: curl told us: " << res << endl;
                return 1;
            }

            if(cfile.stream)
                fclose(cfile.stream);

            input_file = cfile.filename;

            curl_global_cleanup();

            cout << " Done." << endl;
        }
    }


    bool ret = CreateThumb_Evas(input_file, output_file, size);

    if (to_del)
        ecore_file_unlink(input_file.c_str());

    if (ret)
        return 0;

    cout << "calaos_thumb: Unable to do thumbnails..." << endl;
    return 1;
}

bool CreateThumb_Evas(string &input_file, string &output_file, ThumbSize &thumb_size)
{
    evas_init();
    ecore_init();

    Ecore_Evas *ee = ecore_evas_buffer_new(thumb_size.w, thumb_size.h);
    if (!ee) return false;

    Evas_Object *im = ecore_evas_object_image_new(ee);
    if (!im) { ecore_evas_free(ee); return false; }

    evas_object_move(im, 0, 0);
    evas_object_resize(im, thumb_size.w, thumb_size.h);
    evas_object_image_fill_set(im, 0, 0, thumb_size.w, thumb_size.h);
    evas_object_image_size_set(im, thumb_size.w, thumb_size.h);
    if (!thumb_size.empty())
        evas_object_show(im);
    Ecore_Evas *ee_im = (Ecore_Evas *)evas_object_data_get(im, "Ecore_Evas");
    Evas *evas_im = ecore_evas_get(ee_im);

    Evas_Object *image = evas_object_image_add(evas_im);
    evas_object_image_file_set(image, input_file.c_str(), NULL);
    Evas_Load_Error err = evas_object_image_load_error_get(image);
    if (err != EVAS_LOAD_ERROR_NONE)
    {
        cout << "calaos_thumb: Error, can't load image: " << evas_load_error_str(err) << endl;
        return false;
    }

    if (thumb_size.empty())
    {
        //Get size from image
        evas_object_image_size_get(image, &thumb_size.w, &thumb_size.h);
        cout << "calaos_thumb: Using detected size: " << thumb_size.w << "x" << thumb_size.h << endl;
        ecore_evas_resize(ee, thumb_size.w, thumb_size.h);
        evas_object_resize(im, thumb_size.w, thumb_size.h);
        evas_object_image_fill_set(im, 0, 0, thumb_size.w, thumb_size.h);
        evas_object_image_size_set(im, thumb_size.w, thumb_size.h);
        evas_object_show(im);
    }

    evas_object_image_fill_set(image, 0, 0, thumb_size.w, thumb_size.h);
    evas_object_move(image, 0, 0);
    evas_object_resize(image, thumb_size.w, thumb_size.h);
    evas_object_show(image);

    ecore_evas_buffer_pixels_get(ee);
    if (!evas_object_image_save(im, output_file.c_str(), NULL, "quality=85 compress=9"))
        cout << "calaos_thumb: evas_object_image_save() failed..." << endl;
    else
        cout << "calaos_thumb: Save image to " << output_file << endl;

    evas_object_del(image);
    evas_object_del(im);
    ecore_evas_free(ee);

    ecore_shutdown();
    evas_shutdown();

    return true;
}

void split(const string &str, vector<string> &tokens, const string &delimiters, int max /* = 0 */)
{
    // Skip delimiters at beginning.
    string::size_type lastPos = str.find_first_not_of(delimiters, 0);
    // Find first "non-delimiter".
    string::size_type pos     = str.find_first_of(delimiters, lastPos);

    int counter = 0;

    while (string::npos != pos || string::npos != lastPos)
    {
        if (counter + 1 >= max && max > 0)
        {
            tokens.push_back(str.substr(lastPos, string::npos));
            break;
        }

        // Found a token, add it to the vector.
        tokens.push_back(str.substr(lastPos, pos - lastPos));
        // Skip delimiters.  Note the "not_of"
        lastPos = str.find_first_not_of(delimiters, pos);
        // Find next "non-delimiter"
        pos = str.find_first_of(delimiters, lastPos);

        counter++;
    }

    while ((int)tokens.size() < max) tokens.push_back("");
}
