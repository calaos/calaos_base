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
#ifndef S_IOFactory_H
#define S_IOFactory_H

#include <Calaos.h>
#include <IOBase.h>
#include <AudioPlayer.h>
#include <IPCam.h>

namespace Calaos
{

class Registrar
{
public:
    Registrar(std::string type, std::function<IOBase *(Params &)> classFunc);
};

#define REGISTER_FACTORY(NAME, TYPE, RETURNCLASS) \
  static Calaos::Registrar NAME##_reg_##RETURNCLASS(#NAME,      \
      std::function<RETURNCLASS *(Params &)>( \
        [](Params &_p) -> Calaos::RETURNCLASS * { return new TYPE(_p); } ));

#define REGISTER_IO_USERTYPE(NAME, TYPE) REGISTER_FACTORY(NAME, TYPE, IOBase)
#define REGISTER_IO(TYPE) REGISTER_IO_USERTYPE(TYPE, TYPE)

class IOFactory
{
private:
    IOFactory() {}

    std::unordered_map<std::string, std::function<IOBase *(Params &)>> ioFunctionRegistry;
    std::unordered_map<std::string, std::string> origNameMap;

public:

    void readParams(TiXmlElement *node, Params &p);

    IOBase *CreateIO(std::string type, Params &params);
    IOBase *CreateIO(TiXmlElement *node);

    void RegisterClass(std::string type, std::function<IOBase *(Params &)> classFunc)
    {
        std::string orig = type;
        std::transform(type.begin(), type.end(), type.begin(), Utils::to_lower());
        origNameMap[type] = orig;
        ioFunctionRegistry[type] = classFunc;
    }

    void genDoc(std::string path);
    void genDocIO(std::string docPath);

    static IOFactory &Instance()
    {
        static IOFactory inst;
        return inst;
    }
};

}

#endif
