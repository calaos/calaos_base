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
#ifndef CALAOSNETWORK_H
#define CALAOSNETWORK_H

#include <Utils.h>
#include <FileDownloader.h>

class CalaosNetwork: public sigc::trackable
{
private:
    std::string username;
    std::string password;

    FileDownloader *fdownloader;

    bool download_in_progress;

    void register_cb(std::string result, void *data);
    void update_ip_cb(std::string result, void *data);
    void get_ip_cb(std::string result, void *data);

public:
    CalaosNetwork(std::string username = "", std::string password = "");
    ~CalaosNetwork();

    /**
                  * Registers the user with the machine on calaos network
                  */
    void Register(std::string username, std::string password);

    /**
                  * Update Calaos Network DNS IP
                  */
    void updateIP(std::string private_ip);

    /**
                  * Query Calaos Network account for private/public IP
                  */
    void getIP();

    /* Signals */
    sigc::signal<void, std::string> registered;
    sigc::signal<void, std::string> ip_updated;
    sigc::signal<void, std::string, std::string /* public_ip */, std::string /* private_ip */, bool /* at_home */> ip_retrieved;

};

#endif // CALAOSNETWORK_H
