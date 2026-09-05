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

// Declared exactly like account_ppc.cpp/act_othe.cpp/interpre.cpp -- descriptor_list has no
// header declaration anywhere in the codebase. Must live at file scope (not inside the
// anonymous namespace below) or it names a distinct per-translation-unit symbol instead of
// the real global in comm.cpp.
extern struct descriptor_data* descriptor_list;

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

// ppc_store_character_to_account_in refuses to write the account until the character has read
// it (char_data::ppc_account_loaded, set only by ppc_apply_account_to_character_in). Tests that
// are about the *store* logic rather than about the invariant itself stand in for that read
// with this helper, so they keep asserting what they were written to assert.
void mark_account_ppc_read(char_data* character)
{
    character->ppc_account_loaded = true;
}

TEST(AccountPpcInvariant, AFreshCharacterHasNotReadItsAccountYet)
{
    PpcTestCharacter subject;
    EXPECT_FALSE(subject.character->ppc_account_loaded)
        << "clear_char must leave the flag false, or the guard is open on every new character.";
}

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

// A brand-new (level 0) character reaches the apply/seed call before do_start runs, so it is
// still at default colours/flags. On an account that already has other linked characters, a
// player might create a new character before returning to an established one -- seeding from
// that new character's untouched defaults would clobber the account's real scheme the next time
// it saves. Must not seed, and therefore must not write the account, in that situation.
TEST(AccountPpcLogin, DoesNotSeedFromABrandNewCharacterWhenTheAccountHasOtherLinkedCharacters)
{
    TemporaryDirectory root;
    account::AccountData account = make_minimal_account();
    account.characters = { "established", "brandnew" };
    ASSERT_TRUE(account::write_account_file(root.path(), account, nullptr));

    PpcTestCharacter subject;
    ASSERT_EQ(GET_LEVEL(subject.character), 0);
    PRF_FLAGS(subject.character) = PRF_BRIEF;
    subject.character->profs->colors[COLOR_SAY] = CYEL;

    ppc_apply_account_to_character_in(root.path(), account.account_name.c_str(), subject.character);

    account::AccountData reloaded;
    ASSERT_TRUE(account::read_account_file_uncached(root.path(), account.account_name, &reloaded, nullptr));
    EXPECT_FALSE(reloaded.preferences.present)
        << "A level-0 character must not seed an account that already has other linked characters.";
    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_BRIEF))
        << "Skipping the seed must not disturb the character.";
}

// The very first character linked to a brand-new account is the legitimate case: it still
// seeds, including whatever colour/latin-1 answers the player just gave during creation.
TEST(AccountPpcLogin, SeedsFromABrandNewCharacterWhenItIsTheOnlyLinkedCharacter)
{
    TemporaryDirectory root;
    account::AccountData account = make_minimal_account();
    account.characters = { "brandnew" };
    ASSERT_TRUE(account::write_account_file(root.path(), account, nullptr));

    PpcTestCharacter subject;
    ASSERT_EQ(GET_LEVEL(subject.character), 0);
    PRF_FLAGS(subject.character) = PRF_BRIEF | PRF_LATIN1;
    subject.character->profs->colors[COLOR_SAY] = CYEL;

    ppc_apply_account_to_character_in(root.path(), account.account_name.c_str(), subject.character);

    account::AccountData reloaded;
    ASSERT_TRUE(account::read_account_file_uncached(root.path(), account.account_name, &reloaded, nullptr));
    EXPECT_TRUE(reloaded.preferences.present)
        << "The first character on a brand-new account must still seed.";
    EXPECT_EQ(reloaded.preferences.preference_flags, PRF_BRIEF | PRF_LATIN1);
    EXPECT_EQ(reloaded.preferences.colors[COLOR_SAY], CYEL);
}

TEST(AccountPpcSave, DoesNotWriteWhenNothingChanged)
{
    TemporaryDirectory root;
    account::AccountData account = make_minimal_account();
    account.preferences.present = true;
    account.preferences.preference_flags = PRF_BRIEF | PRF_WRAP;
    ASSERT_TRUE(account::write_account_file(root.path(), account, nullptr));

    const std::string account_path = account::account_file_path(root.path(), account.normalized_email);
    std::string bytes_before;
    ASSERT_TRUE(account::read_text_file(account_path, &bytes_before, nullptr));

    PpcTestCharacter subject;
    PRF_FLAGS(subject.character) = PRF_BRIEF | PRF_WRAP | PRF_NOTELL;
    mark_account_ppc_read(subject.character);

    EXPECT_FALSE(ppc_store_character_to_account_in(root.path(), account.account_name, subject.character))
        << "An unchanged PPC must not write the account: every write flushes the account cache.";

    std::string bytes_after;
    ASSERT_TRUE(account::read_text_file(account_path, &bytes_after, nullptr));
    EXPECT_EQ(bytes_before, bytes_after)
        << "The account file's bytes must be untouched -- a false return with a broken read "
           "would otherwise pass this test just as easily as a genuine no-op.";
}

TEST(AccountPpcSave, DoesNotWriteWhenTheAccountHasNoPreferencesYet)
{
    TemporaryDirectory root;
    account::AccountData account = make_minimal_account();
    ASSERT_TRUE(account::write_account_file(root.path(), account, nullptr));
    ASSERT_FALSE(account.preferences.present);

    PpcTestCharacter subject;
    // Above level 0 so this exercises the plain absent-preferences path, not the level-0
    // seeding guard (which AccountPpcSave.DoesNotSeedFromABrandNewCharacter... covers).
    GET_LEVEL(subject.character) = 10;
    PRF_FLAGS(subject.character) = PRF_BRIEF;
    mark_account_ppc_read(subject.character);

    EXPECT_TRUE(ppc_store_character_to_account_in(root.path(), account.account_name, subject.character))
        << "A character's first save must store the account's not-yet-present PPC.";

    account::AccountData reloaded;
    ASSERT_TRUE(account::read_account_file_uncached(root.path(), account.account_name, &reloaded, nullptr));
    EXPECT_TRUE(reloaded.preferences.present);
    EXPECT_EQ(reloaded.preferences.preference_flags, PRF_BRIEF);
}

// Mirrors AccountPpcLogin.DoesNotSeedFromABrandNewCharacterWhenTheAccountHasOtherLinkedCharacters:
// without this guard, a level-0 character that login refused to seed from writes its default
// scheme back at its very first autosave a minute later, defeating the login-time guard.
TEST(AccountPpcSave, DoesNotSeedFromABrandNewCharacterWhenTheAccountHasOtherLinkedCharacters)
{
    TemporaryDirectory root;
    account::AccountData account = make_minimal_account();
    account.characters = { "established", "brandnew" };
    ASSERT_TRUE(account::write_account_file(root.path(), account, nullptr));

    PpcTestCharacter subject;
    ASSERT_EQ(GET_LEVEL(subject.character), 0);
    PRF_FLAGS(subject.character) = PRF_BRIEF;
    subject.character->profs->colors[COLOR_SAY] = CYEL;
    mark_account_ppc_read(subject.character);

    EXPECT_FALSE(ppc_store_character_to_account_in(root.path(), account.account_name, subject.character))
        << "A brand-new character's untouched defaults must not seed an account that already "
           "has other linked characters.";

    account::AccountData reloaded;
    ASSERT_TRUE(account::read_account_file_uncached(root.path(), account.account_name, &reloaded, nullptr));
    EXPECT_FALSE(reloaded.preferences.present);
}

// The very first character linked to a brand-new account is the legitimate case, matching
// AccountPpcLogin.SeedsFromABrandNewCharacterWhenItIsTheOnlyLinkedCharacter.
TEST(AccountPpcSave, SeedsFromABrandNewCharacterWhenItIsTheOnlyLinkedCharacter)
{
    TemporaryDirectory root;
    account::AccountData account = make_minimal_account();
    account.characters = { "brandnew" };
    ASSERT_TRUE(account::write_account_file(root.path(), account, nullptr));

    PpcTestCharacter subject;
    ASSERT_EQ(GET_LEVEL(subject.character), 0);
    PRF_FLAGS(subject.character) = PRF_BRIEF | PRF_LATIN1;
    subject.character->profs->colors[COLOR_SAY] = CYEL;
    mark_account_ppc_read(subject.character);

    EXPECT_TRUE(ppc_store_character_to_account_in(root.path(), account.account_name, subject.character))
        << "The first character on a brand-new account must still be able to store its PPC.";

    account::AccountData reloaded;
    ASSERT_TRUE(account::read_account_file_uncached(root.path(), account.account_name, &reloaded, nullptr));
    EXPECT_TRUE(reloaded.preferences.present);
    EXPECT_EQ(reloaded.preferences.preference_flags, PRF_BRIEF | PRF_LATIN1);
    EXPECT_EQ(reloaded.preferences.colors[COLOR_SAY], CYEL);
}

// Regression test for the stale-RGB bug in sync_color_slot_foreground_from_ansi: switching a
// slot from truecolor back to ANSI16 used to leave the previous red/green/blue bytes behind,
// so the in-memory struct never matched its own serialized-and-reloaded form and ppc_equal
// would report a difference on every single save.
TEST(AccountPpcColorSlots, AnsiOverwriteAfterTruecolorRoundTripsCleanly)
{
    PpcTestCharacter subject;
    set_truecolor_foreground(subject.character, COLOR_SAY, 255, 0, 0);
    set_colornum(subject.character, COLOR_SAY, CRED);

    const account::AccountPreferences original = ppc_read_from_character(subject.character);
    ASSERT_EQ(original.color_settings[COLOR_SAY].foreground.mode, COLOR_VALUE_ANSI16);

    account::AccountData account = make_minimal_account();
    account.preferences = original;
    const std::string json = account::serialize_account_to_json(account);
    account::AccountData reloaded;
    std::string error_message;
    ASSERT_TRUE(account::deserialize_account_from_json(json, &reloaded, &error_message)) << error_message;

    EXPECT_TRUE(ppc_equal(original, reloaded.preferences))
        << "A slot switched back to ANSI16 must not carry stale RGB bytes that make it "
           "compare unequal to its own serialized-and-reloaded form.";
}

TEST(AccountPpcSave, WritesWhenAFlagChanged)
{
    TemporaryDirectory root;
    account::AccountData account = make_minimal_account();
    account.preferences.present = true;
    account.preferences.preference_flags = PRF_BRIEF;
    ASSERT_TRUE(account::write_account_file(root.path(), account, nullptr));

    PpcTestCharacter subject;
    PRF_FLAGS(subject.character) = PRF_BRIEF | PRF_COMPACT;
    mark_account_ppc_read(subject.character);

    EXPECT_TRUE(ppc_store_character_to_account_in(root.path(), account.account_name, subject.character));

    account::AccountData reloaded;
    ASSERT_TRUE(account::read_account_file_uncached(root.path(), account.account_name, &reloaded, nullptr));
    EXPECT_EQ(reloaded.preferences.preference_flags, PRF_BRIEF | PRF_COMPACT);
}

TEST(AccountPpcSave, WritesWhenAColourChanged)
{
    TemporaryDirectory root;
    account::AccountData account = make_minimal_account();
    account.preferences.present = true;
    account.preferences.preference_flags = PRF_BRIEF;
    ASSERT_TRUE(account::write_account_file(root.path(), account, nullptr));

    PpcTestCharacter subject;
    PRF_FLAGS(subject.character) = PRF_BRIEF;
    subject.character->profs->colors[COLOR_YELL] = CBMAG;
    subject.character->profs->color_settings[COLOR_YELL].foreground.mode = COLOR_VALUE_ANSI16;
    subject.character->profs->color_settings[COLOR_YELL].foreground.ansi = CBMAG;
    mark_account_ppc_read(subject.character);

    EXPECT_TRUE(ppc_store_character_to_account_in(root.path(), account.account_name, subject.character));

    account::AccountData reloaded;
    ASSERT_TRUE(account::read_account_file_uncached(root.path(), account.account_name, &reloaded, nullptr));
    EXPECT_EQ(reloaded.preferences.colors[COLOR_YELL], CBMAG);
}

TEST(AccountPpcSave, PreservesUnrelatedAccountFields)
{
    TemporaryDirectory root;
    account::AccountData account = make_minimal_account();
    account.roster_sort = "level";
    account.email_verified = true;
    account.characters.push_back("someone");
    account.preferences.present = true;
    account.preferences.preference_flags = PRF_BRIEF;
    ASSERT_TRUE(account::write_account_file(root.path(), account, nullptr));

    PpcTestCharacter subject;
    PRF_FLAGS(subject.character) = PRF_BRIEF | PRF_COMPACT;
    mark_account_ppc_read(subject.character);
    ASSERT_TRUE(ppc_store_character_to_account_in(root.path(), account.account_name, subject.character));

    account::AccountData reloaded;
    ASSERT_TRUE(account::read_account_file_uncached(root.path(), account.account_name, &reloaded, nullptr));
    EXPECT_EQ(reloaded.roster_sort, "level");
    EXPECT_TRUE(reloaded.email_verified);
    ASSERT_EQ(reloaded.characters.size(), 1u);
    EXPECT_EQ(reloaded.characters[0], "someone");
}

// THE regression test for the live bug found on a booted server: logging into any character
// silently reverted the account's shared PPC to that character's last-saved values, because the
// enter-game path called save_char (which stores the PPC) before it applied the account's PPC.
// This is that shape reduced to its essentials -- a store with no apply anywhere before it --
// and it fails against the pre-fix code, which happily overwrites the account here.
TEST(AccountPpcInvariant, StoringBeforeAnyApplyLeavesTheAccountUntouched)
{
    TemporaryDirectory root;
    account::AccountData account = make_minimal_account();
    account.characters = { "established", "justloggedin" };
    account.preferences.present = true;
    account.preferences.preference_flags = PRF_BRIEF | PRF_COMPACT;
    account.preferences.colors[COLOR_NARR] = CRED;
    ASSERT_TRUE(account::write_account_file(root.path(), account, nullptr));

    const std::string account_path = account::account_file_path(root.path(), account.normalized_email);
    std::string bytes_before;
    ASSERT_TRUE(account::read_text_file(account_path, &bytes_before, nullptr));

    // A character freshly loaded from its own player file: level 10 so no seeding guard is in
    // play, and a PPC that genuinely differs from the account's, exactly like the live repro
    // where one character had brief/compact/red narrate and the other did not.
    PpcTestCharacter subject;
    GET_LEVEL(subject.character) = 10;
    PRF_FLAGS(subject.character) = PRF_SPAM | PRF_NOTELL;
    subject.character->profs->colors[COLOR_NARR] = CYEL;
    ASSERT_FALSE(subject.character->ppc_account_loaded);

    EXPECT_FALSE(ppc_store_character_to_account_in(root.path(), account.account_name, subject.character))
        << "A character that has not read the account's PPC must never write it.";

    std::string bytes_after;
    ASSERT_TRUE(account::read_text_file(account_path, &bytes_after, nullptr));
    EXPECT_EQ(bytes_before, bytes_after)
        << "The account file must be byte-identical: a stale character overwriting it here is "
           "exactly how one character's login used to discard another character's settings.";

    account::AccountData reloaded;
    ASSERT_TRUE(account::read_account_file_uncached(root.path(), account.account_name, &reloaded, nullptr));
    EXPECT_EQ(reloaded.preferences.preference_flags, PRF_BRIEF | PRF_COMPACT);
    EXPECT_EQ(reloaded.preferences.colors[COLOR_NARR], CRED);
}

// The other half of the same guarantee: once the character has read the account, an actual
// change made during play still reaches the account. A guard that blocked everything would pass
// the test above and break the feature outright.
TEST(AccountPpcInvariant, ApplyThenChangeThenStoreUpdatesTheAccount)
{
    TemporaryDirectory root;
    account::AccountData account = make_minimal_account();
    account.characters = { "established", "justloggedin" };
    account.preferences.present = true;
    account.preferences.preference_flags = PRF_BRIEF;
    account.preferences.colors[COLOR_NARR] = CRED;
    ASSERT_TRUE(account::write_account_file(root.path(), account, nullptr));

    PpcTestCharacter subject;
    GET_LEVEL(subject.character) = 10;
    PRF_FLAGS(subject.character) = PRF_SPAM | PRF_NOTELL;
    subject.character->profs->colors[COLOR_NARR] = CYEL;

    ppc_apply_account_to_character_in(root.path(), account.account_name.c_str(), subject.character);
    EXPECT_TRUE(subject.character->ppc_account_loaded)
        << "A successful apply is what opens the guard.";
    ASSERT_TRUE(PRF_FLAGGED(subject.character, PRF_BRIEF));
    ASSERT_EQ(subject.character->profs->colors[COLOR_NARR], CRED);

    // The player now changes something in game, as do_gen_tog/do_color would. set_colornum is
    // what do_color uses: it updates the colour slot's setting as well as the legacy colors[]
    // byte, and it is the setting that the account file is encoded from.
    SET_BIT(PRF_FLAGS(subject.character), PRF_COMPACT);
    set_colornum(subject.character, COLOR_NARR, CBLU);

    EXPECT_TRUE(ppc_store_character_to_account_in(root.path(), account.account_name, subject.character))
        << "A real change made after the apply must still reach the account.";

    account::AccountData reloaded;
    ASSERT_TRUE(account::read_account_file_uncached(root.path(), account.account_name, &reloaded, nullptr));
    EXPECT_EQ(reloaded.preferences.preference_flags, PRF_BRIEF | PRF_COMPACT);
    EXPECT_EQ(reloaded.preferences.colors[COLOR_NARR], CBLU);
    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_NOTELL))
        << "Non-PPC bits stay the character's own throughout.";
}

// The live sequence, in the wrong order on purpose: this is what interpre.cpp's enter-game
// branch did (save_char, then apply). The ordering there is fixed, but the point of the guard is
// that even this order is harmless -- the account keeps the sibling's settings and the character
// still ends up wearing them.
TEST(AccountPpcInvariant, SavingBeforeApplyingCannotDiscardASiblingsSettings)
{
    TemporaryDirectory root;
    account::AccountData account = make_minimal_account();
    account.characters = { "debugbot", "bashtest" };
    ASSERT_TRUE(account::write_account_file(root.path(), account, nullptr));

    // Character A plays and stores brief + compact + red narrate on the account.
    PpcTestCharacter first;
    GET_LEVEL(first.character) = 10;
    ppc_apply_account_to_character_in(root.path(), account.account_name.c_str(), first.character);
    PRF_FLAGS(first.character) = PRF_BRIEF | PRF_COMPACT;
    first.character->profs->colors[COLOR_NARR] = CRED;
    ASSERT_TRUE(ppc_store_character_to_account_in(root.path(), account.account_name, first.character));

    // Character B now logs in with its own older settings still in the struct, and its login
    // path saves before it applies.
    PpcTestCharacter second;
    GET_LEVEL(second.character) = 10;
    PRF_FLAGS(second.character) = PRF_SPAM;
    second.character->profs->colors[COLOR_NARR] = CYEL;

    EXPECT_FALSE(ppc_store_character_to_account_in(root.path(), account.account_name, second.character))
        << "The save that runs before the apply must be refused, not merely harmless.";
    ppc_apply_account_to_character_in(root.path(), account.account_name.c_str(), second.character);

    EXPECT_TRUE(PRF_FLAGGED(second.character, PRF_BRIEF));
    EXPECT_TRUE(PRF_FLAGGED(second.character, PRF_COMPACT));
    EXPECT_FALSE(PRF_FLAGGED(second.character, PRF_SPAM));
    EXPECT_EQ(second.character->profs->colors[COLOR_NARR], CRED)
        << "Settings must follow the player onto the character that just logged in.";

    account::AccountData reloaded;
    ASSERT_TRUE(account::read_account_file_uncached(root.path(), account.account_name, &reloaded, nullptr));
    EXPECT_EQ(reloaded.preferences.preference_flags, PRF_BRIEF | PRF_COMPACT)
        << "The account must still hold the sibling's settings, not the logging-in "
           "character's stale copy.";
    EXPECT_EQ(reloaded.preferences.colors[COLOR_NARR], CRED);
}

// Seeding is the migration path, and it leaves the account holding exactly this character's
// PPC -- the same reconciled state a successful apply leaves -- so it opens the guard too.
// Without this, the first character on a brand-new account could never save a later change.
TEST(AccountPpcInvariant, SeedingAnEmptyAccountAlsoCountsAsHavingRead)
{
    TemporaryDirectory root;
    account::AccountData account = make_minimal_account();
    account.characters = { "onlyone" };
    ASSERT_TRUE(account::write_account_file(root.path(), account, nullptr));

    PpcTestCharacter subject;
    PRF_FLAGS(subject.character) = PRF_BRIEF | PRF_LATIN1;

    ppc_apply_account_to_character_in(root.path(), account.account_name.c_str(), subject.character);
    EXPECT_TRUE(subject.character->ppc_account_loaded);

    SET_BIT(PRF_FLAGS(subject.character), PRF_COMPACT);
    EXPECT_TRUE(ppc_store_character_to_account_in(root.path(), account.account_name, subject.character));

    account::AccountData reloaded;
    ASSERT_TRUE(account::read_account_file_uncached(root.path(), account.account_name, &reloaded, nullptr));
    EXPECT_EQ(reloaded.preferences.preference_flags, PRF_BRIEF | PRF_LATIN1 | PRF_COMPACT);
}

// An apply that could not read the account learned nothing, so it must not open the guard:
// otherwise a transient read failure at login would licence the next save to overwrite the
// account with whatever the character happened to be carrying.
TEST(AccountPpcInvariant, AFailedApplyDoesNotOpenTheGuard)
{
    TemporaryDirectory root;
    PpcTestCharacter subject;
    GET_LEVEL(subject.character) = 10;
    PRF_FLAGS(subject.character) = PRF_BRIEF;

    ppc_apply_account_to_character_in(root.path(), "nosuchaccount", subject.character);
    EXPECT_FALSE(subject.character->ppc_account_loaded);

    ppc_apply_account_to_character_in(root.path(), "", subject.character);
    ppc_apply_account_to_character_in(root.path(), nullptr, subject.character);
    EXPECT_FALSE(subject.character->ppc_account_loaded);
}

// The level-0 refusal in ppc_apply_account_to_character_in deliberately leaves the account
// alone, so the character has not reconciled with it and must not be able to write it later
// either -- the store path's own level-0 guard stops caring the moment the character levels.
TEST(AccountPpcInvariant, RefusingToSeedFromABrandNewCharacterDoesNotOpenTheGuard)
{
    TemporaryDirectory root;
    account::AccountData account = make_minimal_account();
    account.characters = { "established", "brandnew" };
    ASSERT_TRUE(account::write_account_file(root.path(), account, nullptr));

    PpcTestCharacter subject;
    ASSERT_EQ(GET_LEVEL(subject.character), 0);
    PRF_FLAGS(subject.character) = PRF_BRIEF;

    ppc_apply_account_to_character_in(root.path(), account.account_name.c_str(), subject.character);
    EXPECT_FALSE(subject.character->ppc_account_loaded);

    GET_LEVEL(subject.character) = 1;
    EXPECT_FALSE(ppc_store_character_to_account_in(root.path(), account.account_name, subject.character));

    account::AccountData reloaded;
    ASSERT_TRUE(account::read_account_file_uncached(root.path(), account.account_name, &reloaded, nullptr));
    EXPECT_FALSE(reloaded.preferences.present);
}

TEST(AccountPpcPropagate, CopiesPpcBitsAndColoursBetweenCharacters)
{
    PpcTestCharacter source;
    PpcTestCharacter destination;
    PRF_FLAGS(source.character) = PRF_BRIEF | PRF_COMPACT | PRF_NOTELL;
    source.character->profs->colors[COLOR_HIT] = CBRED;
    PRF_FLAGS(destination.character) = PRF_SPAM | PRF_SING;

    ppc_copy_between_characters(source.character, destination.character);

    EXPECT_TRUE(PRF_FLAGGED(destination.character, PRF_BRIEF));
    EXPECT_TRUE(PRF_FLAGGED(destination.character, PRF_COMPACT));
    EXPECT_FALSE(PRF_FLAGGED(destination.character, PRF_SPAM))
        << "A PPC bit clear on the source must be cleared on the destination.";
    EXPECT_TRUE(PRF_FLAGGED(destination.character, PRF_SING))
        << "Non-PPC bits belong to the character and must survive.";
    EXPECT_FALSE(PRF_FLAGGED(destination.character, PRF_NOTELL))
        << "A non-PPC bit must not be carried across from the source.";
    EXPECT_EQ(destination.character->profs->colors[COLOR_HIT], CBRED);
}

TEST(AccountPpcPropagate, IsSafeForNullAndSelf)
{
    PpcTestCharacter subject;
    PRF_FLAGS(subject.character) = PRF_BRIEF;
    ppc_copy_between_characters(nullptr, subject.character);
    ppc_copy_between_characters(subject.character, nullptr);
    ppc_copy_between_characters(subject.character, subject.character);
    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_BRIEF));
}

TEST(AccountPpcPropagate, ReachesPlyngAndLinklsSiblingsButNotAnotherAccount)
{
    // descriptor_list is a real global (comm.cpp); hang a small fake chain off it for the
    // duration of this test and restore whatever was there before we leave.
    struct descriptor_data* const original_descriptor_list = descriptor_list;

    PpcTestCharacter source;
    PpcTestCharacter online_sibling;
    PpcTestCharacter linkless_sibling;
    PpcTestCharacter other_account_character;

    descriptor_data source_desc {};
    descriptor_data online_desc {};
    descriptor_data linkless_desc {};
    descriptor_data other_desc {};

    std::strcpy(source_desc.account_name, "PropagateTester");
    std::strcpy(online_desc.account_name, "PropagateTester");
    std::strcpy(linkless_desc.account_name, "PropagateTester");
    std::strcpy(other_desc.account_name, "SomeoneElse");

    source_desc.character = source.character;
    online_desc.character = online_sibling.character;
    linkless_desc.character = linkless_sibling.character;
    other_desc.character = other_account_character.character;

    source.character->desc = &source_desc;
    online_sibling.character->desc = &online_desc;
    linkless_sibling.character->desc = &linkless_desc;
    other_account_character.character->desc = &other_desc;

    source_desc.connected = CON_PLYNG;
    online_desc.connected = CON_PLYNG;
    linkless_desc.connected = CON_LINKLS;
    other_desc.connected = CON_PLYNG;

    source_desc.next = &online_desc;
    online_desc.next = &linkless_desc;
    linkless_desc.next = &other_desc;
    other_desc.next = nullptr;

    descriptor_list = &source_desc;

    PRF_FLAGS(source.character) = PRF_BRIEF;
    PRF_FLAGS(online_sibling.character) = PRF_SPAM;
    PRF_FLAGS(linkless_sibling.character) = PRF_SPAM;
    PRF_FLAGS(other_account_character.character) = PRF_SPAM;

    ppc_propagate_from(source.character);

    descriptor_list = original_descriptor_list;

    EXPECT_TRUE(PRF_FLAGGED(online_sibling.character, PRF_BRIEF))
        << "A CON_PLYNG sibling on the same account must receive the change.";
    EXPECT_FALSE(PRF_FLAGGED(online_sibling.character, PRF_SPAM));

    EXPECT_TRUE(PRF_FLAGGED(linkless_sibling.character, PRF_BRIEF))
        << "A CON_LINKLS sibling must receive the change too -- it stays resident with desc "
           "attached and can otherwise be swept into the shutdown/reboot save loop with a "
           "stale copy that clobbers this change.";
    EXPECT_FALSE(PRF_FLAGGED(linkless_sibling.character, PRF_SPAM));

    EXPECT_FALSE(PRF_FLAGGED(other_account_character.character, PRF_BRIEF))
        << "A descriptor on a different account must not receive the change.";
    EXPECT_TRUE(PRF_FLAGGED(other_account_character.character, PRF_SPAM));
}

TEST(AccountPpcQuery, ReportsWhetherAnAccountHasPreferences)
{
    TemporaryDirectory root;
    account::AccountData account = make_minimal_account();
    ASSERT_TRUE(account::write_account_file(root.path(), account, nullptr));
    EXPECT_FALSE(ppc_account_has_preferences_in(root.path(), account.account_name.c_str()));

    account.preferences.present = true;
    account.preferences.preference_flags = PRF_BRIEF;
    ASSERT_TRUE(account::write_account_file(root.path(), account, nullptr));
    EXPECT_TRUE(ppc_account_has_preferences_in(root.path(), account.account_name.c_str()));

    EXPECT_FALSE(ppc_account_has_preferences_in(root.path(), "nosuchaccount"));
    EXPECT_FALSE(ppc_account_has_preferences_in(root.path(), ""));
    EXPECT_FALSE(ppc_account_has_preferences_in(root.path(), nullptr));
}

} // namespace
