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
#include <IOFactory.h>
#include <Ecore_File.h>

namespace Calaos {

Registrar::Registrar(std::string type, std::function<IOBase *(Params &)> classFunc)
{
    IOFactory::Instance().RegisterClass(type, classFunc);
}

void IOFactory::readParams(TiXmlElement *node, Params &p)
{
    TiXmlAttribute *attr = node->ToElement()->FirstAttribute();

    for (; attr; attr = attr->Next())
    {
        p.Add(attr->Name(), attr->ValueStr());
    }
}

IOBase *IOFactory::CreateIO(std::string type, Params &params)
{
    IOBase *obj = nullptr;

    std::transform(type.begin(), type.end(), type.begin(), Utils::to_lower());

    auto it = ioFunctionRegistry.find(type);
    if (it != ioFunctionRegistry.end())
        obj = it->second(params);

    if (obj)
        cInfo() << type << ": Ok";
    else
        cWarning() <<  type << ": Unknown Input type !";

    return obj;
}

IOBase *IOFactory::CreateIO(TiXmlElement *node)
{
    Params p;
    readParams(node, p);

    IOBase *io = CreateIO(p["type"], p);
    io->LoadFromXml(node);

    return io;
}

void IOFactory::genDocIO(std::string docPath)
{
    Params p;

    json_t *j = json_object();

    std::string mdPath = docPath + "/io_doc.md";
    std::string jsonPath = docPath + "/io_doc.json";

    std::ofstream mdFile(mdPath, std::ofstream::out);
    std::ofstream jsonFile(jsonPath, std::ofstream::out);

    std::map<std::string, std::function<IOBase *(Params &)>> list(ioFunctionRegistry.begin(), ioFunctionRegistry.end());
    for ( auto it = list.begin(); it != list.end(); ++it )
    {
        p.Add("type", origNameMap[it->first]);
        p.Add("id", "doc");
        auto io =  CreateIO(it->first, p);
        IODoc *doc = io->getDoc();
        if (doc && !doc->isAlias(it->first.c_str()))
        {
            json_object_set_new(j, origNameMap[it->first].c_str(), doc->genDocJson());
            mdFile << doc->genDocMd(origNameMap[it->first]);
        }
    }

    jsonFile << json_dumps(j, JSON_PRESERVE_ORDER | JSON_INDENT(4));
    jsonFile.close();
    mdFile.close();
}

void IOFactory::genDoc(std::string path)
{
    if (!ecore_file_exists(path.c_str()))
    {
        cDebug() << "Creating Documentation path " << path;

        if (!ecore_file_mkpath(path.c_str()))
        {
            cError() << "Unable to create path " << path;
            return;
        }

    }

    genDocIO(path);
}

}
