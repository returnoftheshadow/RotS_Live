#include "../js_game_adapter.h"

#include "../db.h"
#include "../structs.h"
#include "../utils.h"
#include "../zone.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>

extern char *sector_types[];
extern char num_of_sector_types;

namespace {

char_data make_character(const char *name, int race, int level, int hit, int max_hit, bool npc)
{
    char_data character {};
    character.abs_number = 77;
    character.nr = npc ? 1 : -1;
    character.in_room = 0;
    character.player.name = const_cast<char *>(name);
    character.player.short_descr = const_cast<char *>(name);
    character.player.race = race;
    character.player.level = level;
    character.player.ranking = level + 3;
    character.points.exp = level * 1000;
    character.tmpabilities.hit = hit;
    character.tmpabilities.str = 18;
    character.tmpabilities.intel = 13;
    character.tmpabilities.wil = 15;
    character.tmpabilities.dex = 17;
    character.tmpabilities.con = 16;
    character.tmpabilities.lea = 9;
    character.abilities.hit = max_hit;
    character.classpoints = 4;
    character.interrupt_count = 2;
    character.interrupt_time = 9;
    character.spec_busy = true;
    character.specials2.idnum = npc ? -1 : 1234;
    if (npc)
        character.specials2.act |= MOB_ISNPC;
    return character;
}

obj_data make_object(const char *name, int item_number)
{
    obj_data object {};
    object.item_number = item_number;
    object.in_room = 0;
    object.name = const_cast<char *>(name);
    object.short_description = const_cast<char *>(name);
    object.description = const_cast<char *>("A detailed object description.");
    object.action_description = const_cast<char *>("A detailed action description.");
    object.obj_flags.type_flag = ITEM_WEAPON;
    object.obj_flags.wear_flags = ITEM_TAKE | ITEM_WIELD;
    object.obj_flags.extra_flags = ITEM_GLOW | ITEM_MAGIC;
    object.obj_flags.level = 12;
    object.obj_flags.weight = 700;
    object.obj_flags.cost = 450;
    object.obj_flags.cost_per_day = 15;
    object.obj_flags.timer = 30;
    object.obj_flags.rarity = 2;
    object.obj_flags.material = 4;
    object.owner = 88;
    object.touched = 1;
    return object;
}

room_data make_room(const char *name, int number, int zone)
{
    room_data room {};
    room.name = const_cast<char *>(name);
    room.description = const_cast<char *>("A detailed room description.");
    room.number = number;
    room.zone = zone;
    room.level = 4;
    room.sector_type = SECT_CITY;
    room.room_flags = DARK | INDOORS;
    room.alignment = -3;
    room.light = 2;
    return room;
}

zone_data make_zone(const char *name, int number)
{
    zone_data zone {};
    zone.name = const_cast<char *>(name);
    zone.description = const_cast<char *>("A detailed zone description.");
    zone.map = const_cast<char *>("N-G-S");
    zone.number = number;
    zone.level = 5;
    zone.lifespan = 45;
    zone.age = 7;
    zone.top = number + 99;
    zone.x = 11;
    zone.y = -4;
    zone.symbol = 'Z';
    zone.white_power = 21;
    zone.dark_power = 13;
    zone.magi_power = 8;
    zone.min_level_look = 3;
    zone.reset_mode = 2;
    return zone;
}

JsGameTriggerFixture make_trigger()
{
    JsGameTriggerFixture trigger;
    trigger.name = "onEnter";
    trigger.legacy_name = "ON_ENTER";
    trigger.host_type = "character";
    trigger.legacy_value = 11;
    return trigger;
}

class ScopedSunlight {
  public:
    explicit ScopedSunlight(int sunlight)
        : previous_sunlight_(weather_info.sunlight)
    {
        weather_info.sunlight = sunlight;
    }

    ~ScopedSunlight() { weather_info.sunlight = previous_sunlight_; }

  private:
    int previous_sunlight_;
};

JsGameAdapterOptions make_options(const char_data *const *characters, std::size_t character_count,
    const obj_data *const *objects, std::size_t object_count, room_data *world, int top_of_world,
    index_data *mob_index, std::size_t mob_index_count, index_data *obj_index,
    std::size_t obj_index_count, zone_data *zones, std::size_t zone_count,
    const char *const *race_names, std::size_t race_name_count)
{
    JsGameAdapterOptions options;
    options.live_characters = characters;
    options.live_character_count = character_count;
    options.live_objects = objects;
    options.live_object_count = object_count;
    options.world = world;
    options.world_count = top_of_world >= 0 ? static_cast<std::size_t>(top_of_world + 1) : 0;
    options.top_of_world = top_of_world;
    options.mobile_index = mob_index;
    options.mobile_index_count = mob_index_count;
    options.object_index = obj_index;
    options.object_index_count = obj_index_count;
    options.zones = zones;
    options.zone_count = zone_count;
    options.race_names = race_names;
    options.race_name_count = race_name_count;
    return options;
}

} // namespace

TEST(JsGameAdapter, SnapshotsApprovedCharacterFields)
{
    const char *races[] = { "God", "Human", "Dwarf" };
    index_data mobile_index[2] {};
    mobile_index[1].virt = 5100;
    char_data npc = make_character("Gate Guard", 2, 15, 41, 55, true);
    const char_data *live_characters[] = { &npc };
    room_data world[1] = { make_room("Northern Gate", 1204, 0) };
    zone_data zones[1] = { make_zone("Old City", 12) };
    JsGameAdapterOptions options = make_options(live_characters, 1, nullptr, 0, world, 0,
        mobile_index, 2, nullptr, 0, zones, 1, races, 3);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&npc, options, &fixture));

    EXPECT_EQ(fixture.id, "mob:5100");
    EXPECT_EQ(fixture.name, "Gate Guard");
    EXPECT_EQ(fixture.race, "Dwarf");
    EXPECT_EQ(fixture.vnum, 5100);
    EXPECT_EQ(fixture.prototype_vnum, 5100);
    EXPECT_EQ(fixture.level, 15);
    EXPECT_EQ(fixture.experience, 15000);
    EXPECT_EQ(fixture.rank, 18);
    EXPECT_EQ(fixture.hit_points, 41);
    EXPECT_EQ(fixture.max_hit_points, 55);
    EXPECT_EQ(fixture.class_points, 4);
    EXPECT_EQ(fixture.interrupt_count, 2);
    EXPECT_EQ(fixture.interrupt_time, 9);
    EXPECT_TRUE(fixture.special_busy);
    EXPECT_EQ(fixture.current_abilities.strength, 18);
    EXPECT_EQ(fixture.current_abilities.intelligence, 13);
    EXPECT_EQ(fixture.current_abilities.willpower, 15);
    EXPECT_EQ(fixture.current_abilities.dexterity, 17);
    EXPECT_EQ(fixture.current_abilities.constitution, 16);
    EXPECT_EQ(fixture.current_abilities.leadership, 9);
    EXPECT_TRUE(fixture.is_npc);
    ASSERT_TRUE(fixture.has_room);
    EXPECT_EQ(fixture.room.vnum, 1204);
    ASSERT_TRUE(fixture.room.has_zone);
    EXPECT_EQ(fixture.room.zone.vnum, 12);
}

TEST(JsGameAdapter, SnapshotsPlayerWithoutPrototypeVnum)
{
    const char *races[] = { "God", "Human" };
    char_data player = make_character("PlayerOne", 1, 29, 90, 120, false);
    const char_data *live_characters[] = { &player };
    JsGameAdapterOptions options = make_options(live_characters, 1, nullptr, 0, nullptr, -1,
        nullptr, 0, nullptr, 0, nullptr, 0, races, 2);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&player, options, &fixture));

    EXPECT_EQ(fixture.id, "player");
    EXPECT_EQ(fixture.name, "PlayerOne");
    EXPECT_EQ(fixture.race, "Human");
    EXPECT_EQ(fixture.vnum, -1);
    EXPECT_EQ(fixture.prototype_vnum, -1);
    EXPECT_EQ(fixture.experience, 29000);
    EXPECT_EQ(fixture.rank, 32);
    EXPECT_FALSE(fixture.is_npc);
}

TEST(JsGameAdapter, RejectsNullAndStaleCharacters)
{
    char_data live = make_character("Live", 1, 10, 10, 10, false);
    char_data stale = make_character("Stale", 1, 10, 10, 10, false);
    const char_data *live_characters[] = { &live };
    JsGameAdapterOptions options = make_options(live_characters, 1, nullptr, 0, nullptr, -1,
        nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture fixture;
    EXPECT_FALSE(js_game_adapter_character_fixture(nullptr, options, &fixture));
    EXPECT_FALSE(js_game_adapter_character_fixture(&stale, options, &fixture));

    JsGameAdapterOptions default_options;
    EXPECT_FALSE(js_game_adapter_character_fixture(&live, default_options, &fixture));

    JsGameAdapterOptions missing_table_options;
    missing_table_options.live_character_count = 1;
    EXPECT_FALSE(js_game_adapter_character_fixture(&live, missing_table_options, &fixture));
}

TEST(JsGameAdapter, SnapshotsObjectRoomAndZoneFields)
{
    index_data object_index[1] {};
    object_index[0].virt = 300;
    obj_data object = make_object("silver lever", 0);
    const obj_data *live_objects[] = { &object };
    room_data world[1] = { make_room("Northern Gate", 1204, 0) };
    zone_data zones[1] = { make_zone("Old City", 12) };
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 1, world, 0, nullptr, 0,
        object_index, 1, zones, 1, nullptr, 0);

    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &object_fixture));
    EXPECT_EQ(object_fixture.id, "object:300");
    EXPECT_EQ(object_fixture.name, "silver lever");
    EXPECT_EQ(object_fixture.description, "A detailed object description.");
    EXPECT_EQ(object_fixture.short_description, "silver lever");
    EXPECT_TRUE(object_fixture.has_action_description);
    EXPECT_EQ(object_fixture.action_description, "A detailed action description.");
    EXPECT_EQ(object_fixture.vnum, 300);
    EXPECT_EQ(object_fixture.flags.item_type, "weapon");
    EXPECT_EQ(object_fixture.flags.wear_flags, (std::vector<std::string> { "take", "wield" }));
    EXPECT_EQ(object_fixture.flags.extra_flags, (std::vector<std::string> { "glow", "magic" }));
    EXPECT_EQ(object_fixture.flags.level, 12);
    EXPECT_EQ(object_fixture.flags.weight, 700);
    EXPECT_EQ(object_fixture.flags.cost, 450);
    EXPECT_EQ(object_fixture.flags.cost_per_day, 15);
    EXPECT_EQ(object_fixture.flags.timer, 30);
    EXPECT_EQ(object_fixture.flags.rarity, 2);
    EXPECT_EQ(object_fixture.flags.material, "metal");
    ASSERT_TRUE(object_fixture.has_room);
    EXPECT_EQ(object_fixture.room.vnum, 1204);
    ASSERT_TRUE(object_fixture.room.has_zone);
    EXPECT_EQ(object_fixture.room.zone.vnum, 12);

    JsGameRoomFixture room_fixture;
    ASSERT_TRUE(js_game_adapter_room_fixture(0, options, &room_fixture));
    EXPECT_EQ(room_fixture.id, "room:1204");
    EXPECT_EQ(room_fixture.name, "Northern Gate");
    EXPECT_EQ(room_fixture.description, "A detailed room description.");
    EXPECT_EQ(room_fixture.vnum, 1204);
    EXPECT_EQ(room_fixture.level, 4);
    EXPECT_EQ(room_fixture.sector_type, "City");
    EXPECT_EQ(room_fixture.flags, (std::vector<std::string> { "dark", "indoors" }));
    EXPECT_EQ(room_fixture.alignment, -3);
    EXPECT_EQ(room_fixture.light, 2);
    EXPECT_FALSE(room_fixture.is_sunlit);

    JsGameZoneFixture zone_fixture;
    ASSERT_TRUE(js_game_adapter_zone_fixture(0, options, &zone_fixture));
    EXPECT_EQ(zone_fixture.id, "zone:12");
    EXPECT_EQ(zone_fixture.name, "Old City");
    EXPECT_TRUE(zone_fixture.has_description);
    EXPECT_EQ(zone_fixture.description, "A detailed zone description.");
    EXPECT_TRUE(zone_fixture.has_map);
    EXPECT_EQ(zone_fixture.map, "N-G-S");
    EXPECT_EQ(zone_fixture.vnum, 12);
    EXPECT_EQ(zone_fixture.level, 5);
    EXPECT_EQ(zone_fixture.lifespan, 45);
    EXPECT_EQ(zone_fixture.age, 7);
    EXPECT_EQ(zone_fixture.top_room_vnum, 111);
    EXPECT_EQ(zone_fixture.x, 11);
    EXPECT_EQ(zone_fixture.y, -4);
    EXPECT_EQ(zone_fixture.symbol, "Z");
    EXPECT_EQ(zone_fixture.white_power, 21);
    EXPECT_EQ(zone_fixture.dark_power, 13);
    EXPECT_EQ(zone_fixture.magi_power, 8);
    EXPECT_EQ(zone_fixture.minimum_look_level, 3);
    EXPECT_EQ(zone_fixture.reset_mode, 2);
}

TEST(JsGameAdapter, FiltersUnknownObjectFlagDomains)
{
    index_data object_index[1] {};
    object_index[0].virt = 301;
    obj_data object = make_object("strange shard", 0);
    object.obj_flags.type_flag = -1;
    object.obj_flags.material = 999;
    object.obj_flags.wear_flags = ITEM_TAKE | (1L << 29);
    object.obj_flags.extra_flags = ITEM_GLOW | (1L << 13) | (1L << 29);
    const obj_data *live_objects[] = { &object };
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 1, nullptr, -1,
        nullptr, 0, object_index, 1, nullptr, 0, nullptr, 0);

    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &object_fixture));
    EXPECT_EQ(object_fixture.flags.item_type, "Unknown");
    EXPECT_EQ(object_fixture.flags.material, "Unknown");
    EXPECT_EQ(object_fixture.flags.wear_flags, (std::vector<std::string> { "take" }));
    EXPECT_EQ(object_fixture.flags.extra_flags, (std::vector<std::string> { "glow" }));

    object.obj_flags.type_flag = 999;
    object.obj_flags.material = -1;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &object_fixture));
    EXPECT_EQ(object_fixture.flags.item_type, "Unknown");
    EXPECT_EQ(object_fixture.flags.material, "Unknown");
}

TEST(JsGameAdapter, ModelsUnknownRoomSectorTypes)
{
    room_data world[1] = { make_room("Strange Room", 1205, 0) };
    JsGameAdapterOptions options = make_options(nullptr, 0, nullptr, 0, world, 0, nullptr, 0,
        nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameRoomFixture room_fixture;

    world[0].sector_type = -1;
    ASSERT_TRUE(js_game_adapter_room_fixture(0, options, &room_fixture));
    EXPECT_EQ(room_fixture.sector_type, "Unknown");

    world[0].sector_type = num_of_sector_types;
    ASSERT_TRUE(js_game_adapter_room_fixture(0, options, &room_fixture));
    EXPECT_EQ(room_fixture.sector_type, "Unknown");
}

TEST(JsGameAdapter, SnapshotsEveryKnownRoomSectorTypeName)
{
    room_data world[1] = { make_room("Sector Room", 1206, 0) };
    JsGameAdapterOptions options = make_options(nullptr, 0, nullptr, 0, world, 0, nullptr, 0,
        nullptr, 0, nullptr, 0, nullptr, 0);

    ASSERT_GT(num_of_sector_types, 0);
    for (int sector = 0; sector < num_of_sector_types; ++sector) {
        ASSERT_NE(sector_types[sector], nullptr) << sector;
        ASSERT_STRNE(sector_types[sector], "\n") << "sentinel must not be in live sector range";

        world[0].sector_type = sector;
        JsGameRoomFixture room_fixture;
        ASSERT_TRUE(js_game_adapter_room_fixture(0, options, &room_fixture)) << sector;
        EXPECT_EQ(room_fixture.sector_type, sector_types[sector]) << sector;
    }
}

TEST(JsGameAdapter, PreservesNullableTextGetterNulls)
{
    index_data object_index[1] {};
    object_index[0].virt = 301;
    obj_data object = make_object("ancient key", 0);
    object.action_description = nullptr;
    const obj_data *live_objects[] = { &object };

    zone_data zone = make_zone("Old City", 12);
    zone.description = nullptr;
    zone.map = nullptr;
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 1, nullptr, -1,
        nullptr, 0, object_index, 1, &zone, 1, nullptr, 0);

    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &object_fixture));
    EXPECT_FALSE(object_fixture.has_action_description);
    EXPECT_EQ(object_fixture.action_description, "");

    JsGameZoneFixture zone_fixture;
    ASSERT_TRUE(js_game_adapter_zone_fixture(0, options, &zone_fixture));
    EXPECT_FALSE(zone_fixture.has_description);
    EXPECT_EQ(zone_fixture.description, "");
    EXPECT_FALSE(zone_fixture.has_map);
    EXPECT_EQ(zone_fixture.map, "");
}

TEST(JsGameAdapter, PreservesPresentEmptyNullableTextGetters)
{
    index_data object_index[1] {};
    object_index[0].virt = 302;
    obj_data object = make_object("blank sign", 0);
    object.action_description = const_cast<char *>("");
    const obj_data *live_objects[] = { &object };

    zone_data zone = make_zone("Old City", 12);
    zone.description = const_cast<char *>("");
    zone.map = const_cast<char *>("");
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 1, nullptr, -1,
        nullptr, 0, object_index, 1, &zone, 1, nullptr, 0);

    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &object_fixture));
    EXPECT_TRUE(object_fixture.has_action_description);
    EXPECT_EQ(object_fixture.action_description, "");

    JsGameZoneFixture zone_fixture;
    ASSERT_TRUE(js_game_adapter_zone_fixture(0, options, &zone_fixture));
    EXPECT_TRUE(zone_fixture.has_description);
    EXPECT_EQ(zone_fixture.description, "");
    EXPECT_TRUE(zone_fixture.has_map);
    EXPECT_EQ(zone_fixture.map, "");
}

TEST(JsGameAdapter, ObjectShortDescriptionFallsBackToObjectName)
{
    index_data object_index[1] {};
    object_index[0].virt = 301;
    obj_data object = make_object("ancient key", 0);
    object.short_description = nullptr;
    const obj_data *live_objects[] = { &object };
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 1, nullptr, -1,
        nullptr, 0, object_index, 1, nullptr, 0, nullptr, 0);

    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &object_fixture));
    EXPECT_EQ(object_fixture.name, "ancient key");
    EXPECT_EQ(object_fixture.short_description, "ancient key");
}

TEST(JsGameAdapter, SnapshotsRoomSunlitStateFromCurrentWeatherAndRoomFlags)
{
    struct Case {
        const char *name;
        int sunlight;
        int sector_type;
        long room_flags;
        byte light;
        bool expected_is_sunlit;
    };
    const Case cases[] = {
        { "daylight lit room", SUN_LIGHT, SECT_FIELD, 0, 1, true },
        { "sunrise lit room", SUN_RISE, SECT_FIELD, 0, 1, true },
        { "sunset lit room", SUN_SET, SECT_FIELD, 0, 1, false },
        { "dark outdoor room", SUN_DARK, SECT_FIELD, 0, 0, false },
        { "dark flagged unlit room", SUN_LIGHT, SECT_FIELD, DARK, 0, false },
        { "dark flagged room with light source", SUN_LIGHT, SECT_FIELD, DARK, 1, true },
        { "inside night room", SUN_DARK, SECT_INSIDE, 0, 0, false },
        { "city night room", SUN_DARK, SECT_CITY, 0, 0, false },
    };

    for (const Case &test_case : cases) {
        ScopedSunlight sunlight(test_case.sunlight);
        room_data world[1] = { make_room(test_case.name, 101, 0) };
        world[0].sector_type = test_case.sector_type;
        world[0].room_flags = test_case.room_flags;
        world[0].light = test_case.light;
        JsGameAdapterOptions options = make_options(nullptr, 0, nullptr, 0, world, 0, nullptr, 0,
            nullptr, 0, nullptr, 0, nullptr, 0);

        JsGameRoomFixture room;
        ASSERT_TRUE(js_game_adapter_room_fixture(0, options, &room)) << test_case.name;
        EXPECT_EQ(room.is_sunlit, test_case.expected_is_sunlit) << test_case.name;
    }
}

TEST(JsGameAdapter, FiltersRoomFlagsForBuilderSnapshots)
{
    room_data world[1] = { make_room("Northern Gate", 1204, 0) };
    world[0].room_flags = BFS_MARK | (1L << 30);
    JsGameAdapterOptions options = make_options(nullptr, 0, nullptr, 0, world, 0, nullptr, 0,
        nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameRoomFixture room;
    ASSERT_TRUE(js_game_adapter_room_fixture(0, options, &room));
    EXPECT_TRUE(room.flags.empty());

    struct ExpectedRoomFlag {
        long bit;
        const char *name;
    };
    const ExpectedRoomFlag expected_flags[] = {
        {DARK, "dark"},
        {DEATH, "death"},
        {NO_MOB, "noMob"},
        {INDOORS, "indoors"},
        {NORIDE, "noRide"},
        {PERMAFFECT, "permanentAffect"},
        {SHADOWY, "shadowy"},
        {NO_MAGIC, "noMagic"},
        {TUNNEL, "tunnel"},
        {PRIVATE, "private"},
        {GODROOM, "godRoom"},
        {DRINK_WATER, "drinkWater"},
        {DRINK_POISON, "drinkPoison"},
        {SECURITYROOM, "securityRoom"},
        {PEACEROOM, "peaceRoom"},
        {NO_TELEPORT, "noTeleport"},
        {HIDE_VNUM, "hideVnum"},
    };

    long all_safe_flags = 0;
    std::vector<std::string> all_safe_names;
    for (const ExpectedRoomFlag &expected : expected_flags) {
        world[0].room_flags = expected.bit;
        ASSERT_TRUE(js_game_adapter_room_fixture(0, options, &room)) << expected.name;
        EXPECT_EQ(room.flags, (std::vector<std::string> { expected.name })) << expected.name;
        all_safe_flags |= expected.bit;
        all_safe_names.emplace_back(expected.name);
    }

    world[0].room_flags = all_safe_flags | BFS_MARK | (1L << 30);
    ASSERT_TRUE(js_game_adapter_room_fixture(0, options, &room));
    EXPECT_EQ(room.flags, all_safe_names);
    EXPECT_EQ(std::find(room.flags.begin(), room.flags.end(), "BFS_MARK"), room.flags.end());
}

TEST(JsGameAdapter, RejectsStaleObjectsAndInvalidRoomBounds)
{
    obj_data live = make_object("live object", 0);
    obj_data stale = make_object("stale object", 0);
    const obj_data *live_objects[] = { &live };
    room_data world[1] = { make_room("Only Room", 1, 0) };
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 1, world, 0, nullptr, 0,
        nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameObjectFixture object_fixture;
    EXPECT_FALSE(js_game_adapter_object_fixture(&stale, options, &object_fixture));

    JsGameAdapterOptions default_options;
    EXPECT_FALSE(js_game_adapter_object_fixture(&live, default_options, &object_fixture));

    JsGameAdapterOptions missing_table_options;
    missing_table_options.live_object_count = 1;
    EXPECT_FALSE(js_game_adapter_object_fixture(&live, missing_table_options, &object_fixture));

    JsGameRoomFixture room_fixture;
    EXPECT_FALSE(js_game_adapter_room_fixture(-1, options, &room_fixture));
    EXPECT_FALSE(js_game_adapter_room_fixture(1, options, &room_fixture));
}

TEST(JsGameAdapter, ModelsObjectRoomAsMissingWhenObjectIsNotDirectlyInRoom)
{
    index_data object_index[1] {};
    object_index[0].virt = 300;
    obj_data object = make_object("carried lever", 0);
    object.in_room = -1;
    const obj_data *live_objects[] = { &object };
    room_data world[1] = { make_room("Only Room", 1, 0) };
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 1, world, 0, nullptr, 0,
        object_index, 1, nullptr, 0, nullptr, 0);

    JsGameObjectFixture object_fixture;
    object_fixture.has_room = true;
    object_fixture.room.id = "sentinel-room";
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &object_fixture));
    EXPECT_FALSE(object_fixture.has_room);
    EXPECT_EQ(object_fixture.room.id, "sentinel-room");
}

TEST(JsGameAdapter, SnapshotsObjectCarriedByWhenCarrierIsLive)
{
    char_data carrier = make_character("Carrier", 1, 20, 30, 40, false);
    obj_data object = make_object("carried lever", 0);
    object.in_room = -1;
    object.carried_by = &carrier;
    carrier.carrying = &object;
    const char_data *live_characters[] = { &carrier };
    const obj_data *live_objects[] = { &object };
    index_data object_index[1] {};
    object_index[0].virt = 300;
    room_data world[1] = { make_room("Carrier Room", 100, 0) };
    zone_data zones[1] = { make_zone("Carrier Zone", 10) };
    JsGameAdapterOptions options = make_options(live_characters, 1, live_objects, 1, world, 0,
        nullptr, 0, object_index, 1, zones, 1, nullptr, 0);

    JsGameObjectFixture fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &fixture));

    EXPECT_FALSE(fixture.has_room);
    ASSERT_TRUE(fixture.has_carried_by);
    EXPECT_FALSE(fixture.has_worn_by);
    EXPECT_EQ(fixture.carried_by.name, "Carrier");
    EXPECT_EQ(fixture.carried_by.level, 20);
    ASSERT_TRUE(fixture.carried_by.has_room);
    EXPECT_EQ(fixture.carried_by.room.vnum, 100);
    ASSERT_TRUE(fixture.carried_by.room.has_zone);
    EXPECT_EQ(fixture.carried_by.room.zone.vnum, 10);
}

TEST(JsGameAdapter, SnapshotsObjectWornByWhenCarrierHasObjectEquipped)
{
    char_data wearer = make_character("Wearer", 1, 21, 31, 41, false);
    obj_data object = make_object("worn amulet", 0);
    object.in_room = -1;
    object.carried_by = &wearer;
    wearer.equipment[WEAR_NECK_1] = &object;
    const char_data *live_characters[] = { &wearer };
    const obj_data *live_objects[] = { &object };
    index_data object_index[1] {};
    object_index[0].virt = 301;
    room_data world[1] = { make_room("Wearer Room", 101, 0) };
    JsGameAdapterOptions options = make_options(live_characters, 1, live_objects, 1, world, 0,
        nullptr, 0, object_index, 1, nullptr, 0, nullptr, 0);

    JsGameObjectFixture fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &fixture));

    EXPECT_FALSE(fixture.has_room);
    EXPECT_FALSE(fixture.has_carried_by);
    ASSERT_TRUE(fixture.has_worn_by);
    EXPECT_EQ(fixture.worn_by.name, "Wearer");
    EXPECT_EQ(fixture.worn_by.level, 21);
    ASSERT_TRUE(fixture.worn_by.has_room);
    EXPECT_EQ(fixture.worn_by.room.vnum, 101);
}

TEST(JsGameAdapter, DoesNotTrustUnlinkedObjectCarrierBackPointer)
{
    char_data carrier = make_character("Carrier", 1, 20, 30, 40, false);
    obj_data object = make_object("unlinked lever", 0);
    object.in_room = -1;
    object.carried_by = &carrier;
    const char_data *live_characters[] = { &carrier };
    const obj_data *live_objects[] = { &object };
    index_data object_index[1] {};
    object_index[0].virt = 300;
    room_data world[1] = { make_room("Only Room", 100, 0) };
    JsGameAdapterOptions options = make_options(live_characters, 1, live_objects, 1, world, 0,
        nullptr, 0, object_index, 1, nullptr, 0, nullptr, 0);

    JsGameObjectFixture fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &fixture));

    EXPECT_FALSE(fixture.has_room);
    EXPECT_FALSE(fixture.has_carried_by);
    EXPECT_FALSE(fixture.has_worn_by);
}

TEST(JsGameAdapter, DoesNotExposeOwnerForRoomOrNestedObjects)
{
    char_data carrier = make_character("Carrier", 1, 20, 30, 40, false);
    obj_data room_object = make_object("room lever", 0);
    room_object.in_room = 0;
    room_object.carried_by = &carrier;
    obj_data container = make_object("container", 1);
    obj_data nested_object = make_object("nested lever", 0);
    nested_object.in_room = -1;
    nested_object.in_obj = &container;
    nested_object.carried_by = &carrier;
    const char_data *live_characters[] = { &carrier };
    const obj_data *live_objects[] = { &room_object, &nested_object };
    index_data object_index[2] {};
    object_index[0].virt = 300;
    object_index[1].virt = 301;
    room_data world[1] = { make_room("Only Room", 100, 0) };
    JsGameAdapterOptions options = make_options(live_characters, 1, live_objects, 2, world, 0,
        nullptr, 0, object_index, 2, nullptr, 0, nullptr, 0);

    JsGameObjectFixture room_fixture;
    room_fixture.has_carried_by = true;
    room_fixture.has_worn_by = true;
    ASSERT_TRUE(js_game_adapter_object_fixture(&room_object, options, &room_fixture));
    ASSERT_TRUE(room_fixture.has_room);
    EXPECT_FALSE(room_fixture.has_carried_by);
    EXPECT_FALSE(room_fixture.has_worn_by);

    JsGameObjectFixture nested_fixture;
    nested_fixture.has_carried_by = true;
    nested_fixture.has_worn_by = true;
    ASSERT_TRUE(js_game_adapter_object_fixture(&nested_object, options, &nested_fixture));
    EXPECT_FALSE(nested_fixture.has_room);
    EXPECT_FALSE(nested_fixture.has_carried_by);
    EXPECT_FALSE(nested_fixture.has_worn_by);
}

TEST(JsGameAdapter, RejectionPathsDoNotModifyExistingFixtures)
{
    char_data stale_character = make_character("Stale", 1, 1, 1, 1, false);
    obj_data stale_object = make_object("stale", 0);
    room_data world[1] = { make_room("Only Room", 1, 0) };
    JsGameAdapterOptions options = make_options(nullptr, 0, nullptr, 0, world, 0, nullptr, 0,
        nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture character_fixture;
    character_fixture.id = "sentinel-character";
    EXPECT_FALSE(js_game_adapter_character_fixture(&stale_character, options, &character_fixture));
    EXPECT_EQ(character_fixture.id, "sentinel-character");

    JsGameObjectFixture object_fixture;
    object_fixture.id = "sentinel-object";
    EXPECT_FALSE(js_game_adapter_object_fixture(&stale_object, options, &object_fixture));
    EXPECT_EQ(object_fixture.id, "sentinel-object");

    JsGameRoomFixture room_fixture;
    room_fixture.id = "sentinel-room";
    EXPECT_FALSE(js_game_adapter_room_fixture(2, options, &room_fixture));
    EXPECT_EQ(room_fixture.id, "sentinel-room");

    JsGameZoneFixture zone_fixture;
    zone_fixture.id = "sentinel-zone";
    EXPECT_FALSE(js_game_adapter_zone_fixture(0, options, &zone_fixture));
    EXPECT_EQ(zone_fixture.id, "sentinel-zone");
}

TEST(JsGameAdapter, BuildsContextFromOnlyLiveValidInputs)
{
    const char *races[] = { "God", "Human" };
    char_data self = make_character("Self", 1, 11, 22, 33, false);
    char_data stale_actor = make_character("StaleActor", 1, 44, 55, 66, false);
    obj_data object = make_object("key", -1);
    obj_data stale_weapon = make_object("stale weapon", 0);
    const char_data *live_characters[] = { &self };
    const obj_data *live_objects[] = { &object };
    room_data world[1] = { make_room("Room", 100, 0) };
    zone_data zones[1] = { make_zone("Zone", 10) };
    JsGameAdapterOptions options = make_options(live_characters, 1, live_objects, 1, world, 0,
        nullptr, 0, nullptr, 0, zones, 1, races, 2);

    JsGameAdapterContextInput input;
    input.self = &self;
    input.actor = &stale_actor;
    input.speaker = &stale_actor;
    input.attacker = &stale_actor;
    input.victim = &stale_actor;
    input.killer = &stale_actor;
    input.object = &object;
    input.weapon = &stale_weapon;
    input.room = 0;
    input.text = "hello";
    input.wear_slot = MAX_WEAR;
    input.command = "say";
    input.args = "open sesame";
    input.has_tick = true;
    input.tick = 0;
    input.direction = "south";
    input.reverse_direction = "north";
    input.trigger = make_trigger();

    JsGameTriggerContextFixture context = js_game_adapter_context_fixture(input, options);

    EXPECT_TRUE(context.has_self);
    EXPECT_FALSE(context.has_actor);
    EXPECT_FALSE(context.has_speaker);
    EXPECT_FALSE(context.has_attacker);
    EXPECT_FALSE(context.has_victim);
    EXPECT_FALSE(context.has_killer);
    EXPECT_TRUE(context.has_object);
    EXPECT_FALSE(context.has_weapon);
    EXPECT_EQ(context.object.vnum, -1);
    EXPECT_TRUE(context.has_room);
    EXPECT_TRUE(context.has_zone);
    EXPECT_TRUE(context.has_text);
    EXPECT_EQ(context.text, "hello");
    EXPECT_FALSE(context.has_wear_slot);
    EXPECT_TRUE(context.has_command);
    EXPECT_EQ(context.command, "say");
    EXPECT_TRUE(context.has_args);
    EXPECT_EQ(context.args, "open sesame");
    EXPECT_TRUE(context.has_tick);
    EXPECT_EQ(context.tick, 0);
    EXPECT_TRUE(context.has_direction);
    EXPECT_EQ(context.direction, "south");
    EXPECT_TRUE(context.has_reverse_direction);
    EXPECT_EQ(context.reverse_direction, "north");
    EXPECT_EQ(context.trigger.legacy_name, "ON_ENTER");
}

TEST(JsGameAdapter, ContextUsesInvocationLocalRoleIds)
{
    char_data self = make_character("Self", 1, 11, 22, 33, false);
    self.specials2.idnum = 98765;
    char_data actor = make_character("Actor", 1, 11, 22, 33, false);
    actor.specials2.idnum = 11111;
    obj_data object = make_object("object", 0);
    object.owner = 98765;
    object.touched = 55;
    obj_data weapon = make_object("weapon", 0);
    index_data object_index[1] {};
    object_index[0].virt = 400;
    const char_data *live_characters[] = { &self, &actor };
    const obj_data *live_objects[] = { &object, &weapon };
    JsGameAdapterOptions options = make_options(live_characters, 2, live_objects, 2, nullptr, -1,
        nullptr, 0, object_index, 1, nullptr, 0, nullptr, 0);

    JsGameAdapterContextInput input;
    input.self = &self;
    input.actor = &actor;
    input.speaker = &actor;
    input.attacker = &actor;
    input.victim = &self;
    input.killer = &actor;
    input.object = &object;
    input.weapon = &weapon;
    input.wear_slot = WIELD;

    JsGameTriggerContextFixture context = js_game_adapter_context_fixture(input, options);

    EXPECT_EQ(context.self.id, "self");
    EXPECT_EQ(context.actor.id, "actor");
    EXPECT_EQ(context.speaker.id, "speaker");
    EXPECT_EQ(context.attacker.id, "attacker");
    EXPECT_EQ(context.victim.id, "victim");
    ASSERT_TRUE(context.has_killer);
    EXPECT_EQ(context.killer.id, "killer");
    EXPECT_EQ(context.object.id, "object");
    EXPECT_EQ(context.weapon.id, "weapon");
    ASSERT_TRUE(context.has_wear_slot);
    EXPECT_EQ(context.wear_slot, "wield");
    EXPECT_EQ(context.self.id.find("98765"), std::string::npos);
    EXPECT_EQ(context.actor.id.find("11111"), std::string::npos);
    EXPECT_EQ(context.speaker.id.find("11111"), std::string::npos);
    EXPECT_EQ(context.attacker.id.find("11111"), std::string::npos);
    EXPECT_EQ(context.victim.id.find("98765"), std::string::npos);
    EXPECT_EQ(context.killer.id.find("11111"), std::string::npos);
    EXPECT_EQ(context.object.id.find("55"), std::string::npos);
}

TEST(JsGameAdapter, MapsTypedTargetsFromLiveInputs)
{
    char_data self = make_character("Self", 1, 11, 22, 33, false);
    char_data target_character = make_character("Target", 1, 44, 55, 66, false);
    obj_data target_object = make_object("target object", 0);
    index_data object_index[1] {};
    object_index[0].virt = 700;
    const char_data *live_characters[] = { &self, &target_character };
    const obj_data *live_objects[] = { &target_object };
    room_data world[2] = { make_room("Room", 100, 0), make_room("Other", 101, 0) };
    zone_data zones[1] = { make_zone("Zone", 10) };
    JsGameAdapterOptions options = make_options(live_characters, 2, live_objects, 1, world, 1,
        nullptr, 0, object_index, 1, zones, 1, nullptr, 0);

    target_data targ1;
    targ1.type = TARGET_CHAR;
    targ1.ptr.ch = &target_character;
    targ1.ch_num = target_character.abs_number;
    target_data targ2;
    targ2.type = TARGET_OBJ;
    targ2.ptr.obj = &target_object;

    JsGameAdapterContextInput input;
    input.self = &self;
    input.targ1 = &targ1;
    input.targ2 = &targ2;
    input.target_room = 1;
    input.dying = &target_character;

    JsGameTriggerContextFixture context = js_game_adapter_context_fixture(input, options);

    ASSERT_TRUE(context.has_targ1);
    EXPECT_TRUE(context.targ1.has_character);
    EXPECT_EQ(context.targ1.character.name, "Target");
    EXPECT_EQ(context.targ1.character.id, "targ1");
    ASSERT_TRUE(context.has_targ2);
    EXPECT_TRUE(context.targ2.has_object);
    EXPECT_EQ(context.targ2.object.vnum, 700);
    EXPECT_EQ(context.targ2.object.id, "targ2");
    ASSERT_TRUE(context.has_target);
    EXPECT_TRUE(context.target.has_room);
    EXPECT_EQ(context.target.room.vnum, 101);
    EXPECT_EQ(context.target.room.id, "target");
    ASSERT_TRUE(context.has_dying);
    EXPECT_EQ(context.dying.name, "Target");
    EXPECT_EQ(context.dying.id, "dying");
    ASSERT_EQ(context.target_types.size(), 2u);
    EXPECT_EQ(context.target_types[0], "character");
    EXPECT_EQ(context.target_types[1], "object");
}

TEST(JsGameAdapter, TargetMappingSkipsStaleAndUnsupportedSlots)
{
    char_data self = make_character("Self", 1, 11, 22, 33, false);
    char_data stale_character = make_character("Stale", 1, 44, 55, 66, false);
    obj_data live_object = make_object("live object", -1);
    obj_data stale_object = make_object("stale object", 0);
    const char_data *live_characters[] = { &self };
    const obj_data *live_objects[] = { &live_object };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options = make_options(live_characters, 1, live_objects, 1, world, 0,
        nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    target_data stale_targ1;
    stale_targ1.type = TARGET_CHAR;
    stale_targ1.ptr.ch = &stale_character;
    stale_targ1.ch_num = stale_character.abs_number;
    target_data live_targ2;
    live_targ2.type = TARGET_OBJ;
    live_targ2.ptr.obj = &live_object;

    JsGameAdapterContextInput input;
    input.self = &self;
    input.targ1 = &stale_targ1;
    input.targ2 = &live_targ2;

    JsGameTriggerContextFixture context = js_game_adapter_context_fixture(input, options);

    EXPECT_FALSE(context.has_targ1);
    ASSERT_TRUE(context.has_targ2);
    EXPECT_TRUE(context.targ2.has_object);
    ASSERT_TRUE(context.has_target);
    EXPECT_TRUE(context.target.has_object);
    EXPECT_EQ(context.target.object.id, "target");
    ASSERT_EQ(context.target_types.size(), 2u);
    EXPECT_EQ(context.target_types[0], "character");
    EXPECT_EQ(context.target_types[1], "object");

    struct UnsupportedTargetType {
        signed char type;
        const char *name;
    };
    const UnsupportedTargetType unsupported_types[] = {
        { TARGET_TEXT, "text" },
        { TARGET_DIR, "direction" },
        { TARGET_GOLD, "gold" },
        { TARGET_IN, "in" },
        { TARGET_ALL, "all" },
        { TARGET_VALUE, "value" },
        { TARGET_OTHER, "other" },
        { TARGET_IGNORE, "ignore" },
        { static_cast<signed char>(99), "unknown" },
    };
    for (const UnsupportedTargetType &unsupported_type : unsupported_types) {
        target_data unsupported;
        unsupported.type = unsupported_type.type;
        unsupported.ptr.other = &stale_object;
        JsGameAdapterContextInput unsupported_input;
        unsupported_input.targ1 = &unsupported;

        JsGameTriggerContextFixture unsupported_context =
            js_game_adapter_context_fixture(unsupported_input, options);

        EXPECT_FALSE(unsupported_context.has_targ1) << static_cast<int>(unsupported_type.type);
        EXPECT_FALSE(unsupported_context.has_target) << static_cast<int>(unsupported_type.type);
        ASSERT_EQ(unsupported_context.target_types.size(), 1u);
        EXPECT_EQ(unsupported_context.target_types[0], unsupported_type.name)
            << static_cast<int>(unsupported_type.type);
    }
}

TEST(JsGameAdapter, RejectsStaleObjectTargetDataAndFallsBackToLiveSecondSlot)
{
    char_data self = make_character("Self", 1, 11, 22, 33, false);
    obj_data live_object = make_object("live object", -1);
    obj_data stale_object = make_object("stale object", 0);
    const char_data *live_characters[] = { &self };
    const obj_data *live_objects[] = { &live_object };
    JsGameAdapterOptions options = make_options(live_characters, 1, live_objects, 1, nullptr, -1,
        nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    target_data stale_targ1;
    stale_targ1.type = TARGET_OBJ;
    stale_targ1.ptr.obj = &stale_object;
    target_data live_targ2;
    live_targ2.type = TARGET_OBJ;
    live_targ2.ptr.obj = &live_object;

    JsGameAdapterContextInput input;
    input.self = &self;
    input.targ1 = &stale_targ1;
    input.targ2 = &live_targ2;

    JsGameTriggerContextFixture context = js_game_adapter_context_fixture(input, options);

    EXPECT_FALSE(context.has_targ1);
    ASSERT_TRUE(context.has_targ2);
    ASSERT_TRUE(context.targ2.has_object);
    EXPECT_EQ(context.targ2.object.id, "targ2");
    EXPECT_EQ(context.targ2.object.name, "live object");
    ASSERT_TRUE(context.has_target);
    ASSERT_TRUE(context.target.has_object);
    EXPECT_EQ(context.target.object.id, "target");
    EXPECT_EQ(context.target.object.name, "live object");
    ASSERT_EQ(context.target_types.size(), 2u);
    EXPECT_EQ(context.target_types[0], "object");
    EXPECT_EQ(context.target_types[1], "object");
}

TEST(JsGameAdapter, ExplicitStaleTargetDoesNotFallbackToTargetSlots)
{
    char_data self = make_character("Self", 1, 11, 22, 33, false);
    obj_data live_object = make_object("live object", -1);
    obj_data stale_object = make_object("stale object", 0);
    const char_data *live_characters[] = { &self };
    const obj_data *live_objects[] = { &live_object };
    JsGameAdapterOptions options = make_options(live_characters, 1, live_objects, 1, nullptr, -1,
        nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);
    target_data live_targ2;
    live_targ2.type = TARGET_OBJ;
    live_targ2.ptr.obj = &live_object;
    JsGameAdapterContextInput input;
    input.target_object = &stale_object;
    input.targ2 = &live_targ2;

    JsGameTriggerContextFixture context = js_game_adapter_context_fixture(input, options);

    ASSERT_TRUE(context.has_targ2);
    EXPECT_FALSE(context.has_target);
    ASSERT_EQ(context.target_types.size(), 1u);
    EXPECT_EQ(context.target_types[0], "object");
}

TEST(JsGameAdapter, RejectsCharacterTargetDataWhenAbsNumberDoesNotMatch)
{
    char_data self = make_character("Self", 1, 11, 22, 33, false);
    char_data target_character = make_character("Target", 1, 44, 55, 66, false);
    const char_data *live_characters[] = { &self, &target_character };
    JsGameAdapterOptions options = make_options(live_characters, 2, nullptr, 0, nullptr, -1,
        nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);
    target_data target;
    target.type = TARGET_CHAR;
    target.ptr.ch = &target_character;
    target.ch_num = target_character.abs_number + 1;
    JsGameAdapterContextInput input;
    input.targ1 = &target;

    JsGameTriggerContextFixture context = js_game_adapter_context_fixture(input, options);

    EXPECT_FALSE(context.has_targ1);
    EXPECT_FALSE(context.has_target);
    ASSERT_EQ(context.target_types.size(), 1u);
    EXPECT_EQ(context.target_types[0], "character");
}

TEST(JsGameAdapter, MapsTargetDataRoomPointerToTypedRoom)
{
    room_data world[2] = { make_room("Room", 100, 0), make_room("Target Room", 101, 0) };
    zone_data zones[1] = { make_zone("Zone", 10) };
    JsGameAdapterOptions options = make_options(nullptr, 0, nullptr, 0, world, 1, nullptr, 0,
        nullptr, 0, zones, 1, nullptr, 0);
    target_data room_target;
    room_target.type = TARGET_ROOM;
    room_target.ptr.room = &world[1];

    JsGameAdapterContextInput input;
    input.targ1 = &room_target;

    JsGameTriggerContextFixture context = js_game_adapter_context_fixture(input, options);

    ASSERT_TRUE(context.has_targ1);
    ASSERT_TRUE(context.targ1.has_room);
    EXPECT_EQ(context.targ1.room.vnum, 101);
    EXPECT_EQ(context.targ1.room.id, "targ1");
    ASSERT_TRUE(context.has_target);
    ASSERT_TRUE(context.target.has_room);
    EXPECT_EQ(context.target.room.vnum, 101);
    EXPECT_EQ(context.target.room.id, "target");
    ASSERT_EQ(context.target_types.size(), 1u);
    EXPECT_EQ(context.target_types[0], "room");

    room_data detached_room = make_room("Detached", 999, 0);
    room_target.ptr.room = &detached_room;
    JsGameTriggerContextFixture detached_context =
        js_game_adapter_context_fixture(input, options);
    EXPECT_FALSE(detached_context.has_targ1);
    EXPECT_FALSE(detached_context.has_target);
    ASSERT_EQ(detached_context.target_types.size(), 1u);
    EXPECT_EQ(detached_context.target_types[0], "room");
}

TEST(JsGameAdapter, MapsEveryWearSlotName)
{
    struct ExpectedSlot {
        int slot;
        const char *name;
    };
    const ExpectedSlot expected_slots[] = {
        { WEAR_LIGHT, "light" },
        { WEAR_FINGER_R, "fingerRight" },
        { WEAR_FINGER_L, "fingerLeft" },
        { WEAR_NECK_1, "neck1" },
        { WEAR_NECK_2, "neck2" },
        { WEAR_BODY, "body" },
        { WEAR_HEAD, "head" },
        { WEAR_LEGS, "legs" },
        { WEAR_FEET, "feet" },
        { WEAR_HANDS, "hands" },
        { WEAR_ARMS, "arms" },
        { WEAR_SHIELD, "shield" },
        { WEAR_ABOUT, "aboutBody" },
        { WEAR_WAISTE, "waist" },
        { WEAR_WRIST_R, "wristRight" },
        { WEAR_WRIST_L, "wristLeft" },
        { WIELD, "wield" },
        { HOLD, "hold" },
        { WEAR_BACK, "back" },
        { WEAR_BELT_1, "belt1" },
        { WEAR_BELT_2, "belt2" },
        { WEAR_BELT_3, "belt3" },
    };
    JsGameAdapterOptions options;

    for (const ExpectedSlot &expected : expected_slots) {
        JsGameAdapterContextInput input;
        input.wear_slot = expected.slot;

        JsGameTriggerContextFixture context = js_game_adapter_context_fixture(input, options);

        ASSERT_TRUE(context.has_wear_slot) << expected.slot;
        EXPECT_EQ(context.wear_slot, expected.name) << expected.slot;
    }

    JsGameAdapterContextInput negative_input;
    negative_input.wear_slot = -1;
    EXPECT_FALSE(js_game_adapter_context_fixture(negative_input, options).has_wear_slot);

    JsGameAdapterContextInput too_large_input;
    too_large_input.wear_slot = MAX_WEAR;
    EXPECT_FALSE(js_game_adapter_context_fixture(too_large_input, options).has_wear_slot);
}

TEST(JsGameAdapter, HandlesInvalidRoomZoneMetadata)
{
    char_data self = make_character("Self", 1, 11, 22, 33, false);
    const char_data *live_characters[] = { &self };
    zone_data zones[1] = { make_zone("Zone", 10) };
    room_data negative_zone_world[1] = { make_room("Bad Zone", 100, -1) };
    room_data out_of_range_zone_world[1] = { make_room("Bad Zone", 100, 1) };

    JsGameAdapterContextInput input;
    input.self = &self;
    input.room = 0;

    JsGameAdapterOptions negative_options = make_options(live_characters, 1, nullptr, 0,
        negative_zone_world, 0, nullptr, 0, nullptr, 0, zones, 1, nullptr, 0);
    EXPECT_FALSE(js_game_adapter_context_fixture(input, negative_options).has_zone);

    JsGameAdapterOptions out_of_range_options = make_options(live_characters, 1, nullptr, 0,
        out_of_range_zone_world, 0, nullptr, 0, nullptr, 0, zones, 1, nullptr, 0);
    EXPECT_FALSE(js_game_adapter_context_fixture(input, out_of_range_options).has_zone);

    JsGameAdapterOptions missing_zone_options = make_options(live_characters, 1, nullptr, 0,
        out_of_range_zone_world, 0, nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);
    EXPECT_FALSE(js_game_adapter_context_fixture(input, missing_zone_options).has_zone);
}

TEST(JsGameAdapter, HandlesUnresolvedVnumIndexesWithoutLeakingIndexes)
{
    char_data npc = make_character("Unindexed Mob", 1, 1, 1, 1, true);
    npc.nr = 99;
    obj_data object = make_object("Unindexed Object", 99);
    const char_data *live_characters[] = { &npc };
    const obj_data *live_objects[] = { &object };
    JsGameAdapterOptions options = make_options(live_characters, 1, live_objects, 1, nullptr, -1,
        nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture character_fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&npc, options, &character_fixture));
    EXPECT_EQ(character_fixture.vnum, -1);
    EXPECT_EQ(character_fixture.id, "mob:unresolved");
    EXPECT_EQ(character_fixture.id.find("99"), std::string::npos);

    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &object_fixture));
    EXPECT_EQ(object_fixture.vnum, -1);
    EXPECT_EQ(object_fixture.id, "object:unresolved");
    EXPECT_EQ(object_fixture.id.find("99"), std::string::npos);
}

TEST(JsGameAdapter, DoesNotDereferenceObjectRelationshipPointers)
{
    char_data stale_carrier = make_character("Carrier", 1, 1, 1, 1, false);
    obj_data stale_container = make_object("Container", 0);
    obj_data object = make_object("Nested", 0);
    object.carried_by = &stale_carrier;
    object.in_obj = &stale_container;
    object.contains = &stale_container;
    object.in_room = -1;
    index_data object_index[1] {};
    object_index[0].virt = 300;
    const obj_data *live_objects[] = { &object };
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 1, nullptr, -1, nullptr,
        0, object_index, 1, nullptr, 0, nullptr, 0);

    JsGameObjectFixture fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &fixture));
    EXPECT_EQ(fixture.id, "object:300");
    EXPECT_EQ(fixture.name, "Nested");
    EXPECT_FALSE(fixture.has_carried_by);
    EXPECT_FALSE(fixture.has_worn_by);

    object.in_obj = nullptr;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &fixture));
    EXPECT_FALSE(fixture.has_carried_by);
    EXPECT_FALSE(fixture.has_worn_by);
}

TEST(JsGameAdapter, BoundsCopiedStrings)
{
    char long_name[700];
    std::fill(std::begin(long_name), std::end(long_name), 'x');
    long_name[699] = '\0';
    char_data character = make_character(long_name, 1, 1, 1, 1, false);
    const char_data *live_characters[] = { &character };
    JsGameAdapterOptions options = make_options(live_characters, 1, nullptr, 0, nullptr, -1,
        nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&character, options, &fixture));
    EXPECT_EQ(fixture.name.size(), 512u);

    char long_text[1200];
    std::fill(std::begin(long_text), std::end(long_text), 'y');
    long_text[1199] = '\0';
    JsGameAdapterContextInput input;
    input.self = &character;
    input.text = long_text;

    JsGameTriggerContextFixture context = js_game_adapter_context_fixture(input, options);
    EXPECT_EQ(context.text.size(), 1024u);
}

TEST(JsGameAdapter, SnapshotsStringsWithoutRetainingAliases)
{
    char original_name[] = "Original";
    char_data character = make_character(original_name, 99, 1, 2, 3, false);
    const char_data *live_characters[] = { &character };
    JsGameAdapterOptions options = make_options(live_characters, 1, nullptr, 0, nullptr, -1,
        nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&character, options, &fixture));

    original_name[0] = 'X';
    character.player.name = const_cast<char *>("Changed");

    EXPECT_EQ(fixture.name, "Original");
    EXPECT_EQ(fixture.race, "race:99");
}

TEST(JsGameAdapter, OpaqueIdsDoNotContainPointerLookingText)
{
    char_data character = make_character("NoPointer", 1, 1, 1, 1, false);
    obj_data object = make_object("NoPointerObject", -1);
    const char_data *live_characters[] = { &character };
    const obj_data *live_objects[] = { &object };
    JsGameAdapterOptions options = make_options(live_characters, 1, live_objects, 1, nullptr, -1,
        nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture character_fixture;
    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&character, options, &character_fixture));
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &object_fixture));

    EXPECT_EQ(character_fixture.id.find("0x"), std::string::npos);
    EXPECT_EQ(object_fixture.id.find("0x"), std::string::npos);
    EXPECT_EQ(character_fixture.id.find("char_data"), std::string::npos);
    EXPECT_EQ(object_fixture.id.find("obj_data"), std::string::npos);
    EXPECT_EQ(character_fixture.id.find("1234"), std::string::npos);
    EXPECT_EQ(object_fixture.id.find("88"), std::string::npos);
}
