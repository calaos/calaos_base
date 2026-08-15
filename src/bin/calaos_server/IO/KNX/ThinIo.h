/******************************************************************************
 **  Copyright (c) 2006-2026, Calaos. All Rights Reserved.
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
#ifndef THINIO_H
#define THINIO_H

#include <functional>

#include "IODoc.h"

namespace Calaos
{

//Generic skeleton for "thin" driver IO subclasses (T3.2a).
//
//Every hardware driver family (KNX, ...) repeats the same constructor
//preamble in each of its thin subclasses: set the ioDoc friendly name and
//description, then add the driver-specific documentation parameters. This
//template factors that preamble while leaving the actual driver logic to a
//driver-side mixin layered on top of it (see KNXIo.h).
//
//This header is deliberately self-contained and driver-agnostic (it only
//uses the IOBase/IODoc API): it currently lives in IO/KNX/ because T3.2a
//owns only this directory, but it can be promoted as-is to IO/ by a later
//harmonization ticket so the other driver families can share it.
template <typename Base>
class ThinIo: public Base
{
protected:
    ThinIo(Params &p, const char *friendlyName, const string &description,
           const std::function<void(IODoc *)> &extraDoc = {}):
        Base(p)
    {
        this->ioDoc->friendlyNameSet(friendlyName);
        this->ioDoc->descriptionSet(description);

        //Documentation parameters specific to one IO type. Called before the
        //driver mixin adds its own common parameters, to keep the historical
        //paramAdd order (and thus the generated doc) unchanged.
        if (extraDoc)
            extraDoc(this->ioDoc);
    }
};

}

#endif // THINIO_H
