/******************************************************************************
**  Copyright (c) 2007-2013, Calaos. All Rights Reserved.
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
**  along with Foobar; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#ifndef S_WEBCTRL_H
#define S_WEBCTRL_H

#include <unordered_map>

#include <Utils.h>
#include <DownloadManager.h>
#include <EcoreTimer.h>
#include <Params.h>

namespace Calaos
{

class WebCtrl
{
private:

        WebCtrl(Params &p, int file_type);
        DownloadManager *dlManager;
        double frequency;
        EcoreTimer *timer;
        void timerExpired();
        void downloadFinished(string emission, string source, void* data);
        void downloadProgress(string url, string destination_file, double dl_now, double dl_total, void* data);
        Params param;
        int file_type;
        static unordered_map<string, WebCtrl> hash;
public:
        static WebCtrl &Instance(Params &p); //Singleton
        WebCtrl();
        ~WebCtrl();
        enum {XML, JSON, UNKNOWN};
        void Add(double _frequency);
        double getValueJson(string path);
        double getValueXml(string path);
        double getValue(string path);

};

}
#endif
