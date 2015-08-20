/******************************************************************************
**  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
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
#include "ZibaseDigitalOut.h"
#include "Zibase.h"
#include <IOFactory.h>

using namespace Calaos;

REGISTER_IO(ZibaseDigitalOut)

ZibaseDigitalOut::ZibaseDigitalOut(Params &p):
    OutputLight(p),
    port(0)
{
    std::string type = get_param("zibase_sensor");
    host = get_param("host");
    Utils::from_string(get_param("port"), port);
    id = get_param("zibase_id");


    Utils::from_string(get_param("protocol"), protocol);
    Utils::from_string(get_param("nbburst"), nbburst);

    /* allocate new ZibaseInfoProtocol structure*/
    prot = new(ZibaseInfoProtocol);
    if(prot)
    {
        /* Transform ID in house code / device code */
        char housecode;
         char *device;
        if(protocol==ZibaseInfoProtocol::eZWAVE)
        {
            housecode=id[1];
            device=&id[2];
        }
        else
        {
            housecode=id[0];
            device=&id[1];
        }
        switch(housecode)
        {
            case 'A':prot->house_code = ZibaseInfoProtocol::A;break;
            case 'B':prot->house_code = ZibaseInfoProtocol::B;break;
            case 'C':prot->house_code = ZibaseInfoProtocol::C;break;
            case 'D':prot->house_code = ZibaseInfoProtocol::D;break;
            case 'E':prot->house_code = ZibaseInfoProtocol::E;break;
            case 'F':prot->house_code = ZibaseInfoProtocol::F;break;
            case 'G':prot->house_code = ZibaseInfoProtocol::G;break;
            case 'H':prot->house_code = ZibaseInfoProtocol::H;break;
            case 'I':prot->house_code = ZibaseInfoProtocol::I;break;
            case 'J':prot->house_code = ZibaseInfoProtocol::J;break;
            case 'K':prot->house_code = ZibaseInfoProtocol::K;break;
            case 'L':prot->house_code = ZibaseInfoProtocol::L;break;
            case 'M':prot->house_code = ZibaseInfoProtocol::M;break;
            case 'N':prot->house_code = ZibaseInfoProtocol::N;break;
            case 'O':prot->house_code = ZibaseInfoProtocol::O;break;
            case 'P':prot->house_code = ZibaseInfoProtocol::P;break;
            default:prot->house_code = ZibaseInfoProtocol::A;break;
        }


        prot->device_number = (unsigned char)atoi(device);

        if(protocol<=ZibaseInfoProtocol::eXDD868boiler)
            prot->protocol = (ZibaseInfoProtocol::eZibaseProtocol)protocol;
        else
            prot->protocol=ZibaseInfoProtocol::eDFLT_PROTOCOL;

        prot->nb_burst = nbburst;
        prot->ID=id;
        /* connect signal */
        Zibase::Instance(host, port).sig_newframe.connect(sigc::mem_fun(*this, &ZibaseDigitalOut::valueUpdated));

        /* read variable to know output state*/
        Zibase::Instance(host, port).rw_variable(prot);

        //Utils::logger("output") << Priority::DEBUG << "ZibaseDigitalOut::ZibaseDigitalOut(" << get_param("id") << "): Ok" << log4cpp::eol;
        cDebugDom("output") << get_param("id");

    }
    //else Utils::logger("output") << Priority::DEBUG << "ZibaseDigitalOut::ZibaseDigitalOut(" << get_param("id") << "): NOK" << log4cpp::eol;
}

ZibaseDigitalOut::~ZibaseDigitalOut()
{
    if(prot) delete(prot);
    //Utils::logger("output") << Priority::DEBUG << "ZibaseDigitalOut::~ZibaseDigitalOut(): Ok" << log4cpp::eol;
    cDebugDom("output");
}

void ZibaseDigitalOut::valueUpdated(ZibaseInfoSensor *sensor)
{
    /*check that sensor id match */
    if((id==sensor->id) && sensor->Error == false)
    {
        value = sensor->DigitalVal;
        emitChange();
    }
}

bool ZibaseDigitalOut::set_value_real(bool val)
{
    Zibase::Instance(host, port).rf_frame_sending(val,prot);
    return true;
}

