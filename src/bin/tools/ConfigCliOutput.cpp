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

#include "ConfigCliOutput.h"

#include <Utils.h>

#include <ostream>
#include <sys/ioctl.h>
#include <unistd.h>

using namespace Calaos;

namespace
{

bool colorOn = false;
int termWidth = 80;

//Number of columns a UTF-8 string takes: every byte that is not a
//continuation byte counts for one. Good enough for the latin text of the
//registry, and it never over-estimates the width of the box drawings below.
std::size_t displayWidth(const std::string &s)
{
    std::size_t w = 0;
    for (char c: s)
    {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
            w++;
    }
    return w;
}

const char *seqBold() { return colorOn ? "\033[1m" : ""; }
const char *seqDim() { return colorOn ? "\033[2m" : ""; }
const char *seqKey() { return colorOn ? "\033[1;36m" : ""; }
const char *seqWarn() { return colorOn ? "\033[33m" : ""; }
const char *seqOff() { return "\033[0m"; }

std::string colorize(const char *seq, const std::string &text)
{
    if (!colorOn)
        return text;
    return std::string(seq) + text + seqOff();
}

//Greedy word wrapping, no line longer than room columns unless a single word
//is longer than that.
std::vector<std::string> wrapLines(const std::string &text, std::size_t room)
{
    std::vector<std::string> words, lines;
    Utils::split(text, words, " \t\n");

    if (room < 20)
        room = 20;

    std::string line;
    for (const std::string &word: words)
    {
        if (!line.empty() && displayWidth(line) + 1 + displayWidth(word) > room)
        {
            lines.push_back(line);
            line.clear();
        }

        if (!line.empty())
            line += " ";
        line += word;
    }

    if (!line.empty())
        lines.push_back(line);

    return lines;
}

//Every produced line, the first one included, is prefixed with indent and
//styled with seq when colours are enabled -- the escape sequences are emitted
//per line so that a pager never leaks a style.
void writeWrapped(std::ostream &out, const std::string &text,
                  const std::string &indent, std::size_t width,
                  const char *seq = nullptr)
{
    std::size_t indentWidth = displayWidth(indent);
    std::size_t room = width > indentWidth ? width - indentWidth : 0;

    for (const std::string &line: wrapLines(text, room))
    {
        out << indent;
        if (seq && colorOn)
            out << seq << line << seqOff();
        else
            out << line;
        out << std::endl;
    }
}

const std::size_t fieldWidth = 14;

//Lines of a "Category      Network / API" field, already wrapped: the label
//column is padded to a fixed width so that the values line up, and the
//continuation lines keep that alignment. Labels are pure ASCII, no width
//surprise.
void writeFieldLines(std::ostream &out, const std::string &name,
                     const std::vector<std::string> &entries)
{
    if (entries.empty())
        return;

    std::size_t used = fieldWidth + 3;
    std::size_t room = static_cast<std::size_t>(termWidth) > used ?
                       static_cast<std::size_t>(termWidth) - used : 20;

    bool firstLine = true;
    for (const std::string &entry: entries)
    {
        //An entry too long for the terminal is wrapped with a hanging indent,
        //so that it is not mistaken for the next entry of the field
        std::vector<std::string> wrapped = wrapLines(entry, room > 2 ? room - 2 : room);
        for (std::size_t i = 0; i < wrapped.size(); i++)
        {
            std::string padded = firstLine ? name : std::string();
            while (padded.size() < fieldWidth)
                padded += " ";

            out << "  " << colorize(seqDim(), padded) << " "
                << (i ? "  " : "") << wrapped[i] << std::endl;
            firstLine = false;
        }
    }
}

void writeField(std::ostream &out, const std::string &name, const std::string &value)
{
    writeFieldLines(out, name, { value });
}

std::string defaultText(const ConfigOption &opt)
{
    if (opt.hasDef())
    {
        if (opt.def().empty())
            return _("(empty)");
        return opt.def();
    }

    std::string dyn = opt.defDynamic();
    if (!dyn.empty())
        return dyn;

    return _("(none)");
}

std::string rangeText(const ConfigOption &opt)
{
    if (!opt.hasRange())
        return std::string();

    std::ostringstream os;
    os << _("from") << " " << opt.rangeMin() << " " << _("to") << " " << opt.rangeMax();
    return os.str();
}

std::vector<std::string> flagLines(const ConfigOption &opt)
{
    std::vector<std::string> flags;

    if (opt.isRestartRequired())
        flags.push_back(_("calaos_server must be restarted to apply a change"));
    if (opt.isGenerated())
        flags.push_back(_("generated automatically, changing it invalidates the existing clients"));
    if (opt.isSecret())
        flags.push_back(_("secret, the value is masked when displayed"));
    if (opt.isConfirmReset())
        flags.push_back(_("resetting it to the default asks for a confirmation"));
    if (opt.isAdvanced())
        flags.push_back(_("advanced option, leave it alone unless you know why"));
    if (opt.isDeprecated())
    {
        std::string s = _("deprecated");
        if (!opt.replacement().empty())
            s += std::string(", ") + _("replaced by") + " " + opt.replacement();
        flags.push_back(s);
    }

    return flags;
}

//Short annotation of an option in the "options" listing: type, default, and
//the flags that change what the user should expect.
std::string metaText(const ConfigOption &opt)
{
    std::string s = ConfigOptions::typeLabel(opt.type());

    if (opt.hasDef() || !opt.defDynamic().empty())
        s += std::string(", ") + _("default") + " " + defaultText(opt);

    std::string r = rangeText(opt);
    if (!r.empty())
        s += std::string(", ") + r;

    if (opt.isRestartRequired())
        s += std::string("; ") + _("restart required");
    if (opt.isGenerated())
        s += std::string("; ") + _("auto-generated");
    if (opt.isSecret())
        s += std::string("; ") + _("secret");
    if (opt.isAdvanced())
        s += std::string("; ") + _("advanced");
    if (opt.isDeprecated())
    {
        s += std::string("; ") + _("deprecated");
        if (!opt.replacement().empty())
            s += std::string(" (") + _("use") + " " + opt.replacement() + ")";
    }

    return s;
}

}

void ConfigCli::initColor(ColorMode mode)
{
    switch (mode)
    {
    case ColorMode::Always:
        colorOn = true;
        break;
    case ColorMode::Never:
        colorOn = false;
        break;
    case ColorMode::Auto:
    {
        const char *term = getenv("TERM");
        colorOn = isatty(STDOUT_FILENO) &&
                  getenv("NO_COLOR") == nullptr &&
                  (!term || (std::string(term) != "dumb" && std::string(term)[0] != '\0'));
        break;
    }
    }

    struct winsize ws;
    termWidth = 80;
    if (isatty(STDOUT_FILENO) && ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        termWidth = ws.ws_col;

    //Never narrower than a readable block, never wider than a readable line
    if (termWidth < 60) termWidth = 60;
    if (termWidth > 100) termWidth = 100;
}

bool ConfigCli::colorEnabled()
{
    return colorOn;
}

int ConfigCli::outputWidth()
{
    return termWidth;
}

std::string ConfigCli::typeHint(const ConfigOption &opt)
{
    std::string s = ConfigOptions::typeLabel(opt.type());

    if (!opt.values().empty())
    {
        std::string accepted;
        for (const auto &kv: opt.values())
        {
            if (!accepted.empty())
                accepted += ", ";
            accepted += kv.first;
        }
        s += ": " + accepted;
    }
    else
    {
        std::string r = rangeText(opt);
        if (!r.empty())
            s += ", " + r;
    }

    if (opt.example())
        s += std::string(", ") + _("for instance") + " " + opt.example();
    else if (opt.pattern())
        s += std::string(", ") + _("matching") + " " + opt.pattern();

    return s;
}

void ConfigCli::printDescribe(std::ostream &out, const ConfigOption &opt,
                              bool isSet, const std::string &rawValue)
{
    out << colorize(seqKey(), opt.key());
    if (!opt.label().empty())
        out << colorize(seqDim(), " - ") << colorize(seqBold(), opt.label());
    out << std::endl << std::endl;

    writeField(out, _("Category"), ConfigOptions::categoryLabel(opt.category()));
    writeField(out, _("Type"), ConfigOptions::typeLabel(opt.type()));

    //Current value: an absent key and an empty value are two different states
    std::string current;
    if (!isSet)
        current = _("(not set, the default applies)");
    else if (opt.isSecret())
        current = std::string("•••••••• ") +
                  (rawValue.empty() ? _("(set to an empty value)") : _("(set, hidden)"));
    else if (rawValue.empty())
        current = _("(set to an empty value)");
    else
    {
        current = rawValue;
        std::string disp = opt.displayValue(rawValue);
        if (!disp.empty() && disp != rawValue)
            current += "  (" + disp + ")";
    }
    writeField(out, _("Current"), current);

    writeField(out, _("Default"), defaultText(opt));

    std::string r = rangeText(opt);
    if (!r.empty())
        writeField(out, _("Range"), r);

    if (!opt.values().empty())
    {
        std::vector<std::string> lines;
        for (const auto &kv: opt.values())
        {
            std::string v = kv.first;
            std::string lbl = opt.valueLabel(kv.first);
            while (v.size() < 10)
                v += " ";
            lines.push_back(v + (lbl.empty() ? std::string() : lbl));
        }
        writeFieldLines(out, _("Values"), lines);
    }

    if (opt.pattern())
        writeField(out, _("Format"), opt.pattern());

    if (opt.example())
        writeField(out, _("Example"), opt.example());

    writeField(out, _("Used by"), ConfigOptions::consumerLabel(opt.consumer()));

    writeFieldLines(out, _("Flags"), flagLines(opt));

    if (!opt.seeAlso().empty())
    {
        std::string s;
        for (const std::string &k: opt.seeAlso())
        {
            if (!s.empty())
                s += ", ";
            s += k;
        }
        writeField(out, _("See also"), s);
    }

    if (!opt.doc().empty())
    {
        out << std::endl;
        writeWrapped(out, opt.doc(), "  ", static_cast<std::size_t>(termWidth));
    }
}

void ConfigCli::printDescribeUnknown(std::ostream &out, const std::string &key,
                                     const std::string &rawValue)
{
    out << colorize(seqKey(), key) << "  "
        << colorize(seqWarn(), _("(undocumented option)")) << std::endl << std::endl;

    writeField(out, _("Category"), ConfigOptions::categoryLabel(ConfigOption::Category::Unknown));
    writeField(out, _("Current"), rawValue.empty() ? _("(empty)") : rawValue);
    out << std::endl;
    writeWrapped(out, _("This key is present in local_config.xml but is not described by the "
                        "option registry. It may come from another Calaos program, from a newer "
                        "version, or from a typo. It is left untouched by every calaos_config "
                        "action."), "  ", static_cast<std::size_t>(termWidth));
}

void ConfigCli::printOptions(std::ostream &out, int consumerMask)
{
    out << colorize(seqBold(), _("Options of local_config.xml")) << std::endl;
    writeWrapped(out, _("Run \"calaos_config describe <key>\" for the full documentation of one "
                        "option, \"calaos_config set <key> <value>\" to change it."),
                 "", static_cast<std::size_t>(termWidth));

    for (ConfigOption::Category c: ConfigOptions::categories())
    {
        std::vector<const ConfigOption *> opts = ConfigOptions::byCategory(c);

        std::vector<const ConfigOption *> shown;
        for (const ConfigOption *o: opts)
        {
            if (!consumerMask || (o->consumer() & consumerMask))
                shown.push_back(o);
        }

        if (shown.empty())
            continue;

        std::string title = ConfigOptions::categoryLabel(c);
        out << std::endl << colorize(seqBold(), title) << std::endl;
        out << colorize(seqDim(), std::string(displayWidth(title), '-')) << std::endl;

        bool first = true;
        for (const ConfigOption *o: shown)
        {
            if (!first)
                out << std::endl;
            first = false;

            out << "  " << colorize(seqKey(), o->key());
            if (!o->label().empty())
                out << "  -  " << o->label();
            out << std::endl;

            std::string meta = metaText(*o);
            if (!meta.empty())
                writeWrapped(out, meta, "      ", static_cast<std::size_t>(termWidth), seqDim());

            if (!o->doc().empty())
                writeWrapped(out, o->doc(), "      ", static_cast<std::size_t>(termWidth));
        }
    }
}
