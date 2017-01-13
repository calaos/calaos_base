/******************************************************************************
 **  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
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
#ifndef S_WEBCTRL_H
#define S_WEBCTRL_H

#include <unordered_map>

#include <Utils.h>
#include <DownloadManager.h>
#include <Timer.h>
#include <Params.h>

namespace Calaos
{

class WebCtrl
{
private:    
    WebCtrl(Params &p, int file_type);
    DownloadManager *dlManager;
    double frequency;
    Timer *timer;
    void downloadFinished(string emission, string source, void* data);
    void downloadProgress(string url, string destination_file, double dl_now, double dl_total, void* data);
    Params param;
    int file_type;
    static unordered_map<string, WebCtrl> hash;
    void launchDownload();
    vector<pair<string, std::function<void()>>> fileDownloadedCallbacks;

public:
    static WebCtrl &Instance(Params &p); //Singleton
    WebCtrl();
    ~WebCtrl();
    enum {XML, JSON, TEXT, UNKNOWN};
    void Add(string path, double _frequency = 60.0,
             std::function<void()> fileDownloaded_cb = []() { cDebugDom("web") << "File downloaded"; });
    void Del(string path);

    string getValueJson(string path, string filename);
    string getValueXml(string path, string filename);
    string getValueText(string path, string filename);
    string getValue(string path);
    double getValueDouble(string path);

    void setValue(string value);
};

}
#endif
