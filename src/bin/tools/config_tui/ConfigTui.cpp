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

#include "ConfigTui.h"

#include "ConfigRow.h"

#include <Utils.h>

#include <signal.h>
#include <termios.h>
#include <unistd.h>

#if defined(__has_include)
    #if __has_include(<langinfo.h>)
        #define CALAOS_HAVE_LANGINFO 1
    #endif
#elif defined(__unix__) || defined(__APPLE__)
    #define CALAOS_HAVE_LANGINFO 1
#endif

#ifdef CALAOS_HAVE_LANGINFO
    #include <langinfo.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

/* Full screen browser of local_config.xml.
 * ========================================
 *
 * Composition of cpp-tui widgets on top of ConfigModel, which owns every rule
 * about what a value means and what saving does. Nothing here decides whether
 * a value is valid, what a reset writes or how the file is merged.
 *
 * Four things are not composition and deserve a word:
 *
 * - Keyboard model. Two panes, one cursor each: Tab switches pane, the arrows
 *   move inside the pane, Enter opens an editor and Space activates a line.
 *   cpp-tui cannot express that on its own -- it has one flat Tab ring, so the
 *   editors of the option pane used to be tab stops themselves, which made Tab
 *   mean two different things depending on the side of the screen. MainPanes
 *   is therefore the only tab stop of the browser: it owns both cursors, and
 *   hands events to the widget of a line only while that line is being
 *   edited. See MainPanes and ConfigTuiScreen::paneEvent().
 *
 * - Single letter shortcuts. App::register_key() fires before the focused
 *   widget, so a bare "s" would save instead of being typed into a path. The
 *   letter bindings are therefore registered and unregistered as the editor
 *   opens and closes: see updateShortcutState(). They are off while a line is
 *   being edited, while the search prompt is open, or while a dialog is open.
 *
 * - Search. "/" opens a one line prompt in the status bar, not a widget: it is
 *   a state of the keyboard model, so that it can drive the two cursors of
 *   MainPanes while the user types. See actionSearch() and searchKey().
 *
 * - Terminal restoration. cpp-tui only restores the terminal from ~Terminal(),
 *   i.e. on the normal way out of App::run(). run() is wrapped in a try/catch
 *   for the exception path, and SIGTERM/SIGINT/SIGHUP are caught by an
 *   async-signal-safe handler that writes the restore sequence itself before
 *   letting the default action kill the process.
 *
 * - Monochrome. The library has no reverse video, so with the default theme
 *   (every colour "terminal default") nothing would show the focus. Bold and
 *   a "> " marker on the line each cursor sits on ("* " when the other pane
 *   has the keyboard) carry that job instead.
 *
 * - Terminal capabilities. Two independent questions, decided in two
 *   different places and on two different signals: which glyphs the frames
 *   are made of depends on the codeset of the locale (frameStyle()), and
 *   whether anything is coloured depends on the terminal (runConfigTui()). A
 *   UTF-8 terminal with no colour still gets real box drawing, and a colour
 *   terminal in a C locale still gets ASCII frames. Each question has its own
 *   escape hatch, --frames and --color, and they stay independent: the locale
 *   does not always reach the process ("podman exec -it" sets TERM and nothing
 *   else), so automatic detection has to be overridable on its own.
 */

using namespace Calaos;
using namespace cpptui;

namespace
{

//---------------------------------------------------------------------------
// Terminal restoration on signals
//---------------------------------------------------------------------------

/* Show the cursor, leave the alternate screen, stop mouse reporting and
 * bracketed paste: the exact counterpart of what Terminal::enableRawMode()
 * turns on.
 */
const char RESTORE_SEQUENCE[] = "\033[?1003l\033[?1006l\033[?2004l\033[?1049l\033[?25h";

volatile sig_atomic_t g_signalReceived = 0;
struct termios g_savedTermios;
bool g_termiosSaved = false;

//Async-signal-safe: write() and tcsetattr() only, no allocation, no stdio
void restoreTerminalFromSignal()
{
    ssize_t written = write(STDOUT_FILENO, RESTORE_SEQUENCE, sizeof(RESTORE_SEQUENCE) - 1);
    (void)written;

    if (g_termiosSaved)
        tcsetattr(STDIN_FILENO, TCSANOW, &g_savedTermios);
}

void signalHandler(int signo)
{
    g_signalReceived = signo;
    restoreTerminalFromSignal();

    /* Die of the signal that was asked for, so that the shell sees the right
     * status. Nothing else can be done safely from here.
     */
    signal(signo, SIG_DFL);
    raise(signo);
}

//---------------------------------------------------------------------------
// Widgets
//---------------------------------------------------------------------------

//Wraps text on word boundaries, never dropping a character
std::vector<std::string> wrapText(const std::string &text, int width)
{
    std::vector<std::string> lines;

    if (width < 1)
        return lines;

    std::string current;
    size_t pos = 0;

    while (pos <= text.size())
    {
        size_t space = text.find(' ', pos);
        std::string word = text.substr(pos, space == std::string::npos?
                                       std::string::npos: space - pos);

        if (current.empty())
            current = word;
        else if ((int)(current.size() + 1 + word.size()) <= width)
            current += " " + word;
        else
        {
            lines.push_back(current);
            current = word;
        }

        //A single word longer than the pane: cut it, losing it would be worse
        while ((int)current.size() > width)
        {
            lines.push_back(current.substr(0, width));
            current = current.substr(width);
        }

        if (space == std::string::npos)
            break;

        pos = space + 1;
    }

    lines.push_back(current);

    return lines;
}

/* Scrollable block of text: the documentation pane, and the body of every full
 * screen view (i, v, ?). cpptui::Paragraph cannot do it because it does not
 * know its own wrapped height, which is what a ScrollableVertical needs.
 */
class TextPane: public Widget
{
public:
    struct Line
    {
        std::string text;
        bool bold = false;
        //Left as a default colour unless the line really needs one
        Color color;
        bool wrap = true;
    };

    std::vector<Line> lines;
    int scroll = 0;

    TextPane() { focusable = true; }

    void setLines(const std::vector<Line> &newLines)
    {
        lines = newLines;
        scroll = 0;
    }

    void render(Buffer &buffer) override
    {
        if (width < 1 || height < 1)
            return;

        const Theme &theme = Theme::current();
        Color bg = theme.panel_bg;

        std::vector<std::pair<std::string, const Line *>> visual;
        for (size_t i = 0; i < lines.size(); i++)
        {
            if (!lines[i].wrap)
            {
                visual.push_back(std::make_pair(lines[i].text, &lines[i]));
                continue;
            }

            std::vector<std::string> wrapped = wrapText(lines[i].text, width - 1);
            for (size_t j = 0; j < wrapped.size(); j++)
                visual.push_back(std::make_pair(wrapped[j], &lines[i]));
        }

        int maxScroll = (int)visual.size() - height;
        if (maxScroll < 0)
            maxScroll = 0;
        if (scroll > maxScroll)
            scroll = maxScroll;
        if (scroll < 0)
            scroll = 0;

        for (int row = 0; row < height; row++)
        {
            for (int col = 0; col < width; col++)
            {
                Cell cell;
                cell.content = " ";
                cell.fg_color = theme.foreground;
                cell.bg_color = bg;
                buffer.set(x + col, y + row, cell);
            }

            size_t index = (size_t)(scroll + row);
            if (index >= visual.size())
                continue;

            const Line *line = visual[index].second;
            Color fg = line->color.resolve(theme.foreground);

            render_utf8_text(buffer, visual[index].first, x, y + row, width, fg, bg,
                             line->bold, false, false);
        }

        if ((int)visual.size() > height)
            render_scrollbar(buffer, x + width - 1, y, height, scroll,
                             (int)visual.size(), true);
    }

    bool on_event(const Event &event) override
    {
        if (!visible)
            return false;

        if (event.is_mouse_event() && event.mouse_wheel())
        {
            if (event.x < x || event.x >= x + width || event.y < y || event.y >= y + height)
                return false;

            scroll += event.mouse_wheel_up()? -2: 2;
            if (scroll < 0)
                scroll = 0;
            return true;
        }

        if (!event.is_key_event() || !has_focus())
            return false;

        if (event.is_nav_up()) { scroll--; }
        else if (event.is_nav_down()) { scroll++; }
        else if (event.is_nav_pgup()) { scroll -= height; }
        else if (event.is_nav_pgdn()) { scroll += height; }
        else if (event.is_nav_home()) { scroll = 0; }
        else if (event.is_nav_end()) { scroll = (int)lines.size() * 4; }
        else
            return false;

        if (scroll < 0)
            scroll = 0;

        return true;
    }
};

/* Category column. A plain list with a visible cursor, written here because
 * every list of the library shows its selection with a background colour,
 * which a monochrome terminal cannot render.
 *
 * It is not focusable: MainPanes owns the keyboard for both panes and calls
 * navigate() when this one is the active side.
 */
class SelectList: public Widget
{
public:
    struct Item
    {
        std::string label;
        int count = 0;
    };

    std::vector<Item> items;
    int selected = 0;
    int scroll = 0;
    //True when the keyboard is driving this pane: "> " instead of "* "
    bool active = false;

    std::function<void(int)> on_select;

    SelectList()
    {
        focusable = false;
        tab_stop = false;
    }

    void render(Buffer &buffer) override
    {
        if (width < 1 || height < 1)
            return;

        const Theme &theme = Theme::current();
        Color bg = theme.panel_bg;

        if (selected < 0)
            selected = 0;
        if (selected >= (int)items.size())
            selected = (int)items.size() - 1;

        if (selected >= 0)
        {
            if (selected < scroll)
                scroll = selected;
            if (selected >= scroll + height)
                scroll = selected - height + 1;
        }
        if (scroll < 0)
            scroll = 0;

        for (int row = 0; row < height; row++)
        {
            for (int col = 0; col < width; col++)
            {
                Cell cell;
                cell.content = " ";
                cell.fg_color = theme.foreground;
                cell.bg_color = bg;
                buffer.set(x + col, y + row, cell);
            }

            size_t index = (size_t)(scroll + row);
            if (index >= items.size())
                continue;

            bool isSelected = (int)index == selected;
            Color fg = isSelected? theme.primary.resolve(theme.foreground): theme.foreground;

            std::string marker = isSelected? (active? "> ": "* "): "  ";
            std::string count = std::to_string(items[index].count);

            std::string text = marker + items[index].label;
            int room = width - (int)count.size() - 1;
            if (room < 1)
                room = 1;
            if ((int)text.size() > room)
                text = text.substr(0, room);

            render_utf8_text(buffer, text, x, y + row, width, fg, bg,
                             isSelected, false, false);
            render_utf8_text(buffer, count, x + width - (int)count.size(), y + row,
                             (int)count.size(), fg, bg, false, false, false);
        }
    }

    //MainPanes is the only thing that talks to this list
    bool on_event(const Event &) override { return false; }

    //One of the keys that move the cursor of a pane, true when it was one
    bool navigate(const Event &event)
    {
        if (items.empty())
            return false;

        if (event.is_nav_up()) select(selected - 1);
        else if (event.is_nav_down()) select(selected + 1);
        else if (event.is_nav_pgup()) select(selected - height);
        else if (event.is_nav_pgdn()) select(selected + height);
        else if (event.is_nav_home()) select(0);
        else if (event.is_nav_end()) select((int)items.size() - 1);
        else return false;

        return true;
    }

    //A click or a wheel inside the column, true when it was one
    bool point(const Event &event)
    {
        if (items.empty())
            return false;

        if (event.x < x || event.x >= x + width || event.y < y || event.y >= y + height)
            return false;

        if (event.mouse_wheel())
        {
            select(selected + (event.mouse_wheel_up()? -1: 1));
            return true;
        }

        if (event.mouse_left() && !event.mouse_motion())
        {
            select(scroll + (event.y - y));
            return true;
        }

        return false;
    }

    void select(int index)
    {
        if (index < 0)
            index = 0;
        if (index >= (int)items.size())
            index = (int)items.size() - 1;

        if (index == selected)
            return;

        selected = index;
        if (on_select)
            on_select(selected);
    }
};

/* Option column. Only the scrolling and the layout of ScrollableVertical are
 * wanted: its own key handling would fight with the cursor MainPanes keeps,
 * and its children must never take the focus of the library.
 */
class RowsPane: public ScrollableVertical
{
public:
    RowsPane()
    {
        focusable = false;
        tab_stop = false;
    }

    /* The pane paints its own surface, exactly like the category column and
     * the documentation pane. The lines are then drawn on top of it, and the
     * cells no widget of a line covers -- the gap in front of the steppers of
     * a number, everything past the end of a switch -- are part of that
     * surface instead of being holes onto the background of the screen.
     */
    void render(Buffer &buffer) override
    {
        const Theme &theme = Theme::current();

        for (int row = 0; row < height; row++)
        {
            for (int col = 0; col < width; col++)
            {
                Cell cell;
                cell.content = " ";
                cell.fg_color = theme.foreground;
                cell.bg_color = theme.panel_bg;
                buffer.set(x + col, y + row, cell);
            }
        }

        ScrollableVertical::render(buffer);
    }

    bool on_event(const Event &) override { return false; }
};

/* The glyphs the frames of the browser are drawn with, and the style they come
 * from. Both live with the rest of the frame code further down: which set is
 * used is one decision, taken in one place, on --frames or, failing that, on
 * the codeset of the locale.
 */
struct FrameGlyphs;
const FrameGlyphs &frameGlyphs();
BorderStyle frameStyle();

/* The two panes of the browser, and the only tab stop of the whole screen.
 * ========================================================================
 *
 * cpp-tui has one flat ring of tab stops and no way to move the focus from the
 * outside: App::handle_tab() and its focused widget are private, and set_focus()
 * only moves the pointer the widgets share, not the one App dispatches with.
 * A tree with one tab stop per pane could therefore never answer to Right or
 * Left, and a tree with one tab stop per editor is what made Tab mean "next
 * option" on the right and nothing at all on the left.
 *
 * So the library sees exactly one tab stop here. Tab, the arrows, Enter, Space
 * and the mouse all arrive at this widget, which routes them to the pane the
 * user is in. The screen does the routing, this class is only the seam.
 *
 * The focus is deliberately not checked: nothing else on this screen can take
 * it, a click on a frame parks it on the root view, and an open dialog is
 * modal and never lets an event down here in the first place.
 *
 * It also draws the frame of both panes, because there is only one.
 * =================================================================
 *
 * A frame of its own around each pane put two verticals side by side down the
 * middle of the screen, which reads as a seam between two windows and not as
 * the divider of one. The two panes are two columns of the same thing, so they
 * are one box with one rule between them, joined to the top and bottom edges
 * with a tee. The titles stay exactly where a frame would have put them, one
 * over each column, and the column the rule saves goes to the options.
 */
class MainPanes: public Container
{
public:
    MainPanes()
    {
        focusable = true;
        tab_stop = true;
    }

    //Returns true when the screen consumed the event
    std::function<bool(const Event &)> handler;

    //Cells the category column gets, the frame and the rule excluded
    int leftWidth = 24;

    void setLeftTitle(const std::string &title) { m_leftTitle = title; }
    void setRightTitle(const std::string &title) { m_rightTitle = title; }

    //Column the shared rule is drawn on, between the two panes
    int dividerX() const { return x + 1 + leftWidth; }

    void layout() override
    {
        if (children_.size() < 2)
            return;

        //One row of frame above and below, one column on each side, and the
        //rule between the two panes
        int inner = std::max(0, height - 2);
        int left = std::max(0, std::min(leftWidth, width - 4));
        int right = std::max(0, width - 3 - left);

        place(children_[0], x + 1, y + 1, left, inner);
        place(children_[1], dividerX() + 1, y + 1, right, inner);
    }

    void render(Buffer &buffer) override;

    bool on_event(const Event &event) override
    {
        if (!visible || !handler)
            return false;

        if (!event.is_key_event() && !event.is_mouse_event() &&
            event.type != EventType::Paste)
            return false;

        return handler(event);
    }

private:
    void place(const std::shared_ptr<Widget> &child, int px, int py,
               int pwidth, int pheight)
    {
        child->update_responsive();
        child->x = px;
        child->y = py;
        child->width = pwidth;
        child->height = pheight;

        std::shared_ptr<Container> container =
                std::dynamic_pointer_cast<Container>(child);
        if (container)
            container->layout();
    }

    std::string m_leftTitle;
    std::string m_rightTitle;
};

/* cpptui::Border takes the focus when its title is clicked, which would leave
 * the shortcut state describing a widget that is not the focused one. The
 * frames of this TUI are decoration, nothing else.
 */
class TuiBorder: public Border
{
public:
    using Border::Border;

    bool on_event(const Event &event) override
    {
        return Container::on_event(event);
    }
};

/* Root of the tree. Below 80x24 it draws a single message instead of a layout
 * nobody could read; the check runs at every layout, so a resize fixes it.
 */
class RootView: public Vertical
{
public:
    std::string message;

    RootView()
    {
        min_width = 0;
        min_height = 0;

        /* Not a tab stop, but focusable: App::run() clears its focus when a
         * click lands on a widget that cannot take it, which would leave the
         * keyboard dead until the next Tab. Clicking a frame or the
         * documentation pane therefore parks the focus here, and MainPanes
         * keeps answering from the tree.
         */
        focusable = true;
        tab_stop = false;
    }

    bool tooSmall() const { return width < 80 || height < 24; }

    void addChild(std::shared_ptr<Widget> child, bool wanted)
    {
        add(child);
        m_wanted.push_back(wanted);
        child->visible = wanted;
    }

    void setWanted(size_t index, bool wanted)
    {
        if (index < m_wanted.size())
            m_wanted[index] = wanted;
    }

    void layout() override
    {
        bool small = tooSmall();

        for (size_t i = 0; i < children_.size(); i++)
            children_[i]->visible = small? false: m_wanted[i];

        if (!small)
            Vertical::layout();
    }

    void render(Buffer &buffer) override
    {
        if (!tooSmall())
        {
            Vertical::render(buffer);
            return;
        }

        const Theme &theme = Theme::current();
        std::vector<std::string> lines = wrapText(message, width > 4? width - 2: 1);

        for (int row = 0; row < height; row++)
        {
            for (int col = 0; col < width; col++)
            {
                Cell cell;
                cell.content = " ";
                cell.fg_color = theme.foreground;
                cell.bg_color = theme.background;
                buffer.set(x + col, y + row, cell);
            }
        }

        int top = (height - (int)lines.size()) / 2;
        if (top < 0)
            top = 0;

        for (size_t i = 0; i < lines.size(); i++)
        {
            int row = top + (int)i;
            if (row >= height)
                break;

            int offset = (width - (int)lines[i].size()) / 2;
            if (offset < 0)
                offset = 0;

            render_utf8_text(buffer, lines[i], x + offset, y + row, width - offset,
                             theme.foreground, theme.background, true, false, false);
        }
    }

private:
    std::vector<bool> m_wanted;
};

//---------------------------------------------------------------------------
// Frame glyphs
//---------------------------------------------------------------------------

//True when the name of a codeset or of a locale designates UTF-8
bool namesUtf8(const std::string &name)
{
    std::string lower;
    lower.reserve(name.size());
    for (char c: name)
        lower += (char)std::tolower((unsigned char)c);

    return lower.find("utf-8") != std::string::npos ||
           lower.find("utf8") != std::string::npos;
}

/* Whether the terminal can show a box drawing character. This is a question
 * about the character encoding and about nothing else: colour has no say in
 * it, and neither has TERM.
 *
 * calaos_config runs setlocale(LC_ALL, "") before anything else, so
 * nl_langinfo(CODESET) answers for the locale actually in effect. The
 * environment is looked at as well, because setlocale() falls back to the C
 * locale when the requested locale is not installed on the machine: the user
 * asked for a UTF-8 environment and the terminal is sending and expecting
 * UTF-8, whether or not the locale files were generated. The variables are
 * read in the order POSIX gives them, first one set wins, so LC_ALL=C means
 * ASCII even under LANG=en_US.UTF-8.
 */
bool localeIsUtf8()
{
#ifdef CALAOS_HAVE_LANGINFO
    const char *codeset = nl_langinfo(CODESET);
    if (codeset && namesUtf8(codeset))
        return true;
#endif

    const char *vars[] = { "LC_ALL", "LC_CTYPE", "LANG" };
    for (const char *var: vars)
    {
        const char *value = getenv(var);
        if (value && value[0] != '\0')
            return namesUtf8(value);
    }

    return false;
}

/* The border style of every frame of the browser, panes and dialogs alike.
 *
 * Single line rather than Rounded or Double: the four rounded corners are
 * absent from the fonts of the framebuffer consoles (they are not part of the
 * CP437 repertoire those fonts descend from) while the single line set is in
 * every one of them, and Double is heavier than the rest of the screen. It is
 * also the style the library pins its own Dropdown popup to, so a stray popup
 * could not look out of place.
 *
 * Resolved once, by resolveFrameStyle() at the top of runConfigTui(), before a
 * single widget exists: the locale does not change under a running browser, and
 * neither does --frames. A function-local static in frameStyle() would cache
 * whatever the first caller happened to see, so the answer is stored here
 * instead and every reader is a plain read.
 */
BorderStyle g_frameStyle = BorderStyle::ASCII;

void resolveFrameStyle(TuiFrameMode mode)
{
    if (mode == TuiFrameMode::Unicode)
        g_frameStyle = BorderStyle::Single;
    else if (mode == TuiFrameMode::Ascii)
        g_frameStyle = BorderStyle::ASCII;
    else
        g_frameStyle = localeIsUtf8()? BorderStyle::Single: BorderStyle::ASCII;
}

BorderStyle frameStyle()
{
    return g_frameStyle;
}

/* The same glyphs cpptui::Border draws its box with, plus the two tees it has
 * no use for: the shared rule of the option panes has to meet the top and the
 * bottom edge of their frame.
 */
struct FrameGlyphs
{
    const char *horizontal;
    const char *vertical;
    const char *topLeft;
    const char *topRight;
    const char *bottomLeft;
    const char *bottomRight;
    const char *teeDown;
    const char *teeUp;
};

const FrameGlyphs &frameGlyphs()
{
    //Escaped, so that the file itself stays ASCII like the rest of the source
    static const FrameGlyphs unicode = {
        "\u2500", "\u2502", "\u250C", "\u2510",
        "\u2514", "\u2518", "\u252C", "\u2534"
    };

    //A tee is a plus, like every corner of the ASCII box of the library
    static const FrameGlyphs ascii = { "-", "|", "+", "+", "+", "+", "+", "+" };

    return frameStyle() == BorderStyle::Single? unicode: ascii;
}

void MainPanes::render(Buffer &buffer)
{
    //Four columns of frame and rule, and a row of frame above and below
    if (width < 5 || height < 3)
        return;

    const Theme &theme = Theme::current();
    const FrameGlyphs &glyphs = frameGlyphs();

    Color fg = theme.border;
    Color bg = theme.background;

    int divider = dividerX();
    int bottom = y + height - 1;

    Cell cell;
    cell.fg_color = fg;
    cell.bg_color = bg;

    for (int col = 0; col < width; col++)
    {
        const char *top = glyphs.horizontal;
        const char *low = glyphs.horizontal;

        if (col == 0)
        {
            top = glyphs.topLeft;
            low = glyphs.bottomLeft;
        }
        else if (col == width - 1)
        {
            top = glyphs.topRight;
            low = glyphs.bottomRight;
        }
        else if (x + col == divider)
        {
            top = glyphs.teeDown;
            low = glyphs.teeUp;
        }

        cell.content = top;
        buffer.set(x + col, y, cell);
        cell.content = low;
        buffer.set(x + col, bottom, cell);
    }

    for (int row = y + 1; row < bottom; row++)
    {
        cell.content = glyphs.vertical;
        buffer.set(x, row, cell);
        buffer.set(divider, row, cell);
        buffer.set(x + width - 1, row, cell);
    }

    /* Two cells in from the corner and from the rule, and cut two cells before
     * the next one: exactly where cpptui::Border puts the title of a box, so
     * that the two columns are titled the way every other frame of the browser
     * is.
     */
    render_utf8_text(buffer, m_leftTitle, x + 2, y, divider - x - 3,
                     fg, bg, false, false, false);
    render_utf8_text(buffer, m_rightTitle, divider + 2, y,
                     x + width - divider - 4, fg, bg, false, false, false);

    Container::render(buffer);
}

//A frame around a widget
std::shared_ptr<TuiBorder> framed(const std::string &title, std::shared_ptr<Widget> child)
{
    std::shared_ptr<TuiBorder> border = std::make_shared<TuiBorder>(frameStyle());
    border->focusable = false;
    border->set_title(title, Alignment::Left);
    border->add(child);

    return border;
}

std::shared_ptr<Label> barLabel(const std::string &text)
{
    std::shared_ptr<Label> label = std::make_shared<Label>(StyledText(text));
    label->focusable = false;
    label->selectable = false;
    label->fixed_height = 1;

    return label;
}

//---------------------------------------------------------------------------
// The screen
//---------------------------------------------------------------------------

struct DialogButton
{
    std::string label;
    std::function<void()> action;
};

class ConfigTuiScreen: public ConfigRowHost
{
public:
    ConfigTuiScreen(const std::string &configFile, bool mono):
        m_model(configFile),
        m_mono(mono)
    {
    }

    int run();

    //--- ConfigRowHost -----------------------------------------------------
    bool rowSetValue(ConfigRow *row, const std::string &value, std::string *error) override;
    bool rowValidate(ConfigRow *row, const std::string &value, std::string *error) override;
    void rowReset(ConfigRow *row) override;
    void rowMessage(ConfigRow *row, const std::string &message) override;

private:
    struct DisplayRow
    {
        size_t category = 0;
        size_t row = 0;
    };

    //Which of the two panes the keyboard drives
    enum class Pane { Categories, Options };

    /* One option the pattern matches, in the coordinates of the display: the
     * index of its category in m_visibleCategories, and the index of its line
     * among the ones that category shows.
     */
    struct SearchMatch
    {
        int category = 0;
        int row = 0;
        //Lower is better, see searchRank()
        int rank = 0;
    };

    //--- Keyboard model ----------------------------------------------------
    bool paneEvent(const Event &event);
    bool paneKey(const Event &event);
    bool paneMouse(const Event &event);
    bool editKey(const Event &event);
    bool searchKey(const Event &event);

    void setPane(Pane pane);
    void setCursor(int index);
    void refreshCursor();
    ConfigRow *cursorRow() const;

    void startEdit();
    void stopEdit(bool cancel);
    void leaveEdit();

    void buildUi();
    void rebuildCategories();
    void rebuildRows();
    void refreshRows(ConfigRow *typing);
    void updateValueColumn();
    void updateTitle();
    void updateDoc();
    void updateStatusBar();
    void updateShortcutState();
    void ensureRowVisible(ConfigRow *row);

    std::vector<DisplayRow> displayRows() const;
    bool rowFiltered(const ConfigModel::Row &row) const;

    //--- Search ------------------------------------------------------------
    void searchClose(bool accept);
    void searchUpdate();
    void searchStep(int delta);
    void searchGoto(int category, int row);
    std::vector<SearchMatch> searchCollect(const std::string &pattern) const;
    static int searchRank(const ConfigModel::Row &row, const std::string &needle);
    std::string searchPrompt() const;

    void setLetterShortcuts(bool enabled);
    void setEscapeShortcut(bool enabled);

    void openDialog(std::shared_ptr<Dialog> dialog);
    void closeDialog();
    std::shared_ptr<Dialog> makeDialog(const std::string &title,
                                       const std::vector<TextPane::Line> &lines,
                                       const std::vector<DialogButton> &buttons,
                                       int width, int height, bool focusText);
    void showDialog(const std::string &title, const std::vector<TextPane::Line> &lines,
                    const std::vector<DialogButton> &buttons, bool full);

    //Actions
    void actionSave(bool force);
    void actionQuit();
    void actionCtrlC();
    void actionReset();
    void actionFullDoc();
    void actionPending();
    void actionHelp();
    void actionSearch();
    void actionToggleAdvanced();

    void applyReset(const std::string &key);
    void scheduleRebuild();

    ConfigModel m_model;
    bool m_mono = false;

    App m_app;

    std::shared_ptr<RootView> m_root;
    std::shared_ptr<Label> m_title;
    std::shared_ptr<Label> m_banner;
    std::shared_ptr<Label> m_bar;
    std::shared_ptr<MainPanes> m_panes;
    std::shared_ptr<SelectList> m_categories;
    std::shared_ptr<RowsPane> m_rowsPane;
    std::shared_ptr<TuiBorder> m_docBorder;
    std::shared_ptr<TextPane> m_doc;

    std::vector<std::unique_ptr<ConfigRow>> m_rows;
    //Model categories currently displayed, in model order
    std::vector<size_t> m_visibleCategories;
    int m_category = 0;

    //The line the cursor of the option pane sits on, and what the doc describes
    ConfigRow *m_current = nullptr;
    int m_cursor = -1;
    Pane m_pane = Pane::Categories;
    //True while the editor of the current line is open
    bool m_editing = false;

    //Refusal attached to the current row, shown in red in the doc pane
    std::string m_message;
    //Transient line at the bottom, replaces the shortcuts when set
    std::string m_status;

    bool m_showAdvanced = false;
    bool m_fileChanged = false;
    bool m_lettersEnabled = false;
    bool m_escapeEnabled = false;
    int m_modalCount = 0;

    //True while the search prompt owns the keyboard
    bool m_searching = false;
    std::string m_pattern;
    std::vector<SearchMatch> m_matches;
    //Match the two cursors are previewing, -1 when there is none
    int m_match = -1;
    //Where the two cursors were when "/" was pressed, for Esc and for an
    //emptied pattern
    int m_searchCategory = 0;
    int m_searchCursor = -1;
    Pane m_searchPane = Pane::Categories;

    std::shared_ptr<Dialog> m_dialog;
    std::shared_ptr<TextPane> m_dialogText;
};

//---------------------------------------------------------------------------
// Rows and categories
//---------------------------------------------------------------------------

bool ConfigTuiScreen::rowFiltered(const ConfigModel::Row &row) const
{
    //"a" hides what most people never touch. The filter lives here and not in
    //the model: it is a display choice, the model knows every option.
    if (!m_showAdvanced && row.option &&
        (row.option->isAdvanced() || row.option->isDeprecated()))
        return true;

    return false;
}

std::vector<ConfigTuiScreen::DisplayRow> ConfigTuiScreen::displayRows() const
{
    std::vector<DisplayRow> result;
    const std::vector<ConfigModel::Category> &categories = m_model.categories();

    if (m_category < 0 || m_category >= (int)m_visibleCategories.size())
        return result;

    size_t c = m_visibleCategories[m_category];
    for (size_t r = 0; r < categories[c].rows.size(); r++)
    {
        if (rowFiltered(categories[c].rows[r]))
            continue;

        DisplayRow entry;
        entry.category = c;
        entry.row = r;
        result.push_back(entry);
    }

    return result;
}

void ConfigTuiScreen::rebuildCategories()
{
    const std::vector<ConfigModel::Category> &categories = m_model.categories();

    m_visibleCategories.clear();
    m_categories->items.clear();

    for (size_t c = 0; c < categories.size(); c++)
    {
        int count = 0;
        for (size_t r = 0; r < categories[c].rows.size(); r++)
        {
            if (!rowFiltered(categories[c].rows[r]))
                count++;
        }

        //A category left empty by the filter is not shown at all
        if (count == 0)
            continue;

        SelectList::Item item;
        item.label = categories[c].label;
        item.count = count;
        m_categories->items.push_back(item);
        m_visibleCategories.push_back(c);
    }

    if (m_category >= (int)m_visibleCategories.size())
        m_category = (int)m_visibleCategories.size() - 1;
    if (m_category < 0)
        m_category = 0;

    m_categories->selected = m_category;
}

//---------------------------------------------------------------------------
// Keyboard model
//---------------------------------------------------------------------------
//
// Tab and Shift-Tab switch pane, and nothing else is a tab stop. Inside a
// pane the arrows move a cursor. Right and Enter go from the categories to
// the options, Left comes back. On a line, Space activates and Enter opens an
// editor; while an editor is open every key belongs to it, except Enter which
// commits, Escape which cancels and Tab which leaves the pane.
//
// "/" adds a third state on top of that: the search prompt owns every key,
// and moves the two cursors as the pattern grows.

ConfigRow *ConfigTuiScreen::cursorRow() const
{
    if (m_cursor < 0 || m_cursor >= (int)m_rows.size())
        return nullptr;

    return m_rows[m_cursor].get();
}

void ConfigTuiScreen::refreshCursor()
{
    m_categories->active = m_pane == Pane::Categories;

    for (size_t i = 0; i < m_rows.size(); i++)
        m_rows[i]->setSelected((int)i == m_cursor, m_pane == Pane::Options);
}

void ConfigTuiScreen::setCursor(int index)
{
    if (m_rows.empty())
        index = -1;
    else
    {
        if (index < 0)
            index = 0;
        if (index >= (int)m_rows.size())
            index = (int)m_rows.size() - 1;
    }

    if (index != m_cursor)
        leaveEdit();

    m_cursor = index;
    m_current = cursorRow();
    m_message.clear();

    refreshCursor();

    if (m_current)
        ensureRowVisible(m_current);

    updateDoc();
    updateStatusBar();
}

void ConfigTuiScreen::setPane(Pane pane)
{
    //Nothing to go to: an empty option list keeps the keyboard on the left
    if (pane == Pane::Options && m_rows.empty())
        return;

    if (pane == m_pane)
        return;

    leaveEdit();

    m_pane = pane;

    if (m_pane == Pane::Options && m_cursor < 0 && !m_rows.empty())
        setCursor(0);

    refreshCursor();
    updateStatusBar();
}

void ConfigTuiScreen::startEdit()
{
    ConfigRow *row = cursorRow();
    if (!row || m_editing)
        return;

    if (!row->editable())
    {
        //A switch has nothing to type into: Enter flips it, like Space
        row->activate();
        updateDoc();
        return;
    }

    m_editing = true;
    row->beginEdit();

    updateShortcutState();
    updateStatusBar();
}

/* Closes the editor on the way out of a line or of the pane: what was typed is
 * committed if the registry takes it, and put back if it does not. Leaving an
 * invalid value open on a line the cursor has left is the one outcome that
 * cannot be allowed.
 */
void ConfigTuiScreen::leaveEdit()
{
    if (!m_editing)
        return;

    stopEdit(false);

    if (m_editing)
        stopEdit(true);
}

void ConfigTuiScreen::stopEdit(bool cancel)
{
    if (!m_editing)
        return;

    ConfigRow *row = cursorRow();
    if (row && !row->endEdit(cancel))
    {
        //Refused: the editor stays open on the value that was typed
        updateDoc();
        updateStatusBar();
        return;
    }

    m_editing = false;

    //The value that was just committed may be longer than the ones the column
    //was sized for. Here and not while it was being typed.
    updateValueColumn();

    updateShortcutState();
    updateDoc();
    updateStatusBar();
}

bool ConfigTuiScreen::editKey(const Event &event)
{
    ConfigRow *row = cursorRow();
    if (!row)
    {
        m_editing = false;
        updateShortcutState();
        return false;
    }

    //Tab leaves the editor and the pane, like everywhere else
    if (event.is_key_event() && event.is_tab())
    {
        leaveEdit();
        setPane(Pane::Categories);
        return true;
    }

    if (event.is_key_event() && event.is_enter())
    {
        stopEdit(false);
        return true;
    }

    /* There is no lone Escape in this library: the parser waits for the rest
     * of a sequence and only delivers the pair, as Alt + ESC. Both shapes mean
     * cancel, exactly like in the dialogs.
     */
    if (event.is_key_event() && event.is_escape())
    {
        stopEdit(true);
        return true;
    }

    row->editEvent(event);

    //Everything else belongs to the editor while it is open, consumed or not:
    //letting a key fall back through would move the cursor under the field
    return true;
}

bool ConfigTuiScreen::paneKey(const Event &event)
{
    if (event.is_tab())
    {
        setPane(m_pane == Pane::Categories? Pane::Options: Pane::Categories);
        return true;
    }

    if (m_pane == Pane::Categories)
    {
        if (m_categories->navigate(event))
            return true;

        if (event.is_nav_right() || event.is_enter())
        {
            setPane(Pane::Options);
            return true;
        }

        return false;
    }

    //Option pane, no editor open
    if (event.is_nav_left())
    {
        setPane(Pane::Categories);
        return true;
    }

    if (event.is_nav_up()) { setCursor(m_cursor - 1); return true; }
    if (event.is_nav_down()) { setCursor(m_cursor + 1); return true; }
    if (event.is_nav_pgup()) { setCursor(m_cursor - std::max(1, m_rowsPane->height)); return true; }
    if (event.is_nav_pgdn()) { setCursor(m_cursor + std::max(1, m_rowsPane->height)); return true; }
    if (event.is_nav_home()) { setCursor(0); return true; }
    if (event.is_nav_end()) { setCursor((int)m_rows.size() - 1); return true; }

    ConfigRow *row = cursorRow();
    if (!row)
        return false;

    if (event.is_space())
    {
        //The one verb that means "act on this line", whatever its type
        if (row->activate())
            updateDoc();
        return true;
    }

    if (event.is_enter())
    {
        startEdit();
        return true;
    }

    return false;
}

bool ConfigTuiScreen::paneMouse(const Event &event)
{
    /* The editors are not focusable, so App gives this widget the focus for a
     * click anywhere in either pane. Only the two cursors have to follow.
     */
    if (m_categories->point(event))
    {
        if (m_pane != Pane::Categories)
        {
            leaveEdit();
            m_pane = Pane::Categories;
            refreshCursor();
            updateStatusBar();
        }

        return true;
    }

    if (event.x < m_rowsPane->x || event.x >= m_rowsPane->x + m_rowsPane->width ||
        event.y < m_rowsPane->y || event.y >= m_rowsPane->y + m_rowsPane->height)
        return false;

    //A drag inside the field being edited is a text selection
    if (m_editing && !event.mouse_wheel())
    {
        ConfigRow *row = cursorRow();
        if (row && row->widget() && row->widget()->hit_test(event.x, event.y))
            return row->editEvent(event);
    }

    if (event.mouse_wheel())
    {
        setCursor(m_cursor + (event.mouse_wheel_up()? -1: 1));
        return true;
    }

    if (event.mouse_left() && !event.mouse_motion())
    {
        int index = m_rowsPane->scroll_offset + (event.y - m_rowsPane->y);
        if (index < 0 || index >= (int)m_rows.size())
            return true;

        if (m_pane != Pane::Options)
        {
            leaveEdit();
            m_pane = Pane::Options;
        }

        setCursor(index);
        refreshCursor();
        updateStatusBar();

        return true;
    }

    return false;
}

bool ConfigTuiScreen::paneEvent(const Event &event)
{
    if (event.is_mouse_event())
    {
        //Pointing at something is a way of saying where you wanted to go: the
        //prompt takes the jump it has made and gets out of the way
        if (m_searching)
            searchClose(true);

        return paneMouse(event);
    }

    if (m_searching)
        return searchKey(event);

    if (m_editing)
        return editKey(event);

    if (event.type == EventType::Paste)
        return false;

    if (!event.is_key_event())
        return false;

    return paneKey(event);
}

//---------------------------------------------------------------------------
// Search
//---------------------------------------------------------------------------
//
// "/" does not filter anything: it walks. The prompt lives on the status bar
// line and is not a widget at all, because the two cursors it has to drive
// belong to MainPanes and a focused Input would have taken the keyboard away
// from it. Every key of the prompt therefore arrives through paneEvent(), and
// the single letter shortcuts are off for the whole time so that an "s" is an
// s. Esc puts both cursors back where "/" found them.

//Lower is better, -1 when the row does not match at all. needle is lower case
//and never empty.
int ConfigTuiScreen::searchRank(const ConfigModel::Row &row, const std::string &needle)
{
    /* The order the user expects: the key they half remember first, then the
     * ones that merely contain it, and only at the end the rows whose prose
     * happens to mention the word.
     */
    std::string key = Utils::str_to_lower(row.key);

    if (key == needle)
        return 0;
    if (key.compare(0, needle.size(), needle) == 0)
        return 1;
    if (key.find(needle) != std::string::npos)
        return 2;

    //An undocumented row has nothing but its key
    if (row.option)
    {
        if (Utils::str_to_lower(row.option->label()).find(needle) != std::string::npos)
            return 3;
        if (Utils::str_to_lower(row.option->doc()).find(needle) != std::string::npos)
            return 4;
    }

    return -1;
}

std::vector<ConfigTuiScreen::SearchMatch>
ConfigTuiScreen::searchCollect(const std::string &pattern) const
{
    std::vector<SearchMatch> matches;

    if (pattern.empty())
        return matches;

    std::string needle = Utils::str_to_lower(pattern);
    const std::vector<ConfigModel::Category> &categories = m_model.categories();

    /* Only what the browser is showing: an option hidden by "a" cannot be
     * jumped to, and the obsolete keys are in no category at all. Walking the
     * visible categories in order is the registry order, which a stable sort on
     * the rank then keeps as the tie break -- the walk has to be the same list
     * every time or Down would not be predictable.
     */
    for (size_t c = 0; c < m_visibleCategories.size(); c++)
    {
        const ConfigModel::Category &category = categories[m_visibleCategories[c]];
        int display = 0;

        for (size_t r = 0; r < category.rows.size(); r++)
        {
            if (rowFiltered(category.rows[r]))
                continue;

            int rank = searchRank(category.rows[r], needle);
            if (rank >= 0)
            {
                SearchMatch match;
                match.category = (int)c;
                match.row = display;
                match.rank = rank;
                matches.push_back(match);
            }

            display++;
        }
    }

    std::stable_sort(matches.begin(), matches.end(),
                     [](const SearchMatch &a, const SearchMatch &b)
                     { return a.rank < b.rank; });

    return matches;
}

//Moves both cursors to one line of one category, scrolling it into view
void ConfigTuiScreen::searchGoto(int category, int row)
{
    if (category < 0 || category >= (int)m_visibleCategories.size())
        return;

    if (category != m_category)
    {
        /* Straight to the list, not through SelectList::select(): its
         * on_select would rebuild the rows a second time.
         */
        m_category = category;
        m_categories->selected = category;
        rebuildRows();
    }

    setCursor(row);
}

void ConfigTuiScreen::searchUpdate()
{
    //Back to an empty pattern is back to where the search started, and the
    //prompt stays open
    if (m_pattern.empty())
    {
        m_matches.clear();
        m_match = -1;
        searchGoto(m_searchCategory, m_searchCursor);
        updateStatusBar();
        return;
    }

    std::vector<SearchMatch> matches = searchCollect(m_pattern);

    /* Nothing matches: the cursors stay on the last match instead of jumping
     * somewhere the user never asked for, and the prompt says so.
     */
    if (matches.empty())
    {
        m_matches.clear();
        m_match = -1;
        updateStatusBar();
        return;
    }

    m_matches = matches;
    m_match = 0;

    searchGoto(m_matches[0].category, m_matches[0].row);
    updateStatusBar();
}

void ConfigTuiScreen::searchStep(int delta)
{
    if (m_matches.empty())
        return;

    int count = (int)m_matches.size();
    m_match = ((m_match + delta) % count + count) % count;

    searchGoto(m_matches[m_match].category, m_matches[m_match].row);
    updateStatusBar();
}

void ConfigTuiScreen::searchClose(bool accept)
{
    if (!m_searching)
        return;

    m_searching = false;

    if (accept)
    {
        //Land on the option, ready to be acted on: the cursors do not move,
        //only the pane the keyboard is in
        setPane(Pane::Options);
    }
    else
    {
        searchGoto(m_searchCategory, m_searchCursor);
        setPane(m_searchPane);
    }

    m_pattern.clear();
    m_matches.clear();
    m_match = -1;

    updateShortcutState();
    updateStatusBar();
}

bool ConfigTuiScreen::searchKey(const Event &event)
{
    if (!event.is_key_event())
        return false;

    /* There is no lone Escape in this library: it only comes out as the pair,
     * plain or as Alt + ESC, exactly like in the editors and the dialogs.
     */
    if (event.is_escape())
    {
        searchClose(false);
        return true;
    }

    if (event.is_enter())
    {
        searchClose(true);
        return true;
    }

    if (event.is_nav_down() || (event.ctrl && event.key == 'n'))
    {
        searchStep(1);
        return true;
    }

    if (event.is_nav_up() || (event.ctrl && event.key == 'p'))
    {
        searchStep(-1);
        return true;
    }

    if (event.is_backspace())
    {
        //One character, not one byte: a pattern may be typed in any language
        while (!m_pattern.empty() && ((unsigned char)m_pattern.back() & 0xC0) == 0x80)
            m_pattern.erase(m_pattern.size() - 1);

        if (!m_pattern.empty())
        {
            m_pattern.erase(m_pattern.size() - 1);
            searchUpdate();
        }

        return true;
    }

    if (event.is_printable() && !event.ctrl && !event.alt)
    {
        m_pattern += (char)event.key;
        searchUpdate();
        return true;
    }

    //Everything else belongs to the prompt while it is open: letting a key
    //fall back through would move a cursor under the pattern being typed
    return true;
}

std::string ConfigTuiScreen::searchPrompt() const
{
    /* "_" is the caret: this prompt is drawn text, not a widget, so nothing
     * puts a terminal cursor at its end, and a highlighted cell would need a
     * colour the monochrome mode has not got.
     */
    std::string prompt = "/" + m_pattern + "_  ";

    if (m_pattern.empty())
        prompt += std::string("(") + _("type to search") + ")";
    else if (m_matches.empty())
        prompt += std::string("(") + _("no match") + ")";
    else
        prompt += "(" + std::to_string(m_match + 1) + "/" +
                  std::to_string((int)m_matches.size()) + ")";

    //ASCII, like every other bar: the arrow glyphs would be mojibake in a
    //locale that is not UTF-8, and the words work everywhere
    prompt += std::string("   ") + _("Enter go  Esc Esc cancel  Up/Down next match");

    return prompt;
}

void ConfigTuiScreen::rebuildRows()
{
    /* Cut every callback of the lines about to disappear: cpp-tui keeps its
     * own reference to the focused widget, so one of them can survive this
     * call and still receive a keystroke.
     */
    for (size_t i = 0; i < m_rows.size(); i++)
        m_rows[i]->detach();

    m_rows.clear();
    m_rowsPane->clear_children();
    m_rowsPane->scroll_offset = 0;
    m_current = nullptr;
    m_cursor = -1;
    m_message.clear();

    //No editor survives a rebuild: the letter shortcuts go back on, or the
    //keyboard would stay dead
    m_editing = false;
    updateShortcutState();

    const std::vector<ConfigModel::Category> &categories = m_model.categories();
    std::vector<DisplayRow> rows = displayRows();

    ConfigTuiScreen *self = this;

    for (size_t i = 0; i < rows.size(); i++)
    {
        const ConfigModel::Row &row = categories[rows[i].category].rows[rows[i].row];

        std::unique_ptr<ConfigRow> configRow(
                new ConfigRow(m_app, *this, row, m_model.readOnly(), m_mono));

        configRow->restoreFocus = [self]() { self->m_panes->set_focus(true); };

        m_rowsPane->add(configRow->widget());
        m_rows.push_back(std::move(configRow));
    }

    //The cursor of the option pane always points at something when there is
    //something to point at, whichever pane the keyboard is in
    if (!m_rows.empty())
    {
        m_cursor = 0;
        m_current = m_rows[0].get();
    }
    else if (m_pane == Pane::Options)
        m_pane = Pane::Categories;

    refreshCursor();
    updateValueColumn();

    std::string title;
    if (m_category >= 0 && m_category < (int)m_visibleCategories.size())
        title = categories[m_visibleCategories[m_category]].label;

    if (rows.empty())
        title += std::string(" - ") + _("no option matches");

    m_panes->setRightTitle(title);

    updateDoc();
    updateStatusBar();
}

/* One value column for the whole category.
 *
 * Every line asks for the room its own value needs, the widest of them wins,
 * and each line is given that same number: the values start on the same column
 * and the "(default)" markers that follow them do too, whether the line holds
 * a port, a path or a switch. What is left of the pane stays empty rather than
 * pushing the markers against the frame, a screen away from the value they
 * qualify -- and a line too narrow for the column simply clamps it, which is
 * what happens to every line at once at 80 columns.
 *
 * Not called while a value is being typed: a column that grew under the cursor
 * would move the line the user is reading.
 */
void ConfigTuiScreen::updateValueColumn()
{
    int column = 0;
    for (size_t i = 0; i < m_rows.size(); i++)
        column = std::max(column, m_rows[i]->naturalValueWidth());

    for (size_t i = 0; i < m_rows.size(); i++)
        m_rows[i]->setValueColumn(column);
}

void ConfigTuiScreen::refreshRows(ConfigRow *typing)
{
    for (size_t i = 0; i < m_rows.size(); i++)
    {
        const ConfigModel::Row *row = m_model.row(m_rows[i]->key());
        if (!row)
            continue;

        m_rows[i]->refresh(*row, m_rows[i].get() != typing);
    }

    if (!typing)
        updateValueColumn();
}

void ConfigTuiScreen::ensureRowVisible(ConfigRow *row)
{
    int index = -1;
    for (size_t i = 0; i < m_rows.size(); i++)
    {
        if (m_rows[i].get() == row)
        {
            index = (int)i;
            break;
        }
    }

    if (index < 0 || m_rowsPane->height <= 0)
        return;

    //Every line is one cell high, so the offset is the index of the first one
    if (index < m_rowsPane->scroll_offset)
        m_rowsPane->scroll_offset = index;
    else if (index >= m_rowsPane->scroll_offset + m_rowsPane->height)
        m_rowsPane->scroll_offset = index - m_rowsPane->height + 1;
}

//---------------------------------------------------------------------------
// ConfigRowHost
//---------------------------------------------------------------------------

bool ConfigTuiScreen::rowValidate(ConfigRow *row, const std::string &value, std::string *error)
{
    return m_model.validate(row->key(), value, error);
}

bool ConfigTuiScreen::rowSetValue(ConfigRow *row, const std::string &value, std::string *error)
{
    if (!m_model.setValue(row->key(), value, error))
        return false;

    refreshRows(row);
    updateTitle();

    return true;
}

void ConfigTuiScreen::rowReset(ConfigRow *row)
{
    const ConfigOption *option = row->option();
    std::string key = row->key();

    if (m_model.readOnly())
    {
        m_status = _("The configuration file is read only.");
        updateStatusBar();
        refreshRows(nullptr);
        return;
    }

    //Credentials and keys nobody documented are the two cases where losing the
    //current value silently would hurt
    bool confirm = !option || option->isConfirmReset();

    if (!confirm)
    {
        applyReset(key);
        return;
    }

    std::vector<TextPane::Line> lines;
    TextPane::Line line;
    line.text = option?
                std::string(_("Reset this option to its default value?")):
                std::string(_("Remove this key from the configuration file?"));
    lines.push_back(line);

    TextPane::Line keyLine;
    keyLine.text = key;
    keyLine.bold = true;
    lines.push_back(keyLine);

    if (!option)
    {
        TextPane::Line warn;
        warn.text = _("It is not a documented option: nothing here knows what uses it. "
                      "It will be removed from the file when you save.");
        lines.push_back(warn);
    }

    std::vector<DialogButton> buttons;

    DialogButton yes;
    yes.label = _("Reset");
    yes.action = [this, key]()
    {
        closeDialog();
        applyReset(key);
    };
    buttons.push_back(yes);

    DialogButton no;
    no.label = _("Cancel");
    no.action = [this]()
    {
        closeDialog();
        //Put back what an emptied field threw away
        refreshRows(nullptr);
    };
    buttons.push_back(no);

    showDialog(_("Confirm"), lines, buttons, false);
}

void ConfigTuiScreen::rowMessage(ConfigRow *row, const std::string &message)
{
    if (row != m_current)
        return;

    m_message = message;
    m_status = message;

    updateDoc();
    updateStatusBar();
}

void ConfigTuiScreen::applyReset(const std::string &key)
{
    bool undocumented = ConfigOptions::find(key) == nullptr;

    std::string error;
    if (!m_model.resetToDefault(key, &error))
    {
        m_status = error;
        updateStatusBar();
        return;
    }

    updateTitle();

    /* An undocumented key that is reset disappears from the model, so its line
     * has to go too. Rebuilding destroys widgets, and we may well be inside a
     * callback of one of them: do it from the event loop instead.
     */
    if (undocumented)
    {
        scheduleRebuild();
        return;
    }

    refreshRows(nullptr);
    updateDoc();
}

void ConfigTuiScreen::scheduleRebuild()
{
    m_app.post([this]()
    {
        rebuildCategories();
        rebuildRows();
        updateTitle();
        updateStatusBar();
    });
}

//---------------------------------------------------------------------------
// Header, documentation pane and status bar
//---------------------------------------------------------------------------

void ConfigTuiScreen::updateTitle()
{
    StyledText text;
    text.add(" ");
    text.bold(_("Calaos Config"));
    text.add(" - " + m_model.configFile());

    size_t changes = m_model.modifiedCount() + m_model.deletedCount();
    if (changes > 0)
        text.add("  * " + std::to_string(changes) + " " + _("change(s)"));

    if (m_model.readOnly())
        text.add(std::string("  [") + _("read-only") + "]");

    if (m_fileChanged)
        text.add(std::string("  [") + _("file changed") + "]");

    m_title->set_text(text);
}

void ConfigTuiScreen::updateDoc()
{
    std::vector<TextPane::Line> lines;
    const Theme &theme = Theme::current();

    if (!m_current)
    {
        m_docBorder->set_title(_("Documentation"), Alignment::Left);
        TextPane::Line line;
        line.text = _("Pick a category with the arrow keys, then Tab or Right to its "
                      "options: this pane describes the one the cursor is on.");
        lines.push_back(line);
        m_doc->setLines(lines);
        return;
    }

    const ConfigOption *option = m_current->option();
    const ConfigModel::Row *row = m_model.row(m_current->key());

    std::string title = m_current->key();
    if (option)
    {
        title += " - " + ConfigOptions::typeLabel(option->type());
        if (option->isRestartRequired())
            title += std::string(" - ") + _("restart required");
        if (option->isSecret())
            title += std::string(" - ") + _("secret");
        if (option->isGenerated())
            title += std::string(" - ") + _("auto-generated");
        if (option->isDeprecated())
            title += std::string(" - ") + _("deprecated");
    }
    else
        title += std::string(" - ") + _("not documented");

    m_docBorder->set_title(title, Alignment::Left);

    if (option)
    {
        TextPane::Line head;
        if (option->hasDef())
            head.text = std::string(_("Default:")) + " " +
                        (option->def().empty()? std::string(_("(empty)")):
                         option->displayValue(option->def()));
        else if (!option->defDynamic().empty())
            head.text = std::string(_("Default:")) + " " + option->defDynamic();
        else
            head.text = std::string(_("Default:")) + " " + _("none");

        head.text += std::string("    ") + _("Used by:") + " " +
                     ConfigOptions::consumerLabel(option->consumer());
        head.wrap = false;
        lines.push_back(head);

        TextPane::Line doc;
        doc.text = option->doc();
        lines.push_back(doc);

        /* The values of a list, so that Space and Enter on that line have
         * something to aim at: the popup of the library is a mouse affair.
         */
        if (option->type() == ConfigOption::Type::Enum && !option->values().empty())
        {
            std::string all;
            for (size_t i = 0; i < option->values().size(); i++)
            {
                const std::string &value = option->values()[i].first;
                if (!all.empty())
                    all += ", ";
                all += option->valueLabel(value);
            }

            TextPane::Line values;
            values.text = std::string(_("Values:")) + " " + all;
            lines.push_back(values);
        }

        if (option->isDeprecated())
        {
            TextPane::Line dep;
            dep.text = option->replacement().empty()?
                       std::string(_("This option is deprecated.")):
                       std::string(_("This option is deprecated, use instead:")) + " " +
                       option->replacement();
            lines.push_back(dep);
        }

        if (option->isGenerated())
        {
            TextPane::Line gen;
            gen.text = _("This value is generated automatically at first start. "
                         "Changing it invalidates every client that uses the current one.");
            lines.push_back(gen);
        }

        if (!option->seeAlso().empty())
        {
            std::string others;
            for (size_t i = 0; i < option->seeAlso().size(); i++)
            {
                if (!others.empty())
                    others += ", ";
                others += option->seeAlso()[i];
            }

            TextPane::Line also;
            also.text = std::string(_("See also:")) + " " + others;
            lines.push_back(also);
        }

        if (option->example() && option->example()[0])
        {
            TextPane::Line example;
            example.text = std::string(_("Example:")) + " " + option->example();
            lines.push_back(example);
        }
    }
    else
    {
        TextPane::Line line;
        line.text = _("This key is in the file but not in the registry: it may belong to "
                      "Calaos Home, to a newer version, or it may have been written by "
                      "hand. It is edited as free text and never touched on its own.");
        lines.push_back(line);
    }

    if (row && !row->isSet)
    {
        TextPane::Line unset;
        unset.text = _("Not set: the default applies. Type a value to write the key.");
        lines.push_back(unset);
    }

    if (m_model.readOnly())
    {
        TextPane::Line ro;
        ro.text = _("The configuration cannot be written. Run the command with sudo, or "
                    "point it at directories you own with --config and --cache.");
        ro.bold = true;
        lines.push_back(ro);
    }

    if (!m_message.empty())
    {
        TextPane::Line error;
        error.text = std::string("!! ") + m_message;
        error.bold = true;
        error.color = theme.error;
        lines.push_back(error);
    }

    m_doc->setLines(lines);
}

void ConfigTuiScreen::updateStatusBar()
{
    //The prompt takes the whole line while it is open: it is the only thing
    //the keyboard is talking to
    if (m_searching)
    {
        m_bar->set_text(StyledText(" " + searchPrompt()));
        return;
    }

    if (!m_status.empty())
    {
        m_bar->set_text(StyledText(" " + m_status));
        return;
    }

    /* One bar per state, so that the keys the bar names are the keys that
     * work. Plain ASCII: the arrow glyphs would be mojibake in a locale that
     * is not UTF-8, and the words work everywhere.
     */
    std::string shortcuts;

    if (m_editing)
        shortcuts = _("Enter commit  Esc Esc cancel  Up/Down change  Ctrl-R reveal");
    else if (m_pane == Pane::Categories)
        shortcuts = _("Tab pane  Up/Down move  Enter/Right options  "
                      "/ search  s save  ? help  q quit");
    else
        shortcuts = _("Tab pane  Up/Down move  Enter edit  Space toggle  Left back  "
                      "d default  ? help  q quit");

    m_bar->set_text(StyledText(" " + shortcuts));
}

//---------------------------------------------------------------------------
// Shortcuts
//---------------------------------------------------------------------------

void ConfigTuiScreen::setLetterShortcuts(bool enabled)
{
    if (enabled == m_lettersEnabled)
        return;

    m_lettersEnabled = enabled;

    //The bare letters and "?" and "/", i.e. everything a text field would want
    //to receive as a character
    const int keys[] = { 's', 'q', 'd', 'i', 'v', 'a', '?', '/', 8, 127 };

    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
    {
        int key = keys[i];

        if (!enabled)
        {
            m_app.unregister_key(key);
            m_app.unregister_key(key, false, false, true);
            continue;
        }

        std::function<void()> callback;

        switch (key)
        {
        case 's': callback = [this]() { actionSave(false); }; break;
        case 'q': callback = [this]() { actionQuit(); }; break;
        case 'i': callback = [this]() { actionFullDoc(); }; break;
        case 'v': callback = [this]() { actionPending(); }; break;
        case 'a': callback = [this]() { actionToggleAdvanced(); }; break;
        case '?': callback = [this]() { actionHelp(); }; break;
        case '/': callback = [this]() { actionSearch(); }; break;
        default: callback = [this]() { actionReset(); }; break;
        }

        m_app.register_key(key, callback);
        //"?" and "/" may arrive with the shift flag set, depending on the
        //terminal
        m_app.register_key(key, callback, false, false, true);
    }
}

void ConfigTuiScreen::setEscapeShortcut(bool enabled)
{
    if (enabled == m_escapeEnabled)
        return;

    m_escapeEnabled = enabled;

    /* The parser of the library has no escape timeout: a lone ESC leaves it
     * waiting for the rest of a sequence and is never delivered as a key. It
     * only comes out as "Alt + ESC" when a second ESC follows, so Escape here
     * means pressing it twice. Every dialog also has a button that does the
     * same thing, which is the way out that always works.
     */
    if (enabled)
    {
        m_app.register_key(27, [this]() { closeDialog(); });
        m_app.register_key(27, [this]() { closeDialog(); }, false, true);
    }
    else
    {
        m_app.unregister_key(27);
        m_app.unregister_key(27, false, true);
    }
}

/* The letters are on unless something is going to swallow them: an open
 * editor, the search prompt, or a dialog. The editors of the option pane never
 * hold the focus of the library any more, and the prompt is not a widget at
 * all, so "a text field has the focus" is not a question that can be asked
 * here: only these three flags tell whether an s is a save or a letter.
 */
void ConfigTuiScreen::updateShortcutState()
{
    setLetterShortcuts(m_modalCount == 0 && !m_editing && !m_searching);
}

//---------------------------------------------------------------------------
// Dialogs
//---------------------------------------------------------------------------

std::shared_ptr<Dialog> ConfigTuiScreen::makeDialog(const std::string &title,
                                                    const std::vector<TextPane::Line> &lines,
                                                    const std::vector<DialogButton> &buttons,
                                                    int width, int height, bool focusText)
{
    std::shared_ptr<Dialog> dialog = std::make_shared<Dialog>(&m_app, frameStyle());

    /* The shadow of the library darkens real RGB values, which a terminal
     * without truecolor cannot show. It is off in colour mode too: the
     * browser now runs in colour on 256 colour terminals, where the darkened
     * cells would be approximated to something unrelated.
     */
    dialog->shadow = false;
    dialog->modal = true;
    dialog->set_title(title, Alignment::Left);
    dialog->fixed_width = width;
    dialog->fixed_height = height;
    dialog->width = width;
    dialog->height = height;

    std::shared_ptr<Vertical> body = std::make_shared<Vertical>();

    std::shared_ptr<TextPane> text = std::make_shared<TextPane>();
    text->setLines(lines);
    //A short dialog gives the focus straight to its buttons; a long one is
    //meant to be scrolled, so its text is the first tab stop
    text->focusable = focusText;
    text->tab_stop = focusText;
    m_dialogText = text;

    body->add(text);

    if (!buttons.empty())
    {
        std::shared_ptr<Horizontal> row = std::make_shared<Horizontal>();
        row->fixed_height = 1;

        for (size_t i = 0; i < buttons.size(); i++)
        {
            std::shared_ptr<MonoButton> button =
                    std::make_shared<MonoButton>(buttons[i].label, buttons[i].action);
            button->fixed_width = (int)buttons[i].label.size() + 8;
            row->add(button);
        }

        row->add(std::make_shared<HorizontalSpacer>());
        body->add(row);
    }

    dialog->add(body);

    return dialog;
}

/* Opening and closing go through App::post().
 *
 * App::run() holds a reference into its dialog_stack while it moves the focus
 * into a dialog that has just opened, and moving the focus blurs the field
 * that was being edited -- which is exactly where this TUI decides that an
 * emptied field means "reset", and wants to ask for a confirmation. Touching
 * the stack from there reallocates the vector under that reference. Posting
 * the call runs it from the callback phase of the loop instead, where nothing
 * holds such a reference.
 *
 * The bookkeeping stays synchronous, so that a button which closes a dialog
 * and immediately opens another one is not refused by the guard below.
 */
void ConfigTuiScreen::openDialog(std::shared_ptr<Dialog> dialog)
{
    //One modal at a time: the blur that follows an opening would otherwise ask
    //the very same question a second time
    if (m_modalCount > 0)
        return;

    m_dialog = dialog;
    m_modalCount++;

    updateShortcutState();
    setEscapeShortcut(true);

    /* App::run() only notices a new dialog, and moves the focus into it, at
     * the top of the next iteration of its loop -- and it may well be blocked
     * waiting for input until then. update() wakes it up so that the dialog is
     * usable as soon as it appears, instead of on the next keystroke.
     */
    m_app.post([this, dialog]()
    {
        m_app.open_dialog(dialog);
        m_app.update();
    });
}

void ConfigTuiScreen::closeDialog()
{
    if (!m_dialog)
        return;

    std::shared_ptr<Dialog> dialog = m_dialog;

    m_dialog.reset();
    m_dialogText.reset();

    if (m_modalCount > 0)
        m_modalCount--;

    setEscapeShortcut(false);
    updateShortcutState();

    //Same as above: the focus goes back to the option pane on the next
    //iteration, which has to be provoked
    m_app.post([this, dialog]()
    {
        m_app.close_dialog(dialog);
        m_app.update();
    });
}

void ConfigTuiScreen::showDialog(const std::string &title,
                                 const std::vector<TextPane::Line> &lines,
                                 const std::vector<DialogButton> &buttons, bool full)
{
    std::pair<int, int> size = Terminal::getSize();

    int width = full? std::max(40, size.first - 8): std::min(70, std::max(40, size.first - 8));
    int height = full? std::max(10, size.second - 6): std::min(14, std::max(8, size.second - 6));

    openDialog(makeDialog(title, lines, buttons, width, height, full));
}

//---------------------------------------------------------------------------
// Actions
//---------------------------------------------------------------------------

void ConfigTuiScreen::actionSave(bool force)
{
    if (m_model.readOnly())
    {
        m_status = _("The configuration file is read only, nothing was written.");
        updateStatusBar();
        return;
    }

    if (!m_model.hasPendingChanges())
    {
        m_status = _("Nothing to save.");
        updateStatusBar();
        return;
    }

    if (!force && m_model.hasChangedOnDisk())
    {
        std::vector<TextPane::Line> lines;

        TextPane::Line line;
        line.text = _("Another program changed the configuration file since it was read.");
        lines.push_back(line);

        TextPane::Line detail;
        detail.text = _("Saving merges your changes into the current content of the file: "
                        "the keys you did not touch keep the value the other program gave "
                        "them. Reloading throws your changes away and reads the file again.");
        lines.push_back(detail);

        std::vector<DialogButton> buttons;

        DialogButton save;
        save.label = _("Save anyway");
        save.action = [this]()
        {
            closeDialog();
            actionSave(true);
        };
        buttons.push_back(save);

        DialogButton reload;
        reload.label = _("Reload");
        reload.action = [this]()
        {
            closeDialog();
            std::string error;
            m_model.reload(&error);
            m_fileChanged = false;
            m_status = error.empty()? std::string(_("Configuration reloaded.")): error;
            scheduleRebuild();
        };
        buttons.push_back(reload);

        DialogButton cancel;
        cancel.label = _("Cancel");
        cancel.action = [this]() { closeDialog(); };
        buttons.push_back(cancel);

        showDialog(_("File changed on disk"), lines, buttons, false);
        return;
    }

    ConfigModel::SaveResult result = m_model.save();

    if (!result.ok)
    {
        std::vector<TextPane::Line> lines;
        TextPane::Line line;
        line.text = result.error;
        lines.push_back(line);

        TextPane::Line keep;
        keep.text = _("Your changes are still here, nothing was lost.");
        lines.push_back(keep);

        std::vector<DialogButton> buttons;
        DialogButton close;
        close.label = _("Close");
        close.action = [this]() { closeDialog(); };
        buttons.push_back(close);

        showDialog(_("Save failed"), lines, buttons, false);
        return;
    }

    m_fileChanged = false;

    std::string message = std::string(_("Saved.")) + " " +
            std::to_string(result.written) + " " + _("written") + ", " +
            std::to_string(result.removed) + " " + _("removed");
    if (result.purged > 0)
        message += ", " + std::to_string(result.purged) + " " + _("obsolete purged");
    if (result.restartRequired)
        message += ". " + std::string(_("Restart calaos_server to apply."));

    m_status = message;

    //The purge is done: the banner has nothing left to announce
    m_root->setWanted(1, false);

    scheduleRebuild();
}

void ConfigTuiScreen::actionQuit()
{
    if (!m_model.hasPendingChanges())
    {
        App::quit();
        return;
    }

    std::vector<TextPane::Line> lines;

    TextPane::Line line;
    line.text = _("There are unsaved changes.");
    lines.push_back(line);

    if (!m_model.obsoletePresent().empty())
    {
        TextPane::Line purge;
        purge.text = _("Leaving without saving writes nothing at all, the obsolete options "
                       "stay in the file.");
        lines.push_back(purge);
    }

    std::vector<DialogButton> buttons;

    DialogButton save;
    save.label = _("Save and quit");
    save.action = [this]()
    {
        closeDialog();
        actionSave(false);
        if (!m_model.hasPendingChanges())
            App::quit();
    };
    buttons.push_back(save);

    DialogButton discard;
    discard.label = _("Discard");
    discard.action = []() { App::quit(); };
    buttons.push_back(discard);

    DialogButton cancel;
    cancel.label = _("Cancel");
    cancel.action = [this]() { closeDialog(); };
    buttons.push_back(cancel);

    showDialog(_("Quit"), lines, buttons, false);
}

void ConfigTuiScreen::actionCtrlC()
{
    /* Ctrl+C arrives here because the interception of the library is off. It
     * still has to mean Copy when the focused field holds a selection, which
     * is what the interception used to check before quitting.
     */
    if (m_current && m_current->hasSelection())
        return;

    if (m_modalCount > 0)
    {
        closeDialog();
        return;
    }

    actionQuit();
}

void ConfigTuiScreen::actionReset()
{
    if (!m_current)
    {
        m_status = _("No option is selected.");
        updateStatusBar();
        return;
    }

    rowReset(m_current);
}

void ConfigTuiScreen::actionToggleAdvanced()
{
    m_showAdvanced = !m_showAdvanced;
    m_status = m_showAdvanced? _("Advanced and deprecated options shown."):
                               _("Advanced and deprecated options hidden.");

    rebuildCategories();
    scheduleRebuild();
    updateStatusBar();
}

void ConfigTuiScreen::actionFullDoc()
{
    if (!m_current)
        return;

    std::vector<TextPane::Line> lines;
    const ConfigOption *option = m_current->option();

    TextPane::Line key;
    key.text = m_current->key();
    key.bold = true;
    lines.push_back(key);

    if (option)
    {
        TextPane::Line label;
        label.text = option->label();
        lines.push_back(label);

        TextPane::Line type;
        type.text = std::string(_("Type:")) + " " + ConfigOptions::typeLabel(option->type()) +
                    "    " + _("Used by:") + " " + ConfigOptions::consumerLabel(option->consumer());
        lines.push_back(type);

        if (option->hasDef())
        {
            TextPane::Line def;
            def.text = std::string(_("Default:")) + " " +
                       (option->def().empty()? std::string(_("(empty)")):
                        option->displayValue(option->def()));
            lines.push_back(def);
        }
        else if (!option->defDynamic().empty())
        {
            TextPane::Line def;
            def.text = std::string(_("Default:")) + " " + option->defDynamic();
            lines.push_back(def);
        }

        if (option->hasRange())
        {
            TextPane::Line range;
            range.text = std::string(_("Range:")) + " " +
                         std::to_string(option->rangeMin()) + " .. " +
                         std::to_string(option->rangeMax());
            lines.push_back(range);
        }

        for (size_t i = 0; i < option->values().size(); i++)
        {
            TextPane::Line value;
            value.text = "  " + option->values()[i].first + " - " +
                         option->valueLabel(option->values()[i].first);
            lines.push_back(value);
        }

        TextPane::Line empty;
        lines.push_back(empty);

        TextPane::Line doc;
        doc.text = option->doc();
        lines.push_back(doc);
    }
    else
    {
        TextPane::Line line;
        line.text = _("This key is not documented. calaos_config keeps it, shows it and "
                      "never changes it on its own.");
        lines.push_back(line);
    }

    std::vector<DialogButton> buttons;
    DialogButton close;
    close.label = _("Close");
    close.action = [this]() { closeDialog(); };
    buttons.push_back(close);

    showDialog(_("Documentation"), lines, buttons, true);
}

void ConfigTuiScreen::actionPending()
{
    std::vector<ConfigModel::Change> changes = m_model.pendingChanges();
    std::vector<TextPane::Line> lines;

    if (changes.empty())
    {
        TextPane::Line line;
        line.text = _("Nothing is waiting to be written.");
        lines.push_back(line);
    }

    for (size_t i = 0; i < changes.size(); i++)
    {
        const ConfigModel::Change &change = changes[i];

        TextPane::Line line;
        line.text = change.key;
        line.bold = true;
        lines.push_back(line);

        TextPane::Line detail;
        switch (change.kind)
        {
        case ConfigModel::Change::Kind::Obsolete:
            detail.text = std::string("  ") + _("obsolete") + " -> " + _("removed");
            break;
        case ConfigModel::Change::Kind::Reset:
            detail.text = std::string("  ") +
                          (change.before.empty()? std::string(_("(empty)")): change.before) +
                          " -> " + _("default");
            break;
        default:
            detail.text = std::string("  ") +
                          (change.beforeSet? (change.before.empty()?
                                              std::string(_("(empty)")): change.before):
                                             std::string(_("(unset)"))) +
                          " -> " +
                          (change.after.empty()? std::string(_("(empty)")): change.after);
            break;
        }
        lines.push_back(detail);
    }

    std::vector<DialogButton> buttons;

    if (!changes.empty())
    {
        DialogButton save;
        save.label = _("Save now");
        save.action = [this]()
        {
            closeDialog();
            actionSave(false);
        };
        buttons.push_back(save);
    }

    DialogButton close;
    close.label = _("Close");
    close.action = [this]() { closeDialog(); };
    buttons.push_back(close);

    showDialog(_("Pending changes"), lines, buttons, true);
}

void ConfigTuiScreen::actionHelp()
{
    std::vector<TextPane::Line> lines;

    const char *help[] = {
        N_("Two panes: the categories on the left, the options of the category on "
           "the right. Tab switches pane, the arrows move inside the pane."),
        N_(""),
        N_("Tab / Shift-Tab      switch pane, and nothing else"),
        N_("Up / Down            move the cursor of the pane you are in"),
        N_("PgUp / PgDn / Home / End   same, by pages and to the ends"),
        N_("Right or Enter       from a category, go to its options"),
        N_("Left                 from the options, go back to the categories"),
        N_("Space                toggle a switch, next value of a list"),
        N_("Enter                edit the value of the selected option"),
        N_("Enter                while editing: keep the value and close the editor"),
        N_("Esc Esc              while editing: put the old value back"),
        N_("Up / Down            while editing: step a number, walk a list"),
        N_("Ctrl-R               reveal a masked secret while editing it"),
        N_("d or Backspace       back to the default value (asks first when it matters)"),
        N_("Empty a field        same thing: an empty field means the default"),
        N_("i                    full documentation of the current option"),
        N_("/                    jump to an option: the cursors follow as you type"),
        N_("Up / Down            while searching: previous / next match (Ctrl-P, Ctrl-N)"),
        N_("Enter                while searching: stay there, on the option pane"),
        N_("Esc Esc              while searching: put both cursors back"),
        N_("v                    what is waiting to be written"),
        N_("a                    show or hide advanced and deprecated options"),
        N_("s or Ctrl-S          save"),
        N_("q or Ctrl-C          quit"),
        N_("Esc Esc              close a dialog (a single Esc is not a key here)"),
        N_(""),
        N_("The letter shortcuts are off while an editor or the search prompt is "
           "open, so that typing an s in a path does not save."),
        N_(""),
        N_("The frames are drawn with box drawing characters when the locale says "
           "UTF-8, and with ASCII otherwise. Start calaos_config with "
           "--frames=unicode or --frames=ascii to decide it yourself, which is what "
           "is needed when the locale does not reach the tool: docker exec and "
           "podman exec forward no LANG. --color=always and --color=never do the "
           "same for the colours, the two are independent."),
        N_(""),
        N_("The same registry is available without a terminal:"),
        N_("  calaos_config options          every option"),
        N_("  calaos_config describe <key>   one option"),
    };

    for (size_t i = 0; i < sizeof(help) / sizeof(help[0]); i++)
    {
        TextPane::Line line;
        line.text = _(help[i]);
        lines.push_back(line);
    }

    std::vector<DialogButton> buttons;
    DialogButton close;
    close.label = _("Close");
    close.action = [this]() { closeDialog(); };
    buttons.push_back(close);

    showDialog(_("Help"), lines, buttons, true);
}

void ConfigTuiScreen::actionSearch()
{
    if (m_searching)
        return;

    leaveEdit();

    m_searching = true;
    m_pattern.clear();
    m_matches.clear();
    m_match = -1;

    //Where to put the two cursors back on Esc, and on a pattern emptied by
    //Backspace
    m_searchCategory = m_category;
    m_searchCursor = m_cursor;
    m_searchPane = m_pane;

    //From here on s, q, d, v, a and i are text, exactly as they are inside an
    //editor: the prompt has to be able to spell smtp_debug
    updateShortcutState();

    m_status.clear();
    updateStatusBar();
}

//---------------------------------------------------------------------------
// Build and run
//---------------------------------------------------------------------------

void ConfigTuiScreen::buildUi()
{
    m_root = std::make_shared<RootView>();
    m_root->message = _("Terminal too small: calaos_config needs at least 80 columns "
                        "and 24 lines. Resize the window, or use calaos_config list, "
                        "describe and set.");

    m_title = barLabel("");
    m_banner = barLabel("");
    m_bar = barLabel("");

    m_categories = std::make_shared<SelectList>();
    m_rowsPane = std::make_shared<RowsPane>();
    m_doc = std::make_shared<TextPane>();

    /* The documentation pane is out of the focus altogether. It used to be a
     * Tab stop with nothing on screen to say so, which is what made Tab look
     * like it had an invisible step between the last option and the
     * categories; leaving it merely focusable would bring that step back the
     * moment it is clicked. The wheel scrolls it, and i opens the full text in
     * a pane that answers to the arrows.
     */
    m_doc->focusable = false;
    m_doc->tab_stop = false;

    m_docBorder = framed(_("Documentation"), m_doc);
    m_docBorder->fixed_height = 8;

    //One frame for the two panes, with one rule between them: MainPanes draws
    //it itself, see the note on the class
    m_panes = std::make_shared<MainPanes>();
    m_panes->leftWidth = 24;
    m_panes->setLeftTitle(_("Categories"));
    m_panes->add(m_categories);
    m_panes->add(m_rowsPane);

    std::shared_ptr<Widget> middle = m_panes;

    bool hasObsolete = !m_model.obsoletePresent().empty();
    if (hasObsolete)
    {
        m_banner->set_text(StyledText(std::string(" /!\\ ") +
                std::to_string(m_model.obsoletePresent().size()) + " " +
                _("obsolete options found, they will be removed when you save (v to see them)")));
    }

    m_root->addChild(m_title, true);
    m_root->addChild(m_banner, hasObsolete);
    m_root->addChild(middle, true);
    m_root->addChild(m_docBorder, true);
    m_root->addChild(m_bar, true);

    m_title->fixed_height = 1;
    m_banner->fixed_height = 1;
    m_bar->fixed_height = 1;

    ConfigTuiScreen *self = this;

    m_categories->on_select = [self](int index)
    {
        self->m_category = index;
        self->rebuildRows();
    };

    m_panes->handler = [self](const Event &event) { return self->paneEvent(event); };

    rebuildCategories();
    rebuildRows();
    updateTitle();
    updateStatusBar();
}

int ConfigTuiScreen::run()
{
    std::string error;
    if (!m_model.load(&error))
    {
        std::cerr << error << std::endl;
        std::cerr << _("Config file:") << " " << m_model.configFile() << std::endl;
        return 1;
    }

    //Every colour of a default constructed Theme is "terminal default", so the
    //monochrome mode emits \033[39m / \033[49m and never a truecolor escape
    Theme::set_theme(m_mono? Theme(): Theme::Dark());

    buildUi();

    //Ctrl+C must reach actionCtrlC(), not the mandatory exit of the library
    m_app.set_intercept_ctrl_c(false);

    /* consume is false on purpose: with a live selection Ctrl+C means Copy, and
     * the event still has to reach the focused field.
     */
    m_app.register_key('c', [this]() { actionCtrlC(); }, true, false, false, false);
    m_app.register_key('s', [this]() { actionSave(false); }, true);

    //Watch the file another program may be writing behind our back
    m_app.add_timer(2000, [this]()
    {
        bool changed = m_model.hasChangedOnDisk();
        if (changed == m_fileChanged)
            return;

        m_fileChanged = changed;
        updateTitle();
    });


    /* cpp-tui computes the initial focus before the first layout, when every
     * widget is still 0x0 and therefore not focusable. Give the tree its real
     * size first, so that App::run() finds the category list and focuses it:
     * without this the first keystroke goes nowhere.
     */
    std::pair<int, int> size = Terminal::getSize();
    App::update_screen_size(size.first, size.second);
    m_root->width = size.first;
    m_root->height = size.second;
    m_root->layout();

    if (tcgetattr(STDIN_FILENO, &g_savedTermios) == 0)
        g_termiosSaved = true;

    struct sigaction action;
    struct sigaction oldTerm, oldInt, oldHup;

    memset(&action, 0, sizeof(action));
    action.sa_handler = signalHandler;
    sigemptyset(&action.sa_mask);

    sigaction(SIGTERM, &action, &oldTerm);
    sigaction(SIGINT, &action, &oldInt);
    sigaction(SIGHUP, &action, &oldHup);

    int ret = 0;
    std::string failure;

    /* The library restores the terminal from ~Terminal(), which runs while the
     * stack of run() unwinds: catching here is enough to leave a usable
     * terminal behind, and the message is printed once it is back.
     */
    try
    {
        m_app.run(m_root);
    }
    catch (const std::exception &e)
    {
        failure = e.what();
        ret = 1;
    }
    catch (...)
    {
        failure = _("unknown error");
        ret = 1;
    }

    sigaction(SIGTERM, &oldTerm, nullptr);
    sigaction(SIGINT, &oldInt, nullptr);
    sigaction(SIGHUP, &oldHup, nullptr);

    if (!failure.empty())
        std::cerr << _("The configuration browser stopped on an error:") << " "
                  << failure << std::endl;

    return ret;
}

//---------------------------------------------------------------------------
// Colour capability
//---------------------------------------------------------------------------

/* Whether the terminal is plausibly able to show colours, TERM being the only
 * thing there is to go on once COLORTERM is absent.
 *
 * The list is a whitelist of families rather than a blacklist, so an unknown
 * TERM stays monochrome; the browser is still perfectly usable there, since
 * the focus is carried by the "> " and "* " cursors and by bold, never by a
 * colour.
 *
 * Colour is preferred as soon as the family is known, even though the library
 * emits truecolor SGR (ESC[38;2;r;g;b m) and nothing else. That is on purpose
 * and must not be tightened again: a terminal that only understands 256
 * colours either approximates those sequences or drops them, and in both
 * cases the escape is a well formed CSI ... m whose parameters are consumed
 * silently. Nothing is left on the screen, no cell is corrupted, and the
 * layout is untouched. Refusing colour to every terminal that does not
 * announce COLORTERM is what made the browser look like a 1990 installer on
 * the ordinary xterm-256color of a normal console.
 */
bool terminalHasColor(const char *term)
{
    //COLORTERM is proof, when it is there at all
    const char *colorTerm = getenv("COLORTERM");
    if (colorTerm && (std::string(colorTerm) == "truecolor" ||
                      std::string(colorTerm) == "24bit"))
        return true;

    //No TERM at all: nothing says this is a terminal, stay on the safe side
    if (!term || term[0] == '\0')
        return false;

    std::string name(term);

    //Any -256color, -88color or -color variant announces what it can do
    if (name.find("256color") != std::string::npos ||
        name.find("88color") != std::string::npos ||
        name.find("-color") != std::string::npos)
        return true;

    /* Families that have been in colour for decades. vt100, vt220, ansi,
     * cons25 and everything unknown are deliberately not here.
     */
    const char *families[] = {
        "xterm", "screen", "tmux", "rxvt", "linux", "alacritty", "kitty",
        "foot", "vte", "konsole", "st-", "wezterm", "contour", "ghostty"
    };

    for (const char *family: families)
    {
        if (name.compare(0, strlen(family), family) == 0)
            return true;
    }

    return false;
}

}

int Calaos::runConfigTui(const std::string &configFile, TuiColorMode color, TuiFrameMode frames)
{
    /* First thing done here, before any widget is built and therefore before
     * anything can ask for a frame glyph.
     */
    resolveFrameStyle(frames);

    const char *term = getenv("TERM");

    //cpp-tui speaks ANSI and nothing else: there is no point opening a full
    //screen editor on a terminal that cannot move its cursor.
    if (term && (std::string(term) == "dumb" || std::string(term).empty()))
    {
        std::cerr << _("TERM=dumb: this terminal cannot display the interactive browser.")
                  << std::endl;
        std::cerr << _("Use calaos_config options, describe <key>, get <key> and set <key> <value>.")
                  << std::endl;
        return 1;
    }

    /* Colour unless the terminal is plausibly unable to show it, and never
     * mind the frames: which glyphs they are drawn with is decided just above,
     * in resolveFrameStyle(), and has nothing to do with the question asked
     * here. --color=never and NO_COLOR are the escape hatches, and they leave a
     * screen with no colour escape at all.
     */
    bool mono = true;

    if (color == TuiColorMode::Always)
        mono = false;
    else if (color == TuiColorMode::Never)
        mono = true;
    else
        mono = getenv("NO_COLOR") != nullptr || !terminalHasColor(term);

    ConfigTuiScreen screen(configFile, mono);

    return screen.run();
}
