#include "../account_management.h"
#include "../account_management_types.h"
#include "../character_json.h"
#include "../color.h"
#include "../json_utils.h"
#include "../structs.h"

#include <gtest/gtest.h>

namespace {

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

} // namespace
