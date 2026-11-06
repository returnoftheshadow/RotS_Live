#include "../save_benchmark.h"

#include "../account_cache.h"
#include "../account_management.h"
#include "../character_json.h"
#include "../color.h"
#include "../db.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>

namespace {

// Throwaway directory tree for the offline fixture; mirrors the helper used by the
// db_loader/account_management test suites so the pipeline writes nothing live.
class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        char path_template[] = "/tmp/rots-save-benchmark-XXXXXX";
        char* created_path = mkdtemp(path_template);
        EXPECT_NE(created_path, nullptr);
        if (created_path)
            m_path = created_path;
    }

    ~TemporaryDirectory()
    {
        if (!m_path.empty()) {
            std::string command = "rm -rf '" + m_path + "'";
            std::system(command.c_str());
        }
    }

    const std::string& path() const { return m_path; }

private:
    // Absolute path of the temp directory created by mkdtemp; empty if creation failed.
    std::string m_path;
};

char_file_u make_stored_character(const char* name = "aragorn")
{
    char_file_u stored_character {};
    std::snprintf(stored_character.name, sizeof(stored_character.name), "%s", name);
    std::snprintf(stored_character.title, sizeof(stored_character.title), "%s", "the Ranger");
    std::snprintf(stored_character.description, sizeof(stored_character.description), "%s", "A ranger from the north.");
    stored_character.sex = SEX_MALE;
    stored_character.race = RACE_HUMAN;
    stored_character.bodytype = 1;
    stored_character.level = 12;
    stored_character.language = LANG_HUMAN;
    stored_character.birth = 1700000000;
    stored_character.played = 456;
    stored_character.weight = 190;
    stored_character.height = 72;
    stored_character.hometown = 7;
    stored_character.last_logon = 1700000100;
    stored_character.points.gold = 1234;
    stored_character.points.exp = 5678;
    stored_character.specials2.idnum = 4242;
    stored_character.specials2.load_room = 3001;
    stored_character.specials2.act = 0;
    stored_character.specials2.pref = 1L << 5;
    stored_character.specials2.tactics = TACTICS_BERSERK;
    stored_character.specials2.shooting = SHOOTING_FAST;
    stored_character.specials2.casting = CASTING_SLOW;
    stored_character.specials2.two_handed = 1;
    stored_character.profs.colors[COLOR_MAGIC] = CBMAG;
    stored_character.profs.colors[COLOR_WEATHER] = CBCYN;
    stored_character.profs.color_settings[COLOR_MAGIC].foreground.mode = COLOR_VALUE_TRUECOLOR;
    stored_character.profs.color_settings[COLOR_MAGIC].foreground.ansi = CBMAG;
    stored_character.profs.color_settings[COLOR_MAGIC].foreground.red = 180;
    stored_character.profs.color_settings[COLOR_MAGIC].foreground.green = 80;
    stored_character.profs.color_settings[COLOR_MAGIC].foreground.blue = 255;
    stored_character.profs.color_settings[COLOR_WEATHER].background.mode = COLOR_VALUE_TRUECOLOR;
    stored_character.profs.color_settings[COLOR_WEATHER].background.ansi = CBBLU;
    stored_character.profs.color_settings[COLOR_WEATHER].background.red = 10;
    stored_character.profs.color_settings[COLOR_WEATHER].background.green = 20;
    stored_character.profs.color_settings[COLOR_WEATHER].background.blue = 35;
    stored_character.profs.prof_level[PROF_WARRIOR] = 12;
    stored_character.profs.prof_coof[PROF_WARRIOR] = 34;
    return stored_character;
}

// Create the account-character directory chain (accounts/<bucket>/<account>) with direct
// mkdir calls. Avoids account::create_account/admin_link, whose directory-scan
// (opendir/readdir) lookups do not function under this 32-bit QEMU test environment.
void make_account_character_directory(const std::string& root, const std::string& account, const std::string& character)
{
    const std::string directory = account::account_character_directory(root, account, character);
    const std::size_t slash = directory.rfind('/');
    const std::string bucket_directory = (slash == std::string::npos) ? directory : directory.substr(0, slash);
    const std::size_t accounts_slash = bucket_directory.rfind('/');
    const std::string accounts_directory = (accounts_slash == std::string::npos) ? bucket_directory : bucket_directory.substr(0, accounts_slash);
    mkdir(accounts_directory.c_str(), 0700);
    mkdir(bucket_directory.c_str(), 0700);
    mkdir(directory.c_str(), 0700);
}

// Read an account-owned character.json back into a char_file_u using only direct path I/O
// (read_text_file + deserialize + apply) -- the same steps read_account_character_file
// performs after its account lookup, minus the broken directory-scan account read.
bool read_character_file_directly(const std::string& path, char_file_u* stored_character, std::string* error)
{
    std::string json;
    if (!account::read_text_file(path, &json, error))
        return false;
    character_json::CharacterData character;
    if (!character_json::deserialize_character_from_json(json, &character, error))
        return false;
    return character_json::apply_character_data_to_store(character, stored_character, error);
}

} // namespace

// Stages an account-owned character "aragorn" under a TemporaryDirectory using direct path
// I/O, profiles both the SAVE and LOAD pipelines, and asserts the on-disk bytes round-trip.
// Timing checks are loose (informational) to stay flake-free under QEMU emulation.
TEST(SaveBenchmark, ProfilesBothDirectionsAndRoundTrips)
{
    TemporaryDirectory temp;
    const std::string root = temp.path();
    const std::string account = "alpha-admin";
    const std::string character = "aragorn";
    std::string err;

    // Arrange: write the account-owned <character>.character.json directly at its canonical
    // path (the bytes write_account_character_file would produce), then read it back into the
    // char_file_u that feeds the SAVE pipeline.
    make_account_character_directory(root, account, character);
    const std::string character_path = account::account_character_player_path(root, account, character);
    const char_file_u original = make_stored_character("aragorn");
    const std::string json = character_json::serialize_character_to_json(character_json::character_data_from_store(original));
    ASSERT_TRUE(account::write_text_file_atomically(character_path, json, &err)) << err;

    char_file_u source {};
    ASSERT_TRUE(read_character_file_directly(character_path, &source, &err)) << err;

    savebench::PipelineReport save_report;
    savebench::PipelineReport load_report;
    ASSERT_TRUE(savebench::profile_save(source, root, account, character, root + "/sb_scratch.json",
                    5, &save_report, &err))
        << err;
    ASSERT_TRUE(savebench::profile_load(root, account, character, 5, /*include_store_to_char=*/true,
                    &load_report, &err))
        << err;

    // The breakdown lists every stage and the total >= 0; shares sum to ~100%.
    EXPECT_FALSE(save_report.stages.empty());
    EXPECT_GE(save_report.total.avg_us, 0);
    double save_share = save_report.other.share;
    for (const auto& s : save_report.stages)
        save_share += s.share;
    EXPECT_NEAR(save_share, 100.0, 1.0);

    EXPECT_FALSE(load_report.stages.empty());
    EXPECT_GE(load_report.total.avg_us, 0);
    double load_share = load_report.other.share;
    for (const auto& s : load_report.stages)
        load_share += s.share;
    EXPECT_NEAR(load_share, 100.0, 1.0);

    // Round-trip: re-reading the stored character reproduces the source struct byte-for-byte.
    char_file_u reloaded {};
    ASSERT_TRUE(read_character_file_directly(character_path, &reloaded, &err)) << err;
    EXPECT_EQ(0, memcmp(&source, &reloaded, sizeof(char_file_u)));

    // Human-readable output for the engineer running the suite.
    printf("%s", savebench::format_report("SAVE", save_report).c_str());
    printf("%s", savebench::format_report("LOAD", load_report).c_str());
}

// Exercises the opt-in COMPARE report: profile_save/profile_load populate a SEPARATE report with the
// cache + serialize/deserialize A/B stages (byte-exact labels), leave the canonical breakdown
// untouched, and the compare report's own shares reconcile to ~100% because its TOTAL runs each
// compared item exactly once. Timing is not asserted (QEMU-flaky).
TEST(SaveBenchmark, CompareReportPopulatesVariantStages)
{
    account_cache::clear(); // contract: clear the memo maps for test isolation
    TemporaryDirectory temp;
    const std::string root = temp.path();
    const std::string account = "alpha-admin";
    const std::string character = "aragorn";
    std::string err;

    make_account_character_directory(root, account, character);
    const std::string character_path = account::account_character_player_path(root, account, character);
    const char_file_u original = make_stored_character("aragorn");
    const std::string json =
        character_json::serialize_character_to_json(character_json::character_data_from_store(original));
    ASSERT_TRUE(account::write_text_file_atomically(character_path, json, &err)) << err;

    char_file_u source {};
    ASSERT_TRUE(read_character_file_directly(character_path, &source, &err)) << err;

    savebench::PipelineReport save_report, load_report;
    savebench::PipelineReport save_compare, load_compare;
    ASSERT_TRUE(savebench::profile_save(source, root, account, character, root + "/sb_scratch.json",
                    3, &save_report, &err, &save_compare))
        << err;
    ASSERT_TRUE(savebench::profile_load(root, account, character, 3, /*include_store_to_char=*/false,
                    &load_report, &err, &load_compare))
        << err;

    // Canonical report is unaffected by the compare opt-in (still S2,S3,S4,S5).
    EXPECT_EQ(save_report.stages.size(), 4u);

    // Compare reports carry exactly the five A/B stages with the contract's labels.
    ASSERT_EQ(save_compare.stages.size(), 5u);
    EXPECT_EQ(save_compare.stages[0].name, "S2  read_account_file        (v1)");
    EXPECT_EQ(save_compare.stages[1].name, "S2c read_account_file_cached");
    EXPECT_EQ(save_compare.stages[2].name, "S4  serialize_character_to_json     (v1)");
    EXPECT_EQ(save_compare.stages[3].name, "S4a serialize_character_to_json_v2a");
    EXPECT_EQ(save_compare.stages[4].name, "S4b serialize_character_to_json_v2b");

    ASSERT_EQ(load_compare.stages.size(), 5u);
    EXPECT_EQ(load_compare.stages[0].name, "L1  read_account_file        (v1)");
    EXPECT_EQ(load_compare.stages[1].name, "L1c read_account_file_cached");
    EXPECT_EQ(load_compare.stages[2].name, "L3  deserialize_character_from_json     (v1)");
    EXPECT_EQ(load_compare.stages[3].name, "L3a deserialize_character_from_json_v2a");
    EXPECT_EQ(load_compare.stages[4].name, "L3b deserialize_character_from_json_v2b");

    // The compare report's per-stage + other shares reconcile to ~100% (its TOTAL runs each once).
    double save_cmp_share = save_compare.other.share;
    for (const auto& s : save_compare.stages)
        save_cmp_share += s.share;
    EXPECT_NEAR(save_cmp_share, 100.0, 1.0);
    double load_cmp_share = load_compare.other.share;
    for (const auto& s : load_compare.stages)
        load_cmp_share += s.share;
    EXPECT_NEAR(load_cmp_share, 100.0, 1.0);

    // Opt-in only: a report not passed as the compare arg stays empty.
    savebench::PipelineReport plain_report, never_filled;
    ASSERT_TRUE(savebench::profile_save(source, root, account, character, root + "/sb_scratch2.json",
                    3, &plain_report, &err))
        << err;
    EXPECT_TRUE(never_filled.stages.empty());
}
