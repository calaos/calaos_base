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
#ifndef S_AutoScenarioDef_H
#define S_AutoScenarioDef_H

#include <string>
#include <vector>

#include "Params.h"

namespace Calaos
{

/* E4.6b - THE AUTO SCENARIO DEFINITION, and its encoding into the params of
 * the Scenario IO (docs/refactoring/E4.6.md, D1 to D4).
 *
 * ---------------------------------------------------------------------------
 * WHY THIS TYPE EXISTS
 * ---------------------------------------------------------------------------
 * Today an auto scenario is not stored anywhere: it is a projection
 * RE-INFERRED from rules.xml at every reconstruction, by literal pattern
 * matching (AutoScenario::checkScenarioRules()). One changed character in
 * rules.xml costs a whole step, permanently (E4.6.md RC1), and an installer
 * round trip silently amputates an action (RC9). AutoScenarioDef is the
 * declared datum the rules become a projection OF; E4.6c turns AutoScenario
 * into the generator that rebuilds them from here.
 *
 * ---------------------------------------------------------------------------
 * WHERE IT IS PERSISTED, AND WHY THERE - decision Q4, DO NOT RE-OPEN
 * ---------------------------------------------------------------------------
 * In io.xml, carried by the PARAMS of the Scenario IO. Two separate rulings:
 *
 *   - the FILE is a user decision (E4.6.md §1.3): all of Calaos - backup,
 *     download/upload by calaos_installer, restore procedures - turns around
 *     io.xml and rules.xml. A third file would not have removed the risk of
 *     loss, it would have MOVED it into every tool that forgets it exists.
 *     JsonApiHandlerHttp.cpp:631-633 would have rejected the upload anyway.
 *   - the CARRIER is a measurement (E4.6.md D2). calaos_installer regenerates
 *     io.xml in full at every save, and of what it does not model it keeps
 *     ATTRIBUTES verbatim (projectmanager.cpp:611-618 reading, :197-204
 *     writing) while it DROPS child nodes (:653-658, and writeInput() emits
 *     none). Params therefore survive an installer that knows nothing about
 *     them - including the OLD installers that no update will ever reach.
 *
 * ---------------------------------------------------------------------------
 * THE ENCODING
 * ---------------------------------------------------------------------------
 *   autoscenario_uid       = "as_0"          the marker, opaque, never reused
 *   autoscenario_schema    = "1"             format version (see below)
 *   autoscenario_cycle     = "true"|"false"
 *   autoscenario_enabled   = "true"|"false"  the old `disabled`, inverted once
 *   autoscenario_schedule  = "io_204"        omitted when not scheduled
 *   autoscenario_steps     = "s1|s2"         ORDER AND IDENTITY, authoritative
 *   as_s1_pause            = "1.5"
 *   as_s1_actions          = "io_77=up|io_78=down"
 *   as_final_actions       = "io_9=false"    omitted when the final step is empty
 *
 * ONE PARAM PER STEP, not one JSON blob, for three reasons in this order:
 * a corrupt step does not take the definition down with it; io.xml is read by
 * humans in a breakdown and a JSON blob in an XML attribute is a porridge of
 * &quot;; and every line stays short.
 *
 * `autoscenario_steps` IS THE ONE AUTHORITATIVE KEY. A step it does not name
 * is not loaded, whatever `as_*` params exist for it, and those params are
 * REMOVED at the next save. That is the only automatic clean up of the whole
 * epic and it only ever touches our OWN namespaces (E4.6.md D10, requirement
 * 4): a sweep that "tidies up" the configuration at startup is exactly the
 * mechanism that destroys user data in silence.
 *
 * ---------------------------------------------------------------------------
 * PERCENT-ENCODING - '%', '|' AND '='
 * ---------------------------------------------------------------------------
 * '|' separates elements and '=' separates an id from its value. IO ids are
 * safe by construction ("io_N", "input_N") but ACTION VALUES ARE NOT: they are
 * arbitrary user text coming from ActionStd::get_params(). So '%' (first, it
 * is the escape), '|' and '=' are percent-encoded in both the ids and the
 * values of an action list. Invisible in the ordinary case, correct in the
 * rare one. XML escaping (&, <, >, ") is pugixml's business on the server and
 * QXmlStreamWriter's in the installer; it is not repeated here.
 *
 * NORMALIZATION, stated so nobody has to discover it: decoding accepts a '%'
 * that is NOT followed by two hex digits as a literal '%', and decodes
 * "%41" to "A". Both come back re-encoded canonically ("%25", "A"). The round
 * trip is therefore BYTE EXACT on canonical params - the ones this codec
 * writes, which is the criterion of E4.6.md §6 - and IDEMPOTENT after one pass
 * on hand written ones.
 *
 * ---------------------------------------------------------------------------
 * IDS
 * ---------------------------------------------------------------------------
 * `uid` and `stepId` are OPAQUE and NEVER RECYCLED (D3): the allocators are
 * process wide monotonic counters that are also bumped past every id they are
 * ever shown, so an id freed by a deletion is never handed out again. Nothing
 * anywhere may derive meaning from their shape.
 *
 * Step ids are restricted to [A-Za-z0-9_] BY CONSTRUCTION, because a step id
 * is also a fragment of the param name that carries the step. They are
 * therefore NOT percent-encoded (it would be a no-op), and a step token of
 * `autoscenario_steps` outside that alphabet is refused at load - its params
 * become orphans and go at the next save.
 *
 * ---------------------------------------------------------------------------
 * ACTIONS ARE IDS, NEVER POINTERS (D4)
 * ---------------------------------------------------------------------------
 * An Action carries an `ioId` string. This codec NEVER resolves it, so an
 * action whose IO does not exist is loaded, kept and written back unchanged.
 * That is the whole point: today toJson() drops it (IO/Scenario.cpp:158, :181)
 * and a client that reads back and echoes loses it for ever (RC3).
 */

class AutoScenarioDefAction
{
public:
    //An IO id. NEVER resolved here: an id that no longer names an IO is kept.
    std::string ioId;
    //Arbitrary user text. May contain '%', '|' and '='.
    std::string value;
};

class AutoScenarioDefStep
{
public:
    //Opaque, stable, never reused. Empty on the final step, which has no id.
    std::string stepId;
    double pause = 0.0;
    std::vector<AutoScenarioDefAction> actions;
};

class AutoScenarioDef
{
public:
    /* ---------------------------------------------------------------------
     * Param keys. Public because the tests and IO/Scenario.cpp name them, and
     * because a caller must be able to ask "is this key mine?" without
     * hardcoding a prefix a second time.
     * ------------------------------------------------------------------ */
    static const char *const KEY_UID;
    static const char *const KEY_SCHEMA;
    static const char *const KEY_CYCLE;
    static const char *const KEY_ENABLED;
    static const char *const KEY_SCHEDULE;
    static const char *const KEY_STEPS;

    //Prefix of the per step params, and the token of the final step
    static const char *const STEP_PREFIX;      // "as_"
    static const char *const PAUSE_SUFFIX;     // "_pause"
    static const char *const ACTIONS_SUFFIX;   // "_actions"
    static const char *const FINAL_TOKEN;      // "final"

    //Format version written into KEY_SCHEMA. Foresight, not a defence: with
    //the params carrier there is nothing an old installer can lose (D10
    //level 4), but a future format change has to be able to tell itself apart.
    static const char *const SCHEMA_VERSION;

    /* ---------------------------------------------------------------------
     * The codec, both halves, and they are each other's inverse.
     * ------------------------------------------------------------------ */

    //Percent-encode '%', '|' and '='. Every other byte passes through.
    static std::string encode(const std::string &raw);
    //The inverse. A '%' not followed by two hex digits is a literal '%'.
    static std::string decode(const std::string &encoded);

    //True for a param key of the two namespaces this class owns. FALSE for
    //the legacy marker `auto_scenario`, which is in NEITHER of them and which
    //nothing here may ever touch: it is what keeps the orphan sweep of
    //ListeRoom.cpp:324 from destroying the rules of an existing scenario.
    static bool isDefinitionParam(const std::string &key);

    //A step id is also a param name fragment: [A-Za-z0-9_], never empty.
    static bool isValidStepId(const std::string &id);

    /* Allocators. Monotonic, process wide, never recycling. observe*() is how
     * an id read from a configuration file gets folded into the counter, so a
     * uid already in io.xml can never be handed out a second time.
     */
    static std::string newUid();
    static std::string newStepId();
    static void observeUid(const std::string &uid);
    static void observeStepId(const std::string &stepId);

    /* Back to the state a freshly started process is in. The counters are
     * process wide and never recycle, so a harness that drops the whole
     * configuration and loads another one many times in one process has to say
     * so, or the ids it hands out depend on what ran before.
     * Loading folds every id it reads back into the counters, so this can
     * never make an existing id be handed out twice.
     * ForTests, and it is not decoration: production has exactly one way to
     * get here, a restart, and a production caller resetting the counters
     * under a live configuration is what makes an opaque id ambiguous.
     */
    static void resetIdAllocatorsForTests();

    /* Load from the params of a Scenario IO. Answers false - and leaves the
     * definition untouched - when KEY_UID is absent, i.e. when this IO carries
     * no definition at all.
     * `autoscenario_steps` is authoritative: steps are loaded in ITS order,
     * duplicates and invalid tokens are refused, and an `as_*` param it does
     * not name is ignored.
     */
    bool loadFromParams(const Params &p);

    /* Write the definition back into the params, and REMOVE every param of the
     * two owned namespaces that this definition does not produce (the orphan
     * `as_*` clean up). A no-op, writing and removing NOTHING, when the
     * definition carries no uid: a Scenario IO that is not an auto scenario
     * must come out of a save byte for byte the file it went in as.
     */
    void saveToParams(Params &p) const;

    bool isDefined() const { return !uid.empty(); }
    void clear();

    //Serialize/parse one action list, the "io_1=a|io_2=b" half of the codec.
    static std::string encodeActions(const std::vector<AutoScenarioDefAction> &actions);
    static std::vector<AutoScenarioDefAction> decodeActions(const std::string &encoded);

    std::string uid;
    bool cycle = false;
    bool enabled = true;
    //Id of the time range IO, empty when the scenario is not scheduled.
    std::string scheduleIoId;
    std::vector<AutoScenarioDefStep> steps;
    AutoScenarioDefStep finalStep;
};

}

#endif
