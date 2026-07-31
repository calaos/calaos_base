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

#include "ConfigRow.h"

#include <Utils.h>

using namespace Calaos;
using namespace cpptui;

//---------------------------------------------------------------------------
// Widget helpers
//---------------------------------------------------------------------------

bool TuiInput::on_event(const Event &event)
{
    /* A hidden field can still be the one the option pane hands its events
     * to, for one iteration of the loop. Let the event go through instead of
     * editing something nobody sees.
     */
    if (!visible)
        return false;

    if (event.is_key_event() && has_focus())
    {
        //Reveal a masked secret while it is being edited
        if (revealable && event.ctrl && (event.key == 'r' || event.key == 'R'))
        {
            is_password = !is_password;
            return true;
        }

        if (event.is_enter())
        {
            if (onValidate)
                onValidate(false);
            return true;
        }

        /* Escape only reaches an application through this library when it is
         * pressed twice: a lone ESC leaves the parser waiting for the rest of
         * a sequence, and the pair comes out as Alt + ESC.
         */
        if (event.is_escape())
        {
            if (onValidate)
                onValidate(true);
            return true;
        }

        /* The steppers of NumberInput are buttons, i.e. reachable with the
         * mouse only. Up and Down do the same thing from the keyboard.
         */
        if (onStep && (event.is_nav_up() || event.is_nav_down()))
        {
            if (!readOnly)
                onStep(event.is_nav_up()? 1: -1);
            return true;
        }

        if (readOnly && (event.is_printable() || event.is_backspace() ||
                         event.is_delete() || event.is_paste()))
            return true;
    }

    if (readOnly && event.type == EventType::Paste)
        return true;

    return Input::on_event(event);
}

void MonoButton::render(Buffer &buffer)
{
    if (width < 1 || height < 1)
        return;

    const Theme &theme = Theme::current();

    /* Everything here goes through the theme, so a theme made of default
     * colours emits \033[39m / \033[49m and nothing else. The focus is shown
     * with bold and underline, which are plain SGR attributes and survive a
     * terminal without colours at all.
     */
    Color fg = has_focus()? theme.primary.resolve(theme.foreground): theme.foreground;
    Color bg = theme.panel_bg;

    //Brackets and arrows, not colours: the focus has to be visible on a
    //terminal that has none
    std::string text = get_label();
    if (decorated)
        text = has_focus()? ">[ " + text + " ]<": " [ " + text + " ] ";

    for (int i = 0; i < width; i++)
    {
        Cell cell;
        cell.content = " ";
        cell.fg_color = fg;
        cell.bg_color = bg;
        buffer.set(x + i, y, cell);
    }

    int textWidth = TextHelper::utf8_display_width(text);
    int offset = (width - textWidth) / 2;
    if (offset < 0)
        offset = 0;

    render_utf8_text(buffer, text, x + offset, y, width - offset, fg, bg,
                     has_focus(), false, has_focus());
}

namespace
{

//Width of the two fixed columns of a line, in cells
const int NAME_WIDTH = 24;
const int SUFFIX_WIDTH = 11;

//A label of the option pane: never focusable, so that a click on it does not
//steal the focus from the editors and confuse the shortcut handling
std::shared_ptr<Label> plainLabel(const StyledText &text, int fixedWidth)
{
    std::shared_ptr<Label> label = std::make_shared<Label>(text);
    label->focusable = false;
    label->selectable = false;
    label->fixed_height = 1;
    if (fixedWidth > 0)
        label->fixed_width = fixedWidth;

    return label;
}

/* The editors of a line never take the focus of the library.
 * =========================================================
 *
 * The option pane is the single tab stop of the right hand side: it keeps a
 * cursor of its own and hands its events to the widget of the current line
 * while that line is being edited. A widget that could be tabbed to, or that
 * grabbed the focus when it was clicked, would put the pane and the library
 * out of step - which is exactly the "invisible tab stop" this model removes.
 */
void makePassive(const std::shared_ptr<Widget> &widget)
{
    if (!widget)
        return;

    widget->focusable = false;
    widget->tab_stop = false;
}

}

//---------------------------------------------------------------------------
// ConfigRow
//---------------------------------------------------------------------------

ConfigRow::ConfigRow(App &app, ConfigRowHost &host, const ConfigModel::Row &row,
                     bool readOnly, bool mono):
    m_app(app),
    m_host(host),
    m_key(row.key),
    m_option(row.option),
    m_readOnly(readOnly),
    m_mono(mono),
    m_isSet(row.isSet),
    m_isDefault(row.isDefault),
    m_dirty(row.dirty)
{
    m_line = std::make_shared<Horizontal>();
    m_line->fixed_height = 1;
    makePassive(m_line);

    m_marker = plainLabel(" ", 2);
    m_name = plainLabel(" ", NAME_WIDTH);
    m_suffix = plainLabel(" ", SUFFIX_WIDTH);

    /* An undocumented key gets a plain text field with no validation at all:
     * the registry knows nothing about it, and refusing what another program
     * wrote would be the surest way to lose it.
     */
    if (!m_option)
        buildInput(row);
    else
    {
        switch (m_option->type())
        {
        case ConfigOption::Type::Bool:
            buildToggle(row);
            break;
        case ConfigOption::Type::Int:
        case ConfigOption::Type::Float:
        case ConfigOption::Type::Port:
            buildNumber(row);
            break;
        case ConfigOption::Type::Enum:
            buildDropdown(row);
            break;
        default:
            buildInput(row);
            break;
        }
    }

    makePassive(m_editor);
    makePassive(m_input);
    makePassive(m_number);
    makePassive(m_toggle);
    makePassive(m_dropdown);

    m_line->add(m_marker);
    m_line->add(m_name);
    m_line->add(m_editor);
    m_line->add(m_suffix);

    refresh(row);
}

std::shared_ptr<Widget> ConfigRow::editWidget() const
{
    //A NumberInput is a row of three widgets: the one that shows a cursor and
    //answers to the keyboard is its text field, not the group.
    if (m_kind == Kind::Text || m_kind == Kind::Number)
        return m_input;

    return m_editor;
}

std::string ConfigRow::editableValue(const ConfigModel::Row &row) const
{
    //The model puts the default in value when the key is unset. The editor
    //stays empty in that case and shows the default as a placeholder: what is
    //typed is what gets written.
    return row.isSet? row.value: std::string();
}

void ConfigRow::submit(const std::string &value)
{
    if (m_updating)
        return;

    std::string error;
    if (!m_host.rowSetValue(this, value, &error))
    {
        m_host.rowMessage(this, error);
        return;
    }

    m_host.rowMessage(this, std::string());
}

void ConfigRow::buildInput(const ConfigModel::Row &row)
{
    std::shared_ptr<TuiInput> input = std::make_shared<TuiInput>();

    m_input = input;
    m_editor = input;
    m_kind = Kind::Text;

    input->readOnly = m_readOnly;

    if (m_option)
    {
        if (m_option->isSecret() ||
            m_option->type() == ConfigOption::Type::Password ||
            m_option->type() == ConfigOption::Type::Token)
        {
            input->is_password = true;
            input->password_char = "*";
            input->revealable = true;
        }

        if (m_option->pattern() && m_option->pattern()[0])
            input->regex_pattern = m_option->pattern();

        if (m_option->hasDef())
            input->placeholder = m_option->displayValue(m_option->def());
        else if (!m_option->defDynamic().empty())
            input->placeholder = m_option->defDynamic();
    }

    //The red of an invalid value is hardcoded in the library: on a terminal
    //without truecolor it has to go back to the default colour
    if (m_mono)
        input->error_fg_color = Color();

    input->set_value(editableValue(row));

    ConfigRow *self = this;

    /* Typing only checks, it does not write: the model is changed when the
     * edit is validated. Committing every keystroke would store the "999" of
     * a "99999" that the registry is about to refuse.
     */
    input->on_change = [self](std::string value)
    {
        if (self->m_updating)
            return;

        std::string error;
        if (!value.empty() && !self->m_host.rowValidate(self, value, &error))
            self->m_host.rowMessage(self, error);
        else
            self->m_host.rowMessage(self, std::string());
    };
}

void ConfigRow::buildNumber(const ConfigModel::Row &row)
{
    //A decimal value cannot go through the steppers of NumberInput: they are
    //integer only (see the note in ConfigTui.cpp). Latitude and longitude keep
    //the widget, without its buttons.
    bool integer = m_option->type() != ConfigOption::Type::Float;

    std::shared_ptr<NumberInput> number = std::make_shared<NumberInput>(0, integer);
    std::shared_ptr<TuiInput> input = std::make_shared<TuiInput>();

    m_number = number;
    m_input = input;
    m_editor = number;
    m_kind = Kind::Number;

    input->readOnly = m_readOnly;
    input->regex_pattern = integer? "^-?[0-9]*$": "^-?[0-9]*\\.?[0-9]*$";

    if (m_mono)
        input->error_fg_color = Color();

    if (m_option->hasDef())
        input->placeholder = m_option->def();
    else if (!m_option->defDynamic().empty())
        input->placeholder = m_option->defDynamic();

    number->min_value = -1000000;
    number->max_value = 1000000;

    if (m_option->hasRange())
    {
        number->min_value = (int)m_option->rangeMin();
        number->max_value = (int)m_option->rangeMax();
    }
    else if (m_option->type() == ConfigOption::Type::Port)
    {
        number->min_value = 1;
        number->max_value = 65535;
    }

    number->input_ = input;

    if (integer)
    {
        /* The steppers of the library are cpptui::Button, whose colours are
         * hardcoded and always resolved to a real RGB value. Replace them, and
         * keep them out of the focus like every other widget of a line.
         */
        NumberInput *rawNumber = number.get();

        std::shared_ptr<MonoButton> down =
                std::make_shared<MonoButton>("[-]", [rawNumber]() { rawNumber->decrement(); });
        std::shared_ptr<MonoButton> up =
                std::make_shared<MonoButton>("[+]", [rawNumber]() { rawNumber->increment(); });

        //Three columns each: at the minimum size of 80 columns every one of
        //them is taken from the value being edited
        down->decorated = false;
        up->decorated = false;
        down->fixed_width = 3;
        up->fixed_width = 3;
        makePassive(down);
        makePassive(up);

        number->btn_down = down;
        number->btn_up = up;
    }

    number->rebuild_layout();

    NumberInput *stepper = number.get();
    TuiInput *field = input.get();
    std::string startValue = m_option->hasDef()? m_option->def(): std::string();

    input->onStep = [stepper, field, startValue](int direction)
    {
        /* Stepping an unset option starts from its default, not from zero:
         * pressing Up on a port whose default is 5454 has to give 5455.
         */
        if (field->get_value().empty() && !startValue.empty())
            field->set_value(startValue);

        if (direction > 0)
            stepper->increment();
        else
            stepper->decrement();
    };

    input->set_value(editableValue(row));

    ConfigRow *self = this;

    input->on_change = [self](std::string value)
    {
        if (self->m_updating)
            return;

        std::string error;
        if (!value.empty() && !self->m_host.rowValidate(self, value, &error))
            self->m_host.rowMessage(self, error);
        else
            self->m_host.rowMessage(self, std::string());
    };

    //Clicking a stepper goes through NumberInput, not through the field
    number->on_change = [self](int)
    {
        if (!self->m_updating)
            self->commit();
    };
}

void ConfigRow::buildToggle(const ConfigModel::Row &row)
{
    std::shared_ptr<ToggleSwitch> toggle =
            std::make_shared<ToggleSwitch>(StyledText(""), row.value == "true");

    m_toggle = toggle;
    m_editor = toggle;
    m_kind = Kind::Bool;

    toggle->on_label = _("[ Yes ]");
    toggle->off_label = _("[ No  ]");

    ConfigRow *self = this;

    toggle->on_change = [self](bool on)
    {
        if (self->m_updating)
            return;

        self->submit(on? "true": "false");
    };
}

void ConfigRow::buildDropdown(const ConfigModel::Row &row)
{
    //An enumeration with no declared value would leave an empty list: fall
    //back to free text rather than to an unusable widget
    if (m_option->values().empty())
    {
        buildInput(row);
        return;
    }

    std::shared_ptr<Dropdown> dropdown = std::make_shared<Dropdown>(&m_app);

    m_dropdown = dropdown;
    m_editor = dropdown;
    m_kind = Kind::Enum;

    std::vector<StyledText> labels;
    for (size_t i = 0; i < m_option->values().size(); i++)
    {
        const std::string &value = m_option->values()[i].first;
        m_enumValues.push_back(value);
        //The human label, never the raw key
        labels.push_back(StyledText(m_option->valueLabel(value)));
    }

    dropdown->set_options(labels);

    /* No on_change: the popup of the library is the only thing that fires it,
     * and nothing can open that popup any more. It steals the focus of the
     * whole application when it appears -- Dropdown::toggle() builds its
     * Dialog without clearing steal_focus -- and answers to the mouse only,
     * so a keyboard user would be locked out of the browser. Space and Enter
     * pick the value in place instead, and updateDoc() lists what there is to
     * pick from.
     */
}

bool ConfigRow::commit()
{
    if (m_updating || !m_input)
        return true;

    std::string value = m_input->get_value();

    if (value.empty())
    {
        //An emptied field means "remove the key and use the default again".
        //The host asks for a confirmation when the option needs one.
        if (m_isSet)
        {
            m_host.rowReset(this);
            return true;
        }

        m_host.rowMessage(this, std::string());
        return true;
    }

    std::string error;
    if (!m_host.rowSetValue(this, value, &error))
    {
        m_host.rowMessage(this, error);
        return false;
    }

    m_host.rowMessage(this, std::string());

    return true;
}

void ConfigRow::setSelected(bool selected, bool active)
{
    /* The default theme shows the focus with colours only, and there is no
     * reverse video in this library: without this marker nothing at all points
     * at the current line on a monochrome terminal. "* " for the line the
     * cursor sits on while the keyboard is driving the other pane, same as the
     * category column.
     */
    m_marker->set_text(StyledText(selected? (active? "> ": "* "): "  "));

    if (selected != m_selected)
    {
        m_selected = selected;

        StyledText styled;
        std::string name = m_option? m_option->label(): m_key;
        if (m_dirty || m_selected)
            styled.bold(name);
        else
            styled.add(name);
        m_name->set_text(styled);
    }
}

bool ConfigRow::activate()
{
    if (m_kind == Kind::Bool)
    {
        if (m_readOnly)
        {
            m_host.rowMessage(this, _("The configuration file is read only."));
            return true;
        }

        m_toggle->is_on = !m_toggle->is_on;
        submit(m_toggle->is_on? "true": "false");

        return true;
    }

    if (m_kind == Kind::Enum)
    {
        if (m_readOnly)
        {
            m_host.rowMessage(this, _("The configuration file is read only."));
            return true;
        }

        if (m_enumValues.empty())
            return true;

        int index = m_dropdown->selected_index + 1;
        if (index < 0 || index >= (int)m_enumValues.size())
            index = 0;

        m_dropdown->selected_index = index;
        submit(m_enumValues[index]);

        return true;
    }

    return false;
}

void ConfigRow::beginEdit()
{
    if (m_editing || !editable())
        return;

    m_editing = true;

    /* on_focus() is the only public way in: it makes the widget draw itself
     * focused and answer to the keyboard, without moving the focus the library
     * keeps, which stays on the option pane.
     */
    std::shared_ptr<Widget> widget = editWidget();
    if (widget)
        widget->on_focus();
}

bool ConfigRow::endEdit(bool cancel)
{
    if (!m_editing)
        return true;

    if (m_kind == Kind::Enum)
    {
        if (cancel)
        {
            m_dropdown->selected_index = m_enumIndex;
            m_host.rowMessage(this, std::string());
        }
        else if (m_dropdown->selected_index >= 0 &&
                 m_dropdown->selected_index < (int)m_enumValues.size() &&
                 m_dropdown->selected_index != m_enumIndex)
        {
            submit(m_enumValues[m_dropdown->selected_index]);
        }
    }
    else
    {
        if (cancel)
        {
            m_updating = true;
            m_input->set_value(m_modelValue);
            m_updating = false;
            m_host.rowMessage(this, std::string());
        }
        else if (!commit())
        {
            //The registry refused it: the editor stays open on the value that
            //was typed, with the reason in the documentation pane
            return false;
        }
    }

    m_editing = false;

    std::shared_ptr<Widget> widget = editWidget();
    if (widget)
        widget->on_blur();

    return true;
}

bool ConfigRow::editEvent(const Event &event)
{
    if (!m_editing)
        return false;

    if (m_kind == Kind::Enum)
    {
        if (!event.is_key_event() || m_enumValues.empty())
            return false;

        if (!event.is_nav_up() && !event.is_nav_down())
            return false;

        if (m_readOnly)
            return true;

        int index = m_dropdown->selected_index + (event.is_nav_down()? 1: -1);
        if (index < 0)
            index = (int)m_enumValues.size() - 1;
        if (index >= (int)m_enumValues.size())
            index = 0;

        m_dropdown->selected_index = index;

        return true;
    }

    std::shared_ptr<Widget> widget = editWidget();
    if (!widget)
        return false;

    bool consumed = widget->on_event(event);

    /* Input::on_event() grabs the focus of the library when it is clicked,
     * which would take it away from the option pane and leave the keyboard
     * dead. Put it back, and keep the field drawing its cursor.
     */
    if (event.is_mouse_event())
    {
        if (restoreFocus)
            restoreFocus();
        widget->on_focus();
    }

    return consumed;
}

bool ConfigRow::hasSelection() const
{
    return m_input && m_input->has_selection();
}

void ConfigRow::detach()
{
    m_updating = true;
    restoreFocus = nullptr;

    if (m_editing)
    {
        m_editing = false;
        std::shared_ptr<Widget> widget = editWidget();
        if (widget)
            widget->on_blur();
    }

    if (m_input)
    {
        m_input->on_change = nullptr;
        std::shared_ptr<TuiInput> input = std::dynamic_pointer_cast<TuiInput>(m_input);
        if (input)
            input->onValidate = nullptr;
    }

    if (m_number)
        m_number->on_change = nullptr;

    if (m_toggle)
        m_toggle->on_change = nullptr;

    if (m_dropdown)
        m_dropdown->on_change = nullptr;

    m_line->visible = false;
    if (m_editor)
        m_editor->visible = false;
}

void ConfigRow::refresh(const ConfigModel::Row &row, bool updateEditor)
{
    m_updating = true;

    m_isSet = row.isSet;
    m_isDefault = row.isDefault;
    m_dirty = row.dirty;
    m_modelValue = editableValue(row);

    //Entry the model holds, whether the key is set or not: the dropdown always
    //shows something, and a cancelled edit goes back to it
    m_enumIndex = -1;
    for (size_t i = 0; i < m_enumValues.size(); i++)
    {
        if (m_enumValues[i] == row.value)
        {
            m_enumIndex = (int)i;
            break;
        }
    }

    if (updateEditor)
    {
        if (m_input)
            m_input->set_value(m_modelValue);

        if (m_toggle)
            m_toggle->is_on = row.value == "true";

        if (m_dropdown)
            m_dropdown->selected_index = m_enumIndex;
    }

    std::string name = m_option? m_option->label(): m_key;
    StyledText styled;
    if (m_dirty || m_selected)
        styled.bold(name);
    else
        styled.add(name);
    m_name->set_text(styled);

    std::string suffix;
    if (m_dirty)
        suffix += "*";
    if (!m_isSet && m_isDefault)
        suffix += std::string(" ") + _("(default)");
    else if (!m_isSet)
        suffix += std::string(" ") + _("(unset)");

    m_suffix->set_text(StyledText(suffix));

    m_updating = false;
}
