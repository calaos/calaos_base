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
#ifndef S_CONFIGOPTIONS_H
#define S_CONFIGOPTIONS_H

#include <string>
#include <utility>
#include <vector>

/* Central registry of every option of local_config.xml.
 * =====================================================
 *
 * local_config.xml has no schema: its keys are string literals scattered over
 * some thirty files. This registry is the single source of truth describing
 * them -- type, default, documentation, consumer -- and feeds the calaos_config
 * TUI, the calaos_config "describe"/"options" actions and the generated
 * docs/16_config_options.md page.
 *
 * i18n: the table is a function-local static, so it is built lazily. The
 * strings are nevertheless stored untranslated (N_() = gettext_noop) and _()
 * is applied inside the label()/doc() accessors. That is robust whatever the
 * moment the table is first touched, and lets the documentation be regenerated
 * in another locale without rebuilding the table.
 *
 * Consumers: local_config.xml is a shared file. calaos_server is not the only
 * reader; Calaos Home (calaos_mobile built as CALAOS_DESKTOP) reads and writes
 * the very same file, and the calaos_mcp Python sidecar reads a few keys of its
 * own. "Not referenced in calaos_base" therefore does not mean "dead key",
 * hence the consumer() bitmask.
 */

namespace Calaos
{

class ConfigOption
{
public:
    enum class Type { String, Bool, Int, Float, Port, Host, Path, Email, EmailList,
                      Password, Token, Enum, Csv };

    enum class Category { Auth, Network, Logging, Smtp, Notifications, InfluxDb,
                          History, Location, Mcp, Ota,
                          CalaosHome,   //touch screen: cursor, DPMS, language, server
                          Unknown };    //present in the file, absent from the registry

    //Who consumes the key -- bitmask, a key can be shared
    enum Consumer { Server = 1, CalaosHome = 2, McpSidecar = 4 };

    //Enum values: raw key -> untranslated human label
    typedef std::vector<std::pair<std::string, const char *>> ValueList;

    ConfigOption(std::string k, Category cat, Type t);

    //--- Fluent setters, they all return *this -----------------------------
    ConfigOption &label(const char *l);            //short label, N_()
    ConfigOption &doc(const char *d);              //long description, N_()
    ConfigOption &def(const std::string &v);       //literal default
    ConfigOption &defDynamic(const char *human);   //computed default, ex. "<data dir>/app"
    ConfigOption &range(double minValue, double maxValue);
    //Enum: the list of accepted values. Also usable on a Bool to declare the
    //extra spellings the reading code really accepts (see ota_enabled).
    ConfigOption &values(ValueList kv);
    ConfigOption &pattern(const char *regex);      //also feeds cpp-tui Input::set_regex
    ConfigOption &example(const char *e);
    ConfigOption &consumer(int mask);              //default: Server
    ConfigOption &restartRequired();
    ConfigOption &generated();                     //mcp_token & co: auto-generated at first boot
    ConfigOption &secret();                        //masked when displayed and in diffs
    ConfigOption &confirmReset();                  //reset to default must be confirmed
    ConfigOption &deprecated(const char *replacement = nullptr);
    ConfigOption &advanced();                      //hidden behind "show advanced"
    ConfigOption &seeAlso(const char *otherKey);   //user_emails <-> notif/mail_recipients

    //--- Read accessors ----------------------------------------------------
    const std::string &key() const { return m_key; }
    Category category() const { return m_category; }
    Type type() const { return m_type; }

    //Text accessors apply _() (gettext) at call time
    std::string label() const;
    std::string doc() const;

    bool hasDef() const { return m_hasDef; }
    const std::string &def() const { return m_def; }
    //Human description of a default the code computes at runtime, translated.
    //Empty when the option has no dynamic default.
    std::string defDynamic() const;

    bool hasRange() const { return m_hasRange; }
    double rangeMin() const { return m_min; }
    double rangeMax() const { return m_max; }

    const ValueList &values() const { return m_values; }
    //Translated label of a raw enum value, empty when the value is not listed
    std::string valueLabel(const std::string &value) const;

    const char *pattern() const { return m_pattern; }
    const char *example() const { return m_example; }

    int consumer() const { return m_consumer; }
    bool isRestartRequired() const { return m_restartRequired; }
    bool isGenerated() const { return m_generated; }
    bool isSecret() const { return m_secret; }
    bool isConfirmReset() const { return m_confirmReset; }
    bool isDeprecated() const { return m_deprecated; }
    bool isAdvanced() const { return m_advanced; }
    //Replacement key of a deprecated option, empty when there is none
    const std::string &replacement() const { return m_replacement; }
    const std::vector<std::string> &seeAlso() const { return m_seeAlso; }

    //--- Behaviour ---------------------------------------------------------
    /* An empty value is always accepted: it means "key unset", and every
     * reading site of calaos_base falls back to its own default in that case.
     * error is filled with a translated, actionable message when the value is
     * refused; it may be null.
     */
    bool validate(const std::string &value, std::string *error = nullptr) const;

    /* Value as it should be shown to a human: "Yes"/"No" for a boolean, the
     * human label for an enum, a row of bullets for a secret. An empty raw
     * value gives an empty string, callers decide how to show "unset".
     */
    std::string displayValue(const std::string &raw) const;

private:
    std::string m_key;
    Category m_category;
    Type m_type;

    const char *m_label = nullptr;
    const char *m_doc = nullptr;
    const char *m_defDynamic = nullptr;
    const char *m_pattern = nullptr;
    const char *m_example = nullptr;

    std::string m_def;
    bool m_hasDef = false;

    double m_min = 0.0;
    double m_max = 0.0;
    bool m_hasRange = false;

    ValueList m_values;

    int m_consumer = Server;
    bool m_restartRequired = false;
    bool m_generated = false;
    bool m_secret = false;
    bool m_confirmReset = false;
    bool m_deprecated = false;
    bool m_advanced = false;
    std::string m_replacement;
    std::vector<std::string> m_seeAlso;
};

class ConfigOptions
{
public:
    //The whole table, in display order. Meyers singleton, lazily built.
    static const std::vector<ConfigOption> &all();
    static const ConfigOption *find(const std::string &key);
    static std::vector<const ConfigOption *> byCategory(ConfigOption::Category c);
    //Every category, in display order, Unknown included
    static std::vector<ConfigOption::Category> categories();
    static std::string categoryLabel(ConfigOption::Category c);
    static std::string typeLabel(ConfigOption::Type t);
    //Translated "used by" description of a consumer bitmask
    static std::string consumerLabel(int mask);
    //"did you mean ...?" -- closest key within a small edit distance, or empty
    static std::string suggest(const std::string &unknownKey);

    /* Dead keys. They are deliberately absent from all(): they only exist here
     * so that the tools can recognise and remove them. Nothing is ever purged
     * implicitly, the caller decides.
     */
    static const std::vector<std::string> &obsoleteKeys();
    static bool isObsolete(const std::string &key);
    //Obsolete keys really present in local_config.xml right now
    static std::vector<std::string> obsoletePresent();

    /* Removes from local_config.xml the obsolete keys that are present, in a
     * single atomic write, and never touches any other key. Returns true when
     * the file is free of obsolete keys afterwards -- including when there was
     * nothing to remove -- and false when a needed write could not be done.
     * An unwritable config (read-only rootfs) is a silent no-op returning
     * false. This function never throws.
     */
    static bool purgeObsolete(std::vector<std::string> *removed = nullptr);

    static std::string genMarkdown();   //-> docs/16_config_options.md
    static std::string genJson();       //machine readable, like io_doc.json
};

}

#endif /* S_CONFIGOPTIONS_H */
