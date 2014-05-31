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
#include <OWCtrl.h>

#ifdef HAVE_OWCAPI_H
#include <owcapi.h>
#endif


using namespace Calaos;

Owctrl::Owctrl(string args)
{
#ifdef HAVE_OWCAPI_H
    if (OW_init(args.c_str()) != 0)
    {
	cErrorDom("input") << "Unable to initialize OW library : " << strerror(errno);
    }
    else
    {
	cInfoDom("input") << "OW Library initialization ok";
    }
#endif
}


Owctrl::~Owctrl()
{
    OW_finish();

}

bool Owctrl::getValue(string path, string &value)
{
    char *res;
    size_t len;
#ifdef HAVE_OWCAPI_H
    if (OW_get(path.c_str(), &res, &len) >= 0)
    {
	value = res;
	return true;
    }
    else
    {
	return false;
    }
#else
    cInfoDom("input") << ow_id << ": One Wire support not enabled !";
#endif
}

Owctrl &Owctrl::Instance(string args)
{
    static Owctrl inst(args);
    return inst;
}
