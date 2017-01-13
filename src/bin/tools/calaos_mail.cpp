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

#include <Ecore_File.h>
#include "Utils.h"
#include "libquickmail/quickmail.h"
#include "uri_parser/hef_uri_syntax.h"

using namespace Utils;

void print_usage(void)
{
    cout << "Calaos Mail Utility." << endl;
    cout << "(c)2014 Calaos Team" << endl << endl;
    cout << "Usage:\tcalaos_mail --from <from address> --to <to address> --subject <subject> --body <body file> --attach <file to attach>" << endl << endl;
    cout << "\t--attach\t\tAttach a file. Can be repeated to attach multiple files" << endl;
    cout << "\t--delete\t\tDelete files passed in parameters (not done by default)" << endl;
    cout << "\t--verbose\t\tVerbose mode to debug smtp transaction" << endl << endl;
}

#define EXIT_USAGE \
{ print_usage(); return 1; }

int main (int argc, char **argv)
{
    if (argc < 2) EXIT_USAGE;

    string confTo, confFrom, confSubject, confBody;
    list<string> confAttach;
    bool verbose = false, del = false;

    Utils::InitEinaLog("mail");

    char *argconf = argvOptionParam(argv, argv + argc, "--from");
    if (!argconf) EXIT_USAGE;
    confFrom = argconf;

    argconf = argvOptionParam(argv, argv + argc, "--to");
    if (!argconf) EXIT_USAGE;
    confTo = argconf;

    argconf = argvOptionParam(argv, argv + argc, "--subject");
    if (!argconf) EXIT_USAGE;
    confSubject = argconf;

    argconf = argvOptionParam(argv, argv + argc, "--body");
    if (!argconf) EXIT_USAGE;
    confBody = argconf;

    for (int i = 1;i < argc;i++)
    {
        if (string(argv[i]) == "--attach" && i + 1 < argc)
            confAttach.push_back(argv[++i]);
    }

    if (argvOptionCheck(argv, argv + argc, "--verbose"))
        verbose = true;
    if (argvOptionCheck(argv, argv + argc, "--delete"))
        del = true;

    Utils::initConfigOptions(nullptr, nullptr, true);

    quickmail_initialize();

    quickmail mailobj = quickmail_create(confFrom.c_str(), confSubject.c_str());
    quickmail_add_to(mailobj, confTo.c_str());
    quickmail_set_body(mailobj, getFileContent(confBody.c_str()).c_str());

    for (string attach : confAttach)
    {
        quickmail_add_attachment_file(mailobj, attach.c_str(), "image/jpeg");
    }

    const char* errmsg;
    if (verbose)
        quickmail_set_debug_log(mailobj, stderr);

    string smtp_host = Utils::get_config_option("smtp_server");

    if (strStartsWith(smtp_host, "smtp://") ||
        strStartsWith(smtp_host, "smtps://"))
    {
        //To be backward compatible with old config where we had full uri
        hef::HfURISyntax uri(smtp_host);
        smtp_host = uri.getHost();
    }

    u_int smtp_port;
    Utils::from_string(Utils::get_config_option("smtp_port"), smtp_port);

    if (Utils::get_config_option("smtp_auth") != "true")
        errmsg = quickmail_send(mailobj, smtp_host.c_str(), smtp_port, nullptr, nullptr,
                                Utils::get_config_option("smtp_tls") == "true"?1:0);
    else
        errmsg = quickmail_send(mailobj, smtp_host.c_str(), smtp_port,
                                Utils::get_config_option("smtp_username").c_str(),
                                Utils::get_config_option("smtp_password").c_str(),
                                Utils::get_config_option("smtp_tls") == "true"?1:0);

    if (errmsg)
        cout << "Error sending e-mail: " << errmsg << endl;

    quickmail_destroy(mailobj);

    //delete files if needed
    if (del)
    {
        unlink(confBody.c_str());
        for (string attach : confAttach)
            unlink(attach.c_str());
    }

    Utils::FreeEinaLogs();

    return 0;
}
