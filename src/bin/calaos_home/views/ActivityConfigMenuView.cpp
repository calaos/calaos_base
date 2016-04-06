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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>

#include "ActivityConfigMenuView.h"
#include "GengridItemConfig.h"
#include "tcpsocket.h"
#include "Utils.h"

ActivityConfigMenuView::ActivityConfigMenuView(Evas *_e, Evas_Object *_parent):
    ActivityView(_e, _parent, "calaos/config/menu")
{

    setPartText("tab1.text", _("Resume"));
    setPartText("tab1.text.detail", _("Resume of: <light_blue>My Home</light_blue><br><small>Informations about your Calaos System</small"));
    setPartText("tab2.text", _("Our Partners"));
    setPartText("tab2.text.detail", _("<light_blue>Calaos</light_blue> partners<br><small>List of Calaos partners</small>"));
    setPartText("tab3.text", _("About"));
    setPartText("tab3.text.detail", _("About : <light_blue>Calaos products</light_blue><br><small>Touchscreen solutions.</small>"));

    setPartText("tab1.version.label", _("Product Version: "));
    setPartText("tab1.version", PACKAGE_VERSION);
    setPartText("tab1.last_update.label", _("Last update: "));
    setPartText("tab1.last_update", _("Na"));
    setPartText("tab1.uptime.label", _("System started since : "));

    long days = Utils::getUptime() / 60 / 60 / 24;
    std::string uptime;
    uptime = to_string(days);
    if (days == 1)
        uptime += _(" day");
    else
        uptime += _(" days");

    setPartText("tab1.uptime", uptime.c_str());

    setPartText("tab1.hostname.label", _("Machine name : "));

#ifdef  _POSIX_HOSTNAME_MAX
    char hostname[_POSIX_HOSTNAME_MAX];
    gethostname(hostname, _POSIX_HOSTNAME_MAX);
#elif defined HOSTNAME_MAX
    char hostname[HOST_NAME_MAX];
    gethostname(hostname, HOST_NAME_MAX);
#else
    char hostname[128];
    gethostname(hostname, 128);
#endif



    setPartText("tab1.hostname", hostname);

    std::string local_ip = "";
    // Get All interfaces
    std::vector<std::string> intf = TCPSocket::getAllInterfaces();
    if (intf.size() > 0)
    {
        for (unsigned int i = 0; i < intf.size(); i++)
        {
            // Do not try to get ip of loopback interface
            if (intf[i] != "lo")
            {
                // get the ip of the interface
                local_ip = TCPSocket::GetLocalIP(intf[i]);
                // If the ip is set, return, so we return the first interface
                // with a valid address: TODO : display the list of all
                // interfaces with a valid address and Display ipv6 address
                if (local_ip != "")
                    break;
            }
        }
    }
    setPartText("tab1.ipaddress.label", _("Network address :"));
    setPartText("tab1.ipaddress", local_ip.c_str());

    setPartText("tab3.web.label", _("Web Site : "));
    setPartText("tab3.web", CALAOS_WEBSITE_URL);
    setPartText("tab3.mail.label", _("Email : "));
    setPartText("tab3.mail", CALAOS_CONTACT_EMAIL);
    setPartText("tab3.copyright", CALAOS_COPYRIGHT_TEXT);

    grid = elm_gengrid_add(_parent);

    elm_gengrid_select_mode_set(grid, ELM_OBJECT_SELECT_MODE_ALWAYS);

    elm_object_style_set(grid, "calaos");
    evas_object_show(grid);

    elm_gengrid_group_item_size_set(grid, 200, 120);

    naviframe = elm_naviframe_add(parent);
    evas_object_show(naviframe);
    Swallow(naviframe, "naviframe.swallow");

    GengridItemConfig *item;

    item = new GengridItemConfig(evas, grid, _("Date and clock"), "clock");
    item->Append(grid);
    item->item_selected.connect([=](void *data)
    {
        cDebug() << "click on item clock!";
        menu_item_clicked.emit("clock");
    });

    // Disable for now, as it's only usefull in case of Calaos Network.

    // item = new GengridItemConfig(evas, grid, _("Password"), "security");
    // item->Append(grid);
    // item->item_selected.connect([=](void *data)
    // {
    //     cDebug() << "click on item security!";
    //     menu_item_clicked.emit("security");
    // });

    item = new GengridItemConfig(evas, grid, _("Screen saver"), "veille");
    item->Append(grid);
    item->item_selected.connect([=](void *data)
    {
        cDebug() << "click on item Screensaver!";
        menu_item_clicked.emit("screensaver");
    });

    elm_naviframe_item_push(naviframe, NULL, NULL, NULL, grid, "calaos");

}

ActivityConfigMenuView::~ActivityConfigMenuView()
{
    evas_object_del(grid);
}

void ActivityConfigMenuView::resetView()
{
}

