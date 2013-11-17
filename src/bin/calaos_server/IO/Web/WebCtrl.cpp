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
#include <WebCtrl.h>

using namespace Calaos;

WebCtrl::WebCtrl()
{
        dlManager = NULL;
        timer = NULL;
}

WebCtrl::~WebCtrl()
{
        if (timer)
                delete timer;
        if (dlManager)
                delete dlManager;
}

WebCtrl &WebCtrl::Instance()
{
  static WebCtrl inst;
  return inst;
}


void WebCtrl::Add(Params &p)
{

        if (!timer)
                timer = new EcoreTimer(10.0, (sigc::slot<void>)sigc::mem_fun(*this, &WebCtrl::timerExpired));

        if (!dlManager)
                dlManager = new DownloadManager();

        string url = p.get_param("url");

        /* Return immediatly if url is already in the list */
        list<string>::iterator it;
        for (it = lUrls.begin();it != lUrls.end();it++)
        {
                if (url == *it)
                {
                        return;
                }
        }

        lUrls.push_back(url);

}

void WebCtrl::timerExpired()
{

        list<string>::iterator it;

        for (it = lUrls.begin();it != lUrls.end();it++)
        {
                dlManager->add(*it,
                               string("/tmp/calaos-test"));

        }
}
