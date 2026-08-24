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

/*******************************************************************************
 * E4.1k - CHARACTERIZATION of the io_doc.json generator.
 *
 * Two producers, in the same file because they are the same artefact:
 *   - IODoc::genDocJson()   - the JSON document of ONE IO type (5 sections);
 *   - IOFactory::genDocIO() - the aggregation of every registered type, and
 *                             the write of io_doc.json / io_doc.md on disk.
 *
 * This suite is written BEFORE the jansson -> nlohmann migration of those two
 * functions and must stay green across it. It had NO net at all before: not
 * one test of the whole repository looked at what genDocJson() produces, and
 * the only test that ever called genDoc() (IOIdIntegrity_test's
 * GenDocDoesNotPolluteLiveTable) checks the live io_table, never the files.
 *
 * THE ONE FUNCTION THAT CHANGES AT MIGRATION TIME is docToJson() below - the
 * seam. Every assertion is written against that seam and against nlohmann
 * values, so the migration commit touches no assertion at all. Same protocol
 * as tests/ParamsJson_test.cpp (E4.1a).
 *
 * WHAT IS DELIBERATELY *NOT* ASSERTED, and why:
 *
 *  - The ORDER OF THE KEYS inside a document. The user decision of 2026-08-17
 *    assumes sorted keys (nlohmann::json, never ordered_json) and E4.0's
 *    oracle contract compares documents, not strings. io_doc.json is the one
 *    place of the repository that asked jansson for JSON_PRESERVE_ORDER, and
 *    that request is dropped on purpose (E4.1.md Q3): the five sections come
 *    out alphabetically after the migration instead of in the editorial order
 *    description/alias/parameters/conditions/actions. Pinning the order here
 *    would pin exactly what the ticket decided to give up.
 *
 *  - The ORDER OF THE ELEMENTS inside the parameters/conditions/actions
 *    ARRAYS. IODoc holds them in an unordered_map, so that order is a
 *    libstdc++ implementation detail today, jansson or nlohmann alike. Every
 *    helper below therefore sorts by "name" before comparing - which is also
 *    what makes a swap of two documented parameters visible: sorting by name
 *    and comparing the whole object catches a payload that moved to another
 *    name, where a set comparison would not.
 *
 * WHAT IS ASSERTED ON PURPOSE, because the migration could silently break it:
 *
 *  - EVERY leaf value is a JSON *string*. IODoc feeds itself through Params,
 *    which is a map<string,string>; min/max/default of an int or a float
 *    parameter are Utils::to_string()'d BEFORE they reach JSON. An "int that
 *    became a JSON number" is the number one trap of this migration and the
 *    oracle of the golden suite is type-strict (3 != "3").
 *  - Utils::to_string(double) is a bare ostringstream: 1234.56789 comes out
 *    "1234.57". That truncation is frozen here, it must NOT be "fixed".
 *  - An ABSENT key stays ABSENT, it does not become null (paramAdd() with an
 *    empty default adds no "default" key at all).
 *  - The empty document still carries its five sections, with empty arrays -
 *    not null, not absent.
 ******************************************************************************/

#include <fstream>
#include <sstream>
#include <algorithm>
#include <memory>
#include <vector>

#include <jansson.h>

#include "CalaosCoreFixture.h"
#include "IODoc.h"
#include "IOFactory.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

/* THE SEAM. Before E4.1k, IODoc::genDocJson() answers a json_t* and this
 * function transcodes it; after E4.1k it answers a Json and the body is a
 * plain `return doc.genDocJson();`. This is the only place of the file the
 * migration commit is allowed to touch. */
Json docToJson(IODoc &doc)
{
    json_t *j = doc.genDocJson();
    char *s = json_dumps(j, JSON_ENCODE_ANY);
    Json out = Json::parse(s);
    free(s);
    json_decref(j);
    return out;
}

/* A deliberately RICH fixture.
 *
 * The recurring defect of the E4.0/E4.1 series is the "poor fixture": a
 * dataset too uniform to tell two interchangeable fields apart, so that
 * swapping them leaves the suite green. Every choice below exists to make an
 * exchange visible:
 *   - the head description and the base description differ, so a swap of the
 *     two halves of the concatenation shows;
 *   - the aliases are inserted in reverse alphabetical order, so the vector
 *     order is distinguishable from a sorted order;
 *   - no two parameter names, descriptions or values are equal, and each
 *     parameter carries its own vocabulary ("p_int" only ever says "int"), so
 *     a permutation of two parameters cannot hide;
 *   - min and max of the int parameter differ, so a min<->max exchange shows;
 *   - mandatory and readonly differ on every parameter, so a
 *     mandatory<->readonly exchange shows;
 *   - there are TWO list parameters with DISJOINT key/value sets, so a
 *     list_value attached to the wrong parameter shows;
 *   - the conditions and the actions draw from disjoint vocabularies
 *     ("c_*"/"cond" vs "a_*"/"act"), so a swap of the two arrays shows;
 *   - one parameter has NO default, to pin "absent stays absent".
 */
IODoc richDoc()
{
    IODoc doc;
    doc.friendlyNameSet("Friendly Doc Name");
    doc.descriptionSet("d_head_of_the_io");
    doc.descriptionBaseSet("d_base_of_the_family");

    //vector: insertion order, reverse alphabetical on purpose
    doc.aliasAdd("z_alias_inserted_first");
    doc.aliasAdd("a_alias_inserted_second");

    //no default at all -> no "default" key
    doc.paramAdd("p_string", "desc_of_string", IODoc::TYPE_STRING, true);
    //a default, and readonly true while mandatory is false
    doc.paramAdd("p_bool", "desc_of_bool", IODoc::TYPE_BOOL, false, "false", true);
    //distinct min/max/default, all Utils::to_string()'d into STRINGS
    doc.paramAddInt("p_int", "desc_of_int", 11, 99, true, 42, false);
    //pins Utils::to_string(double): 1234.56789 -> "1234.57"
    doc.paramAddFloat("p_float", "desc_of_float", false, -40.5, 125.25, 1234.56789, true);

    Params listA;
    listA.Add("kA_zulu", "vA_zulu");
    listA.Add("kA_alpha", "vA_alpha");
    Params listB;
    listB.Add("kB_zulu", "vB_zulu");
    listB.Add("kB_alpha", "vB_alpha");
    doc.paramAddList("p_lista", "desc_of_lista", true, listA, "kA_alpha", false);
    doc.paramAddList("p_listb", "desc_of_listb", false, listB, "kB_zulu", true);

    doc.conditionAdd("c_first", "desc_cond_first");
    doc.conditionAdd("c_second", "desc_cond_second");

    doc.actionAdd("a_first", "desc_act_first");
    doc.actionAdd("a_second", "desc_act_second");

    return doc;
}

/* The arrays come out of an unordered_map: sort by "name" before comparing.
 * Comparing whole objects after sorting by name is what makes an exchange of
 * two parameters visible - a payload that moved to another name changes the
 * object that sits at that name. */
Json sortedByName(const Json &array)
{
    std::vector<Json> v(array.begin(), array.end());
    std::sort(v.begin(), v.end(),
              [](const Json &a, const Json &b)
              {
                  return a.value("name", std::string()) < b.value("name", std::string());
              });
    return Json(v);
}

Json paramNamed(const Json &doc, const std::string &name)
{
    for (const Json &p: doc.at("parameters"))
    {
        if (p.contains("name") && p.at("name") == name)
            return p;
    }
    return Json();
}

std::string readWholeFile(const std::string &path)
{
    std::ifstream f(path, std::ifstream::in | std::ifstream::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} //namespace

/*******************************************************************************
 * IODoc::genDocJson() - the shape of the document
 ******************************************************************************/

//Exactly five sections, no more, no less. A section that appeared or
//disappeared during the migration is caught here and nowhere else.
TEST(IODocJson, TheDocumentHasExactlyTheFiveSections)
{
    IODoc doc = richDoc();
    Json j = docToJson(doc);

    ASSERT_TRUE(j.is_object());
    EXPECT_EQ(5u, j.size());
    EXPECT_TRUE(j.contains("description"));
    EXPECT_TRUE(j.contains("alias"));
    EXPECT_TRUE(j.contains("parameters"));
    EXPECT_TRUE(j.contains("conditions"));
    EXPECT_TRUE(j.contains("actions"));

    EXPECT_TRUE(j.at("description").is_string());
    EXPECT_TRUE(j.at("alias").is_array());
    EXPECT_TRUE(j.at("parameters").is_array());
    EXPECT_TRUE(j.at("conditions").is_array());
    EXPECT_TRUE(j.at("actions").is_array());
}

//The description is the concatenation "<own> <base>", in that order, joined
//by exactly one space. Swapping the two halves changes the string.
TEST(IODocJson, DescriptionIsOwnThenBaseJoinedByOneSpace)
{
    IODoc doc = richDoc();
    EXPECT_EQ("d_head_of_the_io d_base_of_the_family",
              docToJson(doc).at("description").get<std::string>());
}

//The three degenerate cases of that concatenation, frozen as they are - the
//base-only case really does emit a LEADING space, and that quirk is part of
//the artefact today.
TEST(IODocJson, DescriptionDegenerateCasesIncludingTheLeadingSpaceQuirk)
{
    IODoc onlyOwn;
    onlyOwn.descriptionSet("only_own");
    EXPECT_EQ("only_own", docToJson(onlyOwn).at("description").get<std::string>());

    IODoc onlyBase;
    onlyBase.descriptionBaseSet("only_base");
    EXPECT_EQ(" only_base", docToJson(onlyBase).at("description").get<std::string>());

    IODoc neither;
    EXPECT_EQ("", docToJson(neither).at("description").get<std::string>());
}

//The aliases keep their INSERTION order: IODoc stores them in a vector, not
//in a map. They were inserted in reverse alphabetical order precisely so that
//a sorted output would differ from this one.
TEST(IODocJson, AliasIsAnArrayInInsertionOrderNotSorted)
{
    IODoc doc = richDoc();
    Json alias = docToJson(doc).at("alias");

    ASSERT_EQ(2u, alias.size());
    EXPECT_EQ("z_alias_inserted_first", alias.at(0).get<std::string>());
    EXPECT_EQ("a_alias_inserted_second", alias.at(1).get<std::string>());
}

//An IODoc that was never fed still answers the five sections, with EMPTY
//arrays and an empty string - not null, not absent. jansson and nlohmann
//disagree on nothing here today, and this pins that they keep agreeing.
TEST(IODocJson, AnEmptyDocStillCarriesItsFiveEmptySections)
{
    IODoc doc;
    Json j = docToJson(doc);

    ASSERT_EQ(5u, j.size());
    EXPECT_EQ("", j.at("description").get<std::string>());
    EXPECT_TRUE(j.at("alias").is_array());
    EXPECT_TRUE(j.at("alias").empty());
    EXPECT_TRUE(j.at("parameters").is_array());
    EXPECT_TRUE(j.at("parameters").empty());
    EXPECT_TRUE(j.at("conditions").is_array());
    EXPECT_TRUE(j.at("conditions").empty());
    EXPECT_TRUE(j.at("actions").is_array());
    EXPECT_TRUE(j.at("actions").empty());
    //explicitly NOT null: a section that became null would still be "present"
    EXPECT_FALSE(j.at("alias").is_null());
    EXPECT_FALSE(j.at("parameters").is_null());
    EXPECT_FALSE(j.at("conditions").is_null());
    EXPECT_FALSE(j.at("actions").is_null());
}

/*******************************************************************************
 * IODoc::genDocJson() - the parameters
 ******************************************************************************/

//THE type-strictness assertion of this ticket. Everything IODoc publishes
//goes through Params, i.e. through std::string; an int or a float parameter
//publishes its min/max/default as STRINGS. A serializer that started typing
//its output ("min": 11 instead of "min": "11") would break the type-strict
//oracle without breaking a single golden.
TEST(IODocJson, EveryLeafValueOfEveryParameterIsAJsonString)
{
    IODoc doc = richDoc();
    Json params = docToJson(doc).at("parameters");

    ASSERT_EQ(6u, params.size());
    for (const Json &p: params)
    {
        ASSERT_TRUE(p.is_object());
        for (auto it = p.begin(); it != p.end(); ++it)
        {
            if (it.key() == "list_value")
            {
                //the one nested object: a flat map of string -> string
                ASSERT_TRUE(it.value().is_object()) << "list_value of " << p.dump();
                for (auto lit = it.value().begin(); lit != it.value().end(); ++lit)
                    EXPECT_TRUE(lit.value().is_string())
                        << "list_value[" << lit.key() << "] of " << p.dump();
                continue;
            }
            EXPECT_TRUE(it.value().is_string())
                << "key '" << it.key() << "' of " << p.dump();
        }
    }
}

//Whole-object equality, parameter by parameter. This is the assertion that
//makes an EXCHANGE of two parameters red: every field of every parameter is
//spelled out, and no two parameters share a value.
TEST(IODocJson, EachParameterIsExactlyItsOwnPayload)
{
    IODoc doc = richDoc();
    Json j = docToJson(doc);

    EXPECT_EQ(Json({{"name", "p_string"},
                    {"description", "desc_of_string"},
                    {"type", "string"},
                    {"mandatory", "true"},
                    {"readonly", "false"}}),
              paramNamed(j, "p_string"));

    EXPECT_EQ(Json({{"name", "p_bool"},
                    {"description", "desc_of_bool"},
                    {"type", "bool"},
                    {"mandatory", "false"},
                    {"default", "false"},
                    {"readonly", "true"}}),
              paramNamed(j, "p_bool"));

    EXPECT_EQ(Json({{"name", "p_int"},
                    {"description", "desc_of_int"},
                    {"type", "int"},
                    {"mandatory", "true"},
                    {"default", "42"},
                    {"min", "11"},
                    {"max", "99"},
                    {"readonly", "false"}}),
              paramNamed(j, "p_int"));

    //"1234.57" and not "1234.56789": Utils::to_string(double) is a bare
    //ostringstream with the default 6 significant digits. Frozen on purpose,
    //it must NOT be "corrected" by this migration.
    EXPECT_EQ(Json({{"name", "p_float"},
                    {"description", "desc_of_float"},
                    {"type", "float"},
                    {"mandatory", "false"},
                    {"default", "1234.57"},
                    {"min", "-40.5"},
                    {"max", "125.25"},
                    {"readonly", "true"}}),
              paramNamed(j, "p_float"));
}

//paramAdd() with an empty default adds NO "default" key. Absent is not null:
//a migration that started emitting "default": null would still parse, still
//keep the golden suite green, and would be a change of the artefact.
TEST(IODocJson, AnEmptyDefaultLeavesTheKeyAbsentNotNull)
{
    IODoc doc = richDoc();
    Json p = paramNamed(docToJson(doc), "p_string");

    ASSERT_TRUE(p.is_object());
    EXPECT_FALSE(p.contains("default"));
    EXPECT_EQ(5u, p.size());
}

//list_value is attached to ITS OWN parameter. Two list parameters with
//disjoint key/value vocabularies, so an attachment that slid from one to the
//other is red. Non-list parameters carry no list_value at all.
TEST(IODocJson, ListValueIsAttachedToItsOwnParameterOnly)
{
    IODoc doc = richDoc();
    Json j = docToJson(doc);

    Json a = paramNamed(j, "p_lista");
    ASSERT_TRUE(a.is_object());
    EXPECT_EQ(Json({{"kA_alpha", "vA_alpha"}, {"kA_zulu", "vA_zulu"}}),
              a.at("list_value"));
    EXPECT_EQ("kA_alpha", a.at("default").get<std::string>());
    EXPECT_EQ("list", a.at("type").get<std::string>());

    Json b = paramNamed(j, "p_listb");
    ASSERT_TRUE(b.is_object());
    EXPECT_EQ(Json({{"kB_alpha", "vB_alpha"}, {"kB_zulu", "vB_zulu"}}),
              b.at("list_value"));
    EXPECT_EQ("kB_zulu", b.at("default").get<std::string>());
    EXPECT_EQ("list", b.at("type").get<std::string>());

    for (const std::string &name: {"p_string", "p_bool", "p_int", "p_float"})
    {
        Json other = paramNamed(j, name);
        ASSERT_TRUE(other.is_object()) << name << " is missing from the document";
        EXPECT_FALSE(other.contains("list_value")) << name;
    }
}

/*******************************************************************************
 * IODoc::genDocJson() - conditions and actions
 ******************************************************************************/

//The two arrays are built by the same three lines of code and are therefore
//the easiest pair of the file to swap. Their vocabularies are disjoint, so
//the swap is red on both sides.
TEST(IODocJson, ConditionsAndActionsAreTwoDistinctArrays)
{
    IODoc doc = richDoc();
    Json j = docToJson(doc);

    EXPECT_EQ(Json::array({Json({{"name", "c_first"}, {"description", "desc_cond_first"}}),
                           Json({{"name", "c_second"}, {"description", "desc_cond_second"}})}),
              sortedByName(j.at("conditions")));

    EXPECT_EQ(Json::array({Json({{"name", "a_first"}, {"description", "desc_act_first"}}),
                           Json({{"name", "a_second"}, {"description", "desc_act_second"}})}),
              sortedByName(j.at("actions")));
}

//Non-ASCII survives the round trip as the SAME characters. This says nothing
//about the escaping form on disk (jansson escapes é upper case, nlohmann
//with ensure_ascii escapes é lower case, both parse back identically) -
//that dimension belongs to the tripwire of tests/ParamsJson_test.cpp. What is
//pinned here is that no byte is mangled or dropped on the way.
TEST(IODocJson, NonAsciiTextIsCarriedThroughUnchanged)
{
    IODoc doc;
    doc.descriptionSet("caf\xc3\xa9 \xc3\xa0 la cr\xc3\xa8me"); //"café à la crème"
    doc.paramAdd("p_accent", "temp\xc3\xa9rature ext\xc3\xa9rieure",
                 IODoc::TYPE_STRING, true);

    Json j = docToJson(doc);
    EXPECT_EQ("caf\xc3\xa9 \xc3\xa0 la cr\xc3\xa8me",
              j.at("description").get<std::string>());
    EXPECT_EQ("temp\xc3\xa9rature ext\xc3\xa9rieure",
              paramNamed(j, "p_accent").at("description").get<std::string>());
}

/*******************************************************************************
 * IOFactory::genDocIO() - the two files on disk
 *
 * The aggregation and the write. Only the IO types whose object file is
 * linked into this binary are registered (see CORE_SERVER_OBJECTS in
 * tests/Makefile.am): the internal ones, InPlageHoraire/TimeRange, InputTimer
 * and Scenario. That is enough to prove that the top level maps the ORIGINAL
 * type name to the document of THAT type.
 ******************************************************************************/

class IODocGenFileTest: public CoreFixture
{
protected:
    std::string docDir() const { return cacheDir() + "/e4_1k_gendoc"; }
};

//The JSON goes to io_doc.json and the markdown to io_doc.md, and the two are
//not interchangeable: swapping the two paths makes both halves of this test
//red at once.
TEST_F(IODocGenFileTest, GenDocWritesJsonToTheJsonFileAndMarkdownToTheMdFile)
{
    loadConfig();
    IOFactory::Instance().genDoc(docDir());

    const std::string jsonTxt = readWholeFile(docDir() + "/io_doc.json");
    const std::string mdTxt = readWholeFile(docDir() + "/io_doc.md");

    ASSERT_FALSE(jsonTxt.empty());
    ASSERT_FALSE(mdTxt.empty());

    //io_doc.json really is JSON...
    ASSERT_EQ('{', jsonTxt[0]);
    ASSERT_NO_THROW(Json::parse(jsonTxt));

    //...and io_doc.md really is markdown, not JSON
    EXPECT_NE('{', mdTxt[0]) << "io_doc.md must not start with '{'";
    EXPECT_NE(std::string::npos, mdTxt.find("\n# "))
        << "io_doc.md must carry markdown headings";
    EXPECT_EQ(std::string::npos, mdTxt.find("\"parameters\":"))
        << "io_doc.md must not carry the JSON document";
}

//The top level is a map from the ORIGINAL (case preserved) type name to the
//document produced by genDocJson() for that very type. Two different types
//must carry two different documents: comparing them to each other is what
//catches an aggregation that published the same doc under every name.
TEST_F(IODocGenFileTest, TheTopLevelMapsEachTypeNameToItsOwnDocument)
{
    loadConfig();
    IOFactory::Instance().genDoc(docDir());

    Json j = Json::parse(readWholeFile(docDir() + "/io_doc.json"));
    ASSERT_TRUE(j.is_object());

    //registered by IO/IntValue.o, IO/InputTimer.o and IO/Scenario.o, which
    //are all in CORE_SERVER_OBJECTS
    ASSERT_TRUE(j.contains("InternalBool")) << j.dump();
    ASSERT_TRUE(j.contains("InputTimer")) << j.dump();
    ASSERT_TRUE(j.contains("Scenario")) << j.dump();

    //lower case spelling is the registry key, NOT the published name
    EXPECT_FALSE(j.contains("internalbool"));

    for (const std::string &type: {"InternalBool", "InputTimer", "Scenario"})
    {
        const Json &d = j.at(type);
        EXPECT_TRUE(d.is_object()) << type;
        EXPECT_TRUE(d.contains("description")) << type;
        EXPECT_TRUE(d.contains("alias")) << type;
        EXPECT_TRUE(d.contains("parameters")) << type;
        EXPECT_TRUE(d.contains("conditions")) << type;
        EXPECT_TRUE(d.contains("actions")) << type;
    }

    //three types, three DIFFERENT documents
    EXPECT_NE(j.at("InternalBool"), j.at("InputTimer"));
    EXPECT_NE(j.at("InternalBool"), j.at("Scenario"));
    EXPECT_NE(j.at("InputTimer"), j.at("Scenario"));

    //and each one really is what genDocJson() answers for that type: the
    //aggregation adds nothing and loses nothing.
    Params p;
    p.Add("type", "InputTimer");
    p.Add("id", "doc");
    IOBase::ScopedDocGen docScope;
    std::unique_ptr<IOBase> io(IOFactory::Instance().CreateIO("inputtimer", p));
    ASSERT_NE(nullptr, io.get());
    ASSERT_NE(nullptr, io->getDoc());
    EXPECT_EQ(docToJson(*io->getDoc()), j.at("InputTimer"));
}

//io_doc.json is pretty printed with an indent of FOUR spaces. jansson was
//asked for JSON_INDENT(4); the migration must ask nlohmann for the same.
//A dump that lost its indent, or that used 2, makes this red.
TEST_F(IODocGenFileTest, TheJsonFileIsPrettyPrintedWithAFourSpaceIndent)
{
    loadConfig();
    IOFactory::Instance().genDoc(docDir());

    const std::string jsonTxt = readWholeFile(docDir() + "/io_doc.json");

    //a top level key sits at exactly four spaces
    EXPECT_NE(std::string::npos, jsonTxt.find("\n    \""));
    //...and never at two, nor glued to the newline
    EXPECT_EQ(std::string::npos, jsonTxt.find("\n  \""));
    EXPECT_EQ(std::string::npos, jsonTxt.find("\n\""));
    //a compact dump would hold no newline at all
    EXPECT_NE(std::string::npos, jsonTxt.find('\n'));
}

//genDoc() creates the directory when it does not exist. Pinned because the
//before/after regeneration of the acceptance criterion relies on it.
TEST_F(IODocGenFileTest, GenDocCreatesItsOutputDirectory)
{
    loadConfig();
    const std::string deep = docDir() + "/a/b/c";
    ASSERT_FALSE(FileUtils::exists(deep));

    IOFactory::Instance().genDoc(deep);

    EXPECT_TRUE(FileUtils::exists(deep + "/io_doc.json"));
    EXPECT_TRUE(FileUtils::exists(deep + "/io_doc.md"));
}
