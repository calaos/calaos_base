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
#ifndef S_CONFIGROW_H
#define S_CONFIGROW_H

#include <functional>
#include <memory>
#include <string>

#include "ConfigModel.h"
#include "CppTui.h"

/* One option of local_config.xml, rendered as one editable line.
 * =============================================================
 *
 * ConfigRow turns a ConfigModel::Row into the cpp-tui widget that fits its
 * type, and translates the events of that widget back into ConfigModel calls
 * through ConfigRowHost. It holds no business rule of its own: what a value
 * means, whether it is valid and what resetting does all live in ConfigModel
 * and in the registry.
 *
 * This header is internal to the TUI: it may include CppTui.h, unlike
 * ConfigTui.h which is what the rest of calaos_config sees.
 */

namespace Calaos
{

//---------------------------------------------------------------------------
// Widget helpers shared by ConfigRow.cpp and ConfigTui.cpp
//---------------------------------------------------------------------------

/* cpp-tui only exposes the focus through the virtual on_focus()/on_blur()
 * pair, and its App keeps the focused widget private. Wrapping a widget in
 * FocusAware is how this TUI learns that the focus moved: which row the
 * documentation pane must describe, and whether a text field is being edited
 * (which turns the single letter shortcuts off, see ConfigTui.cpp).
 */
template<class W>
class FocusAware: public W
{
public:
    using W::W;

    std::function<void()> onFocusIn;
    std::function<void()> onFocusOut;

    void on_focus() override
    {
        W::on_focus();
        if (onFocusIn)
            onFocusIn();
    }

    void on_blur() override
    {
        W::on_blur();
        if (onFocusOut)
            onFocusOut();
    }
};

/* Text field of the editor. cpptui::Input does not consume Enter and has no
 * read only mode, both of which this TUI needs; Ctrl-R reveals a masked
 * secret while it is being edited.
 */
class TuiInput: public cpptui::Input
{
public:
    bool readOnly = false;
    bool revealable = false;
    //Called on Enter, and on Escape with the argument set to true
    std::function<void(bool)> onValidate;
    //Up and Down on a numeric field, +1 or -1. Unset on a plain text field.
    std::function<void(int)> onStep;

    bool on_event(const cpptui::Event &event) override;
};

/* cpptui::Button resolves its colours through Color::contrast_color(), which
 * returns a real RGB value even when every colour of the theme is the
 * terminal default: a plain Button emits truecolor escapes on a monochrome
 * terminal. Only render() is replaced here, the behaviour of Button is kept.
 */
class MonoButton: public cpptui::Button
{
public:
    using cpptui::Button::Button;

    /* Draws "[ Label ]", and ">[ Label ]<" when focused. Cleared for the tiny
     * steppers of a NumberInput, which are not focusable and have no room for
     * a decoration.
     */
    bool decorated = true;

    void render(cpptui::Buffer &buffer) override;
};

//---------------------------------------------------------------------------
// ConfigRow
//---------------------------------------------------------------------------

class ConfigRow;

//What a row needs from the screen that hosts it
class ConfigRowHost
{
public:
    virtual ~ConfigRowHost() {}

    //The row took the focus: the documentation pane follows it
    virtual void rowFocused(ConfigRow *row) = 0;
    //Asks the model to accept value. False, and error is filled, when refused.
    virtual bool rowSetValue(ConfigRow *row, const std::string &value,
                             std::string *error) = 0;
    //Checks a value without changing anything: live feedback while typing
    virtual bool rowValidate(ConfigRow *row, const std::string &value,
                             std::string *error) = 0;
    //The user emptied the field, or pressed the reset key, on this row
    virtual void rowReset(ConfigRow *row) = 0;
    //Transient message shown in the documentation pane, empty clears it
    virtual void rowMessage(ConfigRow *row, const std::string &message) = 0;
};

class ConfigRow
{
public:
    /* Builds the line for one row of the model. readOnly disables every
     * editor without hiding anything, mono drops the few hardcoded colours of
     * the library widgets so that a terminal without truecolor stays readable.
     */
    ConfigRow(cpptui::App &app, ConfigRowHost &host, const ConfigModel::Row &row,
              bool readOnly, bool mono);

    const std::string &key() const { return m_key; }
    const ConfigOption *option() const { return m_option; }

    //True when the editor of this row is a free text field. The single letter
    //shortcuts must stay off while such a row has the focus.
    bool isTextEditor() const { return m_textEditor; }

    //The whole line, to be added to the option pane
    std::shared_ptr<cpptui::Widget> widget() const { return m_line; }

    /* Pushes the state of the model row back into the widgets. updateEditor is
     * false for the line being typed into: writing the value back would move
     * the cursor to the end of the field at every keystroke.
     */
    void refresh(const ConfigModel::Row &row, bool updateEditor = true);

    //True when the text field of the line holds a live selection: Ctrl-C then
    //means Copy, not Quit
    bool hasSelection() const;

    /* Cuts every callback and hides the line. cpp-tui keeps its own reference
     * to the focused widget, so a widget can outlive the ConfigRow that owns
     * it; after detach() such an orphan cannot call back into freed memory.
     */
    void detach();

    /* Validates and commits what the field currently holds, on Enter and when
     * the focus leaves the row. False when the registry refused the value: the
     * caller decides whether to keep it on screen or to put the value of the
     * model back.
     */
    bool commit();

    //Draws, or hides, the marker of the focused line
    void setFocusMarker(bool on);

private:
    void buildInput(const ConfigModel::Row &row);
    void buildNumber(const ConfigModel::Row &row);
    void buildToggle(const ConfigModel::Row &row);
    void buildDropdown(const ConfigModel::Row &row);

    //Sends a value to the model, and reports the refusal in the doc pane
    void submit(const std::string &value);
    //Value the editor should show for this row, empty when the key is unset
    std::string editableValue(const ConfigModel::Row &row) const;

    cpptui::App &m_app;
    ConfigRowHost &m_host;

    std::string m_key;
    const ConfigOption *m_option = nullptr;
    bool m_readOnly = false;
    bool m_mono = false;
    bool m_textEditor = false;
    //True while refresh() writes into the widgets: their change callbacks must
    //not be taken for user input
    bool m_updating = false;

    bool m_isSet = false;
    bool m_isDefault = false;
    bool m_dirty = false;

    //Value the model holds for this key, as the editor shows it. Empty when
    //the key is not set: what is typed is what will be written.
    std::string m_modelValue;

    std::shared_ptr<cpptui::Horizontal> m_line;
    std::shared_ptr<cpptui::Label> m_marker;
    std::shared_ptr<cpptui::Label> m_name;
    std::shared_ptr<cpptui::Label> m_suffix;

    //The widget that carries the focus of the line, whatever its type
    std::shared_ptr<cpptui::Widget> m_editor;

    //Only one of these is set, depending on the type of the option
    std::shared_ptr<cpptui::Input> m_input;
    std::shared_ptr<cpptui::NumberInput> m_number;
    std::shared_ptr<cpptui::ToggleSwitch> m_toggle;
    std::shared_ptr<cpptui::Dropdown> m_dropdown;

    //Raw value of each entry of the dropdown, same order as its options
    std::vector<std::string> m_enumValues;
};

}

#endif /* S_CONFIGROW_H */
