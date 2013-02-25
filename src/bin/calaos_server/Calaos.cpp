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
#include <Calaos.h>
#include <FileDownloader.h>
#include <ListeRoom.h>
#include <ListeRule.h>

using namespace Calaos;

Utils::type_signal_wago Utils::signal_wago;

int CURL_write_callback_server(void *buffer, size_t size, size_t nmemb, void *stream)
{
        //don't care about the data
        //just return the simulated number of data read
        return size * nmemb;
}

void Calaos::CallUrl(string url, string post_data)
{
        FileDownloader *downloader = new FileDownloader(url, post_data, "text/plain", true);
        downloader->Start();
}

std::string Calaos::get_new_id(std::string prefix)
{
        int cpt = 0;
        bool found = false;
        while (!found)
        {
                Input *in = ListeRoom::Instance().get_input(prefix + Utils::to_string(cpt));
                Output *out = ListeRoom::Instance().get_output(prefix + Utils::to_string(cpt));

                if (!in && !out)
                        found = true;
                else
                        cpt++;
        }

        std::string ret = prefix + Utils::to_string(cpt);
        return ret;
}

std::string Calaos::get_new_scenario_id()
{
        int cpt = 0;
        bool found = true;
        list<Scenario *> autosc = ListeRoom::Instance().getAutoScenarios();

        while (found && autosc.size() > 0)
        {
                list<Scenario *>::iterator it = autosc.begin();

                bool found2 = false;
                for (;it != autosc.end() && !found2;it++)
                {
                        Scenario *sc = *it;
                        if (sc->get_param("auto_scenario") == "scenario_" + Utils::to_string(cpt))
                                found2 = true;
                }

                if (found2)
                        cpt++;
                else
                        found = false;
        }

        string ret = "scenario_" + Utils::to_string(cpt);
        return ret;
}

StartReadRules::StartReadRules():
        count_io(0)
{
}

void StartReadRules::addIO()
{
        count_io++;
}

void StartReadRules::ioRead()
{
        count_io--;
        if (count_io == 0)
                ListeRule::Instance().ExecuteStartRules();
}
