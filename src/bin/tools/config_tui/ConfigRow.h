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

/* Text of one of the three fixed columns of a line: the cursor marker, the
 * name of the option and the "(default)" marker after its value.
 *
 * cpptui::Label cannot do it: its render() resolves one colour for the whole
 * widget and drops every attribute of the StyledText it was given, so the bold
 * of the selected line and the dim of the marker would both be lost. It also
 * leaves its background transparent, which is what made every line of the pane
 * a patchwork of painted and unpainted cells.
 */
class RowLabel: public cpptui::Widget
{
public:
    std::string text;
    bool bold = false;
    //Left default for the colour of the theme, i.e. no colour at all in
    //monochrome mode
    cpptui::Color color;
    /* Cells of the column the text may not use, on its right. One on the name
     * of an option, so that a label too long for its column is cut one cell
     * early instead of touching the value that follows it.
     */
    int gutter = 0;

    RowLabel();

    void render(cpptui::Buffer &buffer) override;
    //Decoration: the option pane owns the keyboard and the mouse of its lines
    bool on_event(const cpptui::Event &) override { return false; }
};

/* The line itself, and the one place that decides how wide each of its columns
 * is.
 *
 * A plain Horizontal would give the editor every cell the fixed columns leave,
 * i.e. a value area as wide as the pane and a "(default)" marker pushed against
 * the frame, a screen away from the value it qualifies. The value column is
 * therefore sized here, from the same number for every line of the pane
 * (valueColumn, the widest value the category holds), so that the values and
 * the markers of a category line up whatever each line contains.
 */
class ConfigRowLine: public cpptui::Horizontal
{
public:
    //Width the option pane asks for the value column, in cells. Clamped to
    //what the line really has: the pane is what it is at 80 columns.
    int valueColumn = 0;

    std::shared_ptr<cpptui::Widget> nameWidget;
    std::shared_ptr<cpptui::Widget> valueWidget;
    std::shared_ptr<cpptui::Widget> suffixWidget;

    void layout() override;
};

/* List of an enumerated option. cpptui::Dropdown indents the value it shows by
 * one cell and paints its arrow two cells before its right edge, which puts the
 * one list of a category out of line with every other value of the column. Only
 * render() is replaced: the widget itself, and the way the pane drives it, are
 * the ones of the library.
 */
class TuiDropdown: public cpptui::Dropdown
{
public:
    using cpptui::Dropdown::Dropdown;

    void render(cpptui::Buffer &buffer) override;
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
    /* What the line answers to. The keyboard model of the option pane is the
     * same for every kind: Space activates, Enter edits; only what "activate"
     * and "edit" do changes.
     */
    enum class Kind
    {
        Text,   //free text or password: Enter opens the field
        Number, //integer or decimal: Enter opens the field, Up and Down step
        Bool,   //Space and Enter flip it, there is nothing to edit
        Enum    //Space picks the next value, Enter opens the list
    };

    /* Builds the line for one row of the model. readOnly disables every
     * editor without hiding anything, mono drops the few hardcoded colours of
     * the library widgets so that a terminal without truecolor stays readable.
     */
    ConfigRow(cpptui::App &app, ConfigRowHost &host, const ConfigModel::Row &row,
              bool readOnly, bool mono);

    const std::string &key() const { return m_key; }
    const ConfigOption *option() const { return m_option; }
    Kind kind() const { return m_kind; }

    //The whole line, to be added to the option pane
    std::shared_ptr<cpptui::Widget> widget() const { return m_line; }

    /* Width the value of this line would need to be shown whole, steppers and
     * list arrow included. The pane takes the widest of its lines and gives it
     * back to every one of them through setValueColumn().
     */
    int naturalValueWidth() const { return m_naturalValue; }
    void setValueColumn(int width) { m_line->valueColumn = width; }

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

    /* Validates and commits what the field currently holds. False when the
     * registry refused the value: the caller decides whether to keep it on
     * screen or to put the value of the model back.
     */
    bool commit();

    /* Cursor of the option pane. selected is the line the cursor sits on,
     * active tells whether that pane is the one the keyboard drives: the
     * marker is "> " when it is and "* " when it is not, exactly like the
     * category column.
     */
    void setSelected(bool selected, bool active);

    //--- Edit mode ---------------------------------------------------------
    //
    // A Bool has nothing to edit: activate() flips it and editing never
    // starts. Everything else opens on Enter, and while it is open the pane
    // hands it every key it receives.

    //True when Enter on this line opens an editor rather than just acting
    bool editable() const { return m_kind != Kind::Bool; }

    /* Space, and Enter on a line with nothing to edit: flips a Bool, moves an
     * Enum to its next value. False when the kind has nothing to activate.
     */
    bool activate();

    //Opens the editor: the widget of the line starts drawing itself focused
    void beginEdit();

    /* Closes the editor. cancel puts the value of the model back, otherwise
     * what the field holds is committed. False when the registry refused it:
     * the editor stays open on the offending value.
     */
    bool endEdit(bool cancel);

    //Hands one event to the widget being edited, true when it consumed it
    bool editEvent(const cpptui::Event &event);

    /* Puts the focus of the library back on the option pane. A click inside a
     * text field makes cpptui::Input take it, and the pane would stop
     * receiving keys; editEvent() calls this right after.
     */
    std::function<void()> restoreFocus;

private:
    void buildInput(const ConfigModel::Row &row);
    void buildNumber(const ConfigModel::Row &row);
    void buildToggle(const ConfigModel::Row &row);
    void buildDropdown(const ConfigModel::Row &row);

    /* Paints the field of the line, or stops painting it. Only the value being
     * edited has a background of its own: on every other line the value area
     * is the surface of the pane, so that the column reads as one column
     * whatever widget draws each cell of it.
     */
    void setFieldEdited(bool edited);
    //Recomputes what naturalValueWidth() answers, from the value on screen
    void updateNaturalWidth();
    //Shows the value from its first character again, see the definition
    void rewindField();

    //Sends a value to the model, and reports the refusal in the doc pane
    void submit(const std::string &value);
    //Value the editor should show for this row, empty when the key is unset
    std::string editableValue(const ConfigModel::Row &row) const;
    //The widget that draws itself focused while the line is being edited
    std::shared_ptr<cpptui::Widget> editWidget() const;

    cpptui::App &m_app;
    ConfigRowHost &m_host;

    std::string m_key;
    const ConfigOption *m_option = nullptr;
    bool m_readOnly = false;
    bool m_mono = false;
    Kind m_kind = Kind::Text;
    //True while refresh() writes into the widgets: their change callbacks must
    //not be taken for user input
    bool m_updating = false;
    //True between beginEdit() and endEdit()
    bool m_editing = false;
    //Name of the line, drawn bold while the cursor sits on it
    bool m_selected = false;

    bool m_isSet = false;
    bool m_isDefault = false;
    bool m_dirty = false;

    //Value the model holds for this key, as the editor shows it. Empty when
    //the key is not set: what is typed is what will be written.
    std::string m_modelValue;

    //Cells the value of this line would like, see naturalValueWidth()
    int m_naturalValue = 0;

    std::shared_ptr<ConfigRowLine> m_line;
    std::shared_ptr<RowLabel> m_marker;
    std::shared_ptr<RowLabel> m_name;
    std::shared_ptr<RowLabel> m_suffix;

    //The widget that carries the focus of the line, whatever its type
    std::shared_ptr<cpptui::Widget> m_editor;

    //Only one of these is set, depending on the type of the option
    std::shared_ptr<cpptui::Input> m_input;
    std::shared_ptr<cpptui::NumberInput> m_number;
    std::shared_ptr<cpptui::ToggleSwitch> m_toggle;
    std::shared_ptr<cpptui::Dropdown> m_dropdown;

    //Raw value of each entry of the dropdown, same order as its options
    std::vector<std::string> m_enumValues;
    //Entry the model holds, to be put back when an edit is cancelled
    int m_enumIndex = -1;
};

}

#endif /* S_CONFIGROW_H */
