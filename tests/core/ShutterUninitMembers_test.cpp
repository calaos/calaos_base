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

/*----------------------------------------------------------------------------
 * WHAT A FRESH SIMPLE SHUTTER HOLDS, AND WHAT Stop() DOES WITH IT.
 *
 * `OutputShutter` has no readConfig(); its initialiser list covers `sens`,
 * `old_sens`, the four timer pointers and `state_volet`, and stops there.
 * `time`, `is_impulse_action`, `impulse_action_time` and `impulse_time` get a
 * value only inside `set_value()`. `Stop()` branches on `impulse_time`, and
 * the impulse branch drives BOTH relays true at once - the one thing a
 * shutter motor must never be handed.
 *
 * HOW THIS FILE MEASURES INSTEAD OF READING. Every claim about "the
 * constructor left this member alone" comes from POISONING: the storage is
 * filled with a known byte, the object is placement-new'd into it, and the
 * member is read back. A member the constructor writes answers its own value;
 * a member it never touches answers the poison. The census is run TWICE with
 * TWO different poisons and the answers must FOLLOW the poison - a member
 * that answered the same number under both fills would be a member the
 * constructor happens to set, not one it leaves alone. Reading a member
 * "looks uninitialised" is not available to a test: an uninitialised read has
 * no value to assert on. The poison gives it one.
 *
 * THE REFERENCE VALUE OF THE FIX, AND WHY IT IS -1 AND NOT 0. `set_value()`
 * writes `impulse_time = -1` when the `impulse_time` parameter is absent, and
 * `OutputShutterSmart::readConfig()` writes the same -1 on the same
 * condition. -1 means "no impulse": `Stop()` and `Up()` test `> 0`. An
 * initialiser of 0 would also disable the impulse branch today, but it is not
 * the value the absent configuration produces, and `ioDoc` documents 0 as a
 * legal parameter value meaning the same thing - so 0 would make the
 * "parameter absent" and "parameter set to 0" worlds indistinguishable at the
 * member. Two cases below MEASURE the -1, one on each class, instead of
 * quoting it.
 *
 * WHAT IS MEASURED OF THE RELAY CHAIN, AND WHERE THE MEASUREMENT STOPS.
 * `WOVoletBase::setOutputUp/Down()` hand `up_address`/`down_address` to
 * `WagoMap::write_single_bit()`, which serialises the frame and drops it when
 * no sidecar is connected - which is the case in this process. The ADDRESS
 * and the VALUE are therefore observable at the virtual, and only the KIND of
 * the command survives into `WagoMap::mbus_commands`. So the file measures
 * the pair (address, value) at the last point it exists, on a probe that
 * chains up to the production `WOVoletBase` implementation so the addresses
 * are the ones production computed, and a separate case ties that boundary to
 * the production Modbus table so the virtual is not a dead end.
 *
 * THE EIGHT WAYS A CASE HERE COULD LIE, AND WHAT ANSWERS EACH.
 *  - direct call: nothing calls `Stop()` on a shutter that has not been
 *    through `set_value()` in production today, so the entry point IS the
 *    direct one; it is what T3.161 wants to add. The cases that drive the
 *    production `up`/`stop` actions instead are marked as such and are the
 *    ones that pin today's behaviour.
 *  - a name known in advance: no oracle spells a coil number. The two Wago
 *    addresses are asserted DISTINCT and asserted to move with `var_up` /
 *    `var_down`, never compared to a literal.
 *  - a LENGTH: no case asserts only the size of a relay log; each asserts the
 *    full ordered sequence of (relay, value) pairs.
 *  - a false fixture: `TheWagoRelayVirtualsPutOneModbusWriteEachOnTheWire`
 *    shows the same object's relay writes reaching the production
 *    `mbus_commands` table, so the recording probe is not a world of its own.
 *  - presence instead of position: the relay oracles are sequences, so "up
 *    then down" and "down then up" are different answers.
 *  - the haystack excluding the target: the poison fills the WHOLE storage,
 *    including the members the constructor does set, and
 *    `ThePlainConstructorDoesOverwriteWhatItInitialises` requires those to
 *    come back at their real values. A poison that never reached the object
 *    would redden that case.
 *  - permutation with no change of value: the census runs under two poisons
 *    and requires the answers to differ between them.
 *  - ⭐ a sensor that measures nothing passing for a sensor that finds
 *    nothing: `AConfiguredImpulseShutterStillPulsesBothRelaysToStop` requires
 *    the SAME probe to report (up,true),(down,true). Without it, "both relays
 *    at rest" would be indistinguishable from a probe that never records a
 *    true.
 *
 * NO EXCLUSION GUARD. Nothing here is skipped, and nothing is conditional.
 *
 * ⛔ WHAT IS NOT CLOSED. No PLC is reached; that a coil written true really
 * energises a relay, and what a motor does when both are energised, need the
 * hardware. The poison stands in for indeterminate storage: it makes ONE of
 * the values the member can hold observable, it does not enumerate them.
 *--------------------------------------------------------------------------*/

#include "CalaosCoreFixture.h"

#include "OutputShutter.h"
#include "OutputShutterSmart.h"
#include "WOVolet.h"
#include "WagoMap.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <new>
#include <string>
#include <vector>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

/* The two poisons. Distinct, neither is a plausible product of any code path
 * under test, and their int readings straddle the `impulse_time > 0` test the
 * whole ticket turns on: 0xA5A5A5A5 is negative, 0x5A5A5A5A is positive. */
const unsigned char kFillA = 0xA5;
const unsigned char kFillB = 0x5A;
const int kPoisonA = (int)0xA5A5A5A5;   //-1515870811
const int kPoisonB = (int)0x5A5A5A5A;   // 1515870810

/* ⛔ THE POISON HAS TO BE OPAQUE TO THE OPTIMISER, AND THAT IS MEASURED.
 *
 * A plain memset() followed by placement-new is NOT enough. Starting the
 * lifetime of an object leaves every member its constructor does not write
 * with an INDETERMINATE value, and the compiler is entitled to act on that:
 * at -O2 gcc dropped the fill for some of the probes below and kept it for
 * others, so two cases answered stack leftovers (0, 32764) instead of the
 * poison while the rest answered correctly. A harness that loses its poison
 * reports "the constructor wrote something here" - the exact shape of a
 * finding, produced by a dead sensor.
 *
 * Both halves therefore go through volatile byte accesses, which the compiler
 * must emit: the fill is written byte by byte through a volatile pointer, and
 * every member is read back through its object representation the same way.
 * TheHarnessCanShowAPoisonSurvivingAConstructor is the self test of that
 * apparatus and is meant to redden FIRST if it ever stops working. */
/* ⚠️ The volatile stores alone are not enough either: placement-new hands
 * back a pointer to a BRAND NEW object, and a compiler that can see the whole
 * constructor is free to decide that nothing written through the old storage
 * reaches it. Measured on the toy of the self test, whose constructor inlines
 * completely: the poison came back as 0 with volatile stores in place. The
 * barrier is what forbids that reasoning. */
#if defined(__GNUC__) || defined(__clang__)
#define T3172_BARRIER(p) __asm__ __volatile__("" : "+r"(p) : : "memory")
#else
#define T3172_BARRIER(p) do { } while (0)
#endif

void poisonBytes(void *p, size_t n, unsigned char fill)
{
    volatile unsigned char *b = static_cast<volatile unsigned char *>(p);
    for (size_t i = 0; i < n; i++)
        b[i] = fill;
    T3172_BARRIER(b);
}

/* Read the object representation of `m`, byte by byte, through volatile. */
template <class M>
M readThrough(const M &m)
{
    M out;
    const volatile unsigned char *src =
        reinterpret_cast<const volatile unsigned char *>(&m);
    unsigned char *dst = reinterpret_cast<unsigned char *>(&out);
    for (size_t i = 0; i < sizeof(M); i++)
        dst[i] = src[i];
    return out;
}

/* The whole object filled with one byte, for the memcmp of a double. */
template <class M>
M allBytes(unsigned char fill)
{
    M out;
    poisonBytes(&out, sizeof(M), fill);
    return out;
}

/* Construct T over storage filled with `fill`, so that any member the
 * constructor does not write reads back as the poison.
 *
 * ⚠️ The fill covers the whole object, vtable slot and non-trivial members
 * included. That is safe because placement-new runs every constructor of
 * every subobject over it; it is also the point, since a poison that spared
 * the initialised members could not prove it reached the object at all. */
template <class T>
class PoisonedStorage
{
public:
    T *construct(Params &p, unsigned char fill)
    {
        poisonBytes(storage, sizeof(storage), fill);
        unsigned char *raw = storage;
        T3172_BARRIER(raw);
        obj = new (raw) T(p);
        T3172_BARRIER(obj);
        return obj;
    }

    ~PoisonedStorage()
    {
        if (obj) obj->~T();
    }

private:
    alignas(T) unsigned char storage[sizeof(T)];
    T *obj = nullptr;
};

/* Read the object representation of a bool. A poisoned bool holds neither 0
 * nor 1, and reading it AS a bool is undefined; its byte is not. */
unsigned char boolByte(const bool &b)
{
    return readThrough(b);
}

/* The toy the harness self test uses: one member the constructor writes, one
 * it never mentions. Params is taken and ignored so it goes through the same
 * PoisonedStorage as everything else. */
struct ToyPartlyInitialised
{
    int written;
    int neverWritten;

    explicit ToyPartlyInitialised(Params &): written(7) {}
};

/******************************************************************************
 * Census probes: they add NO data member, so the layout and the constructor
 * under measurement are the production ones. The static_asserts hold that.
 ******************************************************************************/

struct PlainCensus: public OutputShutter
{
    using OutputShutter::OutputShutter;

    using OutputShutter::time;
    using OutputShutter::sens;
    using OutputShutter::old_sens;
    using OutputShutter::is_impulse_action;
    using OutputShutter::impulse_action_time;
    using OutputShutter::impulse_time;
    using OutputShutter::state_volet;
    using OutputShutter::cmd_state;
};
static_assert(sizeof(PlainCensus) == sizeof(OutputShutter),
              "the census probe must not change the layout it measures");

struct SmartCensus: public OutputShutterSmart
{
    using OutputShutterSmart::OutputShutterSmart;

    using OutputShutterSmart::sens;
    using OutputShutterSmart::old_sens;
    using OutputShutterSmart::start_time;
    using OutputShutterSmart::start_position;
    using OutputShutterSmart::is_impulse_action;
    using OutputShutterSmart::impulse_action_time;
    using OutputShutterSmart::impulse_time;
};
static_assert(sizeof(SmartCensus) == sizeof(OutputShutterSmart),
              "the census probe must not change the layout it measures");

/******************************************************************************
 * Relay probes. `Stop()` is a protected virtual of the production class, so a
 * derived class is the only way to call the very entry point T3.161 wants to
 * add - without adding it to production.
 ******************************************************************************/

struct RelayWrite
{
    std::string relay;      //"up" or "down"
    bool value = false;
    int address = -1;       //Wago coil, -1 when the probe is not a Wago one

    bool operator==(const RelayWrite &o) const
    {
        return relay == o.relay && value == o.value && address == o.address;
    }
};

std::string describe(const std::vector<RelayWrite> &log)
{
    std::string s = "{";
    for (size_t i = 0; i < log.size(); i++)
    {
        if (i) s += ", ";
        s += log[i].relay + "=" + (log[i].value ? "true" : "false");
        if (log[i].address >= 0)
            s += "@" + Utils::to_string(log[i].address);
    }
    return s + "}";
}

std::vector<RelayWrite> bare(const std::string &r1, bool v1,
                             const std::string &r2, bool v2)
{
    return { { r1, v1, -1 }, { r2, v2, -1 } };
}

class PlainRelayProbe: public OutputShutter
{
public:
    explicit PlainRelayProbe(Params &p): OutputShutter(p) {}

    std::vector<RelayWrite> log;

    void stopNow() { Stop(); }
    void upNow() { Up(); }

    using OutputShutter::impulse_time;

protected:
    void setOutputUp(bool enable) override { log.push_back({ "up", enable, -1 }); }
    void setOutputDown(bool enable) override { log.push_back({ "down", enable, -1 }); }
};

/* The Wago shutter, recording the (coil, value) pair production computed and
 * then letting production issue the real Modbus write. */
class WagoRelayProbe: public WOVolet
{
public:
    explicit WagoRelayProbe(Params &p): WOVolet(p) {}

    std::vector<RelayWrite> log;

    void stopNow() { Stop(); }
    int upCoil() const { return up_address; }
    int downCoil() const { return down_address; }

protected:
    void setOutputUp(bool enable) override
    {
        WOVolet::setOutputUp(enable);   //recomputes up_address, then writes
        log.push_back({ "up", enable, up_address });
    }

    void setOutputDown(bool enable) override
    {
        WOVolet::setOutputDown(enable);
        log.push_back({ "down", enable, down_address });
    }
};

/* The production table of Modbus commands issued and not yet answered, read
 * through a pointer-to-member formed on a derived class. Same procedure as
 * core/WagoVoletRestart_test and core/WagoDaliGetGroup_test; nothing is ever
 * constructed from this type, and production gains no line. */
struct WagoMapProbe: Calaos::WagoMap
{
    static size_t commandCount(const std::string &host)
    {
        size_t n = 0;
        for (WagoMap *m: WagoMap::get_maps())
        {
            if (m->get_host() != host) continue;
            n += (m->*(&WagoMapProbe::mbus_commands)).size();
        }
        return n;
    }
};

}

/******************************************************************************
 * The census
 ******************************************************************************/

class ShutterUninitMembersTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        loadConfig();
    }

    Params plainParams(const std::string &id, bool withImpulseTime = false,
                       int impulseTimeMs = 35)
    {
        Params p = {
            { "type", "OutputShutter" },
            { "id", id },
            { "name", id },
            { "time", "60" },
            { "enabled", "true" },
            { "visible", "false" },
        };
        if (withImpulseTime)
            p.Add("impulse_time", Utils::to_string(impulseTimeMs));
        return p;
    }

    Params smartParams(const std::string &id)
    {
        return Params {
            { "type", "OutputShutterSmart" },
            { "id", id },
            { "name", id },
            { "time", "60" },
            { "enabled", "true" },
            { "visible", "false" },
        };
    }

    Params wagoParams(const std::string &id, const std::string &host,
                      int varUp, int varDown)
    {
        return Params {
            { "type", "WOVolet" },
            { "id", id },
            { "name", id },
            { "host", host },
            { "var_up", Utils::to_string(varUp) },
            { "var_down", Utils::to_string(varDown) },
            { "time", "60" },
            { "enabled", "true" },
            { "visible", "false" },
        };
    }

    /* Put the state cache in the one state that gets a Stop() past its first
     * line: the server believes this shutter is travelling. This is exactly
     * the population a resynchronisation point would act on (T3.161 §7). */
    void seedCacheAsMoving(const std::string &id, const std::string &type)
    {
        Params cache = { { "sens", Utils::to_string(SHUTTER_UP) },
                         { "old_sens", Utils::to_string(SHUTTER_UP) },
                         { "state_volet", "true" },
                         { "cmd_state", "up" } };
        Config::Instance().SaveValueParams(id + "_" + type, cache, false);
    }
};

//⭐ THE HARNESS, EPROUVE AVANT USAGE. One member written by the constructor,
//one never mentioned by it. If the poison ever stops reaching the storage -
//or the optimiser ever eats it again - this case reddens first and names the
//apparatus, instead of every case below quietly reporting stack leftovers as
//"the constructor wrote something here".
TEST_F(ShutterUninitMembersTest, TheHarnessCanShowAPoisonSurvivingAConstructor)
{
    Params p = plainParams("t3172_harness_selftest");

    PoisonedStorage<ToyPartlyInitialised> sa;
    ToyPartlyInitialised *a = sa.construct(p, kFillA);
    PoisonedStorage<ToyPartlyInitialised> sb;
    ToyPartlyInitialised *b = sb.construct(p, kFillB);

    EXPECT_EQ(readThrough(a->written), 7) << "the constructor did not run";
    EXPECT_EQ(readThrough(b->written), 7) << "the constructor did not run";

    EXPECT_EQ(readThrough(a->neverWritten), kPoisonA)
        << "the poison did not survive: this harness measures nothing";
    EXPECT_EQ(readThrough(b->neverWritten), kPoisonB)
        << "the poison did not survive: this harness measures nothing";
}

/* ⭐ ONE CASE PER MEMBER, and that is not cosmetics: a single case covering
 * the four would answer the same red set to four different regressions, and
 * "which member stopped being initialised" is exactly what a maintainer needs
 * from a failure. Each runs under BOTH poisons, because one fill cannot tell
 * a member the constructor writes from a leftover that happened to match. */

//THE member Stop() reads, and the whole hardware risk of this ticket.
//-1 and not 0: see the header.
TEST_F(ShutterUninitMembersTest, ThePlainConstructorMustWriteImpulseTime)
{
    Params pa = plainParams("t3172_census_it");
    Params pb = plainParams("t3172_census_it");

    PoisonedStorage<PlainCensus> sa;
    PlainCensus *a = sa.construct(pa, kFillA);
    PoisonedStorage<PlainCensus> sb;
    PlainCensus *b = sb.construct(pb, kFillB);

    EXPECT_EQ(readThrough(a->impulse_time), -1) << "poison " << kPoisonA << " survived";
    EXPECT_EQ(readThrough(b->impulse_time), -1) << "poison " << kPoisonB << " survived";
}

TEST_F(ShutterUninitMembersTest, ThePlainConstructorMustWriteTime)
{
    Params pa = plainParams("t3172_census_time");
    Params pb = plainParams("t3172_census_time");

    PoisonedStorage<PlainCensus> sa;
    PlainCensus *a = sa.construct(pa, kFillA);
    PoisonedStorage<PlainCensus> sb;
    PlainCensus *b = sb.construct(pb, kFillB);

    EXPECT_EQ(readThrough(a->time), 0) << "poison " << kPoisonA << " survived";
    EXPECT_EQ(readThrough(b->time), 0) << "poison " << kPoisonB << " survived";
}

TEST_F(ShutterUninitMembersTest, ThePlainConstructorMustWriteIsImpulseAction)
{
    Params pa = plainParams("t3172_census_iia");
    Params pb = plainParams("t3172_census_iia");

    PoisonedStorage<PlainCensus> sa;
    PlainCensus *a = sa.construct(pa, kFillA);
    PoisonedStorage<PlainCensus> sb;
    PlainCensus *b = sb.construct(pb, kFillB);

    EXPECT_EQ(boolByte(a->is_impulse_action), 0) << "poison " << (int)kFillA << " survived";
    EXPECT_EQ(boolByte(b->is_impulse_action), 0) << "poison " << (int)kFillB << " survived";
}

TEST_F(ShutterUninitMembersTest, ThePlainConstructorMustWriteImpulseActionTime)
{
    Params pa = plainParams("t3172_census_iat");
    Params pb = plainParams("t3172_census_iat");

    PoisonedStorage<PlainCensus> sa;
    PlainCensus *a = sa.construct(pa, kFillA);
    PoisonedStorage<PlainCensus> sb;
    PlainCensus *b = sb.construct(pb, kFillB);

    EXPECT_EQ(readThrough(a->impulse_action_time), 0) << "poison " << kPoisonA << " survived";
    EXPECT_EQ(readThrough(b->impulse_action_time), 0) << "poison " << kPoisonB << " survived";
}

//The witness that the poison reaches the object: the members the initialiser
//list DOES cover must come back at their real values under both fills. A
//probe that never wrote anything into the storage would redden here.
TEST_F(ShutterUninitMembersTest, ThePlainConstructorDoesOverwriteWhatItInitialises)
{
    Params pa = plainParams("t3172_witness_plain");
    Params pb = plainParams("t3172_witness_plain");

    PoisonedStorage<PlainCensus> sa;
    PlainCensus *a = sa.construct(pa, kFillA);
    PoisonedStorage<PlainCensus> sb;
    PlainCensus *b = sb.construct(pb, kFillB);

    EXPECT_EQ(a->sens, SHUTTER_STOP);
    EXPECT_EQ(b->sens, SHUTTER_STOP);
    EXPECT_EQ(a->old_sens, SHUTTER_UP);
    EXPECT_EQ(b->old_sens, SHUTTER_UP);
    EXPECT_EQ(a->state_volet, "true");
    EXPECT_EQ(b->state_volet, "true");
    EXPECT_EQ(a->cmd_state, "");
    EXPECT_EQ(b->cmd_state, "");

    EXPECT_NE(a->sens, kPoisonA) << "the poison was not even written";
    EXPECT_NE(b->sens, kPoisonB) << "the poison was not even written";
}

//The value an ABSENT impulse_time parameter produces on the class itself,
//measured through the production path that writes it. This is the value the
//initialiser list must carry, and it is read from nowhere.
TEST_F(ShutterUninitMembersTest, AnAbsentImpulseTimeParameterMakesTheCodeWriteMinusOne)
{
    Params p = plainParams("t3172_absent_param");
    PlainRelayProbe sh(p);

    ASSERT_TRUE(sh.set_value("stop"));

    EXPECT_EQ(sh.impulse_time, -1)
        << "the value the absent configuration produces is what the "
           "initialiser list has to reproduce";
}

//The other three members, same question: what does the running code write
//when nothing is configured? That is what the initialiser list has to carry.
TEST_F(ShutterUninitMembersTest, AnAbsentTimeParameterMakesTheCodeWriteZero)
{
    Params p = {
        { "type", "OutputShutter" },
        { "id", "t3172_absent_time" },
        { "name", "t3172_absent_time" },
        { "enabled", "true" },
        { "visible", "false" },
    };
    PlainCensus sh(p);

    ASSERT_TRUE(sh.set_value("stop"));

    EXPECT_EQ(sh.time, 0);
    EXPECT_FALSE(sh.is_impulse_action);
}

//...and the same path writes the parameter when it IS there, so the case
//above is a discrimination and not a constant.
TEST_F(ShutterUninitMembersTest, APresentImpulseTimeParameterReachesTheMember)
{
    Params p = plainParams("t3172_present_param", true, 35);
    PlainRelayProbe sh(p);

    ASSERT_TRUE(sh.set_value("stop"));

    EXPECT_EQ(sh.impulse_time, 35);
}

//The smart shutter calls readConfig() from its constructor, which writes
//impulse_time on both branches. Under both poisons it answers the same -1:
//that is at once the proof it is sound and the second, independent
//measurement of the reference value.
TEST_F(ShutterUninitMembersTest, TheSmartConstructorAlreadyWritesTheNoImpulseValue)
{
    Params pa = smartParams("t3172_census_smart");
    Params pb = smartParams("t3172_census_smart");

    PoisonedStorage<SmartCensus> sa;
    SmartCensus *a = sa.construct(pa, kFillA);
    PoisonedStorage<SmartCensus> sb;
    SmartCensus *b = sb.construct(pb, kFillB);

    EXPECT_EQ(a->impulse_time, -1);
    EXPECT_EQ(b->impulse_time, -1);
}

//CHARACTERISATION, not a regression oracle: what the smart shutter still
//leaves alone. None of it is read by Stop(), so none of it carries the
//hardware risk - see T3.221.
TEST_F(ShutterUninitMembersTest, TheSmartConstructorStillLeavesFourOtherMembersAsItFoundThem)
{
    Params pa = smartParams("t3172_census_smart_rest");
    Params pb = smartParams("t3172_census_smart_rest");

    PoisonedStorage<SmartCensus> sa;
    SmartCensus *a = sa.construct(pa, kFillA);
    PoisonedStorage<SmartCensus> sb;
    SmartCensus *b = sb.construct(pb, kFillB);

    EXPECT_EQ(readThrough(a->impulse_action_time), kPoisonA);
    EXPECT_EQ(readThrough(b->impulse_action_time), kPoisonB);
    EXPECT_EQ(boolByte(a->is_impulse_action), kFillA);
    EXPECT_EQ(boolByte(b->is_impulse_action), kFillB);

    //start_time/start_position are doubles: the poison is compared as a bit
    //pattern, since 0xA5A5... read as a double is a perfectly ordinary number
    //that no equality against an int would catch.
    const double poisonedDoubleA = allBytes<double>(kFillA);
    const double poisonedDoubleB = allBytes<double>(kFillB);
    const double startTimeA = readThrough(a->start_time);
    const double startTimeB = readThrough(b->start_time);
    const double startPosA = readThrough(a->start_position);
    const double startPosB = readThrough(b->start_position);

    EXPECT_EQ(std::memcmp(&startTimeA, &poisonedDoubleA, sizeof(double)), 0);
    EXPECT_EQ(std::memcmp(&startTimeB, &poisonedDoubleB, sizeof(double)), 0);
    EXPECT_EQ(std::memcmp(&startPosA, &poisonedDoubleA, sizeof(double)), 0);
    EXPECT_EQ(std::memcmp(&startPosB, &poisonedDoubleB, sizeof(double)), 0);
}

/******************************************************************************
 * The chain from the member to the relays
 ******************************************************************************/

//Where the chain STOPS, and why the defect is latent today. A shutter whose
//state cache says nothing leaves the constructor at SHUTTER_STOP, and Stop()
//returns on its first line without ever reading impulse_time. The poison is
//the positive one, i.e. the worst case: no relay is commanded all the same.
//The expectation is the empty set, the shape a dead probe also returns;
//AConfiguredImpulseShutterStillPulsesBothRelaysToStop is what shows this same
//probe class reporting a non-empty log.
TEST_F(ShutterUninitMembersTest, AStopOnAFreshPlainShutterCommandsNoRelayAtAll)
{
    Params p = plainParams("t3172_fresh_stop");

    PoisonedStorage<PlainRelayProbe> s;
    PlainRelayProbe *sh = s.construct(p, kFillB);

    sh->stopNow();

    EXPECT_TRUE(sh->log.empty())
        << "a shutter believed stopped must not command a relay: "
        << describe(sh->log);
}

//⭐ THE case. The cache says the shutter is travelling - the one population a
//resynchronisation point would act on - so Stop() gets past its first line
//and branches on the member nothing wrote. Both relays must come back at
//REST. Before the fix this reads {up=true, down=true}: the two relays of one
//shutter energised together.
TEST_F(ShutterUninitMembersTest, AStopOnAMovingPlainShutterMustLeaveBothRelaysAtRest)
{
    seedCacheAsMoving("t3172_moving_stop", "OutputShutter");
    Params p = plainParams("t3172_moving_stop");

    PoisonedStorage<PlainRelayProbe> s;
    PlainRelayProbe *sh = s.construct(p, kFillB);

    sh->stopNow();

    EXPECT_EQ(sh->log, bare("up", false, "down", false))
        << "got " << describe(sh->log)
        << " - both relays of one shutter must never be commanded true at "
           "the same time by a value nothing wrote";
}

//The second member with a consequence, and the only one of the three besides
//impulse_time that shows outside the loop: Up() skips `cmd_state = "up"` when
//it believes it is running an impulse action, so a shutter that left its
//constructor with is_impulse_action true publishes an EMPTY command to the
//API after being opened.
TEST_F(ShutterUninitMembersTest, AnUpOnAFreshPlainShutterPublishesTheUpCommand)
{
    Params p = plainParams("t3172_up_cmdstate");

    PoisonedStorage<PlainRelayProbe> s;
    PlainRelayProbe *sh = s.construct(p, kFillB);

    sh->upNow();

    EXPECT_EQ(sh->log, bare("up", true, "down", false)) << describe(sh->log);
    EXPECT_EQ(sh->get_command_string(), "up");
}

//The sensor liveness control, and the boundary of the fix: a shutter that
//really is an impulse shutter must STILL pulse both relays to stop, because
//that is what an impulse shutter needs. Driven through the production
//actions, so impulse_time comes from the configuration and not from a poison.
TEST_F(ShutterUninitMembersTest, AConfiguredImpulseShutterStillPulsesBothRelaysToStop)
{
    Params p = plainParams("t3172_impulse_stop", true, 35);
    PlainRelayProbe sh(p);

    ASSERT_TRUE(sh.set_value("up"));
    sh.log.clear();
    ASSERT_TRUE(sh.set_value("stop"));

    EXPECT_EQ(sh.log, bare("up", true, "down", true))
        << "got " << describe(sh.log)
        << " - this probe CAN report two energised relays, which is what "
           "makes the previous case a measurement";
}

//Non-regression on the ordinary path: a shutter with no impulse configured,
//driven through the production actions, releases both relays on stop.
TEST_F(ShutterUninitMembersTest, APlainShutterDrivenThroughTheApiReleasesBothRelays)
{
    Params p = plainParams("t3172_api_stop");
    PlainRelayProbe sh(p);

    ASSERT_TRUE(sh.set_value("up"));
    sh.log.clear();
    ASSERT_TRUE(sh.set_value("stop"));

    EXPECT_EQ(sh.log, bare("up", false, "down", false)) << describe(sh.log);
}

/******************************************************************************
 * ...all the way to the two Wago coils
 ******************************************************************************/

//The same scenario on the IO that owns real hardware. What is asserted is the
//ordered pair (coil, value) production itself computed, and the two coils are
//required to be DIFFERENT - a single coil written twice would not be a
//shutter with two relays.
TEST_F(ShutterUninitMembersTest, AStopOnAMovingWagoShutterMustNotDriveBothCoilsTrue)
{
    seedCacheAsMoving("t3172_wago_moving", "WOVolet");
    Params p = wagoParams("t3172_wago_moving", "192.0.2.41", 7, 11);

    PoisonedStorage<WagoRelayProbe> s;
    WagoRelayProbe *sh = s.construct(p, kFillB);

    const int up = sh->upCoil();
    const int down = sh->downCoil();
    ASSERT_NE(up, down) << "a shutter drives two distinct coils";

    sh->stopNow();

    const std::vector<RelayWrite> atRest = { { "up", false, up },
                                             { "down", false, down } };
    EXPECT_EQ(sh->log, atRest)
        << "got " << describe(sh->log)
        << " - the two coils of one shutter must not both be driven true";
}

//The coils are the configured ones, not a fixed pair: moving var_up/var_down
//moves them by the same amount. No literal address appears in the oracle.
TEST_F(ShutterUninitMembersTest, TheTwoWagoCoilsFollowTheConfiguredAddresses)
{
    Params p1 = wagoParams("t3172_wago_addr1", "192.0.2.42", 7, 11);
    Params p2 = wagoParams("t3172_wago_addr2", "192.0.2.42", 19, 23);
    WagoRelayProbe a(p1);
    WagoRelayProbe b(p2);

    EXPECT_EQ(b.upCoil() - a.upCoil(), 19 - 7);
    EXPECT_EQ(b.downCoil() - a.downCoil(), 23 - 11);
    EXPECT_EQ(a.downCoil() - a.upCoil(), 11 - 7);
}

//The virtual is not a dead end: driving the production `up` action on a Wago
//shutter puts its relay writes on the production Modbus table. Without this,
//every case above could be measuring a boundary nothing crosses.
TEST_F(ShutterUninitMembersTest, TheWagoRelayVirtualsPutOneModbusWriteEachOnTheWire)
{
    const std::string host = "192.0.2.43";
    Params p = wagoParams("t3172_wago_wire", host, 7, 11);
    WagoRelayProbe sh(p);

    const size_t before = WagoMapProbe::commandCount(host);
    ASSERT_TRUE(sh.set_value("up"));

    ASSERT_EQ(sh.log.size(), 2u) << describe(sh.log);
    EXPECT_EQ(sh.log[0].relay, "up");
    EXPECT_TRUE(sh.log[0].value);
    EXPECT_EQ(sh.log[1].relay, "down");
    EXPECT_FALSE(sh.log[1].value);

    EXPECT_EQ(WagoMapProbe::commandCount(host) - before, 2u)
        << "the two relay virtuals must each reach the production command "
           "table, otherwise the probe above measures a boundary nothing "
           "crosses";
}
