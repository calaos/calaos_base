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

#include "ConfigModel.h"

#include <FileUtils.h>
#include <Utils.h>

#include <sys/stat.h>

/* No logging in this file on purpose: calaos_config writes its diagnostics to
 * stderr because the logger goes to stdout, which "calaos_config get" and
 * "list" hand over to scripts. Every failure is reported by return value plus
 * a translated message the caller displays where it wants.
 */

using namespace Calaos;

ConfigModel::ConfigModel(const std::string &configFile):
    m_configFile(configFile)
{
}

ConfigModel::Stamp ConfigModel::stampOf(const std::string &path)
{
    Stamp s;
    struct stat st;

    if (::stat(path.c_str(), &st) != 0)
        return s;

    s.valid = true;
    s.mtimeSec = (long long)st.st_mtim.tv_sec;
    s.mtimeNsec = (long long)st.st_mtim.tv_nsec;
    s.size = (long long)st.st_size;

    return s;
}

bool ConfigModel::load(std::string *error)
{
    if (error)
        error->clear();

    /* Utils owns the resolution of the config directory, the lock and the
     * atomic write. The path kept here is the one displayed in the title bar
     * and the one watched for external changes.
     */
    if (m_configFile.empty())
        m_configFile = Utils::getConfigFile(LOCAL_CONFIG);

    m_original.clear();
    m_working.clear();
    m_dirty.clear();
    m_toDelete.clear();
    m_obsoletePresent.clear();

    //A missing directory separator means a bare file name: watch the cwd
    size_t slash = m_configFile.find_last_of('/');
    std::string dir = slash == std::string::npos ? std::string(".") :
                      m_configFile.substr(0, slash);

    /* Writing means creating a temporary file in the directory and renaming it
     * over the target, so both must be writable. Same criterion as
     * ConfigOptions::purgeObsolete().
     */
    m_readOnly = !FileUtils::isWritable(dir) ||
                 (FileUtils::exists(m_configFile) && !FileUtils::isWritable(m_configFile));

    m_stamp = stampOf(m_configFile);

    bool ok = Utils::get_config_options(m_original);
    if (!ok)
    {
        m_original.clear();
        if (error)
            *error = _("Unable to read the configuration file.");
    }

    m_working = m_original;

    /* Dead keys are queued for removal right away, so that the banner can be
     * shown and the purge happens with the next save. This is exactly
     * ConfigOptions::obsoletePresent(), computed on the copy just read instead
     * of reading the file a second time.
     */
    for (const std::string &key: ConfigOptions::obsoleteKeys())
    {
        if (!m_original.Exists(key))
            continue;

        m_obsoletePresent.push_back(key);
        m_toDelete.insert(key);
    }

    buildCategories();

    return ok;
}

bool ConfigModel::reload(std::string *error)
{
    return load(error);
}

ConfigModel::Row ConfigModel::makeRow(const std::string &key, const ConfigOption *opt) const
{
    Row r;

    r.key = key;
    r.option = opt;
    r.isSet = m_working.Exists(key);

    if (r.isSet)
        r.value = m_working.get_param_const(key);
    else if (opt && opt->hasDef())
    {
        //Shown as the effective value, but the key stays absent from the file
        r.value = opt->def();
        r.isDefault = true;
    }

    r.dirty = m_dirty.count(key) > 0 || m_toDelete.count(key) > 0;
    r.display = opt? opt->displayValue(r.value): r.value;

    return r;
}

void ConfigModel::buildCategories()
{
    m_categories.clear();
    m_index.clear();

    for (ConfigOption::Category c: ConfigOptions::categories())
    {
        Category cat;
        cat.category = c;
        cat.label = ConfigOptions::categoryLabel(c);

        if (c == ConfigOption::Category::Unknown)
        {
            /* Everything the file holds and the registry does not know: keys
             * of another Calaos Home version, of a newer server, or simply
             * hand written. They are never hidden and never lost. Obsolete
             * keys are the one exception, they are on their way out.
             */
            for (int i = 0;i < m_working.size();i++)
            {
                std::string key, value;
                m_working.get_item(i, key, value);

                if (ConfigOptions::find(key) || ConfigOptions::isObsolete(key))
                    continue;

                cat.rows.push_back(makeRow(key, nullptr));
            }
        }
        else
        {
            //Documented options always show, set or not: an unset one displays
            //its default and is editable in place.
            for (const ConfigOption *opt: ConfigOptions::byCategory(c))
                cat.rows.push_back(makeRow(opt->key(), opt));
        }

        if (cat.rows.empty())
            continue;

        for (size_t i = 0;i < cat.rows.size();i++)
            m_index[cat.rows[i].key] = std::make_pair(m_categories.size(), i);

        m_categories.push_back(cat);
    }
}

const ConfigModel::Row *ConfigModel::row(const std::string &key) const
{
    std::map<std::string, std::pair<size_t, size_t>>::const_iterator it = m_index.find(key);
    if (it == m_index.end())
        return nullptr;

    return &m_categories[it->second.first].rows[it->second.second];
}

std::string ConfigModel::workingValue(const std::string &key) const
{
    if (!m_working.Exists(key))
        return std::string();

    return m_working.get_param_const(key);
}

bool ConfigModel::validate(const std::string &key, const std::string &value,
                           std::string *error) const
{
    if (error)
        error->clear();

    const ConfigOption *opt = ConfigOptions::find(key);
    if (!opt)
        return true;   //undocumented key: free text, nothing to check against

    return opt->validate(value, error);
}

bool ConfigModel::setValue(const std::string &key, const std::string &value, std::string *error)
{
    if (error)
        error->clear();

    if (m_readOnly)
    {
        if (error)
            *error = _("The configuration file is read only.");
        return false;
    }

    if (ConfigOptions::isObsolete(key))
    {
        if (error)
            *error = _("This option is obsolete, it does not have any effect anymore.");
        return false;
    }

    const ConfigOption *opt = ConfigOptions::find(key);

    /* Only keys the model knows about: the editor edits what it displays, it
     * is not a way to add arbitrary entries. A key removed by resetToDefault()
     * is still known, so a reset can be taken back.
     */
    if (!opt && !m_original.Exists(key) && !m_working.Exists(key))
    {
        if (error)
            *error = _("Unknown option.");
        return false;
    }

    if (opt && !opt->validate(value, error))
        return false;

    bool wasSet = m_original.Exists(key);

    //Back to the value read at load time: there is nothing left to write
    if (wasSet && value == m_original.get_param_const(key))
    {
        m_working.Add(key, value);
        m_dirty.erase(key);
        m_toDelete.erase(key);
        buildCategories();
        return true;
    }

    //Emptying a key that was not there either is not a change
    if (!wasSet && value.empty())
    {
        m_working.Delete(key);
        m_dirty.erase(key);
        m_toDelete.erase(key);
        buildCategories();
        return true;
    }

    m_working.Add(key, value);
    m_dirty.insert(key);
    m_toDelete.erase(key);

    buildCategories();

    return true;
}

bool ConfigModel::resetToDefault(const std::string &key, std::string *error)
{
    if (error)
        error->clear();

    if (m_readOnly)
    {
        if (error)
            *error = _("The configuration file is read only.");
        return false;
    }

    const ConfigOption *opt = ConfigOptions::find(key);

    if (!opt && !m_original.Exists(key) && !m_working.Exists(key))
    {
        if (error)
            *error = _("Unknown option.");
        return false;
    }

    m_working.Delete(key);
    m_dirty.erase(key);

    //Nothing to delete on disk when the key was not there in the first place
    if (m_original.Exists(key))
        m_toDelete.insert(key);
    else
        m_toDelete.erase(key);

    buildCategories();

    return true;
}

void ConfigModel::discardChanges()
{
    m_working = m_original;
    m_dirty.clear();
    m_toDelete.clear();

    //Back to the state right after load(): the purge of the dead keys was not
    //a user change, it stays queued.
    for (const std::string &key: m_obsoletePresent)
        m_toDelete.insert(key);

    buildCategories();
}

bool ConfigModel::isDirty(const std::string &key) const
{
    return m_dirty.count(key) > 0 || m_toDelete.count(key) > 0;
}

bool ConfigModel::hasPendingChanges() const
{
    return !m_dirty.empty() || !m_toDelete.empty();
}

ConfigModel::Change ConfigModel::makeChange(const std::string &key) const
{
    const ConfigOption *opt = ConfigOptions::find(key);

    Change ch;
    ch.key = key;
    ch.documented = opt != nullptr;
    ch.beforeSet = m_original.Exists(key);

    std::string beforeRaw = ch.beforeSet? m_original.get_param_const(key):
                            (opt && opt->hasDef()? opt->def(): std::string());
    ch.before = opt? opt->displayValue(beforeRaw): beforeRaw;

    if (ConfigOptions::isObsolete(key))
    {
        ch.kind = Change::Kind::Obsolete;
        return ch;
    }

    if (m_toDelete.count(key) > 0)
    {
        ch.kind = Change::Kind::Reset;
        //A documented option falls back to its default, an undocumented key
        //simply goes away
        std::string afterRaw = opt && opt->hasDef()? opt->def(): std::string();
        ch.after = opt? opt->displayValue(afterRaw): std::string();
        return ch;
    }

    ch.kind = Change::Kind::Modified;
    ch.afterSet = m_working.Exists(key);
    std::string afterRaw = workingValue(key);
    ch.after = opt? opt->displayValue(afterRaw): afterRaw;

    return ch;
}

std::vector<ConfigModel::Change> ConfigModel::pendingChanges() const
{
    std::vector<Change> changes;

    //Documented options first, in registry order
    for (const ConfigOption &opt: ConfigOptions::all())
    {
        if (isDirty(opt.key()))
            changes.push_back(makeChange(opt.key()));
    }

    //Then the undocumented keys, alphabetically
    std::set<std::string> others(m_dirty.begin(), m_dirty.end());
    others.insert(m_toDelete.begin(), m_toDelete.end());

    for (const std::string &key: others)
    {
        if (ConfigOptions::find(key) || ConfigOptions::isObsolete(key))
            continue;

        changes.push_back(makeChange(key));
    }

    //And the purges last, they are not something the user asked for
    for (const std::string &key: m_obsoletePresent)
    {
        if (m_toDelete.count(key) > 0)
            changes.push_back(makeChange(key));
    }

    return changes;
}

ConfigModel::SaveResult ConfigModel::save()
{
    SaveResult res;

    if (m_readOnly)
    {
        res.error = _("The configuration file is read only, nothing was written.");
        return res;
    }

    //Nothing pending: not a single byte is written
    if (!hasPendingChanges())
    {
        res.ok = true;
        return res;
    }

    Params toSet;
    for (const std::string &key: m_dirty)
        toSet.Add(key, workingValue(key));

    std::vector<std::string> toDelete(m_toDelete.begin(), m_toDelete.end());

    for (const std::string &key: m_dirty)
    {
        const ConfigOption *opt = ConfigOptions::find(key);
        if (opt && opt->isRestartRequired())
            res.restartKeys.push_back(key);
    }

    for (const std::string &key: toDelete)
    {
        //Going back to the default of such an option needs a restart too
        const ConfigOption *opt = ConfigOptions::find(key);
        if (opt && opt->isRestartRequired())
            res.restartKeys.push_back(key);
    }

    //One locked, merging, atomic write for everything at once
    if (!Utils::set_config_options(toSet, toDelete))
    {
        res.restartKeys.clear();
        res.error = _("Failed to write the configuration file.");
        return res;
    }

    res.ok = true;
    res.written = (int)m_dirty.size();
    res.removed = (int)toDelete.size();
    res.restartRequired = !res.restartKeys.empty();

    for (const std::string &key: toDelete)
    {
        if (ConfigOptions::isObsolete(key))
            res.purged++;
    }

    /* Reload: the write merged our changes into whatever the file had become,
     * so the working copy must be rebuilt from the result. It also clears the
     * dirty and delete sets and refreshes the freshness stamp.
     */
    load();

    return res;
}

bool ConfigModel::hasChangedOnDisk() const
{
    return !(stampOf(m_configFile) == m_stamp);
}
