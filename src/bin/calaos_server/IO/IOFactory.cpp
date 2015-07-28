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

using namespace Calaos;

Registrar::Registrar(string type, function<Input *(Params &)> classFunc)
{
    std::transform(type.begin(), type.end(), type.begin(), Utils::to_lower());
    IOFactory::Instance().RegisterClass(type, classFunc);
}

Registrar::Registrar(string type, function<Output *(Params &)> classFunc)
{
    std::transform(type.begin(), type.end(), type.begin(), Utils::to_lower());
    IOFactory::Instance().RegisterClass(type, classFunc);
}

Registrar::Registrar(string type, function<AudioPlayer *(Params &)> classFunc)
{
    std::transform(type.begin(), type.end(), type.begin(), Utils::to_lower());
    IOFactory::Instance().RegisterClass(type, classFunc);
}

Registrar::Registrar(string type, function<IPCam *(Params &)> classFunc)
{
    std::transform(type.begin(), type.end(), type.begin(), Utils::to_lower());
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

Input *IOFactory::CreateInput(std::string type, Params &params)
{
    Input *obj = nullptr;

    std::transform(type.begin(), type.end(), type.begin(), Utils::to_lower());

    auto it = inputFunctionRegistry.find(type);
    if (it != inputFunctionRegistry.end())
        obj = it->second(params);

    if (obj)
        cInfo() << type << ": Ok";
    else
        cWarning() <<  type << ": Unknown Input type !";

    return obj;
}

Input *IOFactory::CreateInput(TiXmlElement *node)
{
    Params p;
    readParams(node, p);

    Input *in = CreateInput(p["type"], p);
    in->LoadFromXml(node);

    return in;
}

Output *IOFactory::CreateOutput(std::string type, Params &params)
{
    Output *obj = nullptr;

    std::transform(type.begin(), type.end(), type.begin(), Utils::to_lower());

    auto it = outputFunctionRegistry.find(type);
    if (it != outputFunctionRegistry.end())
        obj = it->second(params);

    if (obj)
        cInfo() << type << ": Ok";
    else
        cWarning() << type << ": Unknown Output type !";

    return obj;
}

Output *IOFactory::CreateOutput(TiXmlElement *node)
{
    Params p;
    readParams(node, p);

    Output *out = CreateOutput(p["type"], p);
    if (!out)
        return nullptr;
    out->LoadFromXml(node);

    return out;
}

AudioPlayer *IOFactory::CreateAudio (std::string type, Params &params)
{
    AudioPlayer *obj = nullptr;

    std::transform(type.begin(), type.end(), type.begin(), Utils::to_lower());

    auto it = audioFunctionRegistry.find(type);
    if (it != audioFunctionRegistry.end())
        obj = it->second(params);

    if (obj)
        cInfo() << type << ": Ok";
    else
        cWarning() << type << ": Unknown Input type !";

    return obj;
}

AudioPlayer *IOFactory::CreateAudio(TiXmlElement *node)
{
    Params p;
    readParams(node, p);

    AudioPlayer *pl = CreateAudio(p["type"], p);
    pl->LoadFromXml(node);

    return pl;
}

IPCam *IOFactory::CreateIPCamera (std::string type, Params &params)
{
    IPCam *obj = nullptr;

    std::transform(type.begin(), type.end(), type.begin(), Utils::to_lower());

    auto it = camFunctionRegistry.find(type);
    if (it != camFunctionRegistry.end())
        obj = it->second(params);

    if (obj)
        cInfo() << type << ": Ok";
    else
        cWarning() << type << ": Unknown Input type !";

    return obj;
}

IPCam *IOFactory::CreateIPCamera(TiXmlElement *node)
{
    Params p;
    readParams(node, p);

    IPCam *cam = CreateIPCamera(p["type"], p);
    cam->LoadFromXml(node);

    return cam;
}

void IOFactory::genDoc(string path)
{
    Params p;
    string docPath = path + "/" + PACKAGE_STRING;

    if (!ecore_file_exists(docPath.c_str()))
    {
        cDebug() << "Creating Documentation path " << path;

        if (!ecore_file_mkpath(docPath.c_str()))
        {
            cError() << "Unable to create path " << path;
            return;
        }

    }

    string filePath = docPath + "/inputs.md";
    ofstream ofs(filePath, ofstream::out);
    for ( auto it = inputFunctionRegistry.begin(); it != inputFunctionRegistry.end(); ++it )
    {
        std::cout << "#" << it->first << endl;//<< ":" << it->second;
        Input *in = IOFactory::Instance().CreateInput(it->first, p);
        ofs << in->genDocMd();
    }
    ofs.close();

    filePath = docPath + "/inputs.json";
    ofs.open(filePath, ofstream::out);
    json_t *j = json_object();
    for ( auto it = inputFunctionRegistry.begin(); it != inputFunctionRegistry.end(); ++it )
    {
        Input *in = IOFactory::Instance().CreateInput(it->first, p);
        json_object_set_new(j, it->first.c_str(), in->genDocJson());

    }
    ofs << json_dumps(j, JSON_PRESERVE_ORDER | JSON_INDENT(4));

    ofs.close();
}
