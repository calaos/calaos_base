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
#include "SendMail.h"

using namespace Utils;

ostream &operator<< (ostream &os, const vmime::exception &e)
{
        os << "* vmime::exceptions::" << e.name() << std::endl;
        os << "    what = " << e.what() << std::endl;
        if (e.other()) os << *e.other();

        return os;
}

/* MailMessage implementation */
MailMessage::MailMessage()
{
}

MailMessage::~MailMessage()
{
}

void MailMessage::setSubject(string subject)
{
        vmessage.setSubject(vmime::text(subject));
}

void MailMessage::setBodyText(string text)
{
        vmessage.getTextPart()->setCharset(vmime::charsets::UTF_8);
        vmessage.getTextPart()->setText(vmime::create <vmime::stringContentHandler>(text));
}

void MailMessage::setSender(string sender)
{
        vmessage.setExpeditor(vmime::mailbox(sender));
}

void MailMessage::addRecipient(string address)
{
        vmessage.getRecipients().appendAddress(vmime::create <vmime::mailbox>(address));
}

bool MailMessage::addAttachment(string file, string filename, string title, string mimeType)
{
        try
        {
                vmime::ref <vmime::fileAttachment> att = vmime::create <vmime::fileAttachment>
                (
                        file, // full path to file
                        vmime::mediaType(mimeType), // content type
                        vmime::text(title) // description
                );
                att->getFileInfo().setFilename(filename);

                vmessage.attach(att);

                return true;
        }
        catch (vmime::exception &e)
        {
                stringstream s;
                s << e;
                Utils::logger("mail") << Priority::ERROR << "MailMessage::addAttachment(): Error, " << s.str() << log4cpp::eol;
        }
        catch (std::exception &e)
        {
                Utils::logger("mail") << Priority::ERROR << "MailMessage::addAttachment(): std Error, " << e.what() << log4cpp::eol;
        }

        return false;
}

/* SendMail implementation */
SendMail::SendMail(): mutex(false), thread_started(false), exit_thread(false)
{
        // Init vmime
        vmime::platform::setHandler<vmime::platforms::posix::posixHandler>();

        smtp_server = Utils::get_config_option("smtp_server");
        string _port = Utils::get_config_option("smtp_port");
        smtp_port = 0;
        if (is_of_type<int>(_port))
        {
                int p;
                from_string(_port, p);
                if (p > 0 && p < 99999)
                        smtp_port = p;
        }

        if (Utils::get_config_option("smtp_tls") == "true")
                smtp_tls = true;
        else
                smtp_tls = false;

        if (Utils::get_config_option("smtp_auth") == "true")
                smtp_auth = true;
        else
                smtp_auth = false;

        smtp_username = Utils::get_config_option("smtp_username");
        smtp_password = Utils::get_config_option("smtp_password");
}

SendMail::~SendMail()
{
        exit_thread = true;
}

void SendMail::SendMessage(MailMessage *message)
{
        mutex.lock();
        message->sending_count = 0;
        messages.push(message);

        if (messages.size() == 1 && !thread_started)
        {
                //start the thread
                thread_started = true;
                Start();
        }
        mutex.unlock();
}

// dummy Certificate verifier (TLS/SSL)
class alwaysCertificateVerifier : public vmime::security::cert::defaultCertificateVerifier
{
        public:
                void verify(vmime::ref <vmime::security::cert::certificateChain> chain) { }
};

void SendMail::ThreadProc()
{
        while (!messages.empty() && !exit_thread)
        {
                MailMessage *msg = messages.front();

                try
                {
                        Utils::logger("mail") << Priority::INFO << "SendMail: Sending mail (attempt #" << msg->sending_count << ") \""
                                        << msg->vmessage.getSubject().getConvertedText(vmime::charsets::UTF_8)
                                        << "\"" << log4cpp::eol;

                        msg->sending_count++;
                        vmime::ref < vmime::net::session > vsession;
                        vmime::ref < vmime::message > vconstructedMessage = msg->vmessage.construct();

                        //debug
                        /*vmime::utility::outputStreamAdapter out(std::cout);
                        vconstructedMessage->generate(out);
                        cout << endl;*/

                        vmime::ref <alwaysCertificateVerifier> cverif = vmime::create <alwaysCertificateVerifier>();

                        vsession = vmime::create <vmime::net::session>();

                        vmime::utility::url url(smtp_server);
                        vmime::ref <vmime::net::transport> tr = vsession->getTransport(url);

                        tr->setCertificateVerifier(cverif);

                        /* Set smtp options here */
                        tr->setProperty("connection.tls", smtp_tls);
                        tr->setProperty("connection.tls.required", smtp_tls);
                        vsession->getProperties()["transport.smtp.auth.username"] = smtp_username;
                        vsession->getProperties()["transport.smtp.auth.password"] = smtp_password;
                        tr->setProperty("transport.smtp.options.need-authentication", smtp_auth);
                        tr->setProperty("options.need-authentication", smtp_auth);
                        tr->setProperty("auth.username", smtp_username);
                        tr->setProperty("auth.pass", smtp_password);

                        if (smtp_port > 0)
                                tr->setProperty("server.port", smtp_port);

                        tr->connect();
                        tr->send(vconstructedMessage);
                        tr->disconnect();

                        //Remove the message from the queue
                        mutex.lock();
                        messages.pop();
                        mutex.unlock();

                        Utils::logger("mail") << Priority::INFO << "SendMail: Mail successfully sent." << log4cpp::eol;
                }
                catch (vmime::exception &e)
                {
                        stringstream s;
                        s << e;
                        Utils::logger("mail") << Priority::ERROR << "SendMail: an exception occured: " << s.str() << log4cpp::eol;
                }
/*                catch (std::exception &e)
                {
                        Utils::logger("mail") << Priority::ERROR << "SendMail: a std::exception occured: " << e.what() << log4cpp::eol;
                }*/

                if (msg->sending_count > 5)
                {
                        //Remove the message from the queue
                        mutex.lock();
                        messages.pop();
                        mutex.unlock();
                        Utils::logger("mail") << Priority::ERROR << "SendMail: Sending mail failed after 5 tries. Please check log for more information." << log4cpp::eol;
                }

                int n = 2 * 5;
                while (n > 0)
                {
                        if (exit_thread)
                        {
                                thread_started = false;
                                return;
                        }

                        struct timespec t;
                        t.tv_sec = 0;
                        t.tv_nsec = 200000000; //200ms
                        nanosleep(&t, NULL);

                        n--;
                }
        }

        thread_started = false;
}

