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

//Generic skeleton for "thin" driver IO subclasses (T3.2a, promoted to IO/
//by T3.10).
//
//Every hardware driver family (KNX, Mqtt, Web, Gpio, ...) repeats the same
//constructor preamble in each of its thin subclasses: set the ioDoc friendly
//name and description, then add the driver-specific documentation
//parameters. This template factors that preamble while leaving the actual
//driver logic to a driver-side mixin layered on top of it (KNXIo.h,
//MqttIOBase.h, WebDocBase.h, GpioInputBase.h, GpioOutputShutterBase.h).
//
//The header is deliberately self-contained and driver-agnostic: it only uses
//the IOBase/IODoc API and knows nothing about any protocol.
//
//What the generated documentation (--gendoc) depends on is the *set* of
//friendlyNameSet/descriptionSet/paramAdd/aliasAdd/linkAdd calls and their
//arguments, not the order they are made in: the first two write scalar
//members and IODoc keeps its parameters in an unordered_map (IODoc.h).
//Adopting this template is therefore doc-neutral as long as no call is
//added, dropped, or given different arguments -- that is the contract to
//respect, and it is the invariant the byte-identical --gendoc diff checks.
//
//The order the template does impose -- friendlyName, description, extraDoc,
//then the driver mixin's own params -- is a readability convention, kept
//identical to the hand-written constructors it replaced. A driver whose
//preamble does not fit that shape (Wago interleaves aliasAdd between name
//and description) is free to keep its own constructor rather than shuffle
//its ioDoc calls for uniformity's sake.
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
