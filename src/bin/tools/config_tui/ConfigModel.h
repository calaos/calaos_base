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
#ifndef S_CONFIGMODEL_H
#define S_CONFIGMODEL_H

#include <map>
#include <set>
#include <string>
#include <vector>

#include <ConfigOptions.h>
#include <Params.h>

/* Working copy of local_config.xml behind the calaos_config editor.
 * ================================================================
 *
 * This is the business logic of the TUI, and it deliberately knows nothing
 * about terminals: cpptui.hpp must never be included from here, so that every
 * rule below is testable without a tty (tests/ConfigModel_test.cpp).
 *
 * The model loads the configuration once, keeps an in-memory working copy and
 * remembers two things: the keys the user modified (dirty set) and the keys to
 * remove (delete set, pre-loaded with the obsolete keys that are present in the
 * file). Nothing whatsoever is written to disk before save().
 *
 * save() goes through a single Utils::set_config_options(toSet, toDelete): one
 * locked, merging, atomic write. Because the document is reloaded under the
 * lock at write time, the keys this model never touched keep whatever value
 * another program gave them in the meantime -- that merge, not the freshness
 * check below, is what makes the editor non destructive.
 *
 * hasChangedOnDisk() compares the size and the nanosecond mtime recorded at
 * load time. It is a display convenience ("the file changed under you"), not
 * the correctness mechanism: there is always a window between the check and
 * the write. Locking lives in Utils, it is not reimplemented here.
 */

namespace Calaos
{

class ConfigModel
{
public:
    //One editable line of the right hand column
    struct Row
    {
        std::string key;
        //Registry entry, or null when the key is only in the file
        const ConfigOption *option = nullptr;

        /* Value to show and to edit. When the key is not set and the option
         * declares a literal default, this holds that default: the model never
         * invents a value in the file, isSet says where the value comes from.
         */
        std::string value;
        bool isSet = false;      //the working copy really holds this key
        bool isDefault = false;  //not set, and value is the registry default
        bool dirty = false;      //modified or reset, not saved yet

        //value as a human should read it: Yes/No, enum label, masked secret
        std::string display;

        bool documented() const { return option != nullptr; }
    };

    //A category of the left hand column. Only non empty ones are built.
    struct Category
    {
        ConfigOption::Category category = ConfigOption::Category::Unknown;
        std::string label;
        std::vector<Row> rows;
    };

    //One line of the "pending changes" view
    struct Change
    {
        enum class Kind
        {
            Modified,   //a new value will be written
            Reset,      //the key will be removed, the default applies again
            Obsolete,   //dead key, purged on save
        };

        std::string key;
        Kind kind = Kind::Modified;
        bool documented = false;

        //Display strings, secrets already masked
        std::string before;
        std::string after;
        bool beforeSet = false;  //the key was set before the change
        bool afterSet = false;   //the key will still be set afterwards
    };

    struct SaveResult
    {
        bool ok = false;
        std::string error;          //translated, empty when ok

        int written = 0;            //keys created or updated
        int removed = 0;            //keys deleted, purges included
        int purged = 0;             //obsolete keys among them

        //Restart needed because a restartRequired() option changed
        bool restartRequired = false;
        std::vector<std::string> restartKeys;
    };

    /* configFile is the local_config.xml to work on. Left empty, the model
     * uses the one Utils resolved, which is what the tool does; the tests pass
     * an explicit path. The path is only used to display it and to watch it:
     * reads and writes always go through Utils, which owns the lock.
     */
    explicit ConfigModel(const std::string &configFile = std::string());

    //Reads the file and builds the working copy. False when it cannot be read,
    //error then holds a translated message. Never throws.
    bool load(std::string *error = nullptr);
    //Same, and drops every pending change
    bool reload(std::string *error = nullptr);

    const std::string &configFile() const { return m_configFile; }
    //True when the configuration cannot be written: the model still loads and
    //displays everything, edits are refused with a reason.
    bool readOnly() const { return m_readOnly; }

    //Categories to display, in registry order, "Undocumented" last
    const std::vector<Category> &categories() const { return m_categories; }
    //Row of a key, null when unknown to the model
    const Row *row(const std::string &key) const;

    //--- Editing -----------------------------------------------------------
    //Checks a candidate value against the registry without changing anything
    bool validate(const std::string &key, const std::string &value,
                  std::string *error = nullptr) const;

    /* Accepts value for key. Refused, and the model left untouched, when the
     * configuration is read only or the value does not validate. Setting a key
     * back to the value it had on load simply clears its dirty flag.
     */
    bool setValue(const std::string &key, const std::string &value,
                  std::string *error = nullptr);

    /* Queues the removal of the key: a documented option falls back to its
     * default, an undocumented one disappears from the list. The key is really
     * removed from the file at save() time.
     */
    bool resetToDefault(const std::string &key, std::string *error = nullptr);

    /* Throws away every user change and goes back to the state right after
     * load(). The purge of the obsolete keys was not a user change, it stays
     * queued.
     */
    void discardChanges();

    //--- Pending changes ---------------------------------------------------
    bool isDirty(const std::string &key) const;
    bool hasPendingChanges() const;
    size_t modifiedCount() const { return m_dirty.size(); }
    size_t deletedCount() const { return m_toDelete.size(); }
    //Before/after of everything that is queued, secrets masked
    std::vector<Change> pendingChanges() const;

    //--- Obsolete keys -----------------------------------------------------
    //Dead keys found in the file at load time, already queued for removal
    const std::vector<std::string> &obsoletePresent() const { return m_obsoletePresent; }

    //--- Saving ------------------------------------------------------------
    /* One merging write for everything that is pending. On success the dirty
     * and delete sets are cleared and the working copy is reloaded, so that the
     * keys another program changed meanwhile are picked up too. With nothing
     * pending, nothing at all is written.
     */
    SaveResult save();

    //--- Freshness ---------------------------------------------------------
    //True when the file changed since load()/save(): another program wrote it
    bool hasChangedOnDisk() const;

private:
    struct Stamp
    {
        bool valid = false;
        long long mtimeSec = 0;
        long long mtimeNsec = 0;
        long long size = 0;

        bool operator==(const Stamp &o) const
        {
            return valid == o.valid && mtimeSec == o.mtimeSec &&
                   mtimeNsec == o.mtimeNsec && size == o.size;
        }
    };

    static Stamp stampOf(const std::string &path);
    void buildCategories();
    Row makeRow(const std::string &key, const ConfigOption *opt) const;
    //Value currently held by the working copy, empty when the key is unset
    std::string workingValue(const std::string &key) const;
    Change makeChange(const std::string &key) const;

    std::string m_configFile;
    bool m_readOnly = true;

    //As read from the file, never modified until a save reloads it
    Params m_original;
    //Working copy: m_original plus the user changes, minus the removals
    Params m_working;

    std::set<std::string> m_dirty;
    std::set<std::string> m_toDelete;
    std::vector<std::string> m_obsoletePresent;

    std::vector<Category> m_categories;
    std::map<std::string, std::pair<size_t, size_t>> m_index; //key -> category, row

    Stamp m_stamp;
};

}

#endif /* S_CONFIGMODEL_H */
