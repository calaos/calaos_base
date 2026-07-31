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

#include <algorithm>
#include <iostream>
#include <memory>

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
 *   being edited, or while a dialog is open.
 *
 * - Terminal restoration. cpp-tui only restores the terminal from ~Terminal(),
 *   i.e. on the normal way out of App::run(). run() is wrapped in a try/catch
 *   for the exception path, and SIGTERM/SIGINT/SIGHUP are caught by an
 *   async-signal-safe handler that writes the restore sequence itself before
 *   letting the default action kill the process.
 *
 * - Monochrome. The library has no reverse video and emits colours in
 *   truecolor only, so with the default theme (every colour "terminal
 *   default") nothing would show the focus. Bold, a "> " marker on the line
 *   each cursor sits on ("* " when the other pane has the keyboard) and ASCII
 *   borders carry that job instead.
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
 * screen view (i, v, ?, /). cpptui::Paragraph cannot do it because it does not
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

    bool on_event(const Event &) override { return false; }
};

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
 */
class MainPanes: public Horizontal
{
public:
    MainPanes()
    {
        focusable = true;
        tab_stop = true;
    }

    //Returns true when the screen consumed the event
    std::function<bool(const Event &)> handler;

    bool on_event(const Event &event) override
    {
        if (!visible || !handler)
            return false;

        if (!event.is_key_event() && !event.is_mouse_event() &&
            event.type != EventType::Paste)
            return false;

        return handler(event);
    }
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

//A frame around a widget, ASCII so that it survives a terminal without any
//box drawing character
std::shared_ptr<TuiBorder> framed(const std::string &title, std::shared_ptr<Widget> child)
{
    std::shared_ptr<TuiBorder> border = std::make_shared<TuiBorder>(BorderStyle::ASCII);
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

    //--- Keyboard model ----------------------------------------------------
    bool paneEvent(const Event &event);
    bool paneKey(const Event &event);
    bool paneMouse(const Event &event);
    bool editKey(const Event &event);

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
    void updateTitle();
    void updateDoc();
    void updateStatusBar();
    void updateShortcutState();
    void ensureRowVisible(ConfigRow *row);

    std::vector<DisplayRow> displayRows() const;
    bool rowFiltered(const ConfigModel::Row &row) const;
    bool rowMatches(const ConfigModel::Row &row) const;

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
    std::shared_ptr<TuiBorder> m_categoriesBorder;
    std::shared_ptr<TuiBorder> m_rowsBorder;
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

    std::string m_search;
    std::shared_ptr<Dialog> m_dialog;
    std::shared_ptr<TextPane> m_dialogText;
    std::shared_ptr<TuiInput> m_searchInput;
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

    if (!m_search.empty() && !rowMatches(row))
        return true;

    return false;
}

bool ConfigTuiScreen::rowMatches(const ConfigModel::Row &row) const
{
    std::string needle = Utils::str_to_lower(m_search);

    if (Utils::str_to_lower(row.key).find(needle) != std::string::npos)
        return true;

    if (row.option)
    {
        if (Utils::str_to_lower(row.option->label()).find(needle) != std::string::npos)
            return true;
        if (Utils::str_to_lower(row.option->doc()).find(needle) != std::string::npos)
            return true;
    }

    return false;
}

std::vector<ConfigTuiScreen::DisplayRow> ConfigTuiScreen::displayRows() const
{
    std::vector<DisplayRow> result;
    const std::vector<ConfigModel::Category> &categories = m_model.categories();

    //A search flattens every category into one list
    if (!m_search.empty())
    {
        for (size_t c = 0; c < categories.size(); c++)
        {
            for (size_t r = 0; r < categories[c].rows.size(); r++)
            {
                if (rowFiltered(categories[c].rows[r]))
                    continue;

                DisplayRow entry;
                entry.category = c;
                entry.row = r;
                result.push_back(entry);
            }
        }

        return result;
    }

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

    if (!m_search.empty())
    {
        //One virtual entry while a search is on, so the flat result list has
        //something to belong to
        SelectList::Item item;
        item.label = _("Search results");
        item.count = (int)displayRows().size();
        m_categories->items.push_back(item);
        m_category = 0;
        m_categories->selected = 0;

        return;
    }

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
        return paneMouse(event);

    if (m_editing)
        return editKey(event);

    if (event.type == EventType::Paste)
        return false;

    if (!event.is_key_event())
        return false;

    return paneKey(event);
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

    std::string title;
    if (!m_search.empty())
        title = _("Search results");
    else if (m_category >= 0 && m_category < (int)m_visibleCategories.size())
        title = categories[m_visibleCategories[m_category]].label;

    if (rows.empty())
        title += std::string(" - ") + _("no option matches");

    m_rowsBorder->set_title(title, Alignment::Left);

    updateDoc();
    updateStatusBar();
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
    if (!m_status.empty())
    {
        m_bar->set_text(StyledText(" " + m_status));
        return;
    }

    /* Three bars, one per state, so that the keys the bar names are the keys
     * that work. Plain ASCII: the arrow glyphs would be mojibake on the
     * terminals the monochrome mode exists for.
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
 * editor, or a dialog. The editors of the option pane never hold the focus of
 * the library any more, so "a text field has the focus" is not a question that
 * can be asked here: only edit mode tells whether an s is a save or a letter.
 */
void ConfigTuiScreen::updateShortcutState()
{
    setLetterShortcuts(m_modalCount == 0 && !m_editing);
}

//---------------------------------------------------------------------------
// Dialogs
//---------------------------------------------------------------------------

std::shared_ptr<Dialog> ConfigTuiScreen::makeDialog(const std::string &title,
                                                    const std::vector<TextPane::Line> &lines,
                                                    const std::vector<DialogButton> &buttons,
                                                    int width, int height, bool focusText)
{
    std::shared_ptr<Dialog> dialog = std::make_shared<Dialog>(&m_app, BorderStyle::ASCII);

    //The shadow of the library darkens real RGB values, which a terminal
    //without truecolor cannot show
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
    m_searchInput.reset();

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
        N_("/                    search, an empty search clears the filter"),
        N_("v                    what is waiting to be written"),
        N_("a                    show or hide advanced and deprecated options"),
        N_("s or Ctrl-S          save"),
        N_("q or Ctrl-C          quit"),
        N_("Esc Esc              close a dialog (a single Esc is not a key here)"),
        N_(""),
        N_("The letter shortcuts are off while an editor is open, so that typing an "
           "s in a path does not save."),
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
    std::pair<int, int> size = Terminal::getSize();
    int width = std::max(40, size.first - 8);
    int height = std::max(10, size.second - 6);

    std::shared_ptr<Dialog> dialog = std::make_shared<Dialog>(&m_app, BorderStyle::ASCII);
    dialog->shadow = false;
    dialog->modal = true;
    dialog->set_title(_("Search"), Alignment::Left);
    dialog->fixed_width = width;
    dialog->fixed_height = height;
    dialog->width = width;
    dialog->height = height;

    std::shared_ptr<Vertical> body = std::make_shared<Vertical>();

    std::shared_ptr<TuiInput> input = std::make_shared<TuiInput>();
    input->fixed_height = 1;
    input->placeholder = _("key, label or description; empty clears the filter");
    input->set_value(m_search);
    m_searchInput = input;

    std::shared_ptr<TextPane> results = std::make_shared<TextPane>();
    results->focusable = false;
    results->tab_stop = false;
    m_dialogText = results;

    body->add(input);
    body->add(results);

    std::shared_ptr<Horizontal> buttonRow = std::make_shared<Horizontal>();
    buttonRow->fixed_height = 1;

    std::shared_ptr<MonoButton> apply = std::make_shared<MonoButton>(_("Apply"), [this]()
    {
        std::string needle = m_searchInput? m_searchInput->get_value(): std::string();
        closeDialog();
        m_search = needle;
        rebuildCategories();
        scheduleRebuild();
    });
    apply->fixed_width = (int)std::string(_("Apply")).size() + 8;
    buttonRow->add(apply);

    std::shared_ptr<MonoButton> clear = std::make_shared<MonoButton>(_("Clear"), [this]()
    {
        closeDialog();
        m_search.clear();
        rebuildCategories();
        scheduleRebuild();
    });
    clear->fixed_width = (int)std::string(_("Clear")).size() + 8;
    buttonRow->add(clear);
    buttonRow->add(std::make_shared<HorizontalSpacer>());

    body->add(buttonRow);
    dialog->add(body);

    //Incremental: the result list follows every keystroke
    ConfigTuiScreen *self = this;
    TextPane *rawResults = results.get();

    std::function<void(const std::string &)> refresh = [self, rawResults](const std::string &needle)
    {
        std::vector<TextPane::Line> lines;
        std::string saved = self->m_search;
        self->m_search = needle;

        const std::vector<ConfigModel::Category> &categories = self->m_model.categories();
        int count = 0;

        if (!needle.empty())
        {
            for (size_t c = 0; c < categories.size(); c++)
            {
                for (size_t r = 0; r < categories[c].rows.size(); r++)
                {
                    const ConfigModel::Row &row = categories[c].rows[r];
                    if (self->rowFiltered(row))
                        continue;

                    TextPane::Line line;
                    line.text = categories[c].label + " > " +
                                (row.option? row.option->label(): row.key) +
                                "  (" + row.key + ")";
                    line.wrap = false;
                    lines.push_back(line);
                    count++;
                }
            }
        }

        self->m_search = saved;

        if (needle.empty())
        {
            TextPane::Line line;
            line.text = _("Type to search. Applying an empty search shows every category again.");
            lines.push_back(line);
        }
        else if (count == 0)
        {
            TextPane::Line line;
            line.text = _("No option matches.");
            lines.push_back(line);
        }

        rawResults->setLines(lines);
    };

    input->on_change = [refresh](std::string value) { refresh(value); };
    input->onValidate = [self, refresh](bool cancel)
    {
        if (cancel)
        {
            self->closeDialog();
            return;
        }

        std::string needle = self->m_searchInput? self->m_searchInput->get_value(): std::string();
        self->closeDialog();
        self->m_search = needle;
        self->rebuildCategories();
        self->scheduleRebuild();
    };

    refresh(m_search);

    openDialog(dialog);
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

    m_categoriesBorder = framed(_("Categories"), m_categories);
    m_categoriesBorder->fixed_width = 26;

    m_rowsBorder = framed("", m_rowsPane);
    m_docBorder = framed(_("Documentation"), m_doc);
    m_docBorder->fixed_height = 8;

    m_panes = std::make_shared<MainPanes>();
    m_panes->add(m_categoriesBorder);
    m_panes->add(m_rowsBorder);

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

}

int Calaos::runConfigTui(const std::string &configFile, TuiColorMode color)
{
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

    /* The library only knows how to emit truecolor. Without it the themed
     * colours turn into mush, so the default is decided the same way the CLI
     * decides its own colours, plus the truecolor check.
     */
    bool mono = true;

    if (color == TuiColorMode::Always)
        mono = false;
    else if (color == TuiColorMode::Auto)
    {
        const char *colorTerm = getenv("COLORTERM");
        mono = getenv("NO_COLOR") != nullptr ||
               !colorTerm ||
               (std::string(colorTerm) != "truecolor" && std::string(colorTerm) != "24bit");
    }

    ConfigTuiScreen screen(configFile, mono);

    return screen.run();
}
