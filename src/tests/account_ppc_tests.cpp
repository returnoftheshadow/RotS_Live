#include "../account_management.h"
#include "../account_management_types.h"
#include "../account_ppc.h"
#include "../character_json.h"
#include "../color.h"
#include "../db.h"
#include "../json_utils.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        char directory_template[] = "/tmp/rots-account-tests-XXXXXX";
        char* created_path = mkdtemp(directory_template);
        EXPECT_NE(created_path, nullptr) << "Expected mkdtemp to create a temporary directory for account-management tests.";
        if (created_path)
            m_path = created_path;
    }

    ~TemporaryDirectory()
    {
        if (!m_path.empty())
            remove_tree(m_path);
    }

    const std::string& path() const
    {
        return m_path;
    }

private:
    static void remove_tree(const std::string& path)
    {
        DIR* directory = opendir(path.c_str());
        if (directory == nullptr) {
            std::remove(path.c_str());
            return;
        }

        while (dirent* entry = readdir(directory)) {
            if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
                continue;

            const std::string child_path = path + "/" + entry->d_name;
            struct stat file_info { };
            if (stat(child_path.c_str(), &file_info) != 0)
                continue;

            if (S_ISDIR(file_info.st_mode))
                remove_tree(child_path);
            else
                std::remove(child_path.c_str());
        }

        closedir(directory);
        rmdir(path.c_str());
    }

    std::string m_path;
};

TEST(AccountPpcMask, CoversEveryPpcOptionAndNothingElse)
{
    const long expected = PRF_PROMPT | PRF_ADVANCED_PROMPT | PRF_ADVANCED_VIEW
        | PRF_BRIEF | PRF_COMPACT | PRF_SPAM | PRF_WRAP | PRF_ECHO
        | PRF_MSDP | PRF_LATIN1 | PRF_SPINNER
        | PRF_INV_SORT1 | PRF_INV_SORT2 | PRF_COLOR;
    EXPECT_EQ(PPC_PRF_MASK, expected);
}

TEST(AccountPpcMask, ExcludesCharacterAndMoodSettings)
{
    EXPECT_EQ(PPC_PRF_MASK & PRF_NOTELL, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_NARRATE, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_CHAT, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_SING, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_MENTAL, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_SWIM, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_SUMMONABLE, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_HOLYLIGHT, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_ROOMFLAGS, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_WIZ, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_LOG1, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_LOG2, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_LOG3, 0);
}

TEST(AccountPpcPreferences, DefaultsToAbsent)
{
    account::AccountData account;
    EXPECT_FALSE(account.preferences.present);
    EXPECT_EQ(account.preferences.preference_flags, 0);
}

TEST(AccountPpcPreferences, ColourArraysAreZeroInitialised)
{
    account::AccountPreferences preferences;
    for (int index = 0; index < MAX_COLOR_FIELDS; ++index) {
        EXPECT_EQ(preferences.colors[index], 0) << "slot " << index;
        EXPECT_EQ(preferences.color_settings[index].foreground.mode, COLOR_VALUE_DEFAULT);
        EXPECT_EQ(preferences.color_settings[index].background.mode, COLOR_VALUE_DEFAULT);
    }
}

TEST(AccountPpcColorSlots, RoundTripsAnsiAndTruecolorSlots)
{
    char colors[MAX_COLOR_FIELDS] = {};
    color_slot_data settings[MAX_COLOR_FIELDS] = {};

    colors[COLOR_NARR] = CRED;
    settings[COLOR_NARR].foreground.mode = COLOR_VALUE_ANSI16;
    settings[COLOR_NARR].foreground.ansi = CRED;

    settings[COLOR_TELL].foreground.mode = COLOR_VALUE_TRUECOLOR;
    settings[COLOR_TELL].foreground.red = 18;
    settings[COLOR_TELL].foreground.green = 200;
    settings[COLOR_TELL].foreground.blue = 7;
    settings[COLOR_TELL].background.mode = COLOR_VALUE_ANSI16;
    settings[COLOR_TELL].background.ansi = CBBLU;

    const std::string body = character_json::encode_color_slots_object(colors, settings);
    const std::string document = "{\"colors\": {" + body + "}}";

    char decoded_colors[MAX_COLOR_FIELDS] = {};
    color_slot_data decoded_settings[MAX_COLOR_FIELDS] = {};
    std::string error_message;
    json_utils::JsonReader reader(document);
    const bool parsed = reader.parse_root_object(
        [&](const std::string& key, json_utils::JsonReader* nested, std::string* nested_error) {
            if (key == "colors")
                return character_json::parse_color_slots_object(nested, decoded_colors, decoded_settings, nested_error);
            return nested->skip_value(nested_error);
        },
        &error_message);

    ASSERT_TRUE(parsed) << error_message;
    EXPECT_EQ(decoded_colors[COLOR_NARR], CRED);
    EXPECT_EQ(decoded_settings[COLOR_TELL].foreground.mode, COLOR_VALUE_TRUECOLOR);
    EXPECT_EQ(decoded_settings[COLOR_TELL].foreground.red, 18);
    EXPECT_EQ(decoded_settings[COLOR_TELL].foreground.green, 200);
    EXPECT_EQ(decoded_settings[COLOR_TELL].foreground.blue, 7);
    EXPECT_EQ(decoded_settings[COLOR_TELL].background.mode, COLOR_VALUE_ANSI16);
    EXPECT_EQ(decoded_settings[COLOR_TELL].background.ansi, CBBLU);
}

TEST(AccountPpcColorSlots, OmitsDefaultSlotsAndResetsOnParse)
{
    char colors[MAX_COLOR_FIELDS] = {};
    color_slot_data settings[MAX_COLOR_FIELDS] = {};
    EXPECT_EQ(character_json::encode_color_slots_object(colors, settings), "");

    char decoded_colors[MAX_COLOR_FIELDS];
    color_slot_data decoded_settings[MAX_COLOR_FIELDS];
    for (int index = 0; index < MAX_COLOR_FIELDS; ++index) {
        decoded_colors[index] = CWHT;
        decoded_settings[index].foreground.mode = COLOR_VALUE_ANSI16;
    }

    std::string error_message;
    json_utils::JsonReader reader("{\"colors\": {}}");
    ASSERT_TRUE(reader.parse_root_object(
        [&](const std::string& key, json_utils::JsonReader* nested, std::string* nested_error) {
            if (key == "colors")
                return character_json::parse_color_slots_object(nested, decoded_colors, decoded_settings, nested_error);
            return nested->skip_value(nested_error);
        },
        &error_message))
        << error_message;

    for (int index = 0; index < MAX_COLOR_FIELDS; ++index) {
        EXPECT_EQ(decoded_colors[index], 0) << "slot " << index;
        EXPECT_EQ(decoded_settings[index].foreground.mode, COLOR_VALUE_DEFAULT) << "slot " << index;
    }
}

account::AccountData make_minimal_account()
{
    account::AccountData account;
    account.account_name = "ppctester";
    account.normalized_email = "ppctester@example.com";
    account.password_hash = "hash";
    account.password_salt = "salt";
    return account;
}

TEST(AccountPpcStorage, RoundTripsPreferences)
{
    account::AccountData account = make_minimal_account();
    account.preferences.present = true;
    account.preferences.preference_flags = PRF_BRIEF | PRF_COMPACT | PRF_MSDP;
    account.preferences.colors[COLOR_NARR] = CRED;
    account.preferences.color_settings[COLOR_NARR].foreground.mode = COLOR_VALUE_ANSI16;
    account.preferences.color_settings[COLOR_NARR].foreground.ansi = CRED;

    const std::string json = account::serialize_account_to_json(account);
    account::AccountData reloaded;
    std::string error_message;
    ASSERT_TRUE(account::deserialize_account_from_json(json, &reloaded, &error_message)) << error_message;

    EXPECT_TRUE(reloaded.preferences.present);
    EXPECT_EQ(reloaded.preferences.preference_flags, PRF_BRIEF | PRF_COMPACT | PRF_MSDP);
    EXPECT_EQ(reloaded.preferences.colors[COLOR_NARR], CRED);
    EXPECT_EQ(reloaded.preferences.color_settings[COLOR_NARR].foreground.mode, COLOR_VALUE_ANSI16);
    EXPECT_EQ(reloaded.preferences.color_settings[COLOR_NARR].foreground.ansi, CRED);
}

TEST(AccountPpcStorage, AbsentPreferencesKeyYieldsNotPresent)
{
    account::AccountData account = make_minimal_account();
    const std::string json = account::serialize_account_to_json(account);
    ASSERT_EQ(json.find("\"preferences\""), std::string::npos)
        << "An account with no PPC must not emit a preferences key.";

    account::AccountData reloaded;
    std::string error_message;
    ASSERT_TRUE(account::deserialize_account_from_json(json, &reloaded, &error_message)) << error_message;
    EXPECT_FALSE(reloaded.preferences.present);
    EXPECT_EQ(reloaded.version, 1);
}

TEST(AccountPpcStorage, SchemaVersionIsNotBumped)
{
    EXPECT_EQ(account::ACCOUNT_SCHEMA_VERSION, 1)
        << "Bumping the version makes deserialize reject every existing account file.";

    account::AccountData account = make_minimal_account();
    account.preferences.present = true;
    account.preferences.preference_flags = PRF_BRIEF;
    account::AccountData reloaded;
    std::string error_message;
    ASSERT_TRUE(account::deserialize_account_from_json(
        account::serialize_account_to_json(account), &reloaded, &error_message))
        << error_message;
    EXPECT_EQ(reloaded.version, 1);
}

TEST(AccountPpcStorage, PreferencesKeyIsNotTheFinalField)
{
    account::AccountData account = make_minimal_account();
    account.preferences.present = true;
    account.preferences.preference_flags = PRF_BRIEF;
    const std::string json = account::serialize_account_to_json(account);
    EXPECT_LT(json.find("\"preferences\""), json.find("\"password_reset_attempt_count\""))
        << "password_reset_attempt_count must stay the last, comma-less field.";
}

// A rolled-back binary must still boot when an account.json was written by a later build that
// added a PRF flag or colour slot this binary doesn't know about (db.cpp aborts the whole boot
// on any account that fails to parse). An unrecognised name inside "preferences" must be
// skipped, not fatal -- unlike the "unknown name is fatal" behaviour for character files, which
// is untouched (see CharacterJson.RejectsUnknownPreferenceFlagNameByDefault below).
TEST(AccountPpcStorage, PreferencesBlockSkipsUnknownFlagAndColorNamesInsteadOfFailingToParse)
{
    account::AccountData account = make_minimal_account();
    account.preferences.present = true;
    account.preferences.preference_flags = PRF_BRIEF;
    account.preferences.colors[COLOR_NARR] = CRED;
    account.preferences.color_settings[COLOR_NARR].foreground.mode = COLOR_VALUE_ANSI16;
    account.preferences.color_settings[COLOR_NARR].foreground.ansi = CRED;

    std::string json = account::serialize_account_to_json(account);
    ASSERT_NE(json.find("\"flags\": [\"brief\"]"), std::string::npos) << json;
    ASSERT_NE(json.find("\"colors\": {\"narrate\""), std::string::npos) << json;

    // Simulate a future build's account.json: an unrecognised flag name mixed in with a known
    // one, and an unrecognised colour key (with an arbitrary value shape -- skip_value must
    // accept any well-formed JSON value) alongside a known colour slot.
    json.replace(json.find("\"flags\": [\"brief\"]"), std::string("\"flags\": [\"brief\"]").size(),
        "\"flags\": [\"brief\", \"future_flag_from_a_later_build\"]");
    json.replace(json.find("\"colors\": {\"narrate\""), std::string("\"colors\": {").size(),
        "\"colors\": {\"future_color_slot\": 12345, ");

    account::AccountData reloaded;
    std::string error_message;
    ASSERT_TRUE(account::deserialize_account_from_json(json, &reloaded, &error_message)) << error_message;

    EXPECT_TRUE(reloaded.preferences.present);
    EXPECT_EQ(reloaded.preferences.preference_flags, PRF_BRIEF);
    EXPECT_EQ(reloaded.preferences.colors[COLOR_NARR], CRED);
    EXPECT_EQ(reloaded.preferences.color_settings[COLOR_NARR].foreground.mode, COLOR_VALUE_ANSI16);
    EXPECT_EQ(reloaded.preferences.color_settings[COLOR_NARR].foreground.ansi, CRED);
}

TEST(AccountPpcStorage, MasksNonPpcBitsOutOnWriteAndRead)
{
    account::AccountData account = make_minimal_account();
    account.preferences.present = true;
    // PRF_NOTELL is a character/mood setting, not a PPC option -- it must never survive the
    // account-level round trip.
    account.preferences.preference_flags = PRF_BRIEF | PRF_NOTELL;

    const std::string json = account::serialize_account_to_json(account);
    EXPECT_EQ(json.find("\"notell\""), std::string::npos)
        << "PRF_NOTELL is not a PPC bit and must not be emitted.";

    account::AccountData reloaded;
    std::string error_message;
    ASSERT_TRUE(account::deserialize_account_from_json(json, &reloaded, &error_message)) << error_message;
    EXPECT_EQ(reloaded.preferences.preference_flags & PRF_NOTELL, 0)
        << "PRF_NOTELL must not survive deserialization either.";
    EXPECT_NE(reloaded.preferences.preference_flags & PRF_BRIEF, 0);
}

struct PpcTestCharacter {
    PpcTestCharacter()
        : character(new char_data {})
    {
        clear_char(character, MOB_VOID);
        character->profs = &prof_storage;
    }
    ~PpcTestCharacter() { delete character; }

    char_prof_data prof_storage {};
    char_data* character;
};

TEST(AccountPpcApply, PreservesNonPpcPreferenceBits)
{
    PpcTestCharacter subject;
    PRF_FLAGS(subject.character) = PRF_NOTELL | PRF_SING | PRF_MENTAL | PRF_SWIM | PRF_BRIEF;

    account::AccountPreferences preferences;
    preferences.present = true;
    preferences.preference_flags = PRF_COMPACT | PRF_MSDP;

    ppc_write_to_character(preferences, subject.character);

    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_NOTELL));
    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_SING));
    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_MENTAL));
    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_SWIM));
    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_COMPACT));
    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_MSDP));
    EXPECT_FALSE(PRF_FLAGGED(subject.character, PRF_BRIEF))
        << "A PPC bit clear on the account must be cleared on the character.";
}

TEST(AccountPpcApply, IsANoOpWhenPreferencesAreAbsent)
{
    PpcTestCharacter subject;
    PRF_FLAGS(subject.character) = PRF_BRIEF | PRF_NOTELL;

    account::AccountPreferences preferences;
    ppc_write_to_character(preferences, subject.character);

    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_BRIEF));
    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_NOTELL));
}

TEST(AccountPpcApply, RoundTripsThroughACharacter)
{
    PpcTestCharacter source;
    PRF_FLAGS(source.character) = PRF_BRIEF | PRF_WRAP | PRF_COLOR | PRF_NOTELL;
    source.character->profs->colors[COLOR_CHAT] = CGRN;
    source.character->profs->color_settings[COLOR_CHAT].foreground.mode = COLOR_VALUE_ANSI16;
    source.character->profs->color_settings[COLOR_CHAT].foreground.ansi = CGRN;

    const account::AccountPreferences preferences = ppc_read_from_character(source.character);
    EXPECT_TRUE(preferences.present);
    EXPECT_EQ(preferences.preference_flags & PRF_NOTELL, 0)
        << "ppc_read_from_character must mask out non-PPC bits.";

    PpcTestCharacter destination;
    ppc_write_to_character(preferences, destination.character);

    EXPECT_TRUE(PRF_FLAGGED(destination.character, PRF_BRIEF));
    EXPECT_TRUE(PRF_FLAGGED(destination.character, PRF_WRAP));
    EXPECT_TRUE(PRF_FLAGGED(destination.character, PRF_COLOR));
    EXPECT_EQ(destination.character->profs->colors[COLOR_CHAT], CGRN);
    EXPECT_EQ(destination.character->profs->color_settings[COLOR_CHAT].foreground.ansi, CGRN);
}

TEST(AccountPpcEqual, DetectsFlagAndColourDifferences)
{
    account::AccountPreferences left;
    left.present = true;
    left.preference_flags = PRF_BRIEF;
    account::AccountPreferences right = left;
    EXPECT_TRUE(ppc_equal(left, right));

    right.preference_flags |= PRF_COMPACT;
    EXPECT_FALSE(ppc_equal(left, right));

    right = left;
    right.colors[COLOR_SAY] = CYEL;
    EXPECT_FALSE(ppc_equal(left, right));

    right = left;
    right.color_settings[COLOR_SAY].background.mode = COLOR_VALUE_TRUECOLOR;
    EXPECT_FALSE(ppc_equal(left, right));
}

TEST(AccountPpcEqual, IgnoresNonPpcBitsAndPresence)
{
    account::AccountPreferences left;
    left.present = true;
    left.preference_flags = PRF_BRIEF | PRF_NOTELL;
    account::AccountPreferences right;
    right.present = false;
    right.preference_flags = PRF_BRIEF | PRF_SING;
    EXPECT_TRUE(ppc_equal(left, right));
}

TEST(AccountPpcLogin, SeedsAnAccountThatHasNoPreferences)
{
    TemporaryDirectory root;
    account::AccountData account = make_minimal_account();
    ASSERT_TRUE(account::write_account_file(root.path(), account, nullptr));

    PpcTestCharacter subject;
    PRF_FLAGS(subject.character) = PRF_BRIEF | PRF_WRAP | PRF_NOTELL;
    subject.character->profs->colors[COLOR_SAY] = CYEL;

    ppc_apply_account_to_character_in(root.path(), account.account_name.c_str(), subject.character);

    account::AccountData reloaded;
    ASSERT_TRUE(account::read_account_file_uncached(root.path(), account.account_name, &reloaded, nullptr));
    EXPECT_TRUE(reloaded.preferences.present);
    EXPECT_EQ(reloaded.preferences.preference_flags, PRF_BRIEF | PRF_WRAP);
    EXPECT_EQ(reloaded.preferences.colors[COLOR_SAY], CYEL);
    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_NOTELL))
        << "Seeding must not disturb the character.";
}

TEST(AccountPpcLogin, AppliesStoredPreferencesToTheCharacter)
{
    TemporaryDirectory root;
    account::AccountData account = make_minimal_account();
    account.preferences.present = true;
    account.preferences.preference_flags = PRF_COMPACT | PRF_MSDP;
    account.preferences.colors[COLOR_TELL] = CBCYN;
    ASSERT_TRUE(account::write_account_file(root.path(), account, nullptr));

    PpcTestCharacter subject;
    PRF_FLAGS(subject.character) = PRF_BRIEF | PRF_SING;

    ppc_apply_account_to_character_in(root.path(), account.account_name.c_str(), subject.character);

    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_COMPACT));
    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_MSDP));
    EXPECT_FALSE(PRF_FLAGGED(subject.character, PRF_BRIEF));
    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_SING)) << "Non-PPC bits survive.";
    EXPECT_EQ(subject.character->profs->colors[COLOR_TELL], CBCYN);
}

TEST(AccountPpcLogin, IsSafeWhenTheAccountCannotBeRead)
{
    TemporaryDirectory root;
    PpcTestCharacter subject;
    PRF_FLAGS(subject.character) = PRF_BRIEF | PRF_NOTELL;

    ppc_apply_account_to_character_in(root.path(), "nosuchaccount", subject.character);

    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_BRIEF))
        << "An unreadable account must never blank a player's settings.";
    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_NOTELL));
}

TEST(AccountPpcLogin, IsSafeWithNoAccountName)
{
    TemporaryDirectory root;
    PpcTestCharacter subject;
    PRF_FLAGS(subject.character) = PRF_BRIEF;
    ppc_apply_account_to_character_in(root.path(), "", subject.character);
    ppc_apply_account_to_character_in(root.path(), nullptr, subject.character);
    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_BRIEF));
}

} // namespace
