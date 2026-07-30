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

//Tests for ConfigModel, the working copy behind the calaos_config editor.
//It does not include cpptui.hpp, so all of it runs without a terminal.

#include "ConfigModel.h"

#include "Utils.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace Calaos;

//Every test gets its own directory tree:
//  <base>/config    config dir, holds local_config.xml
//  <base>/cache     cache dir, checked by initConfigOptions()
class ConfigModelTest: public ::testing::Test
{
protected:
    virtual void SetUp()
    {
        char tmpl[] = "/tmp/calaos_modeltest_XXXXXX";
        char *d = mkdtemp(tmpl);
        ASSERT_NE(d, nullptr);
        base = d;

        confDir = base + "/config";
        cacheDir = base + "/cache";

        ASSERT_EQ(::mkdir(confDir.c_str(), 0755), 0);
        ASSERT_EQ(::mkdir(cacheDir.c_str(), 0755), 0);
    }

    virtual void TearDown()
    {
        //The read only test made the tree unwritable on purpose
        ::chmod(base.c_str(), 0755);
        ::chmod(confDir.c_str(), 0755);

        std::string cmd = "rm -rf '" + base + "'";
        if (::system(cmd.c_str()) != 0)
            std::cerr << "warning: could not clean " << base << std::endl;
    }

    //Point the library at this test directory. initConfigOptions() is the only
    //entry point that resets the cached config/cache paths of the process.
    void initConfig()
    {
        std::vector<char> c(confDir.begin(), confDir.end());
        c.push_back('\0');
        std::vector<char> k(cacheDir.begin(), cacheDir.end());
        k.push_back('\0');

        Utils::initConfigOptions(c.data(), k.data(), true);
    }

    std::string configFile() const { return confDir + "/local_config.xml"; }

    static void writeFile(const std::string &path, const std::string &content)
    {
        std::ofstream f(path.c_str(), std::ofstream::out | std::ofstream::trunc);
        f << content;
        f.close();
    }

    static std::string readFile(const std::string &path)
    {
        std::ifstream f(path.c_str());
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    //Write the config file directly, the way another program would
    void writeConfigFile(const std::string &options) const
    {
        writeFile(configFile(),
                  std::string("<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
                              "<calaos:config xmlns:calaos=\"http://www.calaos.fr\">\n") +
                  options +
                  "</calaos:config>\n");
    }

    static std::string option(const std::string &key, const std::string &value)
    {
        return "  <calaos:option name=\"" + key + "\" value=\"" + value + "\" />\n";
    }

    //A model loaded on this test tree
    ConfigModel loadedModel()
    {
        ConfigModel model(configFile());
        std::string error;
        EXPECT_TRUE(model.load(&error)) << error;
        return model;
    }

    static const ConfigModel::Category *category(const ConfigModel &model,
                                                 ConfigOption::Category c)
    {
        for (const ConfigModel::Category &cat: model.categories())
        {
            if (cat.category == c)
                return &cat;
        }
        return nullptr;
    }

    static bool hasRow(const ConfigModel::Category &cat, const std::string &key)
    {
        for (const ConfigModel::Row &r: cat.rows)
        {
            if (r.key == key)
                return true;
        }
        return false;
    }

    static const ConfigModel::Change *change(const std::vector<ConfigModel::Change> &changes,
                                             const std::string &key)
    {
        for (const ConfigModel::Change &ch: changes)
        {
            if (ch.key == key)
                return &ch;
        }
        return nullptr;
    }

    static std::string diskValue(const std::string &key)
    {
        Params options;
        if (!Utils::get_config_options(options))
            return std::string();
        if (!options.Exists(key))
            return std::string();
        return options.get_param_const(key);
    }

    static bool onDisk(const std::string &key)
    {
        Params options;
        if (!Utils::get_config_options(options))
            return false;
        return options.Exists(key);
    }

    std::string base;
    std::string confDir;
    std::string cacheDir;
};

//A documented key goes to its category, a key of the file the registry does not
//know goes to "Undocumented", and nothing is lost on the way.
TEST_F(ConfigModelTest, LoadsEveryKeyIntoItsCategory)
{
    writeConfigFile(option("port_api", "6000") +
                    option("cn_user", "bob") +
                    option("tofu/192.168.0.1:5454", "sha256:abcdef"));
    initConfig();

    ConfigModel model = loadedModel();

    const ConfigModel::Row *port = model.row("port_api");
    ASSERT_NE(port, nullptr);
    EXPECT_EQ(port->value, "6000");
    EXPECT_TRUE(port->isSet);
    EXPECT_FALSE(port->isDefault);
    EXPECT_FALSE(port->dirty);
    EXPECT_TRUE(port->documented());

    const ConfigModel::Category *network = category(model, ConfigOption::Category::Network);
    ASSERT_NE(network, nullptr);
    EXPECT_TRUE(hasRow(*network, "port_api"));
    EXPECT_TRUE(hasRow(*network, "listen_address"));
    EXPECT_FALSE(hasRow(*network, "cn_user")) << "an option landed in the wrong category";

    const ConfigModel::Category *auth = category(model, ConfigOption::Category::Auth);
    ASSERT_NE(auth, nullptr);
    EXPECT_TRUE(hasRow(*auth, "cn_user"));

    //An unknown key is never hidden: it is editable in "Undocumented"
    const ConfigModel::Category *unknown = category(model, ConfigOption::Category::Unknown);
    ASSERT_NE(unknown, nullptr);
    EXPECT_EQ(unknown->label, ConfigOptions::categoryLabel(ConfigOption::Category::Unknown));
    ASSERT_TRUE(hasRow(*unknown, "tofu/192.168.0.1:5454"));

    const ConfigModel::Row *tofu = model.row("tofu/192.168.0.1:5454");
    ASSERT_NE(tofu, nullptr);
    EXPECT_FALSE(tofu->documented());
    EXPECT_EQ(tofu->option, nullptr);
    EXPECT_TRUE(tofu->isSet);
    EXPECT_EQ(tofu->value, "sha256:abcdef");
    EXPECT_EQ(tofu->display, "sha256:abcdef");

    //Categories keep the display order of the registry
    ASSERT_FALSE(model.categories().empty());
    EXPECT_EQ(model.categories().back().category, ConfigOption::Category::Unknown);
}

//A category with nothing to show is not displayed at all.
TEST_F(ConfigModelTest, EmptyCategoryIsNotShown)
{
    writeConfigFile(option("port_api", "6000"));
    initConfig();

    ConfigModel model = loadedModel();

    EXPECT_EQ(category(model, ConfigOption::Category::Unknown), nullptr)
        << "the Undocumented category is shown while there is no unknown key";

    for (const ConfigModel::Category &cat: model.categories())
        EXPECT_FALSE(cat.rows.empty()) << "empty category " << cat.label;
}

//An option that is not in the file reports "not set" and offers its default,
//without the model writing that default anywhere.
TEST_F(ConfigModelTest, UnsetOptionFallsBackToItsDefault)
{
    writeConfigFile(option("port_api", "6000"));
    initConfig();

    ConfigModel model = loadedModel();

    const ConfigModel::Row *keep = model.row("history_keep_days");
    ASSERT_NE(keep, nullptr);
    EXPECT_FALSE(keep->isSet);
    EXPECT_TRUE(keep->isDefault);
    EXPECT_EQ(keep->value, "30");
    EXPECT_EQ(keep->display, "30");
    EXPECT_FALSE(keep->dirty);

    //An option with a computed default has no literal value to show
    const ConfigModel::Row *www = model.row("wwwroot");
    ASSERT_NE(www, nullptr);
    EXPECT_FALSE(www->isSet);
    EXPECT_FALSE(www->isDefault);
    EXPECT_EQ(www->value, "");

    //And nothing of that was invented in the file
    EXPECT_FALSE(onDisk("history_keep_days"));
    EXPECT_EQ(readFile(configFile()).find("history_keep_days"), std::string::npos);
}

//Editing only touches memory: the file is not written before save().
TEST_F(ConfigModelTest, SetValueMarksDirtyAndWritesNothing)
{
    writeConfigFile(option("port_api", "6000"));
    initConfig();

    std::string before = readFile(configFile());

    ConfigModel model = loadedModel();

    std::string error;
    ASSERT_TRUE(model.setValue("port_api", "7000", &error)) << error;

    const ConfigModel::Row *port = model.row("port_api");
    ASSERT_NE(port, nullptr);
    EXPECT_EQ(port->value, "7000");
    EXPECT_TRUE(port->dirty);
    EXPECT_TRUE(model.isDirty("port_api"));
    EXPECT_TRUE(model.hasPendingChanges());
    EXPECT_EQ(model.modifiedCount(), 1u);
    EXPECT_EQ(model.deletedCount(), 0u);

    EXPECT_EQ(readFile(configFile()), before) << "the file was written before save()";
    EXPECT_EQ(diskValue("port_api"), "6000");

    //Setting the value back to what it was clears the pending change
    ASSERT_TRUE(model.setValue("port_api", "6000", &error)) << error;
    EXPECT_FALSE(model.isDirty("port_api"));
    EXPECT_FALSE(model.hasPendingChanges());
}

//Resetting queues a removal, and saving really takes the key out of the file.
TEST_F(ConfigModelTest, ResetToDefaultRemovesTheKeyOnSave)
{
    writeConfigFile(option("port_api", "6000") +
                    option("cn_user", "bob"));
    initConfig();

    ConfigModel model = loadedModel();

    std::string error;
    ASSERT_TRUE(model.resetToDefault("port_api", &error)) << error;

    const ConfigModel::Row *port = model.row("port_api");
    ASSERT_NE(port, nullptr);
    EXPECT_FALSE(port->isSet);
    EXPECT_TRUE(port->isDefault);
    EXPECT_EQ(port->value, "5454") << "the row should show the registry default";
    EXPECT_TRUE(port->dirty);
    EXPECT_EQ(model.deletedCount(), 1u);

    std::vector<ConfigModel::Change> changes = model.pendingChanges();
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].key, "port_api");
    EXPECT_EQ(changes[0].kind, ConfigModel::Change::Kind::Reset);
    EXPECT_EQ(changes[0].before, "6000");
    EXPECT_TRUE(changes[0].beforeSet);
    EXPECT_EQ(changes[0].after, "5454");
    EXPECT_FALSE(changes[0].afterSet);

    //Still on disk until we save
    EXPECT_TRUE(onDisk("port_api"));

    ConfigModel::SaveResult res = model.save();
    ASSERT_TRUE(res.ok) << res.error;
    EXPECT_EQ(res.written, 0);
    EXPECT_EQ(res.removed, 1);
    EXPECT_EQ(res.purged, 0);
    EXPECT_TRUE(res.restartRequired) << "port_api is a restartRequired() option";

    EXPECT_FALSE(onDisk("port_api"));
    EXPECT_EQ(diskValue("cn_user"), "bob") << "an untouched key was removed";
    EXPECT_FALSE(model.hasPendingChanges());

    port = model.row("port_api");
    ASSERT_NE(port, nullptr);
    EXPECT_FALSE(port->isSet);
    EXPECT_FALSE(port->dirty);
}

//An invalid value is refused and changes nothing at all.
TEST_F(ConfigModelTest, ValidationRefusesABadValue)
{
    writeConfigFile(option("port_api", "6000"));
    initConfig();

    ConfigModel model = loadedModel();

    std::string error;
    EXPECT_FALSE(model.validate("port_api", "abc", &error));
    EXPECT_FALSE(error.empty()) << "a refusal must say why";

    error.clear();
    EXPECT_FALSE(model.setValue("port_api", "abc", &error));
    EXPECT_FALSE(error.empty());

    EXPECT_FALSE(model.setValue("port_api", "70000", &error)) << "out of range value accepted";

    const ConfigModel::Row *port = model.row("port_api");
    ASSERT_NE(port, nullptr);
    EXPECT_EQ(port->value, "6000");
    EXPECT_FALSE(port->dirty);
    EXPECT_FALSE(model.hasPendingChanges());

    //An undocumented key has nothing to validate against, free text is fine
    EXPECT_TRUE(model.validate("tofu/host", "whatever", &error));

    //An obsolete key is never editable
    EXPECT_FALSE(model.setValue("use_ntp", "true", &error));
    EXPECT_FALSE(error.empty());
}

//Dead keys are queued for removal at load time, shown as such, and purged by
//the next save without anything else moving.
TEST_F(ConfigModelTest, ObsoleteKeysArePreQueuedAndPurgedOnSave)
{
    writeConfigFile(option("port_api", "6000") +
                    option("fw_target", "calaos_tss") +
                    option("use_ntp", "true") +
                    option("my_own_key", "kept"));
    initConfig();

    ConfigModel model = loadedModel();

    ASSERT_EQ(model.obsoletePresent().size(), 2u);
    EXPECT_EQ(model.deletedCount(), 2u);
    EXPECT_TRUE(model.hasPendingChanges());

    //They belong to no category, not even Undocumented
    EXPECT_EQ(model.row("fw_target"), nullptr);
    EXPECT_EQ(model.row("use_ntp"), nullptr);

    const ConfigModel::Category *unknown = category(model, ConfigOption::Category::Unknown);
    ASSERT_NE(unknown, nullptr);
    EXPECT_TRUE(hasRow(*unknown, "my_own_key"));
    EXPECT_FALSE(hasRow(*unknown, "fw_target"));
    EXPECT_FALSE(hasRow(*unknown, "use_ntp"));

    std::vector<ConfigModel::Change> changes = model.pendingChanges();
    ASSERT_EQ(changes.size(), 2u);

    const ConfigModel::Change *fw = change(changes, "fw_target");
    ASSERT_NE(fw, nullptr);
    EXPECT_EQ(fw->kind, ConfigModel::Change::Kind::Obsolete);
    EXPECT_EQ(fw->before, "calaos_tss");
    EXPECT_TRUE(fw->beforeSet);
    EXPECT_FALSE(fw->afterSet);
    EXPECT_TRUE(fw->after.empty());

    ConfigModel::SaveResult res = model.save();
    ASSERT_TRUE(res.ok) << res.error;
    EXPECT_EQ(res.written, 0);
    EXPECT_EQ(res.removed, 2);
    EXPECT_EQ(res.purged, 2);

    EXPECT_FALSE(onDisk("fw_target"));
    EXPECT_FALSE(onDisk("use_ntp"));
    EXPECT_EQ(diskValue("port_api"), "6000");
    EXPECT_EQ(diskValue("my_own_key"), "kept");

    EXPECT_TRUE(model.obsoletePresent().empty());
    EXPECT_FALSE(model.hasPendingChanges());
}

//Saving twice in a row does not write the file a second time.
TEST_F(ConfigModelTest, SaveClearsTheDirtyStateAndDoesNothingTwice)
{
    writeConfigFile(option("port_api", "6000"));
    initConfig();

    ConfigModel model = loadedModel();

    std::string error;
    ASSERT_TRUE(model.setValue("cn_user", "alice", &error)) << error;
    ASSERT_TRUE(model.setValue("port_api", "7000", &error)) << error;

    ConfigModel::SaveResult res = model.save();
    ASSERT_TRUE(res.ok) << res.error;
    EXPECT_EQ(res.written, 2);
    EXPECT_EQ(res.removed, 0);
    EXPECT_TRUE(res.error.empty());
    EXPECT_TRUE(res.restartRequired);
    ASSERT_EQ(res.restartKeys.size(), 1u);
    EXPECT_EQ(res.restartKeys[0], "port_api");

    EXPECT_EQ(diskValue("cn_user"), "alice");
    EXPECT_EQ(diskValue("port_api"), "7000");
    EXPECT_FALSE(model.hasPendingChanges());
    EXPECT_FALSE(model.isDirty("port_api"));

    const ConfigModel::Row *port = model.row("port_api");
    ASSERT_NE(port, nullptr);
    EXPECT_FALSE(port->dirty);
    EXPECT_TRUE(port->isSet);

    //Nothing pending anymore: the file must not be touched at all
    std::string content = readFile(configFile());
    struct stat before;
    ASSERT_EQ(::stat(configFile().c_str(), &before), 0);

    ConfigModel::SaveResult again = model.save();
    EXPECT_TRUE(again.ok);
    EXPECT_EQ(again.written, 0);
    EXPECT_EQ(again.removed, 0);

    struct stat after;
    ASSERT_EQ(::stat(configFile().c_str(), &after), 0);
    EXPECT_EQ(readFile(configFile()), content);
    EXPECT_EQ(before.st_mtim.tv_sec, after.st_mtim.tv_sec);
    EXPECT_EQ(before.st_mtim.tv_nsec, after.st_mtim.tv_nsec);
    EXPECT_FALSE(model.hasChangedOnDisk());
}

//The merge guarantee, end to end: what another program wrote while the editor
//was open survives the save.
TEST_F(ConfigModelTest, SaveMergesWhatAnotherProgramWrote)
{
    writeConfigFile(option("port_api", "6000") +
                    option("cn_user", "bob"));
    initConfig();

    ConfigModel model = loadedModel();

    std::string error;
    ASSERT_TRUE(model.setValue("port_api", "7000", &error)) << error;

    //Another program rewrites the whole file behind our back: it changes a key
    //we did not touch and adds one we never saw.
    writeConfigFile(option("port_api", "6000") +
                    option("cn_user", "changed_by_someone_else") +
                    option("added_by_someone_else", "kept"));

    ConfigModel::SaveResult res = model.save();
    ASSERT_TRUE(res.ok) << res.error;

    EXPECT_EQ(diskValue("port_api"), "7000") << "our own change was lost";
    EXPECT_EQ(diskValue("cn_user"), "changed_by_someone_else") << "we overwrote another writer";
    EXPECT_EQ(diskValue("added_by_someone_else"), "kept") << "a key of another writer disappeared";

    //The working copy was rebuilt from the merged result
    const ConfigModel::Row *user = model.row("cn_user");
    ASSERT_NE(user, nullptr);
    EXPECT_EQ(user->value, "changed_by_someone_else");
    EXPECT_NE(model.row("added_by_someone_else"), nullptr);
}

//Freshness check: a display convenience, not the correctness mechanism.
TEST_F(ConfigModelTest, DetectsAnExternalWriteAndReloadsFromIt)
{
    writeConfigFile(option("port_api", "6000"));
    initConfig();

    ConfigModel model = loadedModel();
    EXPECT_FALSE(model.hasChangedOnDisk());

    std::string error;
    ASSERT_TRUE(model.setValue("port_api", "7000", &error)) << error;
    //Editing in memory does not make the file look modified
    EXPECT_FALSE(model.hasChangedOnDisk());

    //A different size, so the check holds whatever the mtime resolution is
    writeConfigFile(option("port_api", "8000") +
                    option("added_by_someone_else", "kept"));
    EXPECT_TRUE(model.hasChangedOnDisk());

    ASSERT_TRUE(model.reload(&error)) << error;

    EXPECT_FALSE(model.hasChangedOnDisk());
    EXPECT_FALSE(model.hasPendingChanges()) << "reload() must drop the working copy";

    const ConfigModel::Row *port = model.row("port_api");
    ASSERT_NE(port, nullptr);
    EXPECT_EQ(port->value, "8000");
    EXPECT_FALSE(port->dirty);
    EXPECT_NE(model.row("added_by_someone_else"), nullptr);
}

//A secret is never displayed, neither in its row nor in the diff view.
TEST_F(ConfigModelTest, SecretsAreMaskedEverywhere)
{
    writeConfigFile(option("smtp_password", "old_secret"));
    initConfig();

    ConfigModel model = loadedModel();

    const ConfigModel::Row *pass = model.row("smtp_password");
    ASSERT_NE(pass, nullptr);
    ASSERT_NE(pass->option, nullptr);
    ASSERT_TRUE(pass->option->isSecret());
    EXPECT_TRUE(pass->isSet);
    //The raw value stays reachable, the editor has to be able to edit it
    EXPECT_EQ(pass->value, "old_secret");
    EXPECT_EQ(pass->display, "••••••••");

    std::string error;
    ASSERT_TRUE(model.setValue("smtp_password", "new_secret", &error)) << error;

    pass = model.row("smtp_password");
    ASSERT_NE(pass, nullptr);
    EXPECT_EQ(pass->display, "••••••••");

    std::vector<ConfigModel::Change> changes = model.pendingChanges();
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].key, "smtp_password");
    EXPECT_EQ(changes[0].kind, ConfigModel::Change::Kind::Modified);
    EXPECT_EQ(changes[0].before, "••••••••");
    EXPECT_EQ(changes[0].after, "••••••••");
    EXPECT_TRUE(changes[0].afterSet);

    for (const ConfigModel::Change &ch: changes)
    {
        EXPECT_EQ(ch.before.find("old_secret"), std::string::npos);
        EXPECT_EQ(ch.after.find("new_secret"), std::string::npos);
    }
}

//A configuration that cannot be written still loads: the editor shows it,
//says why it is read only, and refuses to write. Nothing throws.
TEST_F(ConfigModelTest, ReadOnlyConfigLoadsAndRefusesToSave)
{
    writeConfigFile(option("port_api", "6000"));
    initConfig();

    //The lock file must stay usable, otherwise the read itself would fail and
    //we would not be testing the read only mode but a broken lock.
    ASSERT_EQ(::chmod(configFile().c_str(), 0444), 0);
    ASSERT_EQ(::chmod((configFile() + ".lock").c_str(), 0444), 0);
    ASSERT_EQ(::chmod(confDir.c_str(), 0555), 0);
    //mkdtemp() made the base directory private, the child has to walk through it
    ASSERT_EQ(::chmod(base.c_str(), 0755), 0);

    //Directory permissions do not apply to root, drop privileges in a child
    pid_t pid = fork();
    ASSERT_GE(pid, 0);

    if (pid == 0)
    {
        if (geteuid() == 0)
        {
            if (::setgid(65534) != 0 || ::setuid(65534) != 0)
                _exit(2);
        }

        ConfigModel model(configFile());
        std::string error;

        if (!model.load(&error))
            _exit(3);
        if (!model.readOnly())
            _exit(4);

        //Everything is still readable
        const ConfigModel::Row *port = model.row("port_api");
        if (!port || port->value != "6000")
            _exit(5);

        //But no edit is accepted, and the refusal says why
        error.clear();
        if (model.setValue("port_api", "7000", &error) || error.empty())
            _exit(6);
        error.clear();
        if (model.resetToDefault("port_api", &error) || error.empty())
            _exit(7);

        ConfigModel::SaveResult res = model.save();
        if (res.ok || res.error.empty())
            _exit(8);
        if (res.written != 0 || res.removed != 0)
            _exit(9);

        _exit(0);
    }

    int status = 0;
    ASSERT_EQ(::waitpid(pid, &status, 0), pid);
    ASSERT_TRUE(WIFEXITED(status));

    ASSERT_EQ(::chmod(confDir.c_str(), 0755), 0);

    if (WEXITSTATUS(status) == 2)
        GTEST_SKIP() << "could not drop privileges to test a read only configuration";

    EXPECT_EQ(WEXITSTATUS(status), 0) << "read only step " << WEXITSTATUS(status) << " failed";

    //And the file was left strictly alone
    EXPECT_EQ(diskValue("port_api"), "6000");
}
