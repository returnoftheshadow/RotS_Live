#include "../js_game_adapter.h"

#include "../db.h"
#include "../structs.h"
#include "../zone.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>

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
    character.abilities.hit = max_hit;
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
    object.owner = 88;
    object.touched = 1;
    return object;
}

room_data make_room(const char *name, int number, int zone)
{
    room_data room {};
    room.name = const_cast<char *>(name);
    room.number = number;
    room.zone = zone;
    return room;
}

zone_data make_zone(const char *name, int number)
{
    zone_data zone {};
    zone.name = const_cast<char *>(name);
    zone.number = number;
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
    EXPECT_EQ(object_fixture.vnum, 300);
    ASSERT_TRUE(object_fixture.has_room);
    EXPECT_EQ(object_fixture.room.vnum, 1204);
    ASSERT_TRUE(object_fixture.room.has_zone);
    EXPECT_EQ(object_fixture.room.zone.vnum, 12);

    JsGameRoomFixture room_fixture;
    ASSERT_TRUE(js_game_adapter_room_fixture(0, options, &room_fixture));
    EXPECT_EQ(room_fixture.id, "room:1204");
    EXPECT_EQ(room_fixture.name, "Northern Gate");
    EXPECT_EQ(room_fixture.vnum, 1204);

    JsGameZoneFixture zone_fixture;
    ASSERT_TRUE(js_game_adapter_zone_fixture(0, options, &zone_fixture));
    EXPECT_EQ(zone_fixture.id, "zone:12");
    EXPECT_EQ(zone_fixture.name, "Old City");
    EXPECT_EQ(zone_fixture.vnum, 12);
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
    const char_data *live_characters[] = { &self };
    const obj_data *live_objects[] = { &object };
    room_data world[1] = { make_room("Room", 100, 0) };
    zone_data zones[1] = { make_zone("Zone", 10) };
    JsGameAdapterOptions options = make_options(live_characters, 1, live_objects, 1, world, 0,
        nullptr, 0, nullptr, 0, zones, 1, races, 2);

    JsGameAdapterContextInput input;
    input.self = &self;
    input.actor = &stale_actor;
    input.object = &object;
    input.room = 0;
    input.text = "hello";
    input.trigger = make_trigger();

    JsGameTriggerContextFixture context = js_game_adapter_context_fixture(input, options);

    EXPECT_TRUE(context.has_self);
    EXPECT_FALSE(context.has_actor);
    EXPECT_TRUE(context.has_object);
    EXPECT_EQ(context.object.vnum, -1);
    EXPECT_TRUE(context.has_room);
    EXPECT_TRUE(context.has_zone);
    EXPECT_TRUE(context.has_text);
    EXPECT_EQ(context.text, "hello");
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
    index_data object_index[1] {};
    object_index[0].virt = 400;
    const char_data *live_characters[] = { &self, &actor };
    const obj_data *live_objects[] = { &object };
    JsGameAdapterOptions options = make_options(live_characters, 2, live_objects, 1, nullptr, -1,
        nullptr, 0, object_index, 1, nullptr, 0, nullptr, 0);

    JsGameAdapterContextInput input;
    input.self = &self;
    input.actor = &actor;
    input.object = &object;

    JsGameTriggerContextFixture context = js_game_adapter_context_fixture(input, options);

    EXPECT_EQ(context.self.id, "self");
    EXPECT_EQ(context.actor.id, "actor");
    EXPECT_EQ(context.object.id, "object");
    EXPECT_EQ(context.self.id.find("98765"), std::string::npos);
    EXPECT_EQ(context.actor.id.find("11111"), std::string::npos);
    EXPECT_EQ(context.object.id.find("55"), std::string::npos);
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
