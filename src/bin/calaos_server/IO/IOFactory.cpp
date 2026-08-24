/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
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

using namespace Calaos;

Registrar::Registrar(string type, function<IOBase *(Params &)> classFunc)
{
    IOFactory::Instance().RegisterClass(type, classFunc);
}

void IOFactory::readParams(pugi::xml_node node, Params &p)
{
    for (pugi::xml_attribute attr: node.attributes())
    {
        p.Add(attr.name(), attr.value());
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

IOBase *IOFactory::CreateIO(pugi::xml_node node)
{
    Params p;
    readParams(node, p);

    //Owning while we configure it, so a throw out of LoadFromXml() cannot
    //leak the IO; released to the caller, which owns it (see the header).
    std::unique_ptr<IOBase> io(CreateIO(p["type"], p));
    if (io)
        io->LoadFromXml(node);

    return io.release();
}

void IOFactory::genDocIO(string docPath)
{
    Json j = Json::object();

    string mdPath = docPath + "/io_doc.md";
    string jsonPath = docPath + "/io_doc.json";

    ofstream mdFile(mdPath, ofstream::out);
    ofstream jsonFile(jsonPath, ofstream::out);

    //The doc IOs below are throwaways sharing id="doc": keep them out of
    //ListeRoom's live io_table (they used to be inserted there, shadowing
    //or colliding with real IOs, and were never deleted).
    IOBase::ScopedDocGen docScope;

    map<string, function<IOBase *(Params &)>> list(ioFunctionRegistry.begin(), ioFunctionRegistry.end());
    for ( auto it = list.begin(); it != list.end(); ++it )
    {
        //fresh Params each iteration: params must not accumulate from one
        //IO type to the next
        Params p;
        p.Add("type", origNameMap[it->first]);
        p.Add("id", "doc");
        //These throwaways belong to nobody else: hold them and let the scope
        //destroy them (the `delete` here was the last manual one of the IO
        //creation path, and it was skipped by the `continue` above).
        std::unique_ptr<IOBase> io(CreateIO(it->first, p));
        if (!io) continue;
        IODoc *doc = io->getDoc();
        if (doc && !doc->isAlias(it->first.c_str()))
        {
            j[origNameMap[it->first]] = doc->genDocJson();
            mdFile << doc->genDocMd(origNameMap[it->first]);
        }
    }

    /* E4.1k: JSON_PRESERVE_ORDER disappears with the old dump and is NOT
     * replaced by nlohmann::ordered_json (user decision, E4.1.md Q3): the keys of this
     * generated file come out alphabetically now. At the top level that
     * changes nothing (it was already filled from a std::map); inside one IO
     * type the five sections move from the editorial order to
     * actions/alias/conditions/description/parameters.
     * JSON_INDENT(4) becomes dump(4, ' '). ensure_ascii = true is the epic
     * invariant; it is a no-op on this artefact today, whose every byte is
     * already ASCII, and it keeps it that way if an accented description ever
     * appears. error_handler_t::replace turns invalid UTF-8 into U+FFFD
     * instead of throwing type_error.316 out of dump(). */
    jsonFile << j.dump(4, ' ', true, Json::error_handler_t::replace);
    jsonFile.close();
    mdFile.close();
}

void IOFactory::genDoc(string path)
{
    if (!FileUtils::exists(path))
    {
        cDebug() << "Creating Documentation path " << path;

        if (!FileUtils::mkpath(path))
        {
            cError() << "Unable to create path " << path;
            return;
        }

    }

    genDocIO(path);
}

