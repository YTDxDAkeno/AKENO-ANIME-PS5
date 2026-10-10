// AKENO STREAM PS5 - Tests for backing up and restoring the data folder.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/app.hpp"
#include "app/backup.hpp"
#include "core/fs.hpp"
#include "host_web_view.hpp"
#include "net/http.hpp"
#include "web/websites.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <unistd.h>

using namespace akeno;

namespace
{
std::string fresh_dir(const char *name)
{
    char path[128];
    std::snprintf(path, sizeof(path), "/tmp/akeno-backup-%s-XXXXXX", name);
    return mkdtemp(path) ? path : "/tmp";
}

void put(const std::string &dir, const std::string &name, const std::string &text)
{
    ASSERT_TRUE(fs::write_atomic(fs::join(dir, name), text));
}

std::string get(const std::string &dir, const std::string &name)
{
    return fs::read_text(fs::join(dir, name)).value_or("");
}
} // namespace

TEST(Backup, SavesTheUserFilesOnly)
{
    const std::string data = fresh_dir("data"), target = fresh_dir("target") + "/akeno-backup";
    EXPECT_TRUE(backup::is_empty(data));
    put(data, "settings.json", R"({"version": 1})");
    put(data, "websites.json", R"({"sites": []})");
    put(data, "history.json", "{ damaged");
    put(data, "secrets.json", R"({"youtube_api_key": "AIza-secret"})");
    put(data, "crash.txt", "crash");
    EXPECT_FALSE(backup::is_empty(data));

    const backup::Result r = backup::save(data, target); // the folder is created
    EXPECT_TRUE(r.error.empty()) << r.error;
    EXPECT_EQ(r.copied, 2);
    EXPECT_EQ(r.skipped, 1); // damaged JSON is not copied over a good backup
    EXPECT_EQ(backup::count(target), 2);
    EXPECT_EQ(get(target, "websites.json"), R"({"sites": []})");
    EXPECT_FALSE(fs::exists(fs::join(target, "secrets.json"))); // never backed up
    EXPECT_FALSE(fs::exists(fs::join(target, "crash.txt")));

    EXPECT_FALSE(backup::save(data, "").error.empty());
    EXPECT_FALSE(backup::save(data, "/nonexistent-root/akeno-backup").error.empty());
}

TEST(Backup, RestoresAndKeepsWhatItReplaced)
{
    const std::string data = fresh_dir("data"), saved = fresh_dir("saved");
    put(saved, "settings.json", R"({"version": 1, "volume": 40})");
    put(saved, "favorites.json", R"([])");
    put(saved, "history.json", "<html>not json</html>");
    put(data, "settings.json", R"({"version": 1, "volume": 90})");

    const backup::Result r = backup::restore(saved, data);
    EXPECT_TRUE(r.error.empty()) << r.error;
    EXPECT_EQ(r.copied, 2);
    EXPECT_EQ(r.skipped, 1);
    EXPECT_EQ(get(data, "settings.json"), R"({"version": 1, "volume": 40})");
    EXPECT_EQ(get(data, "settings.json.before-restore"), R"({"version": 1, "volume": 90})");
    EXPECT_FALSE(fs::exists(fs::join(data, "history.json")));
    EXPECT_FALSE(backup::restore(fresh_dir("none"), data).error.empty());
}

TEST(Backup, RestoresOnlyAnEmptyDataFolderAtStart)
{
    const std::string loader = fresh_dir("loader"), pc = fresh_dir("pc");
    const std::vector<std::string> locations = {loader, pc};
    put(pc, "websites.json", R"({"from": "pc"})");

    // A fresh registration: restored from the first location that holds a backup.
    const std::string fresh = fresh_dir("fresh");
    backup::Result r;
    EXPECT_EQ(backup::restore_at_start(fresh, locations, &r), pc);
    EXPECT_EQ(r.copied, 1);
    EXPECT_EQ(get(fresh, "websites.json"), R"({"from": "pc"})");

    put(loader, "websites.json", R"({"from": "loader"})");
    const std::string fresh2 = fresh_dir("fresh2");
    EXPECT_EQ(backup::restore_at_start(fresh2, locations, nullptr), loader);

    // Existing data is never replaced on its own...
    const std::string used = fresh_dir("used");
    put(used, "websites.json", R"({"from": "user"})");
    EXPECT_EQ(backup::restore_at_start(used, locations, nullptr), "");
    EXPECT_EQ(get(used, "websites.json"), R"({"from": "user"})");

    // ...only when a restore was chosen, once, from a folder AKENO offers.
    std::string error;
    EXPECT_FALSE(backup::stage_restore("/etc", used, locations, &error));
    EXPECT_FALSE(backup::stage_restore(fresh_dir("empty"), used, {fresh_dir("x")}, &error));
    ASSERT_TRUE(backup::stage_restore(pc, used, locations, &error)) << error;
    EXPECT_EQ(backup::restore_at_start(used, locations, nullptr), pc);
    EXPECT_EQ(get(used, "websites.json"), R"({"from": "pc"})");
    EXPECT_FALSE(fs::exists(fs::join(used, backup::kStagedFile)));
    EXPECT_EQ(backup::restore_at_start(used, locations, nullptr), "");

    // A staged path someone wrote by hand is ignored.
    put(used, backup::kStagedFile, "/etc\n");
    EXPECT_EQ(backup::restore_at_start(used, locations, nullptr), "");
}

// The case the backup exists for: the title is registered again (to refresh
// its dashboard name and icon) and starts with an empty data folder.
TEST(Backup, SavedWebsitesSurviveARegistrationAgain)
{
    const std::string first = fresh_dir("first"), saved = fresh_dir("saved");
    setenv("AKENO_BACKUP_DIR", saved.c_str(), 1);
    setenv("AKENO_DATA_DIR", first.c_str(), 1);
    {
        web::WebsiteStore sites(first);
        sites.load();
        std::string why, id;
        ASSERT_TRUE(sites.add("Kept Site", "https://kept.example/", &why, &id)) << why;
    }
    net::set_test_transport(
        [](const net::Request &)
        {
            net::Response e;
            e.outcome = net::Outcome::network_error;
            return e;
        });
    test::reset_web_view_script();
    gfx::FontEngine fonts;
    const auto run = [&fonts](const std::function<void(App &)> &check)
    {
        App app(AppConfig{}, fonts);
        std::uint64_t now = 1000;
        app.start(now);
        for (int i = 0; i < 500 && app.backup_status() == "Saving..."; ++i)
        {
            now += 16;
            app.update(now);
            usleep(1000);
        }
        check(app);
    };
    run(
        [&](App &app) {
            EXPECT_EQ(app.backup_status(),
                      std::to_string(backup::count(saved)) + " files in the backup");
        });
    EXPECT_GE(backup::count(saved), 1);

    // Registered again: a new, empty data folder.
    const std::string second = fresh_dir("second");
    setenv("AKENO_DATA_DIR", second.c_str(), 1);
    run([](App &app) { EXPECT_NE(app.websites().find_by_url("https://kept.example/"), nullptr); });

    unsetenv("AKENO_BACKUP_DIR");
    net::set_test_transport({});
}
