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
 * functions and must stay green across it. Next to nothing held them before:
 * core/WebIO_test.cpp reads the parameter NAMES of the seven Web types out of
 * genDocJson() and nothing else - no type, no value, no section - and the
 * only test that ever called genDoc() (IOIdIntegrity_test's
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
 *  - THE BYTE FORM of io_doc.json, in the single case of this file that looks
 *    at bytes rather than at a parsed document: ensure_ascii = true. No IO of
 *    the tree carries a non-ASCII character in its documentation, so that
 *    assertion is dead on the real artefact and needs a SYNTHETIC non-ASCII
 *    type name to bite - see TheJsonFileStaysPureAsciiWhenATypeNameIsNot.
 *    Without it the whole suite is blind to the flag, measured: flipping
 *    ensure_ascii to false leaves every other case green.
 ******************************************************************************/

#include <fstream>
#include <sstream>
#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "CalaosCoreFixture.h"
#include "IODoc.h"
#include "IOFactory.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

/* THE SEAM. Before E4.1k, IODoc::genDocJson() answered a json_t* and this
 * function transcoded it; since E4.1k it answers a Json directly. This
 * function body is the ONLY thing the migration commit touched in this file -
 * not one assertion moved. */
Json docToJson(IODoc &doc)
{
    return doc.genDocJson();
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

    for (const char *name: {"p_string", "p_bool", "p_int", "p_float"})
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
//about the ESCAPING FORM once dumped - the three forms pinned by the tripwire
//of tests/ParamsJson_test.cpp all parse back into this same string. What is
//pinned here is that no byte is mangled or dropped on the way; the byte form
//itself is pinned by TheJsonFileStaysPureAsciiWhenATypeNameIsNot at the end
//of this file.
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
 * IODoc - the device sections
 *
 * A device documents its family, its link roles and the JSON command fields
 * it accepts (spec §11). Every name and payload below is distinct, so that an
 * exchange of two fields or two sections is red.
 ******************************************************************************/

namespace
{

IODoc deviceDoc()
{
    IODoc doc;
    doc.friendlyNameSet("Dev Name");
    doc.deviceFamilySet("fam_x");
    doc.paramAdd("p_attr", "desc_attr", IODoc::TYPE_STRING, true);
    doc.paramAdd("p_child", "desc_child", IODoc::TYPE_FLOAT, false, "7");
    doc.paramInChild("p_child");

    doc.linkRoleAdd("r_req", "desc_r_req", true, true);
    doc.linkRoleAdd("r_opt", "desc_r_opt", false, false);

    doc.commandFieldEnum("f_enum", "desc_f_enum", { "v_one", "v_two" }, { "r_req" });
    doc.commandFieldEnum("f_list", "desc_f_list", {}, { "r_opt" }, "list_param");
    doc.commandFieldNumber("f_num", "desc_f_num", -5.5, 40, 0.25, "u_unit", { "r_a", "r_b" });
    return doc;
}

//A second impl, delegating to the real one for its document.
IOBase *proxyInstance(Params &p)
{
    return IOFactory::Instance().CreateIO("device:proxy", p);
}

bool contains(const std::string &haystack, const std::string &needle)
{
    return haystack.find(needle) != std::string::npos;
}

} //namespace

TEST(IODocJson, APlainIoCarriesNoDeviceSection)
{
    IODoc doc = richDoc();
    Json j = docToJson(doc);

    EXPECT_EQ(5u, j.size());
    for (const char *key: { "family", "link_roles", "command_format", "command_fields" })
        EXPECT_FALSE(j.contains(key)) << key;

    const std::string md = doc.genDocMd("plain");
    for (const char *heading: { "## Command of", "## Link roles of", "Device of family" })
        EXPECT_FALSE(contains(md, heading)) << heading;
}

TEST(IODocJson, CommandFieldsAreListedInDeclarationOrderUnderTheJsonFormat)
{
    IODoc doc = deviceDoc();
    Json j = docToJson(doc);

    EXPECT_EQ("json", j.at("command_format").get<std::string>());
    ASSERT_EQ(3u, j.at("command_fields").size());
    EXPECT_EQ("f_enum", j.at("command_fields").at(0).at("name").get<std::string>());
    EXPECT_EQ("f_list", j.at("command_fields").at(1).at("name").get<std::string>());
    EXPECT_EQ("f_num", j.at("command_fields").at(2).at("name").get<std::string>());
}

TEST(IODocJson, AnEnumFieldListsItsValuesAndTheLinkItRequires)
{
    IODoc doc = deviceDoc();
    const Json f = docToJson(doc).at("command_fields").at(0);

    EXPECT_EQ(Json({ { "name", "f_enum" },
                     { "type", "enum" },
                     { "description", "desc_f_enum" },
                     { "values", Json::array({ "v_one", "v_two" }) },
                     { "requires_links", Json::array({ "r_req" }) } }),
              f);
}

//The values of an instance dependent list live in a param: the document names
//it, and publishes no values it cannot know.
TEST(IODocJson, AnEnumListedByAParamNamesTheParamInsteadOfValues)
{
    IODoc doc = deviceDoc();
    const Json f = docToJson(doc).at("command_fields").at(1);

    EXPECT_EQ(Json({ { "name", "f_list" },
                     { "type", "enum" },
                     { "description", "desc_f_list" },
                     { "values_param", "list_param" },
                     { "requires_links", Json::array({ "r_opt" }) } }),
              f);
}

//Unlike the parameters, which go through Params and so are strings, the
//bounds are real JSON numbers: a client builds a spin box from them.
TEST(IODocJson, ANumberFieldCarriesRealNumbersAndItsUnit)
{
    IODoc doc = deviceDoc();
    const Json f = docToJson(doc).at("command_fields").at(2);

    EXPECT_EQ(Json({ { "name", "f_num" },
                     { "type", "number" },
                     { "description", "desc_f_num" },
                     { "min", -5.5 },
                     { "max", 40 },
                     { "step", 0.25 },
                     { "unit", "u_unit" },
                     { "requires_links", Json::array({ "r_a", "r_b" }) } }),
              f);
    for (const char *key: { "min", "max", "step" })
        EXPECT_TRUE(f.at(key).is_number()) << key;
}

TEST(IODocJson, ADeviceDocumentsItsFamilyAndItsLinkRoles)
{
    IODoc doc = deviceDoc();
    Json j = docToJson(doc);

    EXPECT_EQ("fam_x", j.at("family").get<std::string>());
    EXPECT_EQ(Json::array({ Json({ { "role", "r_req" },
                                   { "description", "desc_r_req" },
                                   { "required", true },
                                   { "writable", true } }),
                            Json({ { "role", "r_opt" },
                                   { "description", "desc_r_opt" },
                                   { "required", false },
                                   { "writable", false } }) }),
              j.at("link_roles"));
    EXPECT_TRUE(j.at("link_roles").at(0).at("required").is_boolean());
}

TEST(IODocJson, AParamInAChildElementIsMarkedAndTheOthersAreNot)
{
    IODoc doc = deviceDoc();
    Json j = docToJson(doc);

    EXPECT_EQ("calaos:param", paramNamed(j, "p_child").at("storage").get<std::string>());
    EXPECT_FALSE(paramNamed(j, "p_attr").contains("storage"));
    //The default and the type are still those of paramAdd().
    EXPECT_EQ("7", paramNamed(j, "p_child").at("default").get<std::string>());
}

//Marking an unknown name must not invent a parameter made of the mark alone.
//A value of an enum can need links of its own on top of those of the field.
TEST(IODocJson, AnEnumValueCanNeedLinksOfItsOwn)
{
    IODoc doc;
    doc.commandFieldEnum("f_val", "desc_f_val", { "v_a", "v_b", "v_c" }, { "r_all" }, std::string(),
                         { { "v_b", { "r_b" } }, { "v_c", { "r_b", "r_c" } } });

    const Json f = docToJson(doc).at("command_fields").at(0);
    EXPECT_EQ(Json({ { "v_b", Json::array({ "r_b" }) }, { "v_c", Json::array({ "r_b", "r_c" }) } }),
              f.at("value_requires_links"));
    EXPECT_EQ(Json::array({ "r_all" }), f.at("requires_links"));
    EXPECT_EQ(Json::array({ "v_a", "v_b", "v_c" }), f.at("values"));
    EXPECT_TRUE(contains(doc.genDocMd("x"), "v_a, v_b (needs r_b), v_c (needs r_b + r_c) | r_all | desc_f_val\n"));
}

TEST(IODocJson, WithoutValueRequirementsTheKeyIsAbsent)
{
    IODoc doc = deviceDoc();
    for (const Json &f: docToJson(doc).at("command_fields"))
        EXPECT_FALSE(f.contains("value_requires_links")) << f.dump();
}

//An optional role that a param value makes required (humidity with a floor emitter).
TEST(IODocJson, ARoleCanBeRequiredWhenAParamHasAValue)
{
    IODoc doc = deviceDoc();
    doc.linkRoleAdd("r_when", "desc_when", false, false, "p_when", "v_when");

    const Json roles = docToJson(doc).at("link_roles");
    ASSERT_EQ(3u, roles.size());
    EXPECT_EQ(Json({ { "role", "r_when" },
                     { "description", "desc_when" },
                     { "required", false },
                     { "writable", false },
                     { "required_when", { { "param", "p_when" }, { "value", "v_when" } } } }),
              roles.at(2));
    EXPECT_FALSE(roles.at(0).contains("required_when"));
    EXPECT_FALSE(roles.at(1).contains("required_when"));
    EXPECT_TRUE(contains(doc.genDocMd("x"), "r_when | when p_when=v_when | no | desc_when\n"));
}

//CLIM-4's rule (spec §6.3): humidity is required when emitter=floor and
//output_cool is linked. The link is an AND with the param.
TEST(IODocJson, ARequirementCanAlsoNeedALinkToExist)
{
    IODoc doc = deviceDoc();
    doc.linkRoleAdd("r_when", "desc_when", false, false, "p_when", "v_when", "r_req");
    doc.linkRoleAdd("r_plain", "desc_plain", false, false, "p_when", "v_when");

    const Json roles = docToJson(doc).at("link_roles");
    EXPECT_EQ(Json({ { "param", "p_when" }, { "value", "v_when" }, { "link", "r_req" } }),
              roles.at(2).at("required_when"));
    EXPECT_EQ(Json({ { "param", "p_when" }, { "value", "v_when" } }), roles.at(3).at("required_when"));

    const std::string md = doc.genDocMd("x");
    EXPECT_TRUE(contains(md, "r_when | when p_when=v_when and r_req | no | desc_when\n"));
    EXPECT_TRUE(contains(md, "r_plain | when p_when=v_when | no | desc_plain\n"));
}

//The mark does not depend on the order of the calls: a constructor that marks
//first, or adds again, must not lose it.
TEST(IODocJson, AParamMarkedBeforeItIsAddedIsMarked)
{
    IODoc doc;
    doc.paramInChild("p_early");
    doc.paramAdd("p_early", "desc_early", IODoc::TYPE_STRING, false);

    EXPECT_EQ("calaos:param", paramNamed(docToJson(doc), "p_early").at("storage").get<std::string>());
    EXPECT_TRUE(contains(doc.genDocMd("x"), "p_early (calaos:param) | string"));
}

TEST(IODocJson, AParamAddedAgainKeepsItsMark)
{
    //Whichever of the four calls adds it again.
    for (int variant = 0; variant < 4; variant++)
    {
        IODoc doc;
        doc.paramAdd("p_again", "first", IODoc::TYPE_STRING, false);
        doc.paramInChild("p_again");
        switch (variant)
        {
        case 0: doc.paramAdd("p_again", "second", IODoc::TYPE_BOOL, false); break;
        case 1: doc.paramAddInt("p_again", "second", 1, 9, false, 5); break;
        case 2: doc.paramAddFloat("p_again", "second", false, 1, 9, 5); break;
        default: doc.paramAddList("p_again", "second", false, Params({ { "k", "v" } }), "k"); break;
        }

        const Json p = paramNamed(docToJson(doc), "p_again");
        EXPECT_EQ("second", p.at("description").get<std::string>()) << variant;
        EXPECT_EQ("calaos:param", p.at("storage").get<std::string>()) << variant;
    }
}

TEST(IODocJson, MarkingAnUnknownParamAddsNothing)
{
    IODoc doc = deviceDoc();
    doc.paramInChild("p_unknown");

    Json j = docToJson(doc);
    EXPECT_EQ(2u, j.at("parameters").size());
    EXPECT_TRUE(paramNamed(j, "p_unknown").is_null());
}

TEST(IODocJson, TheMarkdownListsRolesFieldsAndChildParams)
{
    IODoc doc = deviceDoc();
    const std::string md = doc.genDocMd("impl_x");

    EXPECT_TRUE(contains(md, "Device of family `fam_x`, impl `impl_x`."));
    EXPECT_TRUE(contains(md, "## Link roles of Dev Name\n"));
    EXPECT_TRUE(contains(md, "Writable: the device writes this link."));
    EXPECT_TRUE(contains(md, "r_req | yes | yes | desc_r_req\n"));
    EXPECT_TRUE(contains(md, "r_opt | no | no | desc_r_opt\n"));
    EXPECT_TRUE(contains(md, "## Command of Dev Name\n"));
    EXPECT_TRUE(contains(md, "f_enum | enum | v_one, v_two | r_req | desc_f_enum\n"));
    EXPECT_TRUE(contains(md, "f_list | enum | values of the list_param param | r_opt | desc_f_list\n"));
    EXPECT_TRUE(contains(md, "f_num | number | -5.5 to 40, step 0.25 u_unit | r_a, r_b | desc_f_num\n"));
    EXPECT_TRUE(contains(md, "p_child (calaos:param) | float | no | desc_child\n"));
    EXPECT_TRUE(contains(md, "p_attr | string | yes | desc_attr\n"));
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

    for (const char *type: {"InternalBool", "InputTimer", "Scenario"})
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

//A device is not an IO type. The top level of io_doc.json is read as the list
//of the types an IO can have, so a "device:<impl>" key there would be
//offered as one; devices are published under "devices" instead.
TEST_F(IODocGenFileTest, NoDeviceIsPublishedAsAnIoType)
{
    loadConfig();
    IOFactory::Instance().genDoc(docDir());

    Json j = Json::parse(readWholeFile(docDir() + "/io_doc.json"));
    for (auto it = j.begin(); it != j.end(); ++it)
    {
        EXPECT_NE(0u, it.key().rfind("device:", 0)) << it.key();
        EXPECT_NE("proxy", it.key());
    }
    ASSERT_TRUE(j.contains("devices")) << "the proxy is linked into this binary";
    EXPECT_TRUE(j.at("devices").is_object());
    EXPECT_TRUE(j.at("devices").contains("proxy"));
    EXPECT_FALSE(j.at("devices").contains("device:proxy"));
    EXPECT_FALSE(j.at("devices").at("proxy").contains("devices"));
}

TEST_F(IODocGenFileTest, TheMarkdownDoesNotNameADeviceByItsRegistryKey)
{
    loadConfig();
    IOFactory::Instance().genDoc(docDir());

    const std::string md = readWholeFile(docDir() + "/io_doc.md");
    EXPECT_FALSE(contains(md, "device:proxy"));
    EXPECT_TRUE(contains(md, "# Climate proxy\n"));
    EXPECT_TRUE(contains(md, "Device of family `climate`, impl `proxy`."));
}

//The "devices" entry is what the device itself documents, and the markdown of
//the devices comes after those of the IO types.
TEST_F(IODocGenFileTest, TheProxyIsDocumentedUnderDevicesByItsImpl)
{
    loadConfig();
    IOFactory::Instance().genDoc(docDir());

    Json j = Json::parse(readWholeFile(docDir() + "/io_doc.json"));
    const Json &proxy = j.at("devices").at("proxy");

    Params p;
    p.Add("type", "device:proxy");
    p.Add("id", "doc");
    IOBase::ScopedDocGen docScope;
    std::unique_ptr<IOBase> io(IOFactory::Instance().CreateIO("device:proxy", p));
    ASSERT_NE(nullptr, io.get());
    EXPECT_EQ(docToJson(*io->getDoc()), proxy);

    Params tp;
    tp.Add("type", "InputTimer");
    tp.Add("id", "doc");
    std::unique_ptr<IOBase> timer(IOFactory::Instance().CreateIO("inputtimer", tp));
    ASSERT_NE(nullptr, timer.get());

    const std::string md = readWholeFile(docDir() + "/io_doc.md");
    const size_t at = md.find(io->getDoc()->genDocMd("proxy"));
    const size_t timerAt = md.find(timer->getDoc()->genDocMd("InputTimer"));
    ASSERT_NE(std::string::npos, at);
    ASSERT_NE(std::string::npos, timerAt);
    EXPECT_LT(timerAt, at);
}

//The impl is published as it was registered, without the registry prefix.
TEST_F(IODocGenFileTest, AnImplKeepsItsSpellingUnderDevices)
{
    loadConfig();
    IOFactory::Instance().RegisterClass("device:SynthImpl", &proxyInstance);

    IOFactory::Instance().genDoc(docDir());
    Json j = Json::parse(readWholeFile(docDir() + "/io_doc.json"));

    EXPECT_TRUE(j.at("devices").contains("SynthImpl"));
    EXPECT_FALSE(j.contains("SynthImpl"));
    EXPECT_FALSE(j.contains("device:SynthImpl"));
    EXPECT_TRUE(contains(readWholeFile(docDir() + "/io_doc.md"), "impl `SynthImpl`."));
}

//What the installer's rule form is built from: the fields, in order, and the
//roles they need.
TEST_F(IODocGenFileTest, TheProxyDocumentsItsCommandAndItsLinks)
{
    loadConfig();
    IOFactory::Instance().genDoc(docDir());

    Json j = Json::parse(readWholeFile(docDir() + "/io_doc.json"));
    const Json &proxy = j.at("devices").at("proxy");

    EXPECT_EQ("climate", proxy.at("family").get<std::string>());
    EXPECT_EQ("json", proxy.at("command_format").get<std::string>());

    std::vector<std::string> names;
    for (const Json &f: proxy.at("command_fields"))
        names.push_back(f.at("name").get<std::string>());
    EXPECT_EQ((std::vector<std::string>{ "mode", "target_temp", "target_temp_low",
                                         "target_temp_high", "fan_mode", "swing_mode" }),
              names);

    const Json &fields = proxy.at("command_fields");
    EXPECT_EQ(Json::array({ "off", "heat", "cool", "auto", "dry", "fan_only" }),
              fields.at(0).at("values"));
    EXPECT_EQ(Json::array({ "mode" }), fields.at(0).at("requires_links"));
    EXPECT_EQ(Json::array({ "target_temp" }), fields.at(1).at("requires_links"));
    EXPECT_EQ(Json::array({ "target_temp_low", "target_temp_high" }), fields.at(2).at("requires_links"));
    EXPECT_EQ(Json::array({ "target_temp_low", "target_temp_high" }), fields.at(3).at("requires_links"));
    EXPECT_EQ("fan_modes", fields.at(4).at("values_param").get<std::string>());
    EXPECT_EQ("swing_modes", fields.at(5).at("values_param").get<std::string>());
    for (int i: { 1, 2, 3 })
    {
        EXPECT_EQ(7, fields.at(i).at("min").get<double>());
        EXPECT_EQ(35, fields.at(i).at("max").get<double>());
        EXPECT_EQ(0.5, fields.at(i).at("step").get<double>());
        EXPECT_EQ(std::string("\xc2\xb0") + "C", fields.at(i).at("unit").get<std::string>());
    }

    //Only the mode is required: the others are capabilities that follow the links.
    std::map<std::string, std::pair<bool, bool>> roles;
    for (const Json &r: proxy.at("link_roles"))
        roles[r.at("role").get<std::string>()] = { r.at("required").get<bool>(),
                                                   r.at("writable").get<bool>() };
    const std::map<std::string, std::pair<bool, bool>> expected = {
        { "mode", { true, true } },
        { "current_temp", { false, false } },
        { "target_temp", { false, true } },
        { "target_temp_low", { false, true } },
        { "target_temp_high", { false, true } },
        { "action", { false, false } },
        { "fan_mode", { false, true } },
        { "swing_mode", { false, true } },
        { "remote_temp", { false, true } },
        { "remote_temp_source", { false, false } },
    };
    EXPECT_EQ(expected, roles);
}

TEST_F(IODocGenFileTest, TheProxyDocumentsItsParams)
{
    loadConfig();
    IOFactory::Instance().genDoc(docDir());

    Json j = Json::parse(readWholeFile(docDir() + "/io_doc.json"));
    const Json &proxy = j.at("devices").at("proxy");

    const Json keepalive = paramNamed(proxy, "remote_temp_keepalive");
    EXPECT_EQ("120", keepalive.at("default").get<std::string>());
    EXPECT_EQ("float", keepalive.at("type").get<std::string>());
    EXPECT_EQ("false", keepalive.at("mandatory").get<std::string>());
    EXPECT_EQ("calaos:param", keepalive.at("storage").get<std::string>());

    const Json hold = paramNamed(proxy, "readback_hold");
    EXPECT_EQ("10", hold.at("default").get<std::string>());
    EXPECT_EQ("calaos:param", hold.at("storage").get<std::string>());

    const Json payload = paramNamed(proxy, "auto_payload");
    EXPECT_EQ("list", payload.at("type").get<std::string>());
    EXPECT_EQ("false", payload.at("mandatory").get<std::string>());
    EXPECT_EQ(Json({ { "auto", "auto" }, { "heat_cool", "heat_cool" } }), payload.at("list_value"));
    EXPECT_EQ("calaos:param", payload.at("storage").get<std::string>());

    for (const char *name: { "modes", "fan_modes", "swing_modes", "min_range" })
        EXPECT_EQ("calaos:param", paramNamed(proxy, name).at("storage").get<std::string>()) << name;

    //The attributes of the element are not children.
    for (const char *name: { "id", "name", "impl" })
        EXPECT_FALSE(paramNamed(proxy, name).contains("storage")) << name;
    EXPECT_EQ("true", paramNamed(proxy, "impl").at("mandatory").get<std::string>());
}

TEST_F(IODocGenFileTest, TheThermostatIsDocumentedUnderDevicesLikeTheProxy)
{
    loadConfig();
    IOFactory::Instance().genDoc(docDir());

    Json j = Json::parse(readWholeFile(docDir() + "/io_doc.json"));
    ASSERT_TRUE(j.at("devices").contains("thermostat"));
    EXPECT_FALSE(j.contains("thermostat"));
    const Json &th = j.at("devices").at("thermostat");

    EXPECT_EQ("climate", th.at("family").get<std::string>());
    EXPECT_EQ("json", th.at("command_format").get<std::string>());
    EXPECT_NE(j.at("devices").at("proxy"), th);

    //The device stores its mode: no link to require, four modes, and the
    //outputs that unlock them said in the description.
    const Json mode = th.at("command_fields").at(0);
    EXPECT_EQ("mode", mode.at("name").get<std::string>());
    EXPECT_EQ(Json::array({ "off", "heat", "cool", "auto" }), mode.at("values"));
    EXPECT_EQ(Json::array(), mode.at("requires_links"));
    //Each mode needs its outputs, whatever the instance: the form greys the others.
    EXPECT_EQ(Json({ { "heat", Json::array({ "output_heat" }) },
                     { "cool", Json::array({ "output_cool" }) },
                     { "auto", Json::array({ "output_heat", "output_cool" }) } }),
              mode.at("value_requires_links"));
    //fan_mode and swing_mode are the proxy's: the thermostat has no such link.
    std::vector<std::string> names;
    for (const Json &f: th.at("command_fields"))
        names.push_back(f.at("name").get<std::string>());
    EXPECT_EQ((std::vector<std::string>{ "mode", "target_temp", "target_temp_low", "target_temp_high" }),
              names);
    const std::string modeText = mode.at("description").get<std::string>();
    EXPECT_TRUE(contains(modeText, "capabilities"));
    EXPECT_TRUE(contains(modeText, "cool needs output_cool"));
    EXPECT_TRUE(contains(modeText, "auto needs both"));

    std::map<std::string, std::pair<bool, bool>> roles;
    std::map<std::string, std::string> texts;
    for (const Json &r: th.at("link_roles"))
    {
        const std::string role = r.at("role").get<std::string>();
        roles[role] = { r.at("required").get<bool>(), r.at("writable").get<bool>() };
        texts[role] = r.at("description").get<std::string>();
    }
    const std::map<std::string, std::pair<bool, bool>> expected = {
        { "current_temp", { true, false } },
        { "target_temp", { true, true } },
        { "target_temp_low", { false, true } },
        { "target_temp_high", { false, true } },
        { "action", { false, true } },
        { "output_heat", { true, true } },
        { "output_cool", { false, true } },
        { "humidity", { false, false } },
    };
    EXPECT_EQ(expected, roles);
    //Required for a cooling floor only.
    for (const Json &r: th.at("link_roles"))
    {
        if (r.at("role") == "humidity")
            EXPECT_EQ(Json({ { "param", "emitter" }, { "value", "floor" }, { "link", "output_cool" } }),
                      r.at("required_when"));
    }
    EXPECT_TRUE(contains(texts["current_temp"], "sensor_timeout"));
    //Decision 17: one output, one thermostat.
    for (const char *role: { "output_heat", "output_cool" })
        EXPECT_TRUE(contains(texts[role], "belongs to a single thermostat")) << role;
    EXPECT_FALSE(contains(texts["target_temp"], "single thermostat"));

    const std::map<std::string, std::string> defaults = {
        { "hysteresis", "0.5" },
        { "min_on_time", "0" },
        { "min_off_time", "0" },
        { "put_back_delay", "1" },
        { "put_back_delay_max", "60" },
        { "sensor_timeout", "9000" },
        { "humidity_max", "65" },
    };
    for (const auto &d: defaults)
    {
        const Json p = paramNamed(th, d.first);
        ASSERT_TRUE(p.is_object()) << d.first;
        EXPECT_EQ(d.second, p.at("default").get<std::string>()) << d.first;
        EXPECT_EQ("float", p.at("type").get<std::string>()) << d.first;
        EXPECT_EQ("calaos:param", p.at("storage").get<std::string>()) << d.first;
    }
    EXPECT_TRUE(contains(paramNamed(th, "put_back_delay").at("description").get<std::string>(), "0.001"));
    EXPECT_EQ("calaos:param", paramNamed(th, "min_range").at("storage").get<std::string>());

    //Only what the thermostat reads: modes, fan_modes and swing_modes are the
    //proxy's, a thermostat ignores them.
    std::set<std::string> childParams;
    for (const Json &p: th.at("parameters"))
    {
        if (p.contains("storage"))
            childParams.insert(p.at("name").get<std::string>());
    }
    EXPECT_EQ((std::set<std::string>{ "min_range", "hysteresis", "min_on_time", "min_off_time",
                                      "put_back_delay", "put_back_delay_max", "sensor_timeout",
                                      "emitter", "humidity_max" }),
              childParams);

    //The outputs switch on past the hysteresis and off as the setpoint is crossed.
    const std::string hysteresis = paramNamed(th, "hysteresis").at("description").get<std::string>();
    EXPECT_TRUE(contains(hysteresis, "switching on"));
    EXPECT_TRUE(contains(hysteresis, "as soon as the setpoint is crossed"));
    //It also sets the window of the log of writes from outside to an output.
    EXPECT_TRUE(contains(paramNamed(th, "put_back_delay_max").at("description").get<std::string>(),
                         "one line per source and output"));
    //A stop that was asked for (mode off) or that safety needs does not wait.
    EXPECT_TRUE(contains(paramNamed(th, "min_on_time").at("description").get<std::string>(),
                         "except when the mode is off or a safety cut stops it"));
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

//⭐ THE ONE BYTE-LEVEL ORACLE OF THIS TICKET.
//
//Everything above compares PARSED documents. Nothing above would notice
//ensure_ascii being dropped from the dump, and that is not a supposition:
//flipping the flag to false leaves the rest of this file GREEN. Yet
//ensure_ascii = true is the invariant the seventeen sub-tickets of E4.1
//apply, so without this case it is applied with no oracle at all.
//
//The difficulty is that NO IO of the tree carries a non-ASCII character in
//its documentation - io_doc.json is already pure ASCII whatever the flag
//says, which makes a byte assertion on the real artefact a dead oracle. The
//way in is that IOFactory::RegisterClass() is PUBLIC and that the factory
//publishes the ORIGINAL spelling of a type name as the top level key: a
//synthetic type whose NAME is non-ASCII, delegating to a real IO for its
//document, puts non-ASCII into the file and nowhere else.
//
//NOTE on the singleton: the IOFactory registry is process wide and a
//registration is permanent. It is purely additive, first registration wins
//(so re-entering this case under --gtest_shuffle is idempotent), and every
//other case of this file asserts only on keys it names itself.
TEST_F(IODocGenFileTest, TheJsonFileStaysPureAsciiWhenATypeNameIsNot)
{
    loadConfig();

    //"AccéntedType": the é is U+00E9, two UTF-8 bytes 0xc3 0xa9
    const std::string accented = "Acc\xc3\xa9ntedType";
    IOFactory::Instance().RegisterClass(
        accented,
        [](Params &p) -> IOBase *
        {
            return IOFactory::Instance().CreateIO("inputtimer", p);
        });

    IOFactory::Instance().genDoc(docDir());
    const std::string jsonTxt = readWholeFile(docDir() + "/io_doc.json");
    ASSERT_FALSE(jsonTxt.empty());

    //the synthetic type really is in the document, so the assertions below
    //are not vacuous
    Json j = Json::parse(jsonTxt);
    ASSERT_TRUE(j.contains(accented))
        << "the synthetic non-ASCII type must be documented, otherwise the "
           "byte assertions below prove nothing";
    EXPECT_TRUE(j.at(accented).contains("parameters"));

    //...and not one byte of the FILE that carries it is >= 0x80: ensure_ascii
    //escaped it. THIS is the assertion that dies the moment the flag goes.
    for (size_t i = 0; i < jsonTxt.size(); i++)
    {
        ASSERT_LT((unsigned int)(unsigned char)jsonTxt[i], 0x80u)
            << "non-ASCII byte at offset " << i
            << ": io_doc.json must stay pure ASCII (ensure_ascii = true)";
    }

    //escaped the nlohmann way: LOWER case hex. jansson with JSON_ENSURE_ASCII
    //writes é, upper case - the two forms are pinned side by side by the
    //tripwire of tests/ParamsJson_test.cpp.
    EXPECT_NE(std::string::npos, jsonTxt.find("Acc\\u00e9ntedType"));
    EXPECT_EQ(std::string::npos, jsonTxt.find("Acc\\u00E9ntedType"));
}
