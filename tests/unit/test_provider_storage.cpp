// AKENO STREAM PS5 - Provider files across versions: upgrade, rollback to 1.0.0, upgrade again.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// 1.0.0 reads anime-providers.json and rewrites any type it does not know as
// "catalog". These tests run 1.0.0's own storage code (tests/host/
// registry_1_0_0.cpp, copied from the release) and this version against one
// data folder and check that no provider is lost, changed or reinterpreted,
// and that damaged, duplicated, incomplete or newer files are kept, never
// silently rewritten. Every test starts from an empty temporary folder.
#include "anime/address.hpp"
#include "anime/provider_store.hpp"
#include "anime/registry.hpp"
#include "app/backup.hpp"
#include "core/fs.hpp"
#include "core/json.hpp"
#include "registry_1_0_0.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace akeno;
using namespace akeno::anime;
namespace legacy = akeno::test::v1_0_0;

namespace
{
std::string fresh_dir(const char *name)
{
    char path[128];
    std::snprintf(path, sizeof(path), "/tmp/akeno-storage-%s-XXXXXX", name);
    return mkdtemp(path) ? path : "/tmp";
}

std::string read(const std::string &dir, const char *file)
{
    return fs::read_text(fs::join(dir, file)).value_or("<missing>");
}

void write(const std::string &dir, const char *file, const std::string &text)
{
    ASSERT_TRUE(fs::write_atomic(fs::join(dir, file), text));
}

ProviderConfig website(const std::string &name, const std::string &address)
{
    ProviderConfig c;
    c.type = ProviderType::website;
    c.name = name;
    c.address = address;
    c.original_url = address;
    c.adapter = "schema-org";
    c.settings["catalogue"] = address + "anime/";
    c.settings["search"] = address + "search?q={search_term_string}";
    return c;
}

const ProviderConfig *by_name(const Registry &r, const std::string &name)
{
    for (const ProviderConfig &c : r.configs())
        if (c.name == name)
            return &c;
    return nullptr;
}

// 1.0.0 creates an AKENO catalogue and a PeerTube channel.
void make_1_0_0_data(const std::string &dir)
{
    legacy::Registry100 old(dir, dir);
    old.load();
    std::string why;
    legacy::ProviderConfig catalogue;
    catalogue.name = "Open Catalogue";
    catalogue.address = "https://catalog.example/anime.json";
    ASSERT_TRUE(old.add(catalogue, &why)) << why;
    legacy::ProviderConfig tube;
    tube.name = "Open Tube";
    tube.type = legacy::ProviderType::peertube;
    tube.address = "https://tube.example/c/openanime";
    tube.anilist_id = 555;
    ASSERT_TRUE(old.add(tube, &why)) << why;
}
} // namespace

TEST(ProviderStorage, UpgradeKeepsCatalogsAndPeerTube)
{
    // A. Install 1.0.0, add providers, upgrade.
    const std::string dir = fresh_dir("upgrade");
    make_1_0_0_data(dir);
    const std::string written_by_1_0_0 = read(dir, kProvidersFile);

    Registry r(dir, dir);
    r.load();
    EXPECT_TRUE(r.storage_problems().empty()) << r.storage_problems().front();
    ASSERT_NE(by_name(r, "Open Catalogue"), nullptr);
    ASSERT_NE(by_name(r, "Open Tube"), nullptr);
    EXPECT_EQ(by_name(r, "Open Catalogue")->type, ProviderType::catalog);
    EXPECT_EQ(by_name(r, "Open Tube")->type, ProviderType::peertube);
    EXPECT_EQ(by_name(r, "Open Tube")->anilist_id, 555);
    // Loading writes nothing.
    EXPECT_EQ(read(dir, kProvidersFile), written_by_1_0_0);
    EXPECT_FALSE(fs::exists(fs::join(dir, kWebsiteProvidersFile)));
}

TEST(ProviderStorage, WebsiteProviderSurvivesRestartInItsOwnFile)
{
    // B. Add a website provider; restart.
    const std::string dir = fresh_dir("website");
    make_1_0_0_data(dir);
    const std::string catalogs_before = read(dir, kProvidersFile);
    {
        Registry r(dir, dir);
        r.load();
        std::string why, added;
        ASSERT_TRUE(
            r.add(website("Fixture Anime", "https://fixture.example/"), &why, nullptr, &added))
            << why;
        EXPECT_EQ(added, "web_fixture-anime");
    }
    // anime-providers.json is not touched at all: 1.0.0 reads it.
    EXPECT_EQ(read(dir, kProvidersFile), catalogs_before);
    const std::string text = read(dir, kWebsiteProvidersFile);
    EXPECT_EQ(text.find("\"type\""), std::string::npos) << "no type field for 1.0.0 to misread";

    Registry again(dir, dir);
    again.load();
    EXPECT_TRUE(again.storage_problems().empty());
    const ProviderConfig *c = again.config("web_fixture-anime");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->type, ProviderType::website);
    EXPECT_EQ(c->name, "Fixture Anime");
    EXPECT_EQ(c->address, "https://fixture.example/");
    EXPECT_EQ(c->original_url, "https://fixture.example/");
    EXPECT_EQ(c->adapter, "schema-org");
    EXPECT_EQ(c->adapter_version, 1);
    EXPECT_TRUE(c->enabled);
    EXPECT_GT(c->added, 0u);
    EXPECT_EQ(c->settings.at("catalogue"), "https://fixture.example/anime/");
    EXPECT_EQ(c->settings.at("search"), "https://fixture.example/search?q={search_term_string}");
    // The catalogues are still there, in their order.
    ASSERT_NE(by_name(again, "Open Catalogue"), nullptr);
    ASSERT_NE(by_name(again, "Open Tube"), nullptr);

    // Disabled, summarised and removed: always in the website file only.
    ASSERT_TRUE(again.set_enabled("web_fixture-anime", false));
    again.set_summary("web_fixture-anime", 12, 140);
    Registry third(dir, dir);
    third.load();
    ASSERT_NE(third.config("web_fixture-anime"), nullptr);
    EXPECT_FALSE(third.config("web_fixture-anime")->enabled);
    EXPECT_EQ(third.config("web_fixture-anime")->known_series, 12);
    EXPECT_EQ(third.config("web_fixture-anime")->known_episodes, 140);
    EXPECT_TRUE(third.remove("web_fixture-anime"));
    EXPECT_EQ(read(dir, kProvidersFile), catalogs_before);
}

TEST(ProviderStorage, RollbackTo100AndUpgradeAgainKeepsEverything)
{
    const std::string dir = fresh_dir("rollback");
    make_1_0_0_data(dir);
    {
        Registry r(dir, dir);
        r.load();
        std::string why;
        ProviderConfig site = website("Fixture Anime", "https://fixture.example/");
        site.settings["series-path"] = "/anime/";
        ASSERT_TRUE(r.add(site, &why)) << why;
    }
    const std::string websites_before = read(dir, kWebsiteProvidersFile);

    // C. Roll back: 1.0.0 runs against the same folder and changes its list.
    {
        legacy::Registry100 old(dir, dir);
        old.load();
        EXPECT_TRUE(old.last_error().empty()) << old.last_error();
        // 1.0.0 sees exactly its own providers: the bundled series and two.
        ASSERT_EQ(old.configs().size(), 3u);
        for (const legacy::ProviderConfig &c : old.configs())
            EXPECT_NE(c.name, "Fixture Anime");
        std::string why;
        legacy::ProviderConfig added;
        added.name = "Web Fixture Anime"; // its id is "web-fixture-anime", never "web_..."
        added.address = "https://other.example/catalog.json";
        ASSERT_TRUE(old.add(added, &why)) << why;
        EXPECT_EQ(old.configs().back().id, "web-fixture-anime");
        ASSERT_TRUE(old.remove("open-catalogue"));
    }
    // 1.0.0 did not open, rewrite or delete the website file.
    EXPECT_EQ(read(dir, kWebsiteProvidersFile), websites_before);

    // D. Upgrade again: the website provider is exactly as it was, and 1.0.0's
    // changes are there too.
    Registry r(dir, dir);
    r.load();
    EXPECT_TRUE(r.storage_problems().empty()) << r.storage_problems().front();
    const ProviderConfig *site = r.config("web_fixture-anime");
    ASSERT_NE(site, nullptr);
    EXPECT_EQ(site->type, ProviderType::website);
    EXPECT_EQ(site->address, "https://fixture.example/");
    EXPECT_EQ(site->adapter, "schema-org");
    EXPECT_EQ(site->settings.at("series-path"), "/anime/");
    EXPECT_EQ(by_name(r, "Open Catalogue"), nullptr); // removed in 1.0.0
    ASSERT_NE(by_name(r, "Open Tube"), nullptr);
    ASSERT_NE(r.config("web-fixture-anime"), nullptr); // added in 1.0.0
    EXPECT_EQ(r.config("web-fixture-anime")->type, ProviderType::catalog);
    EXPECT_EQ(by_name(r, "Open Tube")->anilist_id, 555);
}

TEST(ProviderStorage, BackupCarriesTheWebsiteFileAnd100LeavesIt)
{
    const auto &now = backup::files();
    EXPECT_NE(std::find(now.begin(), now.end(), kWebsiteProvidersFile), now.end());
    const auto &old = legacy::backup_files();
    EXPECT_EQ(std::find(old.begin(), old.end(), kWebsiteProvidersFile), old.end());

    // A 1.0.0 restore writes only its own files: the website file stays.
    const std::string data = fresh_dir("backup-data");
    const std::string saved = fresh_dir("backup-saved");
    make_1_0_0_data(data);
    {
        Registry r(data, data);
        r.load();
        std::string why;
        ASSERT_TRUE(r.add(website("Fixture Anime", "https://fixture.example/"), &why)) << why;
    }
    const backup::Result copy = backup::save(data, saved);
    EXPECT_TRUE(copy.error.empty()) << copy.error;
    EXPECT_TRUE(fs::exists(fs::join(saved, kWebsiteProvidersFile)));
    const std::string websites = read(data, kWebsiteProvidersFile);
    for (const std::string &name : old)
        if (fs::exists(fs::join(saved, name)))
            write(data, name.c_str(), read(saved, name.c_str()));
    EXPECT_EQ(read(data, kWebsiteProvidersFile), websites);
}

TEST(ProviderStorage, UnknownTypesInTheCatalogFileAreKeptNotReinterpreted)
{
    const std::string dir = fresh_dir("unknown-type");
    write(dir, kProvidersFile, R"({
      "version": 1, "futureTopLevel": {"keep": true},
      "providers": [
        {"id": "open", "name": "Open", "type": "catalog", "address": "https://a.example/c.json",
         "added": 5, "futureField": [1, 2]},
        {"id": "radio", "name": "Radio", "type": "radio", "address": "https://r.example/", "added": 6},
        {"id": "dev-site", "name": "Dev Site", "type": "website", "address": "https://d.example/"},
        {"name": "No Id", "type": "catalog", "address": "https://n.example/c.json"},
        {"id": "file", "name": "File", "type": "catalog", "address": "/data/file.json"},
        "not an object"
      ]})");
    Registry r(dir, dir);
    r.load();
    ASSERT_NE(r.config("open"), nullptr);
    EXPECT_EQ(r.config("radio"), nullptr); // never loaded as a catalogue
    EXPECT_EQ(r.config("dev-site"), nullptr);
    EXPECT_EQ(r.storage_problems().size(), 5u);

    // A change to the list keeps every entry it could not use, unchanged.
    std::string why;
    ProviderConfig added;
    added.name = "Second";
    added.address = "https://b.example/c.json";
    ASSERT_TRUE(r.add(added, &why)) << why;
    const auto saved = json::parse(read(dir, kProvidersFile));
    ASSERT_TRUE(saved.ok);
    EXPECT_TRUE(saved.value["futureTopLevel"]["keep"].boolean());
    const auto &list = saved.value["providers"].items();
    ASSERT_EQ(list.size(), 7u);
    EXPECT_EQ(list[0]["id"].str(), "open");
    EXPECT_EQ(list[0]["futureField"].size(), 2u);
    EXPECT_EQ(list[1]["id"].str(), "second");
    EXPECT_EQ(list[2]["type"].str(), "radio");
    EXPECT_EQ(list[3]["type"].str(), "website");
    EXPECT_EQ(list[4]["name"].str(), "No Id");
    EXPECT_EQ(list[5]["address"].str(), "/data/file.json");
    EXPECT_EQ(list[6].str(), "not an object");
    // An id kept aside is not handed out again.
    ProviderConfig radio;
    radio.name = "Radio";
    radio.address = "https://other-radio.example/c.json";
    ASSERT_TRUE(r.add(radio, &why)) << why;
    EXPECT_EQ(r.configs().back().id, "radio-2");
}

TEST(ProviderStorage, DamagedWebsiteFileIsKeptAndThePreviousCopyRestored)
{
    const std::string dir = fresh_dir("damaged");
    {
        Registry r(dir, dir);
        r.load();
        std::string why;
        ASSERT_TRUE(r.add(website("First", "https://first.example/"), &why)) << why;
        ASSERT_TRUE(r.add(website("Second", "https://second.example/"), &why)) << why;
    }
    // The second save kept the first version as .bak. An interrupted write
    // that left half a file behind:
    const std::string good = read(dir, kWebsiteProvidersFile);
    write(dir, kWebsiteProvidersFile, good.substr(0, good.size() / 2));
    {
        Registry r(dir, dir);
        r.load();
        ASSERT_FALSE(r.storage_problems().empty());
        EXPECT_NE(r.storage_problems().front().find("restored"), std::string::npos);
        EXPECT_NE(r.config("web_first"), nullptr); // from the previous copy
        EXPECT_TRUE(fs::exists(fs::join(dir, std::string{kWebsiteProvidersFile} + ".corrupt")));
    }
    // A damaged file without a usable previous copy: kept, nothing invented.
    write(dir, (std::string{kWebsiteProvidersFile} + ".bak").c_str(), "{also damaged");
    write(dir, kWebsiteProvidersFile, "{\"providers\": [");
    Registry r(dir, dir);
    r.load();
    EXPECT_EQ(r.config("web_first"), nullptr);
    ASSERT_FALSE(r.storage_problems().empty());
    // Both damaged versions are kept (a different damaged copy is not overwritten).
    EXPECT_TRUE(fs::exists(fs::join(dir, std::string{kWebsiteProvidersFile} + ".corrupt")));
    EXPECT_TRUE(fs::exists(fs::join(dir, std::string{kWebsiteProvidersFile} + ".corrupt-2")));
}

TEST(ProviderStorage, LeftoverTemporaryFileIsIgnored)
{
    const std::string dir = fresh_dir("tmp");
    {
        Registry r(dir, dir);
        r.load();
        std::string why;
        ASSERT_TRUE(r.add(website("First", "https://first.example/"), &why)) << why;
    }
    const std::string good = read(dir, kWebsiteProvidersFile);
    // A write stopped before its rename: only the .tmp file is incomplete.
    write(dir, (std::string{kWebsiteProvidersFile} + ".tmp").c_str(), "{\"provid");
    Registry r(dir, dir);
    r.load();
    EXPECT_TRUE(r.storage_problems().empty());
    EXPECT_NE(r.config("web_first"), nullptr);
    EXPECT_EQ(read(dir, kWebsiteProvidersFile), good);
}

TEST(ProviderStorage, DuplicateIncompleteAndUnknownEntriesAreKeptAside)
{
    const std::string dir = fresh_dir("entries");
    write(dir, kWebsiteProvidersFile, R"({
      "format": "akeno-website-providers", "version": 1, "minReaderVersion": 1,
      "providers": [
        {"id": "web_one", "name": "One", "url": "https://one.example/", "adapter": "schema-org",
         "settings": {"catalogue": "https://one.example/", "future": {"x": 1}}, "extra": "kept"},
        {"id": "web_one", "name": "One again", "url": "https://one-again.example/", "adapter": "schema-org"},
        {"id": "web_two", "name": "No address", "adapter": "schema-org"},
        {"id": "web_three", "name": "No adapter", "url": "https://three.example/"},
        {"id": "web_four", "name": "Future adapter", "url": "https://four.example/", "adapter": "holo-stream"},
        {"id": "web_five", "name": "Newer settings", "url": "https://five.example/", "adapter": "schema-org", "adapterVersion": 7},
        {"id": "Bad Id!", "name": "Bad id", "url": "https://six.example/", "adapter": "schema-org"},
        {"id": "akeno-sample", "name": "Built-in id", "url": "https://seven.example/", "adapter": "schema-org"},
        {"id": "web_eight", "name": "Bad flag", "url": "https://eight.example/", "adapter": "schema-org", "enabled": "yes"}
      ]})");
    Registry r(dir, dir);
    r.load();
    ASSERT_NE(r.config("web_one"), nullptr);
    EXPECT_EQ(r.config("web_one")->name, "One");
    for (const char *id : {"web_two", "web_three", "web_four", "web_five", "web_eight"})
        EXPECT_EQ(r.config(id), nullptr) << id;
    EXPECT_EQ(r.storage_problems().size(), 8u);
    // A new provider does not take a kept id, and saving keeps all of them.
    std::string why, added;
    ProviderConfig four = website("Four", "https://fourth.example/");
    four.name = "Four";
    ASSERT_TRUE(r.add(four, &why, nullptr, &added)) << why;
    EXPECT_EQ(added, "web_four-2");
    const auto saved = json::parse(read(dir, kWebsiteProvidersFile));
    ASSERT_TRUE(saved.ok);
    const auto &list = saved.value["providers"].items();
    ASSERT_EQ(list.size(), 10u);
    EXPECT_EQ(list[0]["extra"].str(), "kept");
    EXPECT_EQ(list[0]["settings"]["future"]["x"].integer(), 1);
    EXPECT_EQ(list[1]["id"].str(), "web_four-2");
    EXPECT_EQ(list[2]["name"].str(), "One again");
    EXPECT_EQ(list[5]["adapter"].str(), "holo-stream");
    EXPECT_EQ(list[6]["adapterVersion"].integer(), 7);
    EXPECT_EQ(list[9]["enabled"].str(), "yes");
}

TEST(ProviderStorage, NewerFormatIsNeverWritten)
{
    const std::string dir = fresh_dir("newer");
    // Readable by older versions: loaded, but this version does not write it.
    const std::string readable = R"({"format": "akeno-website-providers", "version": 2,
      "minReaderVersion": 1, "newThing": true, "providers": [
        {"id": "web_one", "name": "One", "url": "https://one.example/", "adapter": "schema-org"}]})";
    write(dir, kWebsiteProvidersFile, readable);
    {
        Registry r(dir, dir);
        r.load();
        EXPECT_NE(r.config("web_one"), nullptr);
        std::string why, existing;
        EXPECT_FALSE(r.add(website("Two", "https://two.example/"), &why));
        EXPECT_NE(why.find("newer"), std::string::npos) << why;
        EXPECT_FALSE(r.remove("web_one"));
        EXPECT_NE(r.config("web_one"), nullptr);
        EXPECT_FALSE(r.set_enabled("web_one", false));
        EXPECT_EQ(read(dir, kWebsiteProvidersFile), readable);
        // Catalogues are in their own file and can still be added.
        ProviderConfig catalogue;
        catalogue.name = "Catalogue";
        catalogue.address = "https://c.example/c.json";
        EXPECT_TRUE(r.add(catalogue, &why)) << why;
    }
    // Not meant for this version at all: nothing loaded, nothing written.
    const std::string unreadable = R"({"format": "akeno-website-providers", "version": 3,
      "minReaderVersion": 3, "providers": [
        {"id": "web_one", "name": "One", "url": "https://one.example/", "adapter": "schema-org"}]})";
    write(dir, kWebsiteProvidersFile, unreadable);
    Registry r(dir, dir);
    r.load();
    EXPECT_EQ(r.config("web_one"), nullptr);
    ASSERT_FALSE(r.storage_problems().empty());
    std::string why;
    EXPECT_FALSE(r.add(website("Two", "https://two.example/"), &why));
    EXPECT_EQ(read(dir, kWebsiteProvidersFile), unreadable);
}

TEST(ProviderStorage, OneAddressIsOneProvider)
{
    const std::string dir = fresh_dir("duplicate");
    make_1_0_0_data(dir);
    Registry r(dir, dir);
    r.load();
    std::string why, existing;
    ASSERT_TRUE(r.add(website("Fixture", "https://fixture.example/"), &why)) << why;
    // The same site typed differently.
    for (const char *again :
         {"https://fixture.example", "http://FIXTURE.example/", "https://www.fixture.example:443/"})
    {
        ProviderConfig c = website("Again", again);
        existing.clear();
        EXPECT_FALSE(r.add(c, &why, &existing)) << again;
        EXPECT_EQ(existing, "web_fixture") << again;
    }
    // A website provider at a catalogue's address is that catalogue.
    existing.clear();
    EXPECT_FALSE(r.add(website("Tube", "https://tube.example/c/openanime/"), &why, &existing));
    EXPECT_EQ(existing, "open-tube");
    EXPECT_NE(r.find_by_address("https://fixture.example/"), nullptr);
    EXPECT_EQ(r.find_by_address("https://fixture.example/other"), nullptr);
}

TEST(ProviderStorage, AddressesAreNormalised)
{
    EXPECT_EQ(normalize_site_address("video.blender.org").url, "https://video.blender.org/");
    EXPECT_EQ(normalize_site_address("  HTTPS://Video.Blender.org:443  ").url,
              "https://video.blender.org/");
    EXPECT_EQ(normalize_site_address("http://a.example:8080/x?y=1#frag").url,
              "http://a.example:8080/x?y=1");
    EXPECT_FALSE(normalize_site_address("").ok);
    EXPECT_FALSE(normalize_site_address("https://").ok);
    EXPECT_FALSE(normalize_site_address("ftp://a.example/").ok);
    EXPECT_FALSE(normalize_site_address("https://user:pw@a.example/").ok);
    EXPECT_NE(normalize_site_address("https://user:pw@a.example/").why.find("password"),
              std::string::npos);
    EXPECT_FALSE(normalize_site_address("javascript:alert(1)").ok);
    EXPECT_FALSE(normalize_site_address("not a url").ok);
    EXPECT_TRUE(normalize_site_address("http://localhost:8080/").ok);
    EXPECT_TRUE(same_site_address("https://a.example/x/", "http://www.a.example/x"));
    EXPECT_FALSE(same_site_address("https://a.example/x", "https://a.example/y"));
    EXPECT_FALSE(same_site_address("https://a.example:8443/", "https://a.example/"));
}
