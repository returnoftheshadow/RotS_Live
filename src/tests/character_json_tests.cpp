#include "../character_json.h"
#include "../utils.h"

#include <cstdio>
#include <cstring>

#include <gtest/gtest.h>

namespace {

char_file_u make_stored_character()
{
    char_file_u stored {};
    std::snprintf(stored.name, sizeof(stored.name), "%s", "aragorn");
    std::snprintf(stored.title, sizeof(stored.title), "%s", "the Ranger");
    std::snprintf(stored.description, sizeof(stored.description), "%s", "A tall ranger stands here.");
    stored.specials2.idnum = 12345;
    stored.race = RACE_HUMAN;
    stored.sex = SEX_MALE;
    stored.bodytype = 1;
    stored.language = LANG_HUMAN;
    stored.hometown = 12;
    stored.weight = 210;
    stored.height = 76;
    stored.level = 40;
    stored.birth = 1700000000;
    stored.played = 45678;
    stored.last_logon = 1710000000;
    stored.specials2.load_room = 3001;
    stored.specials2.spells_to_learn = 3;
    stored.specials2.alignment = 250;
    stored.specials2.act = PLR_WRITING | PLR_INCOGNITO;
    stored.specials2.pref = PRF_BRIEF | PRF_COLOR | PRF_ADVANCED_VIEW;
    stored.specials2.wimp_level = 20;
    stored.specials2.freeze_level = 5;
    stored.specials2.rawPerception = 81;
    stored.specials2.perception = 87;
    stored.specials2.conditions[0] = 1;
    stored.specials2.conditions[1] = 15;
    stored.specials2.conditions[2] = 19;
    stored.specials2.mini_level = 2;
    stored.specials2.max_mini_level = 4;
    stored.specials2.morale = 33;
    stored.specials2.owner = 987;
    stored.specials2.rerolls = 2;
    stored.specials2.leg_encumb = 8;
    stored.specials2.rp_flag = 17;
    stored.specials2.retiredon = 1705000000;
    stored.specials2.hide_flags = HIDING_WELL | HIDING_SNUCK_IN;
    stored.specials2.will_teach = 123456;
    stored.specials2.tactics = TACTICS_AGGRESSIVE;
    stored.specials2.shooting = SHOOTING_FAST;
    stored.specials2.casting = CASTING_SLOW;
    stored.specials2.two_handed = 1;
    stored.profs.color_mask = 0x123456;
    stored.profs.colors[COLOR_NARR] = CYEL;
    stored.profs.colors[COLOR_CHAT] = CMAG;
    stored.profs.colors[COLOR_ROOM] = CRED;
    stored.profs.colors[COLOR_DESC] = CGRN;
    stored.profs.colors[COLOR_MAGIC] = CBMAG;
    stored.profs.colors[COLOR_WEATHER] = CBCYN;
    stored.profs.color_settings[COLOR_MAGIC].foreground.mode = COLOR_VALUE_TRUECOLOR;
    stored.profs.color_settings[COLOR_MAGIC].foreground.ansi = CBMAG;
    stored.profs.color_settings[COLOR_MAGIC].foreground.red = 180;
    stored.profs.color_settings[COLOR_MAGIC].foreground.green = 80;
    stored.profs.color_settings[COLOR_MAGIC].foreground.blue = 255;
    stored.profs.color_settings[COLOR_WEATHER].background.mode = COLOR_VALUE_TRUECOLOR;
    stored.profs.color_settings[COLOR_WEATHER].background.ansi = CBBLU;
    stored.profs.color_settings[COLOR_WEATHER].background.red = 10;
    stored.profs.color_settings[COLOR_WEATHER].background.green = 20;
    stored.profs.color_settings[COLOR_WEATHER].background.blue = 35;

    stored.tmpabilities.str = 18;
    stored.tmpabilities.lea = 11;
    stored.tmpabilities.intel = 12;
    stored.tmpabilities.wil = 13;
    stored.tmpabilities.dex = 17;
    stored.tmpabilities.con = 16;
    stored.tmpabilities.hit = 420;
    stored.tmpabilities.mana = 85;
    stored.tmpabilities.move = 110;

    stored.constabilities.str = 17;
    stored.constabilities.lea = 10;
    stored.constabilities.intel = 11;
    stored.constabilities.wil = 12;
    stored.constabilities.dex = 16;
    stored.constabilities.con = 15;
    stored.constabilities.hit = 400;
    stored.constabilities.mana = 80;
    stored.constabilities.move = 105;

    stored.points.bodypart_hit[0] = 100;
    stored.points.bodypart_hit[1] = 95;
    stored.points.bodypart_hit[2] = 90;
    stored.points.gold = 1200;
    stored.points.exp = 1234567;
    stored.points.spirit = 40;
    stored.points.mana_regen = 9;
    stored.points.health_regen = 7;
    stored.points.move_regen = 6;
    stored.points.OB = 95;
    stored.points.damage = 14;
    stored.points.ENE_regen = 12;
    stored.points.parry = 33;
    stored.points.dodge = 27;
    stored.points.encumb = 18;
    stored.points.willpower = 44;
    stored.points.spell_pen = 5;
    stored.points.spell_power = 8;

    stored.talks[0] = 100;
    stored.talks[1] = 75;
    stored.skills[0] = 1;
    stored.skills[1] = 10;
    stored.skills[2] = 95;

    stored.profs.prof_level[PROF_MAGE] = 5;
    stored.profs.prof_level[PROF_CLERIC] = 12;
    stored.profs.prof_level[PROF_RANGER] = 8;
    stored.profs.prof_level[PROF_WARRIOR] = 40;

    stored.profs.prof_coof[PROF_MAGE] = 15;
    stored.profs.prof_coof[PROF_CLERIC] = 45;
    stored.profs.prof_coof[PROF_RANGER] = 30;
    stored.profs.prof_coof[PROF_WARRIOR] = 60;

    stored.profs.prof_exp[PROF_MAGE] = 1000;
    stored.profs.prof_exp[PROF_CLERIC] = 2000;
    stored.profs.prof_exp[PROF_RANGER] = 3000;
    stored.profs.prof_exp[PROF_WARRIOR] = 4000;

    stored.affected[0].type = 77;
    stored.affected[0].duration = 5;
    stored.affected[0].time_phase = 1;
    stored.affected[0].modifier = 2;
    stored.affected[0].location = APPLY_OB;
    stored.affected[0].bitvector = AFF_INVISIBLE | AFF_DETECT_MAGIC;
    stored.affected[0].counter = 9;

    stored.affected[1].type = 88;
    stored.affected[1].duration = 10;
    stored.affected[1].time_phase = 2;
    stored.affected[1].modifier = 3;
    stored.affected[1].location = APPLY_WILL;
    stored.affected[1].bitvector = AFF_SANCTUARY;
    stored.affected[1].counter = 4;

    return stored;
}

std::string make_valid_character_json()
{
    return character_json::serialize_character_to_json(character_json::character_data_from_store(make_stored_character()));
}

std::string replace_once(std::string text, const std::string& from, const std::string& to)
{
    const size_t position = text.find(from);
    if (position == std::string::npos)
        return text;
    text.replace(position, from.size(), to);
    return text;
}

std::string first_named_object_key(const std::string& json, const std::string& label)
{
    const std::string marker = "\"" + label + "\": {";
    const size_t start = json.find(marker);
    if (start == std::string::npos)
        return "";

    const size_t key_open = json.find('"', start + marker.size());
    if (key_open == std::string::npos)
        return "";
    const size_t key_close = json.find('"', key_open + 1);
    if (key_close == std::string::npos)
        return "";

    return json.substr(key_open + 1, key_close - key_open - 1);
}

std::string remove_json_field(std::string json, const std::string& key)
{
    const std::string marker = "  \"" + key + "\": ";
    const size_t start = json.find(marker);
    if (start == std::string::npos)
        return json;

    size_t index = start + marker.size();
    int object_depth = 0;
    int array_depth = 0;
    bool in_string = false;
    bool escaped = false;

    for (; index < json.size(); ++index) {
        const char current = json[index];
        if (in_string) {
            if (escaped)
                escaped = false;
            else if (current == '\\')
                escaped = true;
            else if (current == '"')
                in_string = false;
            continue;
        }

        if (current == '"') {
            in_string = true;
            continue;
        }
        if (current == '{') {
            ++object_depth;
            continue;
        }
        if (current == '}') {
            if (object_depth > 0)
                --object_depth;
            continue;
        }
        if (current == '[') {
            ++array_depth;
            continue;
        }
        if (current == ']') {
            if (array_depth > 0)
                --array_depth;
            continue;
        }
        if (current == '\n' && object_depth == 0 && array_depth == 0) {
            ++index;
            break;
        }
    }

    json.erase(start, index - start);
    return json;
}

TEST(CharacterJson, EncodesFlagBitvectorsAsReadableNames)
{
    EXPECT_EQ(character_json::encode_player_flags(PLR_WRITING | PLR_INCOGNITO), (std::vector<std::string> { "writing", "incognito" }));
    EXPECT_EQ(character_json::encode_preference_flags(PRF_BRIEF | PRF_COLOR), (std::vector<std::string> { "brief", "color" }));
    EXPECT_EQ(character_json::encode_affected_flags(AFF_INVISIBLE | AFF_SANCTUARY), (std::vector<std::string> { "sanctuary", "invisible" }));
}

TEST(CharacterJson, RejectsUnknownFlagNamesWhenDecoding)
{
    long flags = 0;
    std::string error_message;

    EXPECT_FALSE(character_json::decode_player_flags({ "writing", "mystery_flag" }, &flags, &error_message));
    EXPECT_NE(error_message.find("Unknown player flag"), std::string::npos);
}

TEST(CharacterJson, BuildsCharacterDataFromStoredCharacterUsingMysticProfessionName)
{
    const char_file_u stored = make_stored_character();
    const character_json::CharacterData character = character_json::character_data_from_store(stored);

    EXPECT_EQ(character.character_name, "aragorn");
    EXPECT_EQ(character.mystic.level, 12);
    EXPECT_EQ(character.mystic.points, 45);
    EXPECT_EQ(character.mystic.coeff, 45);
    EXPECT_EQ(character.weight, 210);
    EXPECT_EQ(character.height, 76);
    EXPECT_EQ(character.temporary_abilities.hit, 420);
    EXPECT_EQ(character.rolled_abilities.str, 17);
    EXPECT_EQ(character.points.bodypart_hit[1], 95);
    EXPECT_EQ(character.points.experience, 1234567);
    EXPECT_EQ(character.conditions.full, 15);
    EXPECT_EQ(character.timers.last_logon, 1710000000);
    EXPECT_EQ(character.tactics, TACTICS_AGGRESSIVE);
    EXPECT_EQ(character.shooting, SHOOTING_FAST);
    EXPECT_EQ(character.casting, CASTING_SLOW);
    EXPECT_TRUE(character.two_handed);
    EXPECT_EQ(character.color_mask, 0x123456);
    ASSERT_EQ(character.colors.size(), static_cast<size_t>(MAX_COLOR_FIELDS));
    EXPECT_EQ(character.colors[COLOR_NARR], CYEL);
    EXPECT_EQ(character.colors[COLOR_CHAT], CMAG);
    EXPECT_EQ(character.colors[COLOR_ROOM], CRED);
    EXPECT_EQ(character.colors[COLOR_MAGIC], CBMAG);
    EXPECT_EQ(character.colors[COLOR_WEATHER], CBCYN);
    ASSERT_EQ(character.color_settings.size(), static_cast<size_t>(MAX_COLOR_FIELDS));
    EXPECT_EQ(character.color_settings[COLOR_MAGIC].foreground.mode, COLOR_VALUE_TRUECOLOR);
    EXPECT_EQ(character.color_settings[COLOR_MAGIC].foreground.value, CBMAG);
    EXPECT_EQ(character.color_settings[COLOR_MAGIC].foreground.red, 180);
    EXPECT_EQ(character.color_settings[COLOR_MAGIC].foreground.green, 80);
    EXPECT_EQ(character.color_settings[COLOR_MAGIC].foreground.blue, 255);
    EXPECT_EQ(character.color_settings[COLOR_WEATHER].background.mode, COLOR_VALUE_TRUECOLOR);
    EXPECT_EQ(character.color_settings[COLOR_WEATHER].background.value, CBBLU);
    EXPECT_EQ(character.color_settings[COLOR_WEATHER].background.red, 10);
    EXPECT_EQ(character.color_settings[COLOR_WEATHER].background.green, 20);
    EXPECT_EQ(character.color_settings[COLOR_WEATHER].background.blue, 35);
    EXPECT_EQ(character.hide_flags, (std::vector<std::string> { "hiding_well", "snuck_in" }));
    EXPECT_EQ(character.skills[2], 95);
    EXPECT_EQ(character.player_flags, (std::vector<std::string> { "writing", "incognito" }));
    EXPECT_EQ(character.preference_flags, (std::vector<std::string> { "brief", "color", "advanced_view" }));
    ASSERT_EQ(character.affects.size(), 2u);
    EXPECT_EQ(character.affects[0].flags, (std::vector<std::string> { "detect_magic", "invisible" }));
}

TEST(CharacterJson, SerializesAndDeserializesCharacterJsonRoundTrip)
{
    const character_json::CharacterData original = character_json::character_data_from_store(make_stored_character());
    const std::string json = character_json::serialize_character_to_json(original);

    character_json::CharacterData parsed;
    std::string error_message;
    ASSERT_TRUE(character_json::deserialize_character_from_json(json, &parsed, &error_message)) << error_message;

    EXPECT_EQ(parsed.character_name, original.character_name);
    EXPECT_EQ(parsed.mystic.level, original.mystic.level);
    EXPECT_EQ(parsed.mystic.points, original.mystic.points);
    EXPECT_EQ(parsed.mystic.coeff, original.mystic.coeff);
    EXPECT_EQ(parsed.weight, original.weight);
    EXPECT_EQ(parsed.temporary_abilities.move, original.temporary_abilities.move);
    EXPECT_EQ(parsed.points.spell_power, original.points.spell_power);
    EXPECT_EQ(parsed.conditions.thirst, original.conditions.thirst);
    EXPECT_EQ(parsed.timers.played_seconds, original.timers.played_seconds);
    EXPECT_EQ(parsed.tactics, original.tactics);
    EXPECT_EQ(parsed.shooting, original.shooting);
    EXPECT_EQ(parsed.casting, original.casting);
    EXPECT_EQ(parsed.two_handed, original.two_handed);
    EXPECT_EQ(parsed.color_mask, original.color_mask);
    EXPECT_EQ(parsed.colors, original.colors);
    ASSERT_EQ(parsed.color_settings.size(), original.color_settings.size());
    EXPECT_EQ(parsed.color_settings[COLOR_MAGIC].foreground.mode, COLOR_VALUE_TRUECOLOR);
    EXPECT_EQ(parsed.color_settings[COLOR_MAGIC].foreground.value, original.color_settings[COLOR_MAGIC].foreground.value);
    EXPECT_EQ(parsed.color_settings[COLOR_MAGIC].foreground.red, original.color_settings[COLOR_MAGIC].foreground.red);
    EXPECT_EQ(parsed.color_settings[COLOR_MAGIC].foreground.green, original.color_settings[COLOR_MAGIC].foreground.green);
    EXPECT_EQ(parsed.color_settings[COLOR_MAGIC].foreground.blue, original.color_settings[COLOR_MAGIC].foreground.blue);
    EXPECT_EQ(parsed.color_settings[COLOR_WEATHER].background.mode, COLOR_VALUE_TRUECOLOR);
    EXPECT_EQ(parsed.color_settings[COLOR_WEATHER].background.value, original.color_settings[COLOR_WEATHER].background.value);
    EXPECT_EQ(parsed.color_settings[COLOR_WEATHER].background.red, original.color_settings[COLOR_WEATHER].background.red);
    EXPECT_EQ(parsed.color_settings[COLOR_WEATHER].background.green, original.color_settings[COLOR_WEATHER].background.green);
    EXPECT_EQ(parsed.color_settings[COLOR_WEATHER].background.blue, original.color_settings[COLOR_WEATHER].background.blue);
    EXPECT_EQ(parsed.hide_flags, original.hide_flags);
    EXPECT_EQ(parsed.talks, original.talks);
    EXPECT_EQ(parsed.skills, original.skills);
    EXPECT_EQ(parsed.player_flags, original.player_flags);
    EXPECT_EQ(parsed.preference_flags, original.preference_flags);
    ASSERT_EQ(parsed.affects.size(), original.affects.size());
    EXPECT_EQ(parsed.affects[0].flags, original.affects[0].flags);
}

TEST(CharacterJson, AppliesCharacterDataBackToStoredCharacter)
{
    const character_json::CharacterData character = character_json::character_data_from_store(make_stored_character());
    char_file_u stored {};
    std::string error_message;

    ASSERT_TRUE(character_json::apply_character_data_to_store(character, &stored, &error_message)) << error_message;

    EXPECT_STREQ(stored.name, "aragorn");
    EXPECT_EQ(stored.profs.prof_level[PROF_CLERIC], 12);
    EXPECT_EQ(stored.profs.prof_coof[PROF_CLERIC], 45);
    EXPECT_EQ(stored.weight, 210);
    EXPECT_EQ(stored.height, 76);
    EXPECT_EQ(stored.tmpabilities.hit, 420);
    EXPECT_EQ(stored.constabilities.str, 17);
    EXPECT_EQ(stored.points.exp, 1234567);
    EXPECT_EQ(stored.specials2.conditions[1], 15);
    EXPECT_EQ(stored.specials2.hide_flags, HIDING_WELL | HIDING_SNUCK_IN);
    EXPECT_EQ(stored.specials2.tactics, TACTICS_AGGRESSIVE);
    EXPECT_EQ(stored.specials2.shooting, SHOOTING_FAST);
    EXPECT_EQ(stored.specials2.casting, CASTING_SLOW);
    EXPECT_EQ(stored.specials2.two_handed, 1);
    EXPECT_EQ(stored.last_logon, 1710000000);
    EXPECT_EQ(stored.skills[2], 95);
    EXPECT_TRUE((stored.specials2.act & PLR_WRITING) != 0);
    EXPECT_TRUE((stored.specials2.pref & PRF_ADVANCED_VIEW) != 0);
    EXPECT_EQ(stored.affected[0].bitvector, AFF_INVISIBLE | AFF_DETECT_MAGIC);
}

TEST(CharacterJson, DefaultsCombatStateWhenOlderJsonOmitsNewStateFields)
{
    std::string json = make_valid_character_json();
    json = replace_once(json, "\"will_teach\": 123456,\n    \"tactics\": 4,\n    \"shooting\": 3,\n    \"casting\": 1,\n    \"two_handed\": true", "\"will_teach\": 123456");

    character_json::CharacterData parsed;
    std::string error_message;
    ASSERT_TRUE(character_json::deserialize_character_from_json(json, &parsed, &error_message)) << error_message;

    EXPECT_EQ(parsed.tactics, TACTICS_NORMAL);
    EXPECT_EQ(parsed.shooting, SHOOTING_NORMAL);
    EXPECT_EQ(parsed.casting, CASTING_NORMAL);
    EXPECT_FALSE(parsed.two_handed);
}

TEST(CharacterJson, NormalizesOutOfRangeStoredCombatStateWhenBuildingCharacterData)
{
    char_file_u stored = make_stored_character();
    stored.specials2.tactics = 99;
    stored.specials2.shooting = 255;
    stored.specials2.casting = 7;

    const character_json::CharacterData character = character_json::character_data_from_store(stored);

    EXPECT_EQ(character.tactics, TACTICS_NORMAL);
    EXPECT_EQ(character.shooting, SHOOTING_NORMAL);
    EXPECT_EQ(character.casting, CASTING_NORMAL);
}

TEST(CharacterJson, RejectsOutOfRangeCombatStateValuesWhenDeserializingJson)
{
    std::string json = make_valid_character_json();
    ASSERT_NE(json.find("\"tactics\": 4"), std::string::npos);
    ASSERT_NE(json.find("\"shooting\": 3"), std::string::npos);
    ASSERT_NE(json.find("\"casting\": 1"), std::string::npos);
    json = replace_once(json, "\"tactics\": 4", "\"tactics\": 99");
    json = replace_once(json, "\"shooting\": 3", "\"shooting\": 255");
    json = replace_once(json, "\"casting\": 1", "\"casting\": 7");

    character_json::CharacterData parsed;
    std::string error_message;
    EXPECT_FALSE(character_json::deserialize_character_from_json(json, &parsed, &error_message));
    EXPECT_NE(error_message.find("state.tactics"), std::string::npos);
}

TEST(CharacterJson, ApplyCharacterDataToStorePreservesCustomColorSettings)
{
    const character_json::CharacterData character = character_json::character_data_from_store(make_stored_character());
    char_file_u stored {};
    std::string error_message;

    ASSERT_TRUE(character_json::apply_character_data_to_store(character, &stored, &error_message)) << error_message;

    EXPECT_EQ(stored.profs.color_mask, 0x123456);
    EXPECT_EQ(stored.profs.colors[COLOR_NARR], CYEL);
    EXPECT_EQ(stored.profs.colors[COLOR_CHAT], CMAG);
    EXPECT_EQ(stored.profs.colors[COLOR_ROOM], CRED);
    EXPECT_EQ(stored.profs.colors[COLOR_DESC], CGRN);
    EXPECT_EQ(stored.profs.colors[COLOR_MAGIC], CBMAG);
    EXPECT_EQ(stored.profs.colors[COLOR_WEATHER], CBCYN);
    EXPECT_EQ(stored.profs.color_settings[COLOR_MAGIC].foreground.mode, COLOR_VALUE_TRUECOLOR);
    EXPECT_EQ(stored.profs.color_settings[COLOR_MAGIC].foreground.ansi, CBMAG);
    EXPECT_EQ(stored.profs.color_settings[COLOR_MAGIC].foreground.red, 180);
    EXPECT_EQ(stored.profs.color_settings[COLOR_MAGIC].foreground.green, 80);
    EXPECT_EQ(stored.profs.color_settings[COLOR_MAGIC].foreground.blue, 255);
    EXPECT_EQ(stored.profs.color_settings[COLOR_WEATHER].background.mode, COLOR_VALUE_TRUECOLOR);
    EXPECT_EQ(stored.profs.color_settings[COLOR_WEATHER].background.ansi, CBBLU);
    EXPECT_EQ(stored.profs.color_settings[COLOR_WEATHER].background.red, 10);
    EXPECT_EQ(stored.profs.color_settings[COLOR_WEATHER].background.green, 20);
    EXPECT_EQ(stored.profs.color_settings[COLOR_WEATHER].background.blue, 35);
}

TEST(CharacterJson, RejectsProfessionPointCoeffMismatches)
{
    character_json::CharacterData character = character_json::character_data_from_store(make_stored_character());
    character.mystic.points = 45;
    character.mystic.coeff = 44;

    char_file_u stored {};
    std::string error_message;

    EXPECT_FALSE(character_json::apply_character_data_to_store(character, &stored, &error_message));
    EXPECT_NE(error_message.find("points and coeff must match"), std::string::npos);
}

TEST(CharacterJson, RejectsAffectedFlagListThatDoesNotMatchStructuredAffects)
{
    character_json::CharacterData character = character_json::character_data_from_store(make_stored_character());
    character.affected_flags = { "sanctuary" };

    char_file_u stored {};
    std::string error_message;

    EXPECT_FALSE(character_json::apply_character_data_to_store(character, &stored, &error_message));
    EXPECT_NE(error_message.find("Affected flag list must match"), std::string::npos);
}

TEST(CharacterJson, RejectsBodypartArraysThatExceedStoredCapacity)
{
    character_json::CharacterData character = character_json::character_data_from_store(make_stored_character());
    character.points.bodypart_hit.push_back(1);

    char_file_u stored {};
    std::string error_message;

    EXPECT_FALSE(character_json::apply_character_data_to_store(character, &stored, &error_message));
    EXPECT_NE(error_message.find("Bodypart hit array exceeds"), std::string::npos);
}

TEST(CharacterJson, RejectsOutOfRangeNarrowedNumericFields)
{
    character_json::CharacterData character = character_json::character_data_from_store(make_stored_character());
    character.temporary_abilities.str = 500;

    char_file_u stored {};
    std::string error_message;

    EXPECT_FALSE(character_json::apply_character_data_to_store(character, &stored, &error_message));
    EXPECT_NE(error_message.find("abilities.temporary.str"), std::string::npos);
}

TEST(CharacterJson, RejectsOutOfRangeNamedColorValuesDuringDeserialization)
{
    std::string json = make_valid_character_json();
    json = replace_once(json,
        "\"chat\": {\"foreground\": {\"mode\": \"ansi16\", \"value\": 5}, \"background\": {\"mode\": \"default\"}}",
        "\"chat\": {\"foreground\": {\"mode\": \"ansi16\", \"value\": 200}, \"background\": {\"mode\": \"default\"}}");

    character_json::CharacterData parsed;
    std::string error_message;
    EXPECT_FALSE(character_json::deserialize_character_from_json(json, &parsed, &error_message));
    EXPECT_NE(error_message.find("colors[1]"), std::string::npos);
}

TEST(CharacterJson, SerializesSkillsAndTalksAsNamedObjects)
{
    const std::string json = character_json::serialize_character_to_json(character_json::character_data_from_store(make_stored_character()));

    EXPECT_NE(json.find("\"talks\": {"), std::string::npos);
    EXPECT_NE(json.find("\"skills\": {"), std::string::npos);
    EXPECT_EQ(json.find("\"talks\": ["), std::string::npos);
    EXPECT_EQ(json.find("\"skills\": ["), std::string::npos);
}

TEST(CharacterJson, SerializesCustomColorsAsNamedObject)
{
    const std::string json = character_json::serialize_character_to_json(character_json::character_data_from_store(make_stored_character()));

    EXPECT_NE(json.find("\"color_mask\": 1193046"), std::string::npos);
    EXPECT_NE(json.find("\"colors\": {"), std::string::npos);
    EXPECT_NE(json.find("\"narrate\": {\"foreground\": {\"mode\": \"ansi16\", \"value\": 3}, \"background\": {\"mode\": \"default\"}}"), std::string::npos);
    EXPECT_NE(json.find("\"chat\": {\"foreground\": {\"mode\": \"ansi16\", \"value\": 5}, \"background\": {\"mode\": \"default\"}}"), std::string::npos);
    EXPECT_NE(json.find("\"roomname\": {\"foreground\": {\"mode\": \"ansi16\", \"value\": 1}, \"background\": {\"mode\": \"default\"}}"), std::string::npos);
    EXPECT_NE(json.find("\"magic\": {\"foreground\": {\"mode\": \"truecolor\", \"value\": 12, \"r\": 180, \"g\": 80, \"b\": 255}, \"background\": {\"mode\": \"default\"}}"), std::string::npos);
    EXPECT_NE(json.find("\"weather\": {\"foreground\": {\"mode\": \"ansi16\", \"value\": 13}, \"background\": {\"mode\": \"truecolor\", \"value\": 11, \"r\": 10, \"g\": 20, \"b\": 35}}"), std::string::npos);
}

TEST(CharacterJson, DeserializesLegacyCharacterJsonWithoutColorsAsDefaults)
{
    std::string json = make_valid_character_json();
    json = remove_json_field(json, "color_mask");
    json = remove_json_field(json, "colors");

    character_json::CharacterData parsed;
    std::string error_message;
    ASSERT_TRUE(character_json::deserialize_character_from_json(json, &parsed, &error_message)) << error_message;

    EXPECT_EQ(parsed.color_mask, 0);
    ASSERT_EQ(parsed.colors.size(), static_cast<size_t>(MAX_COLOR_FIELDS));
    for (int index = 0; index < MAX_COLOR_FIELDS; ++index)
        EXPECT_EQ(parsed.colors[index], 0) << "Expected legacy JSON without colors to default slot " << index << " to zero.";
    ASSERT_EQ(parsed.color_settings.size(), static_cast<size_t>(MAX_COLOR_FIELDS));
    for (int index = 0; index < MAX_COLOR_FIELDS; ++index) {
        EXPECT_EQ(parsed.color_settings[index].foreground.mode, COLOR_VALUE_DEFAULT);
        EXPECT_EQ(parsed.color_settings[index].background.mode, COLOR_VALUE_DEFAULT);
    }
}

TEST(CharacterJson, DeserializesLegacyIntegerColorsAsAnsiForegroundSelections)
{
    const std::string json = R"({
        "schema_version": 1,
        "character_name": "aragorn",
        "title": "the Ranger",
        "description": "A tall ranger stands here.",
        "identity": { "idnum": 1, "race": 1, "sex": 0, "bodytype": 1, "language": 1, "hometown": 1, "weight": 200, "height": 70 },
        "progression": { "level": 1, "alignment": 0, "mini_level": 0, "max_mini_level": 0, "spells_to_learn": 0, "rerolls": 0 },
        "abilities": {
            "temporary": { "str": 1, "lea": 1, "intel": 1, "wil": 1, "dex": 1, "con": 1, "hit": 1, "mana": 1, "move": 1 },
            "rolled": { "str": 1, "lea": 1, "intel": 1, "wil": 1, "dex": 1, "con": 1, "hit": 1, "mana": 1, "move": 1 }
        },
        "points": {
            "bodypart_hit": [0,0,0,0,0,0,0,0,0,0,0],
            "gold": 0, "experience": 0, "spirit": 0, "mana_regen": 0, "health_regen": 0, "move_regen": 0,
            "ob": 0, "damage": 0, "energy_regen": 0, "parry": 0, "dodge": 0, "encumbrance": 0, "willpower": 0, "spell_pen": 0, "spell_power": 0
        },
        "professions": {
            "mage": { "level": 0, "points": 0, "coeff": 0, "experience": 0 },
            "mystic": { "level": 0, "points": 0, "coeff": 0, "experience": 0 },
            "ranger": { "level": 0, "points": 0, "coeff": 0, "experience": 0 },
            "warrior": { "level": 0, "points": 0, "coeff": 0, "experience": 0 }
        },
        "flags": { "player": [], "preferences": [], "affected": [], "hide": [] },
        "conditions": { "drunk": 0, "full": 0, "thirst": 0 },
        "color_mask": 0,
        "colors": { "chat": 5, "magic": 12 },
        "timers": { "birth": 0, "last_logon": 0, "played_seconds": 0, "retired_on": 0 },
        "perception": { "raw": 0, "current": 0 },
        "state": { "load_room": 0, "wimp_level": 0, "freeze_level": 0, "morale": 0, "owner": 0, "leg_encumbrance": 0, "rp_flag": 0, "will_teach": 0 },
        "talks": {},
        "skills": {},
        "affects": []
    })";

    character_json::CharacterData parsed;
    std::string error_message;
    ASSERT_TRUE(character_json::deserialize_character_from_json(json, &parsed, &error_message)) << error_message;

    EXPECT_EQ(parsed.colors[COLOR_CHAT], CMAG);
    EXPECT_EQ(parsed.color_settings[COLOR_CHAT].foreground.mode, COLOR_VALUE_ANSI16);
    EXPECT_EQ(parsed.color_settings[COLOR_CHAT].foreground.value, CMAG);
    EXPECT_EQ(parsed.colors[COLOR_MAGIC], CBMAG);
    EXPECT_EQ(parsed.color_settings[COLOR_MAGIC].foreground.mode, COLOR_VALUE_ANSI16);
    EXPECT_EQ(parsed.color_settings[COLOR_MAGIC].foreground.value, CBMAG);
}

TEST(CharacterJson, RejectsUnknownNamedSkillOrTalkKeysDuringDeserialization)
{
    const std::string json = R"({
        "schema_version": 1,
        "character_name": "aragorn",
        "title": "the Ranger",
        "description": "A tall ranger stands here.",
        "identity": { "idnum": 1, "race": 1, "sex": 0, "bodytype": 1, "language": 1, "hometown": 1, "weight": 200, "height": 70 },
        "progression": { "level": 1, "alignment": 0, "mini_level": 0, "max_mini_level": 0, "spells_to_learn": 0, "rerolls": 0 },
        "abilities": {
            "temporary": { "str": 1, "lea": 1, "intel": 1, "wil": 1, "dex": 1, "con": 1, "hit": 1, "mana": 1, "move": 1 },
            "rolled": { "str": 1, "lea": 1, "intel": 1, "wil": 1, "dex": 1, "con": 1, "hit": 1, "mana": 1, "move": 1 }
        },
        "points": {
            "bodypart_hit": [0,0,0,0,0,0,0,0,0,0,0],
            "gold": 0, "experience": 0, "spirit": 0, "mana_regen": 0, "health_regen": 0, "move_regen": 0,
            "ob": 0, "damage": 0, "energy_regen": 0, "parry": 0, "dodge": 0, "encumbrance": 0, "willpower": 0, "spell_pen": 0, "spell_power": 0
        },
        "professions": {
            "mage": { "level": 0, "points": 0, "coeff": 0, "experience": 0 },
            "mystic": { "level": 0, "points": 0, "coeff": 0, "experience": 0 },
            "ranger": { "level": 0, "points": 0, "coeff": 0, "experience": 0 },
            "warrior": { "level": 0, "points": 0, "coeff": 0, "experience": 0 }
        },
        "flags": { "player": [], "preferences": [], "affected": [], "hide": [] },
        "conditions": { "drunk": 0, "full": 0, "thirst": 0 },
        "timers": { "birth": 0, "last_logon": 0, "played_seconds": 0, "retired_on": 0 },
        "perception": { "raw": 0, "current": 0 },
        "state": { "load_room": 0, "wimp_level": 0, "freeze_level": 0, "morale": 0, "owner": 0, "leg_encumbrance": 0, "rp_flag": 0, "will_teach": 0 },
        "talks": { "unknown_language": 10 },
        "skills": {},
        "affects": []
    })";

    character_json::CharacterData parsed;
    std::string error_message;

    EXPECT_FALSE(character_json::deserialize_character_from_json(json, &parsed, &error_message));
    EXPECT_NE(error_message.find("Unknown talk key"), std::string::npos);
}

TEST(CharacterJson, RejectsMissingRequiredTopLevelSections)
{
    std::string json = make_valid_character_json();
    const size_t timers_start = json.find("  \"timers\": {");
    ASSERT_NE(timers_start, std::string::npos);
    const size_t timers_end = json.find("  },\n", timers_start);
    ASSERT_NE(timers_end, std::string::npos);
    json.erase(timers_start, (timers_end + 5) - timers_start);

    character_json::CharacterData parsed;
    std::string error_message;
    EXPECT_FALSE(character_json::deserialize_character_from_json(json, &parsed, &error_message));
    EXPECT_NE(error_message.find("required sections"), std::string::npos);
}

TEST(CharacterJson, RejectsMissingRequiredIdentityFields)
{
    std::string json = replace_once(make_valid_character_json(), "\"height\": 76", "\"stature\": 76");
    ASSERT_NE(json.find("\"stature\": 76"), std::string::npos);

    character_json::CharacterData parsed;
    std::string error_message;
    EXPECT_FALSE(character_json::deserialize_character_from_json(json, &parsed, &error_message));
    EXPECT_NE(error_message.find("Identity object"), std::string::npos);
}

TEST(CharacterJson, RejectsMissingRequiredProgressionFields)
{
    std::string json = replace_once(make_valid_character_json(), "\"spells_to_learn\": 3", "\"spells_known\": 3");
    ASSERT_NE(json.find("\"spells_known\": 3"), std::string::npos);

    character_json::CharacterData parsed;
    std::string error_message;
    EXPECT_FALSE(character_json::deserialize_character_from_json(json, &parsed, &error_message));
    EXPECT_NE(error_message.find("Progression object"), std::string::npos);
}

TEST(CharacterJson, RejectsMissingRequiredAbilityFieldsInTemporaryAndRolledSections)
{
    std::string json = replace_once(make_valid_character_json(), "\"mana\": 85", "\"energy\": 85");
    ASSERT_NE(json.find("\"energy\": 85"), std::string::npos);

    character_json::CharacterData parsed;
    std::string error_message;
    EXPECT_FALSE(character_json::deserialize_character_from_json(json, &parsed, &error_message));
    EXPECT_NE(error_message.find("Ability object"), std::string::npos);

    json = replace_once(make_valid_character_json(), "\"move\": 105", "\"stride\": 105");
    ASSERT_NE(json.find("\"stride\": 105"), std::string::npos);

    EXPECT_FALSE(character_json::deserialize_character_from_json(json, &parsed, &error_message));
    EXPECT_NE(error_message.find("Ability object"), std::string::npos);
}

TEST(CharacterJson, RejectsLegacyClericProfessionKey)
{
    std::string json = make_valid_character_json();
    const size_t mystic_pos = json.find("\"mystic\": {");
    ASSERT_NE(mystic_pos, std::string::npos);
    json.replace(mystic_pos + 1, std::string("mystic").size(), "cleric");

    character_json::CharacterData parsed;
    std::string error_message;
    EXPECT_FALSE(character_json::deserialize_character_from_json(json, &parsed, &error_message));
    EXPECT_NE(error_message.find("mystic"), std::string::npos);
}

TEST(CharacterJson, RejectsUnknownAffectedAndHideFlagNamesWhenDeserializing)
{
    std::string json = replace_once(make_valid_character_json(), "\"affected\": [", "\"affected\": [\"ghostwalk\", ");
    ASSERT_NE(json.find("\"ghostwalk\""), std::string::npos);

    character_json::CharacterData parsed;
    std::string error_message;
    EXPECT_FALSE(character_json::deserialize_character_from_json(json, &parsed, &error_message));
    EXPECT_NE(error_message.find("Unknown affected flag"), std::string::npos);

    json = replace_once(make_valid_character_json(), "\"hide\": [", "\"hide\": [\"shadowstep\", ");
    ASSERT_NE(json.find("\"shadowstep\""), std::string::npos);

    EXPECT_FALSE(character_json::deserialize_character_from_json(json, &parsed, &error_message));
    EXPECT_NE(error_message.find("Unknown hide flag"), std::string::npos);
}

TEST(CharacterJson, RejectsDuplicateNamedSkillOrTalkKeys)
{
    std::string json = make_valid_character_json();
    const std::string talk_key = first_named_object_key(json, "talks");
    ASSERT_FALSE(talk_key.empty());
    const size_t talks_close = json.find("},\n  \"skills\":");
    ASSERT_NE(talks_close, std::string::npos);
    json.insert(talks_close, ", \"" + talk_key + "\": 11");

    character_json::CharacterData parsed;
    std::string error_message;
    EXPECT_FALSE(character_json::deserialize_character_from_json(json, &parsed, &error_message));
    EXPECT_NE(error_message.find("Duplicate talk key"), std::string::npos);
}

TEST(CharacterJson, RejectsMissingRequiredAffectFieldsDuringDeserialization)
{
    std::string json = replace_once(make_valid_character_json(), "\"flags\": [", "\"legacy_flags\": [");
    ASSERT_NE(json.find("\"legacy_flags\": ["), std::string::npos);

    character_json::CharacterData parsed;
    std::string error_message;
    EXPECT_FALSE(character_json::deserialize_character_from_json(json, &parsed, &error_message));
    EXPECT_NE(error_message.find("Affect record"), std::string::npos);
}

TEST(CharacterJson, RejectsMissingRequiredPointsFieldsDuringDeserialization)
{
    std::string json = replace_once(make_valid_character_json(), "\"spell_power\": 8", "\"spell_force\": 8");
    ASSERT_NE(json.find("\"spell_force\": 8"), std::string::npos);

    character_json::CharacterData parsed;
    std::string error_message;
    EXPECT_FALSE(character_json::deserialize_character_from_json(json, &parsed, &error_message));
    EXPECT_NE(error_message.find("Points object"), std::string::npos);
}

TEST(CharacterJson, RejectsMissingRequiredConditionFieldsDuringDeserialization)
{
    std::string json = replace_once(make_valid_character_json(), "\"thirst\": 19", "\"hunger\": 19");
    ASSERT_NE(json.find("\"hunger\": 19"), std::string::npos);

    character_json::CharacterData parsed;
    std::string error_message;
    EXPECT_FALSE(character_json::deserialize_character_from_json(json, &parsed, &error_message));
    EXPECT_NE(error_message.find("Conditions object"), std::string::npos);
}

TEST(CharacterJson, RejectsMissingRequiredTimerFieldsDuringDeserialization)
{
    std::string json = replace_once(make_valid_character_json(), "\"retired_on\": 1705000000", "\"retired_at\": 1705000000");
    ASSERT_NE(json.find("\"retired_at\": 1705000000"), std::string::npos);

    character_json::CharacterData parsed;
    std::string error_message;
    EXPECT_FALSE(character_json::deserialize_character_from_json(json, &parsed, &error_message));
    EXPECT_NE(error_message.find("Timers object"), std::string::npos);
}

TEST(CharacterJson, RejectsMissingRequiredPerceptionFieldsDuringDeserialization)
{
    std::string json = replace_once(make_valid_character_json(), "\"current\": 87", "\"effective\": 87");
    ASSERT_NE(json.find("\"effective\": 87"), std::string::npos);

    character_json::CharacterData parsed;
    std::string error_message;
    EXPECT_FALSE(character_json::deserialize_character_from_json(json, &parsed, &error_message));
    EXPECT_NE(error_message.find("Perception object"), std::string::npos);
}

TEST(CharacterJson, RejectsMissingRequiredStateFieldsDuringDeserialization)
{
    std::string json = replace_once(make_valid_character_json(), "\"will_teach\": 123456", "\"mentor\": 123456");
    ASSERT_NE(json.find("\"mentor\": 123456"), std::string::npos);

    character_json::CharacterData parsed;
    std::string error_message;
    EXPECT_FALSE(character_json::deserialize_character_from_json(json, &parsed, &error_message));
    EXPECT_NE(error_message.find("State object"), std::string::npos);
}

TEST(CharacterJson, RejectsOverlongIdentityAndTextFields)
{
    character_json::CharacterData character = character_json::character_data_from_store(make_stored_character());
    character.character_name.assign(MAX_NAME_LENGTH + 1, 'a');

    char_file_u stored {};
    std::string error_message;

    EXPECT_FALSE(character_json::apply_character_data_to_store(character, &stored, &error_message));
    EXPECT_NE(error_message.find("character_name exceeds"), std::string::npos);
}

TEST(CharacterJson, RejectsEmbeddedNulInFixedBufferStrings)
{
    character_json::CharacterData character = character_json::character_data_from_store(make_stored_character());
    character.character_name = std::string("aragorn\0shadow", 14);

    char_file_u stored {};
    std::string error_message;

    EXPECT_FALSE(character_json::apply_character_data_to_store(character, &stored, &error_message)) << error_message;
    EXPECT_NE(error_message.find("character_name"), std::string::npos);
}

TEST(CharacterJson, RejectsOversizedAffectArraysDuringDeserialization)
{
    std::string json = R"({
        "schema_version": 1,
        "character_name": "aragorn",
        "title": "the Ranger",
        "description": "A tall ranger stands here.",
        "identity": { "idnum": 1, "race": 1, "sex": 0, "bodytype": 1, "language": 1, "hometown": 1, "weight": 200, "height": 70 },
        "progression": { "level": 1, "alignment": 0, "mini_level": 0, "max_mini_level": 0, "spells_to_learn": 0, "rerolls": 0 },
        "abilities": {
            "temporary": { "str": 1, "lea": 1, "intel": 1, "wil": 1, "dex": 1, "con": 1, "hit": 1, "mana": 1, "move": 1 },
            "rolled": { "str": 1, "lea": 1, "intel": 1, "wil": 1, "dex": 1, "con": 1, "hit": 1, "mana": 1, "move": 1 }
        },
        "points": {
            "bodypart_hit": [0,0,0,0,0,0,0,0,0,0,0],
            "gold": 0, "experience": 0, "spirit": 0, "mana_regen": 0, "health_regen": 0, "move_regen": 0,
            "ob": 0, "damage": 0, "energy_regen": 0, "parry": 0, "dodge": 0, "encumbrance": 0, "willpower": 0, "spell_pen": 0, "spell_power": 0
        },
        "professions": {
            "mage": { "level": 0, "points": 0, "coeff": 0, "experience": 0 },
            "mystic": { "level": 0, "points": 0, "coeff": 0, "experience": 0 },
            "ranger": { "level": 0, "points": 0, "coeff": 0, "experience": 0 },
            "warrior": { "level": 0, "points": 0, "coeff": 0, "experience": 0 }
        },
        "flags": { "player": [], "preferences": [], "affected": [], "hide": [] },
        "conditions": { "drunk": 0, "full": 0, "thirst": 0 },
        "timers": { "birth": 0, "last_logon": 0, "played_seconds": 0, "retired_on": 0 },
        "perception": { "raw": 0, "current": 0 },
        "state": { "load_room": 0, "wimp_level": 0, "freeze_level": 0, "morale": 0, "owner": 0, "leg_encumbrance": 0, "rp_flag": 0, "will_teach": 0 },
        "talks": {},
        "skills": {},
        "affects": [)";

    for (int index = 0; index <= MAX_AFFECT; ++index) {
        if (index > 0)
            json += ",";
        json += R"({"type":1,"duration":1,"time_phase":0,"modifier":0,"location":0,"counter":0,"flags":[]})";
    }
    json += "] }";

    character_json::CharacterData parsed;
    std::string error_message;

    EXPECT_FALSE(character_json::deserialize_character_from_json(json, &parsed, &error_message));
    EXPECT_NE(error_message.find("affects exceeds the supported entry count"), std::string::npos);
}

} // namespace
