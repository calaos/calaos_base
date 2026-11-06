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

/******************************************************************************
 * T1.11 - IOBase/IOFactory id integrity
 * =====================================
 *
 * ListeRoom::io_table is keyed by the IO's "id" param. Before T1.11 the id
 * was freely mutable through IOBase::set_param()/del_param() (reachable from
 * the JSON API "setparam" call), which left the hash entry keyed on the OLD
 * id: lookups by the new id fail, delIOHash() no longer finds the entry, and
 * the table ends up holding a dangling pointer (UAF).
 *
 * These tests cover:
 *   - "id" is immutable through set_param()/del_param() once the IO exists,
 *   - a duplicate (case-colliding) REGISTER_IO does not silently overwrite
 *     the first registration in IOFactory,
 *   - IOFactory::genDoc()/genDocIO() does not insert its temporary id="doc"
 *     objects into the live io_table (and does not leak them - the leak part
 *     is verified under ASan, not asserted here),
 *   - battery status contract: a real 0% battery reading is reported by
 *     getStatusInfo() (not hidden), and the low-battery notification
 *     timestamp is only recorded when a notification channel actually sends.
 *
 * Note on ordering: the tests that try to corrupt the id run first and
 * restore the id themselves, so a pre-fix (RED) run does not poison the
 * singleton io_table for the following tests more than necessary. The
 * REGISTER_IO collision test runs last because, pre-fix, it permanently
 * overwrites the InternalBool factory entry of this process.
 ******************************************************************************/

#include "CalaosCoreFixture.h"

using namespace Calaos;
using namespace CalaosTest;

class IOIdIntegrityTest: public CoreFixture
{
};

/******************************************************************************
 * id immutability
 ******************************************************************************/

//Changing "id" through the generic param setter (what the JSON API setparam
//exposes) must be refused: the param keeps its value and the io_table entry
//stays consistent.
TEST_F(IOIdIntegrityTest, SetParamCannotChangeIdOnceSet)
{
    loadConfig();

    IOBase *o = io(ID_BOOL_IN);
    ASSERT_NE(o, nullptr);

    o->set_param("id", "hijacked_id");

    EXPECT_EQ(o->get_param("id"), std::string(ID_BOOL_IN));
    EXPECT_EQ(io(ID_BOOL_IN), o);
    EXPECT_EQ(io("hijacked_id"), nullptr);

    //Setting "id" to its current value is a harmless no-op, not an error
    o->set_param("id", ID_BOOL_IN);
    EXPECT_EQ(o->get_param("id"), std::string(ID_BOOL_IN));

    //RED-run state repair: pre-fix the line above already restored the
    //original id, so the fixture teardown can unregister the IO correctly.
}

//del_param("id") would orphan the io_table entry exactly like a rename.
TEST_F(IOIdIntegrityTest, DelParamCannotDropId)
{
    loadConfig();

    IOBase *o = io(ID_BOOL_IN);
    ASSERT_NE(o, nullptr);

    o->del_param("id");

    EXPECT_EQ(o->get_param("id"), std::string(ID_BOOL_IN));
    EXPECT_EQ(io(ID_BOOL_IN), o);

    //RED-run state repair (no-op once the fix is in)
    o->set_param("id", ID_BOOL_IN);
}

//renameId() is THE supported way to change an id: it re-keys the io_table
//entry atomically, and refuses empty or already-taken ids.
//(New API introduced by T1.11, so no pre-fix RED run exists for this test.)
TEST_F(IOIdIntegrityTest, RenameIdRehashesTheTable)
{
    loadConfig();

    IOBase *o = io(ID_BOOL_IN);
    ASSERT_NE(o, nullptr);

    EXPECT_TRUE(o->renameId("io_t111_renamed"));
    EXPECT_EQ(o->get_param("id"), "io_t111_renamed");
    EXPECT_EQ(io("io_t111_renamed"), o);
    EXPECT_EQ(io(ID_BOOL_IN), nullptr);

    //renaming onto an existing id is refused and changes nothing
    EXPECT_FALSE(o->renameId(ID_BOOL_OUT));
    EXPECT_EQ(o->get_param("id"), "io_t111_renamed");
    EXPECT_EQ(io("io_t111_renamed"), o);

    //empty id is refused
    EXPECT_FALSE(o->renameId(""));
    EXPECT_EQ(o->get_param("id"), "io_t111_renamed");

    //rename to self is an accepted no-op
    EXPECT_TRUE(o->renameId("io_t111_renamed"));

    //rename back, the table follows again
    EXPECT_TRUE(o->renameId(ID_BOOL_IN));
    EXPECT_EQ(io(ID_BOOL_IN), o);
    EXPECT_EQ(io("io_t111_renamed"), nullptr);
}

/******************************************************************************
 * battery status contract
 ******************************************************************************/

//A device reporting a truly empty battery (0%) must show up in
//getStatusInfo(): 0.0 is a legitimate reading, not "no battery info".
TEST_F(IOIdIntegrityTest, BatteryZeroPercentIsReported)
{
    loadConfig();

    IOBase *o = io(ID_BOOL_IN);
    ASSERT_NE(o, nullptr);

    //Both notification channels default to enabled (Utils.cpp defaults):
    //disable them so this test only exercises the status reporting.
    Utils::set_config_option("notif/battery_mail_enabled", "false");
    Utils::set_config_option("notif/battery_push_enabled", "false");

    //No battery reading yet: nothing reported
    EXPECT_FALSE(o->getStatusInfo().Exists("battery_level"));
    EXPECT_FALSE(o->hasStatusInfo());

    o->setStatusInfo(IOBase::StatusType::BatteryLevel, 0.0);

    EXPECT_TRUE(o->hasStatusInfo());
    EXPECT_TRUE(o->getStatusInfo().Exists("battery_level"));
}

//The 24h low-battery notification throttle timestamp must only be recorded
//when a notification channel (mail/push) actually sends. Here no
//notif/battery_*_enabled option is set, so nothing is sent and no throttle
//state at all must be recorded (otherwise enabling a channel later silently
//swallows the first real notification for up to 24h).
//Pre-fix this recorded a cache entry on every low reading - and, because
//Params::operator[] is read-only (returns by value), the entry was even
//always EMPTY, so the 24h throttle never worked at all.
TEST_F(IOIdIntegrityTest, BatteryNotifTimestampNotRecordedWithoutChannel)
{
    loadConfig();

    IOBase *o = io(ID_BOOL_IN);
    ASSERT_NE(o, nullptr);

    o->set_param("notif_battery", "true");

    //Explicitly disable both channels: they default to ENABLED
    //(Utils.cpp `notif/battery_*_enabled` default "true"), and a "sent"
    //notification legitimately records the throttle timestamp.
    Utils::set_config_option("notif/battery_mail_enabled", "false");
    Utils::set_config_option("notif/battery_push_enabled", "false");

    o->setStatusInfo(IOBase::StatusType::BatteryLevel, 10.0);

    Params cached;
    bool hasEntry = Config::Instance().ReadValueParams(
        o->get_param("id") + "_" + o->get_param("type"), cached);
    EXPECT_FALSE(hasEntry) << "no throttle cache entry may be created "
                              "when no notification was sent";
    EXPECT_FALSE(cached.Exists("last_battery_notif_time"));
}

/******************************************************************************
 * genDoc hygiene
 ******************************************************************************/

//Doc generation instantiates every registered IO type to read its IODoc.
//Those short-lived objects (all sharing id="doc") must never enter the live
//io_table, and the IOs that were already there must be untouched.
//(The "and does not leak" half of the finding is covered by running this
//test under ASan, where a leaked IO would previously show up.)
TEST_F(IOIdIntegrityTest, GenDocDoesNotPolluteLiveTable)
{
    loadConfig();

    IOBase *before = io(ID_BOOL_IN);
    ASSERT_NE(before, nullptr);

    IOFactory::Instance().genDoc(cacheDir() + "/docgen");

    EXPECT_EQ(io("doc"), nullptr);
    EXPECT_EQ(io(ID_BOOL_IN), before);
}

/******************************************************************************
 * REGISTER_IO collision
 ******************************************************************************/

//A duplicate registration (here: case-colliding "INTERNALBOOL" vs the real
//"InternalBool") must NOT silently replace the first one - first
//registration wins. Pre-fix, the broken factory below overwrote the real one
//and CreateIO returned nullptr.
//Kept last on purpose: pre-fix it permanently corrupts the InternalBool
//entry of this process' IOFactory registry.
TEST_F(IOIdIntegrityTest, DuplicateRegistrationKeepsFirst)
{
    loadConfig();

    IOFactory::Instance().RegisterClass(
        "INTERNALBOOL",
        [](Params &) -> IOBase * { return nullptr; });

    Params p;
    p.Add("type", "InternalBool");
    p.Add("id", "io_t111_collision");
    p.Add("name", "collision probe");

    IOBase *o = IOFactory::Instance().CreateIO("InternalBool", p);
    ASSERT_NE(o, nullptr);
    EXPECT_EQ(io("io_t111_collision"), o);

    delete o;
    EXPECT_EQ(io("io_t111_collision"), nullptr);
}
