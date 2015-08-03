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

void IOFactory::genDocOutput(string docPath, string type)
{
    Params p;

    json_t *j = json_object();

    string mdPath = docPath + "/"+ type + "s.md";
    string jsonPath = docPath + "/" + type + "s.json";

    ofstream mdFile(mdPath, ofstream::out);
    ofstream jsonFile(jsonPath, ofstream::out);

    if (type == "input")
    {
        auto list = inputFunctionRegistry;
        for ( auto it = list.begin(); it != list.end(); ++it )
        {
            auto io =  CreateInput(it->first, p);
            json_object_set_new(j, it->first.c_str(), io->genDocJson());
            mdFile << io->genDocMd();
        }
    }
    else if (type == "output")
    {
        auto list = outputFunctionRegistry;
        for ( auto it = list.begin(); it != list.end(); ++it )
        {
            auto io =  CreateOutput(it->first, p);
            json_object_set_new(j, it->first.c_str(), io->genDocJson());
            mdFile << io->genDocMd();
        }
    }
    else if (type == "audio")
    {
        auto list = audioFunctionRegistry;
        for ( auto it = list.begin(); it != list.end(); ++it )
        {
            auto input =  CreateAudio(it->first, p)->get_input();
            json_object_set_new(j, it->first.c_str(), input->genDocJson());
            auto output =  CreateAudio(it->first, p)->get_output();
            json_object_set_new(j, it->first.c_str(), output->genDocJson());
            mdFile << input->genDocMd();
            mdFile << output->genDocMd();
        }
    }
    else if (type == "cam")
    {
        auto list = camFunctionRegistry;
        for ( auto it = list.begin(); it != list.end(); ++it )
        {
            auto input =  CreateIPCamera(it->first, p)->get_input();
            json_object_set_new(j, it->first.c_str(), input->genDocJson());
            auto output =  CreateIPCamera(it->first, p)->get_output();
            json_object_set_new(j, it->first.c_str(), output->genDocJson());
            mdFile << input->genDocMd();
            mdFile << output->genDocMd();
        }
    }

    jsonFile << json_dumps(j, JSON_PRESERVE_ORDER | JSON_INDENT(4));
    jsonFile.close();
    mdFile.close();
}

void IOFactory::genDoc(string path)
{

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

    genDocOutput(docPath, "input");
    genDocOutput(docPath, "output");
    genDocOutput(docPath, "audio");
    genDocOutput(docPath, "cam");
}

