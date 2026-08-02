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

//
// Write/Read config options from local_config.xml
//

#include <Utils.h>
#include <ConfigOptions.h>

#include "ConfigCliOutput.h"
#include "config_tui/ConfigTui.h"

#include <unistd.h>

using namespace Calaos;

/* Output discipline of this tool
 * ==============================
 *
 * "get" and "list" are consumed by scripts: their stdout is a stable
 * interface and must not change. Every diagnostic therefore goes to stderr
 * with a plain std::cerr, not through the logger: Logger writes to stdout
 * (Logger.cpp:172), so a single cErrorDom() here would corrupt the output of
 * "calaos_config get".
 */

namespace
{

//Parsed command line
struct CliArgs
{
    std::string action;
    std::vector<std::string> params;   //positional parameters of the action

    char *configDir = nullptr;
    char *cacheDir = nullptr;
    ConfigCli::ColorMode color = ConfigCli::ColorMode::Auto;

    //Frames of the interactive browser only: no other action draws a box, and
    //none of them may change a byte because of this option.
    TuiFrameMode frames = TuiFrameMode::Auto;

    bool all = false;
    bool json = false;
    bool markdown = false;
    bool force = false;
    bool dryRun = false;
    bool help = false;
    bool version = false;

    int consumerMask = 0;
};

void printUsage(std::ostream &out)
{
    out << _("Calaos Configuration Utility.") << std::endl;
    out << CALAOS_COPYRIGHT_TEXT << std::endl << std::endl;
    out << _("Usage:\tcalaos_config [global options] <action> [params]") << std::endl << std::endl;

    out << _("Global options:") << std::endl;
    out << _("\t--config <dir>\t\tDirectory holding local_config.xml") << std::endl;
    out << _("\t--cache <dir>\t\tDirectory holding the cache files") << std::endl;
    out << _("\t--color=<when>\t\tColorize the output: auto (default), always or never") << std::endl;
    out << _("\t--frames=<what>\t\tFrames of the interactive browser: auto (default),") << std::endl;
    out << _("\t\t\t\tunicode or ascii. auto draws box drawing characters unless") << std::endl;
    out << _("\t\t\t\tthe locale says the terminal is not UTF-8") << std::endl;
    out << _("\t-h, --help\t\tDisplay this help") << std::endl;
    out << _("\t--version\t\tDisplay the version") << std::endl << std::endl;

    out << _("Where action can be:") << std::endl;
    out << _("\ttui\t\t\tOpen the interactive configuration browser") << std::endl;
    out << _("\tlist [--all] [--json]\tLists all keys:values") << std::endl;
    out << _("\t\t\t\t--all also lists the documented options that are not set") << std::endl;
    out << _("\tget <key>\t\tPrint the value for the specified key") << std::endl;
    out << _("\tset <key> <value> [--force]") << std::endl;
    out << _("\t\t\t\tSet the value for the specified key") << std::endl;
    out << _("\t\t\t\t--force skips the validation of the value") << std::endl;
    out << _("\tdel <key>\t\tDelete entry for the specified key") << std::endl;
    out << _("\tpurge [--dry-run]\tRemove the obsolete keys that are still present") << std::endl;
    out << _("\tdescribe <key>\t\tFull documentation of one option") << std::endl;
    out << _("\toptions [--json|--markdown] [--consumer=server|calaos_home|mcp]") << std::endl;
    out << _("\t\t\t\tDocumentation of every known option") << std::endl << std::endl;

    out << _("Run calaos_config without any argument in a terminal to open the "
             "interactive browser.") << std::endl;
}

void printVersion()
{
    std::cout << _("Calaos Configuration Utility.") << std::endl;
    std::cout << PACKAGE_STRING << std::endl;
    std::cout << CALAOS_COPYRIGHT_TEXT << std::endl;
}

bool parseColorMode(const std::string &value, ConfigCli::ColorMode &mode)
{
    if (value == "auto") mode = ConfigCli::ColorMode::Auto;
    else if (value == "always" || value == "yes" || value == "force") mode = ConfigCli::ColorMode::Always;
    else if (value == "never" || value == "no" || value == "none") mode = ConfigCli::ColorMode::Never;
    else return false;

    return true;
}

bool parseFrameMode(const std::string &value, TuiFrameMode &mode)
{
    if (value == "auto") mode = TuiFrameMode::Auto;
    else if (value == "unicode" || value == "utf8" || value == "utf-8") mode = TuiFrameMode::Unicode;
    else if (value == "ascii") mode = TuiFrameMode::Ascii;
    else return false;

    return true;
}

bool parseConsumer(const std::string &value, int &mask)
{
    if (value == "server") mask = ConfigOption::Server;
    else if (value == "calaos_home") mask = ConfigOption::CalaosHome;
    else if (value == "mcp") mask = ConfigOption::McpSidecar;
    else return false;

    return true;
}

/* Walks the command line once. --config/--cache take their parameter as the
 * next argument, so they cannot be handled by a naive "starts with a dash"
 * loop. The value of "set" is taken verbatim, otherwise "set latitude -3.5"
 * would be read as an unknown option.
 */
bool parseArgs(int argc, char **argv, CliArgs &args)
{
    args.configDir = Utils::argvOptionParam(argv, argv + argc, "--config");
    args.cacheDir = Utils::argvOptionParam(argv, argv + argc, "--cache");

    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];

        /* Verbatim value of "set <key> <value>": a value may start with a dash
         * ("set latitude -3.5"), so nothing is interpreted in that slot. Only
         * --force, the single flag of the action, keeps its meaning there.
         */
        if (args.action == "set" && args.params.size() == 1 && arg != "--force")
        {
            args.params.push_back(arg);
            continue;
        }

        if (arg == "--config" || arg == "--cache")
        {
            //Already read by argvOptionParam(), skip the parameter too
            if (i + 1 < argc)
                i++;
            else
            {
                std::cerr << _("Missing directory after") << " " << arg << std::endl;
                return false;
            }
            continue;
        }

        if (arg == "--color")
        {
            if (i + 1 >= argc || !parseColorMode(argv[i + 1], args.color))
            {
                std::cerr << _("Expected --color=auto, --color=always or --color=never") << std::endl;
                return false;
            }
            i++;
            continue;
        }

        if (arg.compare(0, 8, "--color=") == 0)
        {
            if (!parseColorMode(arg.substr(8), args.color))
            {
                std::cerr << _("Expected --color=auto, --color=always or --color=never") << std::endl;
                return false;
            }
            continue;
        }

        if (arg == "--frames")
        {
            if (i + 1 >= argc || !parseFrameMode(argv[i + 1], args.frames))
            {
                std::cerr << _("Expected --frames=auto, --frames=unicode or --frames=ascii") << std::endl;
                return false;
            }
            i++;
            continue;
        }

        if (arg.compare(0, 9, "--frames=") == 0)
        {
            if (!parseFrameMode(arg.substr(9), args.frames))
            {
                std::cerr << _("Expected --frames=auto, --frames=unicode or --frames=ascii") << std::endl;
                return false;
            }
            continue;
        }

        if (arg.compare(0, 11, "--consumer=") == 0)
        {
            if (!parseConsumer(arg.substr(11), args.consumerMask))
            {
                std::cerr << _("Expected --consumer=server, --consumer=calaos_home or --consumer=mcp")
                          << std::endl;
                return false;
            }
            continue;
        }

        if (arg == "-h" || arg == "--help") { args.help = true; continue; }
        if (arg == "--version" || arg == "-v") { args.version = true; continue; }
        if (arg == "--all") { args.all = true; continue; }
        if (arg == "--json") { args.json = true; continue; }
        if (arg == "--markdown") { args.markdown = true; continue; }
        if (arg == "--force") { args.force = true; continue; }
        if (arg == "--dry-run") { args.dryRun = true; continue; }

        if (!arg.empty() && arg[0] == '-')
        {
            std::cerr << _("Unknown option:") << " " << arg << std::endl;
            return false;
        }

        if (args.action.empty())
            args.action = arg;
        else
            args.params.push_back(arg);
    }

    return true;
}

//stderr note whenever obsolete keys are still lying around, so that "list"
//and "options" keep a byte stable stdout.
void warnObsoletePresent()
{
    std::vector<std::string> present = ConfigOptions::obsoletePresent();
    if (present.empty())
        return;

    std::string keys;
    for (const std::string &k: present)
    {
        if (!keys.empty())
            keys += ", ";
        keys += k;
    }

    std::cerr << _("Warning: obsolete options are still present:") << " " << keys << std::endl;
    std::cerr << _("They have no effect anymore, remove them with: calaos_config purge") << std::endl;
}

//---------------------------------------------------------------------------
// Actions
//---------------------------------------------------------------------------

int actionGet(const CliArgs &args)
{
    if (args.params.empty())
    {
        printUsage(std::cout);
        return 1;
    }

    //No endl: scripts parse this output, it must stay unterminated
    std::cout << Utils::get_config_option(args.params[0]);

    return 0;
}

int actionList(const CliArgs &args)
{
    Params options;
    Utils::get_config_options(options);

    warnObsoletePresent();

    if (args.json)
    {
        Json doc;
        doc["file"] = Utils::getConfigFile(LOCAL_CONFIG);
        doc["options"] = Json::array();

        for (int i = 0; i < options.size(); i++)
        {
            std::string key, value;
            options.get_item(i, key, value);

            const ConfigOption *opt = ConfigOptions::find(key);
            Json j;
            j["key"] = key;
            j["value"] = value;
            j["set"] = true;
            j["documented"] = opt != nullptr;
            j["obsolete"] = ConfigOptions::isObsolete(key);
            if (opt && opt->hasDef())
                j["default"] = opt->def();
            doc["options"].push_back(j);
        }

        if (args.all)
        {
            for (const ConfigOption &opt: ConfigOptions::all())
            {
                if (options.Exists(opt.key()))
                    continue;

                Json j;
                j["key"] = opt.key();
                j["value"] = "";
                j["set"] = false;
                j["documented"] = true;
                j["obsolete"] = false;
                if (opt.hasDef())
                    j["default"] = opt.def();
                if (!opt.defDynamic().empty())
                    j["default_dynamic"] = opt.defDynamic();
                doc["options"].push_back(j);
            }
        }

        std::cout << doc.dump(4) << std::endl;
        return 0;
    }

    //Historic output, byte for byte. Nothing may be added here.
    std::cout << "Local configuration:" << std::endl;
    for (int i = 0; i < options.size(); i++)
    {
        std::string key, value;
        options.get_item(i, key, value);
        std::cout << key << ": " << value << std::endl;
    }

    if (args.all)
    {
        std::cout << std::endl << _("Documented options that are not set:") << std::endl;
        for (const ConfigOption &opt: ConfigOptions::all())
        {
            if (options.Exists(opt.key()))
                continue;

            std::cout << opt.key() << ": ";
            if (opt.hasDef() && opt.def().empty())
                std::cout << _("(default: empty)");
            else if (opt.hasDef())
                std::cout << opt.def() << " " << _("(default)");
            else if (!opt.defDynamic().empty())
                std::cout << opt.defDynamic() << " " << _("(default)");
            else
                std::cout << _("(not set, no default)");
            std::cout << std::endl;
        }
    }

    return 0;
}

int actionSet(const CliArgs &args)
{
    if (args.params.size() < 2)
    {
        printUsage(std::cout);
        return 1;
    }

    const std::string &key = args.params[0];
    const std::string &value = args.params[1];

    bool obsolete = ConfigOptions::isObsolete(key);

    if (obsolete && !args.force)
    {
        std::cerr << _("Refusing to set") << " " << key << ": "
                  << _("this option is obsolete, it does not have any effect anymore.") << std::endl;
        std::cerr << _("Use --force to write it anyway, or calaos_config purge to remove it.")
                  << std::endl;
        return 1;
    }

    const ConfigOption *opt = ConfigOptions::find(key);

    if (obsolete)
    {
        //Forced: say what is really happening instead of the unknown key
        //warning below, an obsolete key is known and known to be useless.
        std::cerr << _("Warning:") << " " << key << " "
                  << _("is obsolete and does not have any effect anymore, --force was given.")
                  << std::endl;
    }
    else if (!opt)
    {
        //Full compatibility: an unknown key is still written, it may belong to
        //Calaos Home or to a newer version.
        std::cerr << _("Warning:") << " " << key << " " << _("is not a known option.") << std::endl;

        std::string hint = ConfigOptions::suggest(key);
        if (!hint.empty())
            std::cerr << _("Did you mean") << " " << hint << "?" << std::endl;

        std::cerr << _("It is written as is, run \"calaos_config options\" to list the known options.")
                  << std::endl;
    }
    else if (!args.force)
    {
        std::string error;
        if (!opt->validate(value, &error))
        {
            std::cerr << _("Invalid value for") << " " << key << ": " << error << std::endl;
            std::cerr << "  " << _("Expected:") << " " << ConfigCli::typeHint(*opt) << std::endl;
            std::cerr << "  " << _("See") << " calaos_config describe " << key << std::endl;
            std::cerr << _("Nothing was written.") << std::endl;
            return 1;
        }
    }

    if (opt && opt->isGenerated())
    {
        std::cerr << _("Warning:") << " " << key << " "
                  << _("is normally generated automatically.") << std::endl;
        std::cerr << _("Rotating it invalidates every client that uses the current value.")
                  << std::endl;
    }

    if (!Utils::set_config_option(key, value))
    {
        std::cerr << _("Failed to write") << " " << key << " " << _("to")
                  << " " << Utils::getConfigFile(LOCAL_CONFIG) << std::endl;
        return 1;
    }

    if (opt && opt->isRestartRequired())
    {
        std::cerr << _("Restart calaos_server to apply the new value of") << " "
                  << key << "." << std::endl;
    }

    return 0;
}

int actionDel(const CliArgs &args)
{
    if (args.params.empty())
    {
        printUsage(std::cout);
        return 1;
    }

    if (!Utils::del_config_option(args.params[0]))
    {
        std::cerr << _("Failed to delete") << " " << args.params[0] << " " << _("from")
                  << " " << Utils::getConfigFile(LOCAL_CONFIG) << std::endl;
        return 1;
    }

    return 0;
}

int actionPurge(const CliArgs &args)
{
    std::vector<std::string> present = ConfigOptions::obsoletePresent();

    if (present.empty())
    {
        std::cout << _("No obsolete option in") << " "
                  << Utils::getConfigFile(LOCAL_CONFIG) << std::endl;
        return 0;
    }

    if (args.dryRun)
    {
        std::cout << _("The following obsolete options would be removed from") << " "
                  << Utils::getConfigFile(LOCAL_CONFIG) << ":" << std::endl;
        for (const std::string &k: present)
            std::cout << "  " << k << std::endl;
        std::cout << _("Nothing was written, run calaos_config purge to remove them.") << std::endl;
        return 0;
    }

    std::vector<std::string> removed;
    if (!ConfigOptions::purgeObsolete(&removed))
    {
        std::cerr << _("Failed to purge the obsolete options of") << " "
                  << Utils::getConfigFile(LOCAL_CONFIG) << std::endl;
        return 1;
    }

    for (const std::string &k: removed)
        std::cout << _("Removed obsolete option:") << " " << k << std::endl;

    return 0;
}

int actionDescribe(const CliArgs &args)
{
    if (args.params.empty())
    {
        printUsage(std::cout);
        return 1;
    }

    const std::string &key = args.params[0];
    const ConfigOption *opt = ConfigOptions::find(key);

    Params options;
    Utils::get_config_options(options);
    bool isSet = options.Exists(key);

    if (opt)
    {
        ConfigCli::printDescribe(std::cout, *opt, isSet,
                                 isSet ? options.get_param_const(key) : std::string());
        return 0;
    }

    if (ConfigOptions::isObsolete(key))
    {
        std::cerr << key << ": " << _("this option is obsolete, it does not have any effect anymore.")
                  << std::endl;
        if (isSet)
            std::cerr << _("It is still present in the file, remove it with: calaos_config purge")
                      << std::endl;
        return 1;
    }

    if (isSet)
    {
        //Never hide a key of the file, even an undocumented one
        ConfigCli::printDescribeUnknown(std::cout, key, options.get_param_const(key));
        return 0;
    }

    std::cerr << _("Unknown option:") << " " << key << std::endl;
    std::string hint = ConfigOptions::suggest(key);
    if (!hint.empty())
        std::cerr << _("Did you mean") << " " << hint << "?" << std::endl;
    std::cerr << _("Run \"calaos_config options\" to list the known options.") << std::endl;

    return 1;
}

int actionOptions(const CliArgs &args)
{
    if (args.json && args.markdown)
    {
        std::cerr << _("--json and --markdown cannot be used together.") << std::endl;
        return 1;
    }

    if (args.markdown)
    {
        if (args.consumerMask)
        {
            //The generated page is compared to docs/16_config_options.md, it
            //always covers the whole registry
            std::cerr << _("--consumer cannot be used with --markdown.") << std::endl;
            return 1;
        }

        std::cout << ConfigOptions::genMarkdown();
        return 0;
    }

    if (args.json)
    {
        if (!args.consumerMask)
        {
            std::cout << ConfigOptions::genJson() << std::endl;
            return 0;
        }

        const char *wanted = args.consumerMask == ConfigOption::Server ? "server" :
                             args.consumerMask == ConfigOption::CalaosHome ? "calaos_home" :
                             "mcp_sidecar";

        Json doc = Json::parse(ConfigOptions::genJson());
        Json filtered = Json::array();
        for (const Json &o: doc["options"])
        {
            for (const Json &c: o["consumers"])
            {
                if (c.get<std::string>() == wanted)
                {
                    filtered.push_back(o);
                    break;
                }
            }
        }
        doc["options"] = filtered;

        std::cout << doc.dump(4) << std::endl;
        return 0;
    }

    warnObsoletePresent();
    ConfigCli::printOptions(std::cout, args.consumerMask);

    return 0;
}

/* The interactive browser. Everything else, argument parsing, config path
 * resolution and locale, is already done when we get here.
 */
int actionTui(const CliArgs &args)
{
    /* cpp-tui reads STDIN_FILENO and never checks isatty(), and
     * Terminal::getSize() does an unchecked ioctl(TIOCGWINSZ): this guard is
     * what keeps "calaos_config tui | cat" from opening an invisible browser.
     */
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
    {
        std::cerr << _("The interactive browser needs a terminal.") << std::endl;
        std::cerr << _("  calaos_config list             list the current configuration") << std::endl;
        std::cerr << _("  calaos_config options          documentation of every option") << std::endl;
        std::cerr << _("  calaos_config describe <key>   documentation of one option") << std::endl;
        std::cerr << _("  calaos_config set <key> <val>  change an option") << std::endl;
        return 1;
    }

    TuiColorMode color = TuiColorMode::Auto;
    if (args.color == ConfigCli::ColorMode::Always)
        color = TuiColorMode::Always;
    else if (args.color == ConfigCli::ColorMode::Never)
        color = TuiColorMode::Never;

    //Empty path: the browser asks Utils for the file it already resolved
    return runConfigTui(std::string(), color, args.frames);
}

}

int main (int argc, char **argv)
{
    #if HAVE_GETTEXT
    setlocale(LC_ALL, "");
    bindtextdomain(PACKAGE_NAME, PACKAGE_LOCALE_DIR);
    textdomain(PACKAGE_NAME);
    #endif

    CliArgs args;
    if (!parseArgs(argc, argv, args))
        return 1;

    ConfigCli::initColor(args.color);

    if (args.help)
    {
        printUsage(std::cout);
        return 0;
    }

    if (args.version)
    {
        printVersion();
        return 0;
    }

    bool interactive = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);

    if (args.action.empty() && !interactive)
    {
        printUsage(std::cout);
        return 1;
    }

    Utils::initLogger("config");

    try
    {
        Utils::initConfigOptions(args.configDir, args.cacheDir, true);
    }
    catch (const std::runtime_error &e)
    {
        std::cerr << _("Cannot use the Calaos configuration:") << " " << e.what() << std::endl;
        std::cerr << _("Config path:") << " " << Utils::getConfigFile("") << std::endl;
        std::cerr << _("Cache path:") << " " << Utils::getCacheFile("") << std::endl;
        std::cerr << _("Both paths must be writable. Run the command with sudo, or point it at "
                       "directories you own with --config <dir> and --cache <dir>.") << std::endl;

        /* The browser is a viewer before it is an editor: it opens read only on
         * a configuration it cannot write, says so and refuses every edit,
         * rather than leaving the user with nothing at all. initConfigOptions()
         * resolves both paths before it checks them, so the file it would have
         * used is known. Every other action really needs a writable config.
         */
        if (!args.action.empty() && args.action != "tui")
        {
            Utils::freeLoggers();
            return 1;
        }

        std::cerr << _("Opening the browser in read-only mode.") << std::endl;
    }

    int ret;

    if (args.action.empty() || args.action == "tui")
        ret = actionTui(args);
    else if (args.action == "get")
        ret = actionGet(args);
    else if (args.action == "set")
        ret = actionSet(args);
    else if (args.action == "list")
        ret = actionList(args);
    else if (args.action == "del")
        ret = actionDel(args);
    else if (args.action == "purge")
        ret = actionPurge(args);
    else if (args.action == "describe")
        ret = actionDescribe(args);
    else if (args.action == "options")
        ret = actionOptions(args);
    else if (args.action == "help")
    {
        printUsage(std::cout);
        ret = 0;
    }
    else
    {
        std::cerr << _("Unknown action:") << " " << args.action << std::endl << std::endl;
        printUsage(std::cout);
        ret = 1;
    }

    //No global sync() needed anymore: Utils writes local_config.xml through a
    //temporary file that is fsync()ed, renamed, and followed by an fsync() of
    //the directory.

    Utils::freeLoggers();

    return ret;
}
