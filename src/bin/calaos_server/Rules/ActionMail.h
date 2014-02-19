/******************************************************************************
**  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
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
#ifndef S_ACTIONMAIL_H
#define S_ACTIONMAIL_H

#include <Calaos.h>
#include <Action.h>

namespace Calaos
{

class ActionMail: public Action
{
        private:
                string mail_sender;
                string mail_recipients;
                string mail_subject;
                string mail_attachment;
                string mail_message;

                sigc::signal<void, string, string, void*, void*> sigDownload;

                void IPCDownloadDone(string source, string signal, void* listener_data, void* sender_data);

        public:
                ActionMail();
                ~ActionMail();

                bool Execute();

                bool LoadFromXml(TiXmlElement *node);
                bool SaveToXml(TiXmlElement *node);
};

}
#endif
