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
 * T3.21 - the immutability guard on "id" is never reached from the API.
 *
 * IOBase::del_param() refuses to delete "id" because ListeRoom's io_table is
 * keyed on it; the header of IOBase says so and its sibling set_param()
 * enforces the same rule. buildJsonDelParam() does not call it. It calls
 * o->get_params().Delete(name) - the mutable Params reference IOBase hands out
 * with a warning in the very comment that documents the guard.
 *
 * WHAT THE CLIENT GETS TODAY, measured on this tree:
 *   - {"success":"true"} for a command the model refuses,
 *   - an IO whose "id" param is gone while io_table still holds the old key:
 *     get_param("id") answers "", get_io() publishes an empty id, and the
 *     EventIOPropertyDelete raised two lines below names the IO by that same
 *     empty string. The IO is still there and can no longer be addressed.
 *   - and, on destruction, an unregister keyed on "" - the stale hash entry
 *     the guard exists to prevent. NOT exercised here: it is a dangling
 *     pointer, and a suite that reads freed memory reports the weather rather
 *     than the defect.
 *
 * THE SIX WAYS A TEST OF THIS COULD LIE, and what is done about each:
 *  1. a direct call to buildJsonDelParam() would not measure the API at all -
 *     every case below goes over a real WebSocket session or a real HTTP
 *     request, and both transports are exercised because only the WS one
 *     carries a scope layer;
 *  2. spelling the parameter name: the guard keys on the literal "id", so a
 *     case sending "Id" or "ID" would be green in both worlds. Cases send the
 *     exact key buildJsonIO() publishes;
 *  3. a length bound: nothing here is bounded by a length;
 *  4. a false fixture: the reference house is the harness's own, three IO
 *     types are covered rather than the one that happens to be first, and
 *     TheGuardHoldsForEveryVariableType is what makes an IO-type-specific
 *     accident visible;
 *  5. presence instead of position: the answer is compared as a whole
 *     document (EXPECT_JSON_EQ), never by "does it contain the word error";
 *  6. a haystack that excludes what is looked for: the control cases delete a
 *     param that really exists and one that never did, so a guard that
 *     refused everything would redden them.
 *
 * ⭐ AND THE SEVENTH, the permutation that changes no value: the refusal must
 * be the one the neighbours already give. TheRefusalIsTheOneSetParamAlready
 * GivesForTheSameKey compares the two answers to each other, so writing a
 * brand new spelling on the del_param side reddens it even though every
 * literal in this file would still be satisfied.
 ******************************************************************************/

#include "JsonApiCharacterization.h"

#include "ListeRoom.h"

using namespace Calaos;
using namespace CalaosTest;

class DelParamIdGuardTest: public JsonApiCharacterizationTest
{
protected:
    void SetUp() override
    {
        JsonApiCharacterizationTest::SetUp();
        loadReferenceHouse();
    }

    //One authenticated del_param over the real WS transport, answer document.
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

    //What the API publishes for one IO, so the id is read back the way a
    //client reads it and not through a C++ accessor.
    Json wsGetIo(const std::string &id)
    {
        WsTestSession ws;
        ws.send(Json{{ "msg", "get_io" },
                     { "msg_id", "1" },
                     { "data", {{ "items", Json::array({ id }) }} }});
        if (ws.count() != 1u)
            return Json::object();
        return ws.lastData()[id];
    }
};

/*******************************************************************************
 * The defect, by the only two paths a client has
 ******************************************************************************/

TEST_F(DelParamIdGuardTest, TheWsTransportRefusesToDeleteTheIdOfAnIo)
{
    //RED BEFORE THE FIX: the server answers {"success":"true"}.
    EXPECT_JSON_EQ(std::string(R"({"error":"param refused"})"),
                   wsDelParam(HOUSE_INT, "id"))
            << "the id is the key of the io_table; deleting it through the "
               "generic parameter command must be refused, not reported done";
}

TEST_F(DelParamIdGuardTest, TheHttpTransportRefusesToDeleteTheIdOfAnIo)
{
    //RED BEFORE THE FIX. The HTTP transport carries no scope layer at all, so
    //a guard placed in the WS handler instead of the model would leave this
    //one open - which is the shape of the defect being closed.
    HttpTestRequest req;
    req.send(authenticated(Json{{ "action", "del_param" },
                                { "id", HOUSE_INT },
                                { "param", "id" }}));
    ASSERT_EQ(1u, req.count());
    EXPECT_JSON_EQ(std::string(R"({"error":"param refused"})"), req.body());
}

TEST_F(DelParamIdGuardTest, ARefusedDeleteLeavesTheIoAddressableUnderItsId)
{
    //RED BEFORE THE FIX: the param is really gone, so the IO the API publishes
    //carries an empty id while io_table still answers to the old one. This is
    //the consequence, and it is read back through get_io - what a client sees.
    ASSERT_EQ(HOUSE_INT, wsGetIo(HOUSE_INT).value("id", std::string()));

    wsDelParam(HOUSE_INT, "id");

    EXPECT_EQ(HOUSE_INT, wsGetIo(HOUSE_INT).value("id", std::string()))
            << "the IO is published without its id: no client can address it "
               "any more, and its own unregister no longer finds it";

    IOBase *io = ListeRoom::Instance().get_io(HOUSE_INT);
    ASSERT_NE(nullptr, io);
    EXPECT_EQ(HOUSE_INT, io->get_param("id"))
            << "the model and the io_table key disagree";
}

TEST_F(DelParamIdGuardTest, TheGuardHoldsForEveryVariableType)
{
    //RED BEFORE THE FIX, three times. The guard lives in IOBase, so nothing
    //about it can depend on the IO class - but a fixture covering one type
    //only could not tell that from an accident of the reference house.
    for (const char *id: { HOUSE_BOOL_IN, HOUSE_INT, HOUSE_STRING })
    {
        EXPECT_JSON_EQ(std::string(R"({"error":"param refused"})"),
                       wsDelParam(id, "id")) << "io: " << id;
        EXPECT_EQ(std::string(id), wsGetIo(id).value("id", std::string()))
                << "io: " << id;
    }
}

/*******************************************************************************
 * The shape of the refusal, which is the part a client has to live with
 ******************************************************************************/

TEST_F(DelParamIdGuardTest, TheRefusalIsTheOneSetParamAlreadyGivesForTheSameKey)
{
    //⭐ THE CASE THAT SURVIVES A RENAMED PAYLOAD. set_param already reaches the
    //immutability guard and already answers a refusal of its own; del_param
    //must answer THAT one and not invent a second vocabulary for the same
    //rule. Comparing the two documents to each other is what keeps this true
    //if either message is ever reworded.
    const Json setRefusal = wsSetParam(HOUSE_INT, "id", "t321_stolen_id");
    const Json delRefusal = wsDelParam(HOUSE_INT, "id");

    EXPECT_JSON_EQ(setRefusal, delRefusal)
            << "the two halves of one rule answer two different things";
    EXPECT_FALSE(delRefusal.contains("success"))
            << "a refusal that also reports success is worse than either";
}

TEST_F(DelParamIdGuardTest, AMissingIoIsStillTheOtherRefusalAndNotThisOne)
{
    //GREEN BEFORE AND AFTER, and it is the discrimination that makes the case
    //above worth writing: "not found" and "refused" are different causes, and
    //a client retrying on the wrong one looks in the wrong place.
    EXPECT_JSON_EQ(std::string(R"({"error":"wrong io/param"})"),
                   wsDelParam("t321_no_such_io", "id"));
    EXPECT_JSON_EQ(std::string(R"({"error":"wrong io/param"})"),
                   wsDelParam(HOUSE_INT, ""));
}

/*******************************************************************************
 * What the rebranched guard must NOT cost
 ******************************************************************************/

TEST_F(DelParamIdGuardTest, DeletingAnOrdinaryParamStillSucceeds)
{
    //GREEN BEFORE AND AFTER. The control: routing the command through
    //IOBase::del_param() must keep the command that works, including the
    //EventIOPropertyDelete it raises and the disappearance of the key from
    //get_io.
    ASSERT_JSON_EQ(std::string(R"({"success":"true"})"),
                   wsSetParam(HOUSE_INT, "unit", "kWh"));
    ASSERT_EQ("kWh", wsGetIo(HOUSE_INT).value("unit", std::string()));

    EXPECT_JSON_EQ(std::string(R"({"success":"true"})"),
                   wsDelParam(HOUSE_INT, "unit"));
    EXPECT_FALSE(wsGetIo(HOUSE_INT).contains("unit"));
}

TEST_F(DelParamIdGuardTest, DeletingAParamThatWasNeverSetStillSucceeds)
{
    //GREEN BEFORE AND AFTER. Params::Delete() on a missing key is a no-op and
    //the answer stays a success; the guard must not turn "nothing to do" into
    //a refusal. Pinned here because the fix changes who performs the delete.
    EXPECT_JSON_EQ(std::string(R"({"success":"true"})"),
                   wsDelParam(HOUSE_INT, "t321_never_set"));
}

TEST_F(DelParamIdGuardTest, AParamWhoseNameMerelyContainsIdIsStillDeletable)
{
    //GREEN BEFORE AND AFTER. The guard compares the whole name, and a prefix
    //or substring match would take away every chauffage_id / autoscenario_uid
    //style parameter the tree really uses.
    ASSERT_JSON_EQ(std::string(R"({"success":"true"})"),
                   wsSetParam(HOUSE_INT, "chauffage_id", "t321_boiler"));
    ASSERT_EQ("t321_boiler",
              wsGetIo(HOUSE_INT).value("chauffage_id", std::string()));

    EXPECT_JSON_EQ(std::string(R"({"success":"true"})"),
                   wsDelParam(HOUSE_INT, "chauffage_id"));
    EXPECT_FALSE(wsGetIo(HOUSE_INT).contains("chauffage_id"));
}
