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
 **  along with Calaos; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/

/******************************************************************************
 * The immutability guard of IOBase keys on ONE name, and "id" is not the only
 * param the installation is rebuilt from.
 *
 * "type" is the other one: it is the only param the configuration loader reads
 * BEFORE any object exists (IOFactory::CreateIO(node) resolves the class with
 * it, Room::LoadFromXml() calls nothing else). Delete it, or set it to a type
 * no driver registers, and the factory answers nullptr - so the <calaos:*>
 * element is read, discarded, and the equipment is simply MISSING from the
 * house at the next start. Not unaddressable as in the "id" case: absent.
 *
 * WHAT THE CLIENT GETS ON THIS TREE, measured with a real disk round trip:
 *   - {"success":"true"} for a command that costs an equipment,
 *   - io.xml written back without the attribute (del_param does not save by
 *     itself, but seven SaveConfigIO() calls of JsonApi.cpp do, and they write
 *     the params as they stand in memory at that moment),
 *   - and after the reload the IO is gone from ListeRoom, from its room, and
 *     from get_io. The rules that named it are dropped with it.
 *
 * THE SIX WAYS A TEST OF THIS COULD LIE, and what is done about each:
 *  1. a direct call to buildJsonDelParam() would not measure the API - every
 *     case goes over a real WebSocket session or a real HTTP request, and both
 *     transports are exercised because only the WS one carries a scope layer;
 *  2. spelling: the guard must not key on how "type" is written, so the cases
 *     send the exact key buildJsonIO() publishes, and the value cases send a
 *     type spelled the way the registry stores it;
 *  3. a length bound: nothing here is bounded by a length;
 *  4. a false fixture - the one that matters here. A suite that asserted on
 *     the answer document only would pass on a guard that says no and saves a
 *     stripped io.xml anyway. Every loss case therefore asserts AFTER
 *     saveConfig() + reloadFromDisk(), i.e. on what came back from the disk;
 *     and TheSameCycleStillCarriesAnOrdinaryDeletion plays the identical cycle
 *     for a param that must disappear, so "the IO is still there" cannot be
 *     the answer of a cycle that reloads nothing;
 *  5. presence instead of position: the answers are compared as whole
 *     documents (EXPECT_JSON_EQ), never by "does it contain the word error";
 *  6. a haystack that excludes what is looked for: the control cases delete a
 *     param that really exists and one that never did, and one case CHANGES
 *     the type to another registered one - a guard that refused everything
 *     named "type" would redden that one.
 *
 * AND THE SEVENTH, the permutation that changes no value: the refusal has to
 * be the one the neighbouring guard already gives. TheRefusalIsTheOneTheIdGuard
 * AlreadyGivesForTheSameCause compares the two answers to each other, so a new
 * spelling on either side reddens it although every literal here would still
 * be satisfied.
 ******************************************************************************/

#include "JsonApiCharacterization.h"

#include "ListeRoom.h"

using namespace Calaos;
using namespace CalaosTest;

class StructuralParamGuardTest: public JsonApiCharacterizationTest
{
protected:
    void SetUp() override
    {
        JsonApiCharacterizationTest::SetUp();
        loadReferenceHouse();
    }

    Json wsDelParam(const std::string &id, const std::string &param)
    {
        WsTestSession ws;
        ws.send(Json{{ "msg", "del_param" },
                     { "msg_id", "1" },
                     { "data", {{ "id", id }, { "param", param }} }});
        if (ws.count() != 1u)
            return Json{{ "error", "<no answer>" }};
        return ws.lastData();
    }

    Json wsSetParam(const std::string &id, const std::string &param,
                    const std::string &value)
    {
        WsTestSession ws;
        ws.send(Json{{ "msg", "set_param" },
                     { "msg_id", "1" },
                     { "data", {{ "id", id }, { "param", param },
                                { "value", value }} }});
        if (ws.count() != 1u)
            return Json{{ "error", "<no answer>" }};
        return ws.lastData();
    }

    Json wsGetIo(const std::string &id)
    {
        WsTestSession ws;
        ws.send(Json{{ "msg", "get_io" },
                     { "msg_id", "1" },
                     { "data", {{ "items", Json::array({ id }) }} }});
        if (ws.count() != 1u)
            return Json::object();
        //An IO the server no longer knows is published as a null, and value()
        //on a null throws: an empty object turns that into a plain assertion
        //failure rather than an exception nobody can read.
        const Json payload = ws.lastData();
        const Json::const_iterator it = payload.find(id);
        if (it == payload.cend() || !it->is_object())
            return Json::object();
        return *it;
    }

    /* The whole point of the ticket: what the installation looks like at the
     * NEXT START. Nothing else in this file is allowed to answer for it. */
    void restartFromDisk()
    {
        saveConfig();
        reloadFromDisk();
    }

    static bool ioIsInTheInstallation(const std::string &id)
    {
        return ListeRoom::Instance().get_io(id) != nullptr;
    }
};

/*******************************************************************************
 * The loss, played to the disk and back
 ******************************************************************************/

TEST_F(StructuralParamGuardTest, ADeletedTypeMakesTheIoVanishFromTheInstallation)
{
    //RED BEFORE THE FIX, and the red is the equipment being absent after the
    //reload - not a code of return, not a message.
    ASSERT_TRUE(ioIsInTheInstallation(HOUSE_INT));

    wsDelParam(HOUSE_INT, "type");
    restartFromDisk();

    EXPECT_TRUE(ioIsInTheInstallation(HOUSE_INT))
            << "the equipment is missing from the house after a restart: the "
               "loader resolves the driver class with \"type\" and has nothing "
               "to build the IO with";
    EXPECT_EQ(HOUSE_INT, wsGetIo(HOUSE_INT).value("id", std::string()))
            << "and no client can see it any more either";
}

TEST_F(StructuralParamGuardTest, TheNodeWrittenBackToIoXmlStillCarriesItsType)
{
    //RED BEFORE THE FIX. The oracle here is the FILE, one step earlier than
    //the case above: a guard that refused the client and let SaveConfigIO()
    //write a stripped node would still lose the equipment.
    //The needle carries its leading space ON PURPOSE: "type=\"" alone is a
    //substring of gui_type= and io_type=, which every node here also carries,
    //and this case was green on the unfixed tree until that was fixed.
    wsDelParam(HOUSE_INT, "type");
    saveConfig();

    const std::string xml = ioXmlOnDisk();
    const std::string idAttr = std::string("id=\"") + HOUSE_INT + "\"";
    const std::string::size_type io = xml.find(idAttr);
    ASSERT_NE(std::string::npos, io) << "the IO left io.xml altogether";

    const std::string::size_type nodeStart = xml.rfind('<', io);
    const std::string::size_type nodeEnd = xml.find('>', io);
    ASSERT_NE(std::string::npos, nodeStart);
    ASSERT_NE(std::string::npos, nodeEnd);
    ASSERT_LT(nodeStart, nodeEnd);

    const std::string node = xml.substr(nodeStart, nodeEnd - nodeStart);
    EXPECT_NE(std::string::npos, node.find(" type=\""))
            << "the node written back carries no type, so the next load will "
               "discard it: " << node;
}

TEST_F(StructuralParamGuardTest, TheSameCycleStillCarriesAnOrdinaryDeletion)
{
    //GREEN BEFORE AND AFTER, and it is what makes the two cases above worth
    //reading: the same save+reload cycle, on a param that MUST go away. If
    //this one stopped seeing the deletion, "the IO is still there" would be
    //the answer of a cycle that reloads nothing.
    ASSERT_JSON_EQ(std::string(R"({"success":"true"})"),
                   wsSetParam(HOUSE_INT, "unit", "kWh"));
    restartFromDisk();
    ASSERT_EQ("kWh", wsGetIo(HOUSE_INT).value("unit", std::string()));

    EXPECT_JSON_EQ(std::string(R"({"success":"true"})"),
                   wsDelParam(HOUSE_INT, "unit"));
    restartFromDisk();

    EXPECT_TRUE(ioIsInTheInstallation(HOUSE_INT));
    EXPECT_FALSE(wsGetIo(HOUSE_INT).contains("unit"))
            << "the disk cycle is not carrying deletions at all";
}

/*******************************************************************************
 * The two transports
 ******************************************************************************/

TEST_F(StructuralParamGuardTest, TheWsTransportRefusesToDeleteTheTypeOfAnIo)
{
    //RED BEFORE THE FIX: the server answers {"success":"true"}.
    EXPECT_JSON_EQ(std::string(R"({"error":"param refused"})"),
                   wsDelParam(HOUSE_INT, "type"));
}

TEST_F(StructuralParamGuardTest, TheHttpTransportRefusesToDeleteTheTypeOfAnIo)
{
    //RED BEFORE THE FIX. HTTP carries no scope layer at all, so a guard put in
    //a handler rather than in the model would leave this half open.
    HttpTestRequest req;
    req.send(authenticated(Json{{ "action", "del_param" },
                                { "id", HOUSE_INT },
                                { "param", "type" }}));
    ASSERT_EQ(1u, req.count());
    EXPECT_JSON_EQ(std::string(R"({"error":"param refused"})"), req.body());
}

TEST_F(StructuralParamGuardTest, TheGuardHoldsForEveryVariableType)
{
    //RED BEFORE THE FIX, three times. The guard lives in IOBase and asks the
    //factory, so nothing about it can depend on the IO class - but a fixture
    //covering one type could not tell that from an accident of the house.
    for (const char *id: { HOUSE_BOOL_IN, HOUSE_INT, HOUSE_STRING })
    {
        EXPECT_JSON_EQ(std::string(R"({"error":"param refused"})"),
                       wsDelParam(id, "type")) << "io: " << id;
    }

    restartFromDisk();

    for (const char *id: { HOUSE_BOOL_IN, HOUSE_INT, HOUSE_STRING })
        EXPECT_TRUE(ioIsInTheInstallation(id)) << "io: " << id;
}

/*******************************************************************************
 * The value, not only the name - what separates a criterion from a name list
 ******************************************************************************/

TEST_F(StructuralParamGuardTest, ATypeNoDriverRegistersIsRefusedLikeAMissingOne)
{
    //RED BEFORE THE FIX. Overwriting the type costs the same equipment as
    //deleting it: IOFactory answers nullptr on a type it has no entry for,
    //and the only difference is one warning line in the log.
    EXPECT_JSON_EQ(std::string(R"({"error":"param refused"})"),
                   wsSetParam(HOUSE_INT, "type", "t3124_no_such_driver"));

    restartFromDisk();
    EXPECT_TRUE(ioIsInTheInstallation(HOUSE_INT));
}

TEST_F(StructuralParamGuardTest, ATypeThatIsStillBuildableIsAccepted)
{
    //GREEN BEFORE AND AFTER, and it is the case a guard written as a list of
    //names cannot keep: the loader can still rebuild this IO, so the write is
    //not a loss and nothing has to refuse it. Migrating an IO from one
    //registered type to another stays a supported edit.
    ASSERT_JSON_EQ(std::string(R"({"success":"true"})"),
                   wsSetParam(HOUSE_INT, "type", "InternalString"));

    restartFromDisk();

    EXPECT_TRUE(ioIsInTheInstallation(HOUSE_INT))
            << "the equipment survived a type change the loader can honour";
    EXPECT_EQ("InternalString", wsGetIo(HOUSE_INT).value("type", std::string()));
}

/*******************************************************************************
 * The shape of the refusal, compared to its neighbour and not to a literal
 ******************************************************************************/

TEST_F(StructuralParamGuardTest, TheRefusalIsTheOneTheIdGuardAlreadyGivesForTheSameCause)
{
    //THE CASE THAT SURVIVES A RENAMED PAYLOAD. "id" is refused for the very
    //same reason - the model cannot let a write cost the IO - and the two must
    //not grow two vocabularies. Comparing the documents to each other keeps
    //that true through any rewording, where a copied literal would not.
    //Two crossings, because one alone would not do it: the first spans the two
    //BUILDERS, so rewording only one of them reddens here; the second spans
    //the two RULES inside del_param, so a refusal invented for this guard
    //alone reddens here too.
    EXPECT_JSON_EQ(wsSetParam(HOUSE_INT, "type", "t3124_no_such_driver"),
                   wsDelParam(HOUSE_INT, "type"));
    EXPECT_JSON_EQ(wsDelParam(HOUSE_INT, "id"), wsDelParam(HOUSE_INT, "type"));

    EXPECT_FALSE(wsDelParam(HOUSE_INT, "type").contains("success"))
            << "a refusal that also reports success is worse than either";
}

TEST_F(StructuralParamGuardTest, AMissingIoIsStillTheOtherRefusalAndNotThisOne)
{
    //GREEN BEFORE AND AFTER. "not found" and "refused" are different causes,
    //and a client retrying on the wrong one looks in the wrong place.
    EXPECT_JSON_EQ(std::string(R"({"error":"wrong io/param"})"),
                   wsDelParam("t3124_no_such_io", "type"));
    EXPECT_JSON_EQ(std::string(R"({"error":"wrong io/param"})"),
                   wsSetParam("t3124_no_such_io", "type", "InternalInt"));
}

/*******************************************************************************
 * What the guard must not cost
 ******************************************************************************/

TEST_F(StructuralParamGuardTest, OrdinaryParamsAreStillWritableAndDeletable)
{
    //GREEN BEFORE AND AFTER. A guard that asked the factory the wrong question
    //would refuse every write, and this is where that shows.
    EXPECT_JSON_EQ(std::string(R"({"success":"true"})"),
                   wsSetParam(HOUSE_INT, "t3124_plain", "42"));
    EXPECT_JSON_EQ(std::string(R"({"success":"true"})"),
                   wsDelParam(HOUSE_INT, "t3124_plain"));
    EXPECT_JSON_EQ(std::string(R"({"success":"true"})"),
                   wsDelParam(HOUSE_INT, "t3124_never_set"));
}

TEST_F(StructuralParamGuardTest, AParamWhoseNameMerelyContainsTypeIsStillDeletable)
{
    //GREEN BEFORE AND AFTER. gui_type and io_type are rewritten by the IO
    //constructors at every load, so they are not structural and a guard
    //matching by prefix or substring would take them away for nothing.
    ASSERT_JSON_EQ(std::string(R"({"success":"true"})"),
                   wsSetParam(HOUSE_INT, "t3124_room_type", "kitchen"));
    EXPECT_JSON_EQ(std::string(R"({"success":"true"})"),
                   wsDelParam(HOUSE_INT, "t3124_room_type"));
    EXPECT_JSON_EQ(std::string(R"({"success":"true"})"),
                   wsDelParam(HOUSE_INT, "gui_type"));
}
