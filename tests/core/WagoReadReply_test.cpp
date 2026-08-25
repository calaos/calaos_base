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
 **  along with Foobar; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/

/*----------------------------------------------------------------------------
 * T3.50 - THE READ HALF OF THE WAGO *REPLY* PATH: (UWord address, int count).
 *
 * T3.31 typed the OUTBOUND half of the Wago chain. T3.46 typed the WRITE half
 * of the reply path (SingleBit_cb / SingleWord_cb) and left this one, named:
 *
 *     MultiBits_cb   (bool status, UWord address, int count, vector<bool> &)
 *     MultiWords_cb  (bool status, UWord address, int count, vector<UWord> &)
 *
 * `UWord` and `int` convert into one another in total silence, in both
 * directions - the two EXPECT_TRUE of TheBarePairReallyIsSilent below measure
 * exactly that and are the reason every EXPECT_FALSE here means something.
 * E4.1h had already measured the consequence on the outbound side: swapping
 * the two left WagoWire_test 31/31 GREEN with no warning at all.
 *
 * ⚠️ WHAT KIND OF ORACLE THIS IS - read this before quoting any result below.
 *
 * The cases named *Refuses* are a COMPILATION property reported through an
 * executable oracle: std::is_invocable_v turns "can a caller still hand this
 * slot, or this implementation, its arguments in the wrong order?" into a
 * value gtest can print. It proves a permuted call no longer TYPE-CHECKS. It
 * proves nothing about what any callback then does. That is the shape T3.31
 * used and T3.46 kept, and it must never be sold as a behavioural oracle.
 *
 * ⭐ WHAT IS NEW HERE, and it is the hole T3.46 declared and could not close
 * (T3.46.md section 7.11, second bullet): "the four implementations are probed
 * by no case at all". Two things close it on this half:
 *
 *   1. TheReadImplementationsThemselvesRefuseThePermutedOrder probes the
 *      MEMBER FUNCTIONS, not only the typedefs, through pointers to member.
 *      is_invocable_v is SFINAE-friendly, so a re-widened implementation makes
 *      this case go RED - it does not make the file stop compiling, which
 *      would turn the test into an ABSENT one instead of a red one.
 *      ⚠️ Two of the six are unreachable to any probe: WOAnalog::
 *      WagoReadCallback and WODigital::WagoReadCallback are PRIVATE, and a
 *      derived class cannot name a private member. For those two what closes
 *      the signature is the sigc::mem_fun registration inside their own .cpp
 *      (WOAnalog.cpp:57, WODigital.cpp:66): converting a mem_functor to a
 *      MultiBits_cb/MultiWords_cb requires the member to be callable with the
 *      typed argument list, so a re-widening there is a BUILD FAILURE. That is
 *      measured, mutation M5/M6 of T3.50.md section 7.3 - not assumed.
 *
 *   2. ⭐ AReadReplyReallyReachesTheImplementation EXERCISES one of the six for
 *      real: a production WIAnalog, built by the production constructor, is
 *      handed a reply and its value is read back through the public
 *      get_value_double(). T3.46 could not do this and said so. It is a
 *      behavioural non-regression witness across the typing, nothing more.
 *
 * ⛔ AND THE DISTINCTION THAT DECIDES WHAT MAY BE ASKED OF A TEST HERE.
 * Measured on this tree, by reading the six bodies AND by asking the compiler
 * (-Wunused-parameter, T3.50.md section 7.2): NOT ONE of the six read
 * implementations uses `address` or `count`. They read `status` and `values`
 * and nothing else. Consequences, both of which are written down rather than
 * rounded off:
 *
 *   - Permuting the two parameters of any implementation is SEMANTICALLY A
 *     NO-OP. The two programs are the same program and no behavioural test
 *     could ever separate them - demanding a red one would be demanding that
 *     a test tell two identical programs apart. ThePermutedReplyIsIndistin-
 *     guishableAtRuntime measures that, so the claim is not a reading.
 *   - The same holds at the four EMISSION sites (WagoMap.cpp:214, :225, :238,
 *     :249). Both values there are LIVE - decoded from the PLC reply at
 *     WagoMap.cpp:204-205, neither is a literal, which is the respect in which
 *     this half really is worse than the write half T3.46 closed - but since
 *     every receiver ignores both, a permutation today changes no observable
 *     behaviour either.
 *   ⚠️ This INFIRMS T3.50.md section 1 as first written ("une permutation
 *     produit une lecture fausse a une adresse fausse" / "count gouverne la
 *     taille du vecteur lu"). At the reply sites the vector is built from the
 *     JSON "values" array, never from `count`. The fiche is corrected.
 *
 * ⇒ What the typing closes here is a CONTRACT - for the next implementation
 *   that does read its address, and for the next emission site - not a live
 *   wrong answer. Said plainly, because the fiche said the opposite.
 *
 * ⚠️ WHAT THIS DOES NOT CLOSE, declared and not promised:
 *   W3 - wrapping the WRONG variable. WagoTypes::Address(count) type-checks
 *        at WagoMap.cpp and always will. Intrinsic to wrapping.
 *   W7 - unwrapping into an untyped layer: `int n = addr.v;` inside an
 *        implementation type-checks. One line wherever the wrapper is undone.
 *   F-WAGO-7 - `UWord address; int count;` left UNINITIALISED in the
 *        calaos_wago dispatcher. Same pair of values, different defect
 *        (initialisation, not signature), different file. Still open.
 *--------------------------------------------------------------------------*/

#include "CalaosCoreFixture.h"
#include "IOFactory.h"
#include "WagoMap.h"
#include "WagoTypes.h"
#include "WIAnalog.h"
#include "WITemp.h"
#include "WIDigitalBP.h"

#include <gtest/gtest.h>
#include <type_traits>
#include <vector>

using Calaos::MultiBits_cb;
using Calaos::MultiWords_cb;
using WagoTypes::Address;
using WagoTypes::Count;

namespace
{

/* ⭐ The four read implementations a probe can legally name. Each `using`
 * re-exports a PROTECTED member so &Probe::WagoReadCallback is well formed;
 * none of these probes changes anything else, and only the WIAnalog one is
 * ever instantiated. */
class T350WagoMapProbe: public Calaos::WagoMap
{
public:
    using Calaos::WagoMap::WagoModbusReadHeartbeatCallback;
};

class T350WIAnalogProbe: public Calaos::WIAnalog
{
public:
    T350WIAnalogProbe(Params &p): Calaos::WIAnalog(p) {}
    using Calaos::WIAnalog::WagoReadCallback;
};

class T350WITempProbe: public Calaos::WITemp
{
public:
    using Calaos::WITemp::WagoReadCallback;
};

class T350WIDigitalProbe: public Calaos::WIDigitalBP
{
public:
    using Calaos::WIDigitalBP::WagoReadCallback;
};

using HeartbeatFn = decltype(&T350WagoMapProbe::WagoModbusReadHeartbeatCallback);
using WIAnalogFn = decltype(&T350WIAnalogProbe::WagoReadCallback);
using WITempFn = decltype(&T350WITempProbe::WagoReadCallback);
using WIDigitalFn = decltype(&T350WIDigitalProbe::WagoReadCallback);

} //namespace

/*----------------------------------------------------------------------------
 * The control that gives every EXPECT_FALSE below its meaning.
 *--------------------------------------------------------------------------*/

TEST(WagoReadReply, TheBarePairReallyIsSilent)
{
    //Nothing in the untyped signature separates an address from a count:
    //the two types convert into one another, both ways, no diagnostic.
    EXPECT_TRUE((std::is_convertible_v<UWord, int>));
    EXPECT_TRUE((std::is_convertible_v<int, UWord>));

    //And the wrappers are what stops it. Without these two lines the two
    //above would just be trivia.
    EXPECT_FALSE((std::is_convertible_v<Address, Count>));
    EXPECT_FALSE((std::is_convertible_v<Count, Address>));
}

/*----------------------------------------------------------------------------
 * The two typedefs.
 *--------------------------------------------------------------------------*/

TEST(WagoReadReply, TheMultiBitsReplyRefusesABareAddressAndCountPair)
{
    std::vector<bool> bits;
    (void)bits;

    EXPECT_FALSE((std::is_invocable_v<MultiBits_cb, bool, UWord, int,
                                      std::vector<bool> &>))
        << "MultiBits_cb still takes a bare UWord and a bare int: at "
           "WagoMap.cpp:214 and :225 the address and the count of a read "
           "reply are interchangeable in total silence";

    EXPECT_FALSE((std::is_invocable_v<MultiBits_cb, bool, int, UWord,
                                      std::vector<bool> &>))
        << "the permuted spelling of the same list still type-checks";

    //The counterpart. Without it a slot that accepted NOTHING would pass the
    //two cases above for free.
    EXPECT_TRUE((std::is_invocable_v<MultiBits_cb, bool, Address, Count,
                                     std::vector<bool> &>))
        << "the correctly ordered typed call must still be accepted";

    //⭐ And the permutation OF THE TYPED FORM, which is the whole point.
    EXPECT_FALSE((std::is_invocable_v<MultiBits_cb, bool, Count, Address,
                                      std::vector<bool> &>))
        << "address and count are still interchangeable once typed";
}

TEST(WagoReadReply, TheMultiWordsReplyRefusesABareAddressAndCountPair)
{
    EXPECT_FALSE((std::is_invocable_v<MultiWords_cb, bool, UWord, int,
                                      std::vector<UWord> &>))
        << "MultiWords_cb still takes a bare UWord and a bare int: at "
           "WagoMap.cpp:238 and :249 the address and the count of a read "
           "reply are interchangeable in total silence";

    EXPECT_FALSE((std::is_invocable_v<MultiWords_cb, bool, int, UWord,
                                      std::vector<UWord> &>))
        << "the permuted spelling of the same list still type-checks";

    EXPECT_TRUE((std::is_invocable_v<MultiWords_cb, bool, Address, Count,
                                     std::vector<UWord> &>))
        << "the correctly ordered typed call must still be accepted";

    EXPECT_FALSE((std::is_invocable_v<MultiWords_cb, bool, Count, Address,
                                      std::vector<UWord> &>))
        << "address and count are still interchangeable once typed";
}

/*----------------------------------------------------------------------------
 * ⭐ THE IMPLEMENTATIONS THEMSELVES - the hole T3.46 named and left open.
 *--------------------------------------------------------------------------*/

TEST(WagoReadReply, TheReadImplementationsThemselvesRefuseThePermutedOrder)
{
    /* WagoMap::WagoModbusReadHeartbeatCallback - WagoMap.cpp:172 */
    EXPECT_TRUE((std::is_invocable_v<HeartbeatFn, T350WagoMapProbe *, bool,
                                     Address, Count, std::vector<bool> &>))
        << "the heartbeat callback no longer accepts a correctly ordered "
           "typed call - every EXPECT_FALSE around it is passing for free";
    EXPECT_FALSE((std::is_invocable_v<HeartbeatFn, T350WagoMapProbe *, bool,
                                      Count, Address, std::vector<bool> &>));
    EXPECT_FALSE((std::is_invocable_v<HeartbeatFn, T350WagoMapProbe *, bool,
                                      UWord, int, std::vector<bool> &>))
        << "WagoMap::WagoModbusReadHeartbeatCallback still takes the bare "
           "(UWord, int) pair";

    /* WIAnalog::WagoReadCallback - WIAnalog.cpp:72 */
    EXPECT_TRUE((std::is_invocable_v<WIAnalogFn, T350WIAnalogProbe *, bool,
                                     Address, Count, std::vector<UWord> &>));
    EXPECT_FALSE((std::is_invocable_v<WIAnalogFn, T350WIAnalogProbe *, bool,
                                      Count, Address, std::vector<UWord> &>));
    EXPECT_FALSE((std::is_invocable_v<WIAnalogFn, T350WIAnalogProbe *, bool,
                                      UWord, int, std::vector<UWord> &>))
        << "WIAnalog::WagoReadCallback still takes the bare (UWord, int) pair";

    /* WITemp::WagoReadCallback - WITemp.cpp:68 */
    EXPECT_TRUE((std::is_invocable_v<WITempFn, T350WITempProbe *, bool,
                                     Address, Count, std::vector<UWord> &>));
    EXPECT_FALSE((std::is_invocable_v<WITempFn, T350WITempProbe *, bool,
                                      Count, Address, std::vector<UWord> &>));
    EXPECT_FALSE((std::is_invocable_v<WITempFn, T350WITempProbe *, bool,
                                      UWord, int, std::vector<UWord> &>))
        << "WITemp::WagoReadCallback still takes the bare (UWord, int) pair";

    /* WIDigitalBase<Base>::WagoReadCallback - WagoIOBase.h:104, probed on the
     * WIDigitalBP instantiation, which is one of its three. */
    EXPECT_TRUE((std::is_invocable_v<WIDigitalFn, T350WIDigitalProbe *, bool,
                                     Address, Count, std::vector<bool> &>));
    EXPECT_FALSE((std::is_invocable_v<WIDigitalFn, T350WIDigitalProbe *, bool,
                                      Count, Address, std::vector<bool> &>));
    EXPECT_FALSE((std::is_invocable_v<WIDigitalFn, T350WIDigitalProbe *, bool,
                                      UWord, int, std::vector<bool> &>))
        << "WIDigitalBase::WagoReadCallback still takes the bare (UWord, int) "
           "pair";
}

/*----------------------------------------------------------------------------
 * The shape of the wrappers, asked of the type system.
 *--------------------------------------------------------------------------*/

namespace
{
/* Positive control for is_base_of_v: a trait that answered FALSE to
 * everything would let the W4 lines below pass for free. */
struct AProbeBase { };
struct AProbeDerived: AProbeBase { };
} //namespace

TEST(WagoReadReply, TheWrapperShapeIsWhatCloses)
{
    //W1 - explicit constructors, so a bare scalar is not an Address/Count.
    EXPECT_FALSE((std::is_convertible_v<UWord, Address>));
    EXPECT_FALSE((std::is_convertible_v<int, Count>));
    EXPECT_TRUE((std::is_constructible_v<Address, UWord>));
    EXPECT_TRUE((std::is_constructible_v<Count, int>));

    //W4 - no common base, so no sibling stands in for another.
    EXPECT_FALSE((std::is_base_of_v<Address, Count>));
    EXPECT_FALSE((std::is_base_of_v<Count, Address>));
    EXPECT_TRUE((std::is_base_of_v<AProbeBase, AProbeDerived>))
        << "is_base_of_v answers FALSE for everything here - the two W4 lines "
           "above are passing for free and prove nothing";

    //W6 - and no way back to the raw scalar without naming .v.
    EXPECT_FALSE((std::is_convertible_v<Address, UWord>));
    EXPECT_FALSE((std::is_convertible_v<Count, int>));
    //⚠️ Count wraps a SIGNED int on purpose (WagoTypes.h): it arrives from the
    //JSON and may be negative. The wrapper does not validate, it only stops
    //the value being mistaken for an address.
    EXPECT_TRUE((std::is_same_v<decltype(Count(-1).v), int>));
}

namespace
{

/* Witnesses for the two BLIND SPOTS of this kind of probe, both measured by
 * T3.31 (F-TYPE-5) and re-armed here rather than trusted from a note. */

struct Payload { int v; explicit Payload(int a): v(a) {} };
struct Slot { int v; explicit Slot(int a): v(a) {} };

/* Blind spot 1 - a NON-CONST LVALUE REFERENCE parameter. is_invocable_v with
 * prvalue arguments answers FALSE even for the CORRECT order, so a signature
 * written that way would make every probe above pass for the wrong reason.
 *
 * ⚠️ T3.46 measured (mutation MXD) that sigc++ REFUSES a non-const lvalue
 * reference in SingleWord_cb outright: rc=2, "cannot bind non-const lvalue
 * reference ... to an rvalue". ⭐ THAT RESULT DOES NOT TRANSFER HERE UNREAD,
 * and T3.50 re-measured it rather than citing it - the reason is visible in
 * the signature itself: MultiBits_cb/MultiWords_cb ALREADY carry a non-const
 * lvalue reference, `vector<bool> &`, and always did. sigc++ accepts it
 * because the emission sites pass a NAMED LOCAL (values_bits, values_words),
 * an lvalue. What decides is what the emission site passes, not the slot. The
 * address and the count ARE passed as prvalues once wrapped
 * (WagoTypes::Address(address)), so on THOSE two parameters the refusal does
 * hold - measured as mutation MXD-R, T3.50.md section 7.3. Stated with its
 * exact scope, because "sigc++ refuses references" is false as a general
 * sentence about these two typedefs. */
inline void takesLvalueRefs(Payload &, Slot &) { }

/* Blind spot 2 - NARROWING. Only is_convertible_v answers the question that
 * matters; is_constructible_v cannot tell an explicit wrapper from an
 * implicit narrowing one. */
struct Narrowable { long v; Narrowable(long a): v(a) {} };

} //namespace

TEST(WagoReadReply, TheProbesActuallyDiscriminate)
{
    using Fn = decltype(&takesLvalueRefs);

    //A matching, correctly ordered list IS invocable - proved on the real
    //slots, not on a toy.
    EXPECT_TRUE((std::is_invocable_v<MultiWords_cb, bool, Address, Count,
                                     std::vector<UWord> &>));

    //Blind spot 1, as a live measurement.
    EXPECT_FALSE((std::is_invocable_v<Fn, Payload, Slot>))
        << "if this ever becomes TRUE the lvalue-reference blind spot is gone "
           "and this comment is stale";
    EXPECT_TRUE((std::is_invocable_v<Fn, Payload &, Slot &>));

    //And the witness that the 4th parameter of the two slots really is such a
    //reference: a prvalue vector is NOT accepted, an lvalue one is. This is
    //what the MXD note above is about.
    EXPECT_FALSE((std::is_invocable_v<MultiWords_cb, bool, Address, Count,
                                      std::vector<UWord>>));

    //Blind spot 2.
    EXPECT_TRUE((std::is_constructible_v<Narrowable, int>));
    EXPECT_TRUE((std::is_convertible_v<int, Narrowable>))
        << "only is_convertible_v separates an explicit wrapper from a "
           "narrowing implicit one - is_constructible_v answers TRUE for both";
    EXPECT_TRUE((std::is_constructible_v<Count, int>));
    EXPECT_FALSE((std::is_convertible_v<int, Count>));
}

/*----------------------------------------------------------------------------
 * ⭐ THE EXERCISE - a real implementation, actually run.
 *
 * T3.46 closed its four implementations by compilation alone and wrote that
 * down as strictly weaker than an executed oracle. On this half one of the six
 * IS reachable: WIAnalog is built by core/WagoPortDefault_test already, with a
 * TEST-NET-1 host so its WagoMap binds nothing and spawns nothing.
 *
 * ⚠️ These cases are BEHAVIOURAL and they are NOT about the permutation: the
 * six bodies ignore address and count, so no behavioural case can separate the
 * permuted program from the correct one (measured below). What they witness is
 * that the typed implementation still does what the untyped one did, and that
 * the callback is REACHED - "linked" is not "exercised", F-LINK-1.
 *--------------------------------------------------------------------------*/

namespace
{

/* Adapter, so this case COMPILES both before and after the typing. Without it
 * the exercise would be an ABSENT test on master, not a red one - and an
 * absent test is the worst of the twelve false greens. Exactly one of the two
 * overloads is viable at a time. */
template<class Obj>
auto callRead(Obj &o, bool status, UWord addr, int count,
              std::vector<UWord> &values)
    -> decltype(o.WagoReadCallback(status, addr, count, values))
{
    return o.WagoReadCallback(status, addr, count, values);
}

template<class Obj>
auto callRead(Obj &o, bool status, UWord addr, int count,
              std::vector<UWord> &values)
    -> decltype(o.WagoReadCallback(status, WagoTypes::Address(addr),
                                   WagoTypes::Count(count), values))
{
    return o.WagoReadCallback(status, WagoTypes::Address(addr),
                              WagoTypes::Count(count), values);
}

void registerT350WagoProbe()
{
    static bool done = false;
    if (done) return;
    done = true;

    Calaos::IOFactory::Instance().RegisterClass(
        "T350WIAnalogProbe",
        [](Params &p) -> Calaos::IOBase * { return new T350WIAnalogProbe(p); });
}

} //namespace

class WagoReadReplyExerciseTest: public CalaosTest::CoreFixture
{
protected:
    void SetUp() override
    {
        registerT350WagoProbe();
        CoreFixture::SetUp();
        loadConfig();
    }

    /* One host per case, TEST-NET-1 (RFC 5737): syntactically valid, never
     * local, so bind() fails with EADDRNOTAVAIL and this binary can never
     * collide with a parallel `make check -j` peer over WAGO_LISTEN_PORT.
     * Same reasoning as core/WagoPortDefault_test. */
    T350WIAnalogProbe *makeProbe(const std::string &hostSuffix,
                                 const std::string &id)
    {
        Params p = {{ "type", "T350WIAnalogProbe" },
                    { "id", id },
                    { "name", "T3.50 read reply probe" },
                    { "host", "192.0.2." + hostSuffix },
                    { "var", "12" },
                    { "enabled", "true" },
                    { "visible", "true" }};
        return dynamic_cast<T350WIAnalogProbe *>(createIO(p));
    }
};

/* ⭐ The implementation is REACHED and does its job. Values are deliberately
 * far apart and neither is small: an address of 40001 cannot be mistaken for a
 * count of 3, and 4321 cannot be mistaken for either. A fixture where the
 * three numbers are close, or all small, makes a permutation invisible. */
TEST_F(WagoReadReplyExerciseTest, AReadReplyReallyReachesTheImplementation)
{
    T350WIAnalogProbe *probe = makeProbe("41", "t350_read_reply");
    ASSERT_NE(nullptr, probe);

    //Sentinel, never 0: reading an uninitialised value that happened to be 0
    //would make an expectation of 0 pass for the wrong reason.
    ASSERT_NE(4321.0, probe->get_value_double());

    std::vector<UWord> values = { 4321 };
    callRead(*probe, true, 40001, 3, values);

    EXPECT_DOUBLE_EQ(4321.0, probe->get_value_double())
        << "WIAnalog::WagoReadCallback did not carry the reply through - it is "
           "linked but not exercised, which is exactly what F-LINK-1 warns "
           "about";
}

/* The control. Without it "the value became 4321" could be satisfied by an
 * implementation that ignores `status` entirely and always assigns. */
TEST_F(WagoReadReplyExerciseTest, AFailedReadReplyChangesNothing)
{
    T350WIAnalogProbe *probe = makeProbe("42", "t350_read_reply_failed");
    ASSERT_NE(nullptr, probe);

    const double before = probe->get_value_double();

    std::vector<UWord> values = { 4321 };
    callRead(*probe, false, 40001, 3, values);

    EXPECT_DOUBLE_EQ(before, probe->get_value_double())
        << "a failed read reply must not move the value";
}

/* ⭐ THE MEASUREMENT BEHIND THE HONEST CLAIM, and it is uncomfortable: the
 * permuted call produces THE SAME OBSERVABLE RESULT. `address` and `count` are
 * unused by all six implementations, so the two programs are the same program.
 * This case exists so that nobody later "fixes" the suite by demanding a red
 * behavioural test for the permutation - there is none to be had, and asking
 * for one is asking a test to tell two identical programs apart. What the
 * typing closes is the CONTRACT, and the contract is closed by the
 * *Refuses* cases above, at compile time. */
TEST_F(WagoReadReplyExerciseTest, ThePermutedReplyIsIndistinguishableAtRuntime)
{
    T350WIAnalogProbe *probe = makeProbe("43", "t350_read_reply_permuted");
    ASSERT_NE(nullptr, probe);

    std::vector<UWord> values = { 4321 };
    //The address and the count handed over the wrong way round.
    callRead(*probe, true, 3, 40001, values);

    EXPECT_DOUBLE_EQ(4321.0, probe->get_value_double())
        << "if this ever fails, one of the six implementations started reading "
           "its address or its count - the no-op note at the top of this file "
           "is then stale and a behavioural oracle becomes possible";
}
