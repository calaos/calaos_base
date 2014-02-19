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
#ifndef SENDMAIL_H
#define SENDMAIL_H

#include <Utils.h>
#include <CThread.h>
#include <Mutex.h>



class MailMessage
{
        friend class SendMail;

        private:
                int sending_count;
                string subject;
                string text;
                string sender;
                string address;

        public:
                MailMessage();
                ~MailMessage();

                void setSubject(string subject);
                void setBodyText(string text);

                void setSender(string sender);
                void addRecipient(string address);

                bool addAttachment(string file, string filename, string title, string mimeType);

};

class SendMail: public CThread
{
        private:
                SendMail();

                queue<MailMessage *> messages;

                Mutex mutex;
                bool thread_started;
                bool exit_thread;

                string smtp_server;
                int smtp_port;
                bool smtp_tls;
                bool smtp_auth;
                string smtp_username;
                string smtp_password;

        public:
                static SendMail &Instance()
                {
                        static SendMail sm;
                        return sm;
                }
                ~SendMail();

                /* Put message in send queue, and retry 5 times if it fails.
                   message will be deleted after sending.
                */
                void SendMessage(MailMessage *message);

                /* The thread procedure, this is where mails are sent
                */
                virtual void ThreadProc();
};

#endif // SENDMAIL_H
