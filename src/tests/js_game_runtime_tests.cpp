#include "../js_game_runtime.h"
#include "../js_api_struct_mapping.h"

#include <gtest/gtest.h>

extern char* sector_types[];
extern char num_of_sector_types;

namespace {

JsGameTriggerContextFixture make_context()
{
    JsGameTriggerContextFixture context;
    context.has_self = true;
    context.self.id = "char:1001";
    context.self.name = "Aldren";
    context.self.race = "Human";
    context.self.level = 42;
    context.self.experience = 42000;
    context.self.rank = 9;
    context.self.hit_points = 125;
    context.self.max_hit_points = 150;
    context.self.class_points = 6;
    context.self.interrupt_count = 2;
    context.self.interrupt_time = 11;
    context.self.special_busy = true;
    context.self.current_abilities.strength = 18;
    context.self.current_abilities.intelligence = 13;
    context.self.current_abilities.willpower = 15;
    context.self.current_abilities.dexterity = 17;
    context.self.current_abilities.constitution = 16;
    context.self.current_abilities.leadership = 9;
    context.self.has_room = true;
    context.self.room.id = "room:1204";
    context.self.room.name = "Northern Gate";
    context.self.room.description = "A gatehouse opens toward the old road.";
    context.self.room.vnum = 1204;
    context.self.room.level = 7;
    context.self.room.sector_type = "City";
    context.self.room.flags = { "dark", "indoors" };
    context.self.room.alignment = -2;
    context.self.room.light = 1;
    context.self.room.is_sunlit = true;
    context.self.room.has_zone = true;
    context.self.room.zone.id = "zone:12";
    context.self.room.zone.name = "Old City";
    context.self.room.zone.has_description = true;
    context.self.room.zone.description = "The old city zone.";
    context.self.room.zone.has_map = true;
    context.self.room.zone.map = "N-G-S";
    context.self.room.zone.vnum = 12;
    context.self.room.zone.level = 6;
    context.self.room.zone.lifespan = 30;
    context.self.room.zone.age = 4;
    context.self.room.zone.top_room_vnum = 1299;
    context.self.room.zone.x = 8;
    context.self.room.zone.y = -3;
    context.self.room.zone.symbol = "C";
    context.self.room.zone.white_power = 120;
    context.self.room.zone.dark_power = 80;
    context.self.room.zone.magi_power = 35;
    context.self.room.zone.minimum_look_level = 2;
    context.self.room.zone.reset_mode = 1;

    context.has_actor = true;
    context.actor.id = "player:7";
    context.actor.name = "Builder";
    context.actor.race = "Elf";
    context.actor.level = 31;
    context.actor.experience = 31000;
    context.actor.rank = 4;
    context.actor.hit_points = 88;
    context.actor.max_hit_points = 92;
    context.actor.class_points = 3;
    context.actor.interrupt_count = 1;
    context.actor.interrupt_time = 5;
    context.actor.special_busy = false;
    context.actor.current_abilities.strength = 11;
    context.actor.current_abilities.intelligence = 19;
    context.actor.current_abilities.willpower = 14;
    context.actor.current_abilities.dexterity = 16;
    context.actor.current_abilities.constitution = 12;
    context.actor.current_abilities.leadership = 10;

    context.has_object = true;
    context.object.id = "object:300";
    context.object.name = "silver lever";
    context.object.description = "A silver lever is bolted to the wall.";
    context.object.short_description = "a silver lever";
    context.object.has_action_description = true;
    context.object.action_description = "The lever clicks under your hand.";
    context.object.vnum = 300;
    context.object.flags.item_type = "weapon";
    context.object.flags.wear_flags = { "take", "wield" };
    context.object.flags.extra_flags = { "glow", "magic" };
    context.object.flags.level = 12;
    context.object.flags.weight = 7;
    context.object.flags.cost = 450;
    context.object.flags.cost_per_day = 15;
    context.object.flags.timer = 30;
    context.object.flags.rarity = 2;
    context.object.flags.material = "metal";
    context.object.has_room = true;
    context.object.room.id = "room:1204";
    context.object.room.name = "Northern Gate";
    context.object.room.description = "A gatehouse opens toward the old road.";
    context.object.room.vnum = 1204;
    context.object.room.level = 7;
    context.object.room.sector_type = "City";
    context.object.room.flags = { "dark", "indoors" };
    context.object.room.alignment = -2;
    context.object.room.light = 1;
    context.object.room.is_sunlit = true;
    context.object.room.has_zone = true;
    context.object.room.zone.id = "zone:12";
    context.object.room.zone.name = "Old City";
    context.object.room.zone.has_description = true;
    context.object.room.zone.description = "The old city zone.";
    context.object.room.zone.has_map = true;
    context.object.room.zone.map = "N-G-S";
    context.object.room.zone.vnum = 12;
    context.object.room.zone.level = 6;
    context.object.room.zone.lifespan = 30;
    context.object.room.zone.age = 4;
    context.object.room.zone.top_room_vnum = 1299;
    context.object.room.zone.x = 8;
    context.object.room.zone.y = -3;
    context.object.room.zone.symbol = "C";
    context.object.room.zone.white_power = 120;
    context.object.room.zone.dark_power = 80;
    context.object.room.zone.magi_power = 35;
    context.object.room.zone.minimum_look_level = 2;
    context.object.room.zone.reset_mode = 1;

    context.has_room = true;
    context.room.id = "room:1204";
    context.room.name = "Northern Gate";
    context.room.description = "A gatehouse opens toward the old road.";
    context.room.vnum = 1204;
    context.room.level = 7;
    context.room.sector_type = "City";
    context.room.flags = { "dark", "indoors" };
    context.room.alignment = -2;
    context.room.light = 1;
    context.room.is_sunlit = true;
    context.room.has_zone = true;
    context.room.zone.id = "zone:12";
    context.room.zone.name = "Old City";
    context.room.zone.has_description = true;
    context.room.zone.description = "The old city zone.";
    context.room.zone.has_map = true;
    context.room.zone.map = "N-G-S";
    context.room.zone.vnum = 12;
    context.room.zone.level = 6;
    context.room.zone.lifespan = 30;
    context.room.zone.age = 4;
    context.room.zone.top_room_vnum = 1299;
    context.room.zone.x = 8;
    context.room.zone.y = -3;
    context.room.zone.symbol = "C";
    context.room.zone.white_power = 120;
    context.room.zone.dark_power = 80;
    context.room.zone.magi_power = 35;
    context.room.zone.minimum_look_level = 2;
    context.room.zone.reset_mode = 1;

    context.has_zone = true;
    context.zone.id = "zone:12";
    context.zone.name = "Old City";
    context.zone.has_description = true;
    context.zone.description = "The old city zone.";
    context.zone.has_map = true;
    context.zone.map = "N-G-S";
    context.zone.vnum = 12;
    context.zone.level = 6;
    context.zone.lifespan = 30;
    context.zone.age = 4;
    context.zone.top_room_vnum = 1299;
    context.zone.x = 8;
    context.zone.y = -3;
    context.zone.symbol = "C";
    context.zone.white_power = 120;
    context.zone.dark_power = 80;
    context.zone.magi_power = 35;
    context.zone.minimum_look_level = 2;
    context.zone.reset_mode = 1;

    context.has_text = true;
    context.text = "say \"open\"\\close\nnext line";

    context.trigger.name = "onPull";
    context.trigger.legacy_name = "ON_PULL";
    context.trigger.host_type = "object";
    context.trigger.legacy_value = 22;
    context.trigger.blocks_gameplay = true;
    return context;
}

void expect_ok_allows(const JsRuntimeEvalResult &result)
{
    EXPECT_EQ(result.status, JsRuntimeStatus::Ok) << result.diagnostic;
    EXPECT_EQ(result.value, JsRuntimeValue::Allow) << result.diagnostic;
}

std::size_t count_occurrences(const std::string &haystack, const std::string &needle)
{
    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = haystack.find(needle, offset)) != std::string::npos) {
        ++count;
        offset += needle.size();
    }
    return count;
}

const char *runtime_owner_name(JsApiStructOwner owner)
{
    switch (owner) {
    case JsApiStructOwner::CharData:
        return "Character";
    case JsApiStructOwner::ObjData:
        return "GameObject";
    case JsApiStructOwner::RoomData:
        return "Room";
    case JsApiStructOwner::ZoneData:
        return "Zone";
    }
    return "Unknown";
}

std::string quoted_js_string(const char *value)
{
    std::string quoted = "'";
    for (const char *cursor = value; *cursor; ++cursor) {
        if (*cursor == '\\' || *cursor == '\'')
            quoted += '\\';
        quoted += *cursor;
    }
    quoted += "'";
    return quoted;
}

std::string setter_surface_list(JsApiStructOwner owner, bool callable)
{
    std::set<std::string> names;
    for (std::size_t index = 0; index < js_api_struct_field_mapping_count(); ++index) {
        const JsApiStructFieldMapping &mapping = js_api_struct_field_mappings()[index];
        if (mapping.owner != owner)
            continue;
        const bool setter_is_callable =
            std::string(mapping.setter_status) == "implemented-validated-setter";
        if (setter_is_callable == callable)
            names.insert(mapping.setter_name);
    }
    if (owner == JsApiStructOwner::ObjData && callable) {
        names.insert("setLevel");
        names.insert("setRarity");
    }

    std::ostringstream out;
    out << '[';
    bool first = true;
    for (const std::string &name : names) {
        if (!first)
            out << ',';
        first = false;
        out << quoted_js_string(name.c_str());
    }
    out << ']';
    return out.str();
}

std::string generated_setter_surface_script()
{
    std::ostringstream out;
    out << "const expected = {\n";
    const JsApiStructOwner owners[] = { JsApiStructOwner::CharData, JsApiStructOwner::ObjData,
        JsApiStructOwner::RoomData, JsApiStructOwner::ZoneData };
    for (std::size_t index = 0; index < sizeof(owners) / sizeof(owners[0]); ++index) {
        if (index > 0)
            out << ",\n";
        out << "  " << runtime_owner_name(owners[index]) << ": { callable: "
            << setter_surface_list(owners[index], true)
            << ", absent: " << setter_surface_list(owners[index], false) << " }";
    }
    out << "\n};\n"
        << "function check(handle, owner) {\n"
        << "  if (!handle) return false;\n"
        << "  const actual = Object.getOwnPropertyNames(handle)\n"
        << "    .filter((name) => /^set[A-Z]/.test(name))\n"
        << "    .sort();\n"
        << "  const expectedCallable = expected[owner].callable.slice().sort();\n"
        << "  return actual.length === expectedCallable.length\n"
        << "    && actual.every((name, index) => name === expectedCallable[index])\n"
        << "    && expected[owner].callable.every((name) => typeof handle[name] === 'function')\n"
        << "    && expected[owner].absent.every((name) => typeof handle[name] === 'undefined');\n"
        << "}\n"
        << "function inferredOwner(handle) {\n"
        << "  if (!handle) return null;\n"
        << "  const matches = [];\n"
        << "  if ('isPlayer' in handle) matches.push('Character');\n"
        << "  if ('isSunlit' in handle || 'sectorType' in handle) matches.push('Room');\n"
        << "  if ('topRoomVnum' in handle || 'resetMode' in handle) matches.push('Zone');\n"
        << "  if ('shortDescription' in handle || 'actionDescription' in handle || 'carriedBy' in handle) matches.push('GameObject');\n"
        << "  return matches.length === 1 ? matches[0] : null;\n"
        << "}\n"
        << "function checkInferred(handle) {\n"
        << "  const owner = inferredOwner(handle);\n"
        << "  return owner !== null && check(handle, owner);\n"
        << "}\n"
        << "function checkNested(handle) {\n"
        << "  const owner = inferredOwner(handle);\n"
        << "  if (owner === null || !check(handle, owner)) return false;\n"
        << "  if ((owner === 'Character' || owner === 'GameObject') && handle.room && !checkNested(handle.room)) return false;\n"
        << "  if (owner === 'GameObject' && handle.carriedBy && !checkNested(handle.carriedBy)) return false;\n"
        << "  if (owner === 'GameObject' && handle.wornBy && !checkNested(handle.wornBy)) return false;\n"
        << "  if (owner === 'Room' && handle.zone && !checkNested(handle.zone)) return false;\n"
        << "  return true;\n"
        << "}\n"
        << "function requireFixtureNestedPath(handle) {\n"
        << "  const owner = inferredOwner(handle);\n"
        << "  if (owner === 'Character') return !!handle.room && !!handle.room.zone;\n"
        << "  if (owner === 'GameObject') return !!handle.room && !!handle.room.zone;\n"
        << "  if (owner === 'Room') return !!handle.zone;\n"
        << "  return owner === 'Zone';\n"
        << "}\n"
        << "return [ctx.self, ctx.actor, ctx.speaker, ctx.attacker, ctx.victim, ctx.killer, ctx.dying]\n"
        << "    .every(requireFixtureNestedPath)\n"
        << "  && [ctx.object, ctx.target, ctx.targ1, ctx.targ2].every(requireFixtureNestedPath)\n"
        << "  && requireFixtureNestedPath(ctx.room)\n"
        << "  && requireFixtureNestedPath(ctx.zone)\n"
        << "  && ctx.weapon.carriedBy && requireFixtureNestedPath(ctx.weapon.carriedBy)\n"
        << "  && ctx.weapon.wornBy && requireFixtureNestedPath(ctx.weapon.wornBy)\n"
        << "  && checkNested(ctx.self)\n"
        << "  && checkNested(ctx.actor)\n"
        << "  && checkNested(ctx.speaker)\n"
        << "  && checkNested(ctx.attacker)\n"
        << "  && checkNested(ctx.victim)\n"
        << "  && checkNested(ctx.killer)\n"
        << "  && checkNested(ctx.object)\n"
        << "  && checkNested(ctx.weapon)\n"
        << "  && checkNested(ctx.room)\n"
        << "  && checkNested(ctx.zone)\n"
        << "  && checkInferred(ctx.target)\n"
        << "  && checkInferred(ctx.targ1)\n"
        << "  && checkInferred(ctx.targ2)\n"
        << "  && checkNested(ctx.target)\n"
        << "  && checkNested(ctx.targ1)\n"
        << "  && checkNested(ctx.targ2)\n"
        << "  && checkNested(ctx.dying);";
    return out.str();
}

void set_character_target(JsGameTargetFixture &target, const JsGameCharacterFixture &character,
    const char *id)
{
    target = JsGameTargetFixture {};
    target.type = "character";
    target.has_character = true;
    target.character = character;
    target.character.id = id;
}

void set_object_target(JsGameTargetFixture &target, const JsGameObjectFixture &object, const char *id)
{
    target = JsGameTargetFixture {};
    target.type = "object";
    target.has_object = true;
    target.object = object;
    target.object.id = id;
}

void set_room_target(JsGameTargetFixture &target, const JsGameRoomFixture &room, const char *id)
{
    target = JsGameTargetFixture {};
    target.type = "room";
    target.has_room = true;
    target.room = room;
    target.room.id = id;
}

} // namespace

TEST(JsGameRuntime, ExecutesScriptsAgainstReadOnlyGameContext)
{
    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return ctx.self.id === 'char:1001'\n"
        "  && ctx.self.name === 'Aldren'\n"
        "  && ctx.self.isPlayer === true\n"
        "  && ctx.self.experience === 42000\n"
        "  && ctx.self.rank === 9\n"
        "  && ctx.self.classPoints === 6\n"
        "  && ctx.self.interruptCount === 2\n"
        "  && ctx.self.interruptTime === 11\n"
        "  && ctx.self.specialBusy === true\n"
        "  && ctx.self.currentAbilities.strength === 18\n"
        "  && ctx.self.currentAbilities.intelligence === 13\n"
        "  && ctx.self.currentAbilities.willpower === 15\n"
        "  && ctx.self.currentAbilities.dexterity === 17\n"
        "  && ctx.self.currentAbilities.constitution === 16\n"
        "  && ctx.self.currentAbilities.leadership === 9\n"
        "  && ctx.self.isValid() === true\n"
        "  && ctx.self.room.vnum === 1204\n"
        "  && ctx.self.room.isSunlit === true\n"
        "  && ctx.self.room.zone.name === 'Old City'\n"
        "  && ctx.actor.race === 'Elf'\n"
        "  && ctx.actor.experience === 31000\n"
        "  && ctx.actor.rank === 4\n"
        "  && ctx.actor.classPoints === 3\n"
        "  && ctx.actor.interruptCount === 1\n"
        "  && ctx.actor.interruptTime === 5\n"
        "  && ctx.actor.specialBusy === false\n"
        "  && ctx.actor.currentAbilities.strength === 11\n"
        "  && ctx.actor.currentAbilities.intelligence === 19\n"
        "  && ctx.actor.currentAbilities.willpower === 14\n"
        "  && ctx.actor.currentAbilities.dexterity === 16\n"
        "  && ctx.actor.currentAbilities.constitution === 12\n"
        "  && ctx.actor.currentAbilities.leadership === 10\n"
        "  && ctx.object.vnum === 300\n"
        "  && ctx.object.room.name === 'Northern Gate'\n"
        "  && ctx.object.room.isSunlit === true\n"
        "  && ctx.object.room.zone.vnum === 12\n"
        "  && ctx.object.isValid() === true\n"
        "  && ctx.room.name === 'Northern Gate'\n"
        "  && ctx.room.isSunlit === true\n"
        "  && ctx.room.zone.vnum === 12\n"
        "  && ctx.room.isValid() === true\n"
        "  && ctx.zone.vnum === 12\n"
        "  && ctx.trigger.name === 'onPull'\n"
        "  && ctx.trigger.handlerName === 'onPull'\n"
        "  && ctx.trigger.kind === 'legacy'\n"
        "  && ctx.trigger.blocksGameplay === true;",
        make_context());

    expect_ok_allows(result);
}

TEST(JsGameRuntime, SetterSurfaceMatchesStructMappingCatalog)
{
    JsGameTriggerContextFixture context = make_context();
    context.actor.has_room = true;
    context.actor.room = context.room;
    context.has_speaker = true;
    context.speaker = context.actor;
    context.speaker.id = "speaker";
    context.has_attacker = true;
    context.attacker = context.self;
    context.attacker.id = "attacker";
    context.has_victim = true;
    context.victim = context.actor;
    context.victim.id = "victim";
    context.has_killer = true;
    context.killer = context.self;
    context.killer.id = "killer";
    context.has_weapon = true;
    context.weapon = context.object;
    context.weapon.has_carried_by = true;
    context.weapon.carried_by = context.actor;
    context.weapon.carried_by.id = "weapon-carrier";
    context.weapon.has_worn_by = true;
    context.weapon.worn_by = context.self;
    context.weapon.worn_by.id = "weapon-wearer";
    context.has_target = true;
    set_character_target(context.target, context.actor, "target-character");
    context.has_targ1 = true;
    set_character_target(context.targ1, context.self, "targ1-character");
    context.has_targ2 = true;
    set_character_target(context.targ2, context.victim, "targ2-character");
    context.has_dying = true;
    context.dying = context.self;
    context.dying.id = "dying";

    JsGameRuntime runtime;
    JsRuntimeEvalResult result =
        runtime.evaluate_trigger_body(generated_setter_surface_script(), context);

    expect_ok_allows(result);

    JsGameTriggerContextFixture object_context = context;
    set_object_target(object_context.target, object_context.object, "target-object");
    set_object_target(object_context.targ1, object_context.weapon, "targ1-object");
    set_object_target(object_context.targ2, object_context.object, "targ2-object");

    JsRuntimeEvalResult object_result =
        runtime.evaluate_trigger_body(generated_setter_surface_script(), object_context);

    expect_ok_allows(object_result);

    JsGameTriggerContextFixture room_context = context;
    set_room_target(room_context.target, room_context.room, "target-room");
    set_room_target(room_context.targ1, room_context.room, "targ1-room");
    set_room_target(room_context.targ2, room_context.room, "targ2-room");

    JsRuntimeEvalResult room_result =
        runtime.evaluate_trigger_body(generated_setter_surface_script(), room_context);

    expect_ok_allows(room_result);
}

TEST(JsGameRuntime, ExposesMobPrototypeVnumWhenPresent)
{
    JsGameTriggerContextFixture context = make_context();
    context.self.is_npc = true;
    context.self.vnum = 5100;
    context.self.prototype_vnum = 5100;

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return ctx.self.isNpc === true\n"
        "  && ctx.self.isPlayer === false\n"
        "  && ctx.self.vnum === 5100\n"
        "  && ctx.self.prototypeVnum === 5100;",
        context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, ModelsUnresolvedMobPrototypeVnumAsNull)
{
    JsGameTriggerContextFixture context = make_context();
    context.self.is_npc = true;
    context.self.vnum = -1;
    context.self.prototype_vnum = -1;

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return ctx.self.isNpc === true\n"
        "  && ctx.self.vnum === null\n"
        "  && ctx.self.prototypeVnum === null;",
        context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, ModelsObjectRoomAsNullWhenMissing)
{
    JsGameTriggerContextFixture context = make_context();
    context.object.has_room = false;

    JsGameRuntime runtime;
    JsRuntimeEvalResult result =
        runtime.evaluate_trigger_body(
            "return ctx.object.room === null\n"
            "  && ctx.object.carriedBy === null\n"
            "  && ctx.object.wornBy === null;",
            context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, ExposesPromotedStructGetterSnapshots)
{
    JsGameTriggerContextFixture context = make_context();

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return ctx.object.description === 'A silver lever is bolted to the wall.'\n"
        "  && ctx.object.shortDescription === 'a silver lever'\n"
        "  && ctx.object.actionDescription === 'The lever clicks under your hand.'\n"
        "  && ctx.object.flags.itemType === 'weapon'\n"
        "  && ctx.object.flags.wearFlags.join(',') === 'take,wield'\n"
        "  && ctx.object.flags.extraFlags.join(',') === 'glow,magic'\n"
        "  && typeof ctx.object.flags.values === 'undefined'\n"
        "  && ctx.object.flags.level === 12\n"
        "  && ctx.object.flags.weight === 7\n"
        "  && ctx.object.flags.cost === 450\n"
        "  && ctx.object.flags.costPerDay === 15\n"
        "  && ctx.object.flags.timer === 30\n"
        "  && ctx.object.flags.rarity === 2\n"
        "  && ctx.object.flags.material === 'metal'\n"
        "  && ctx.room.description === 'A gatehouse opens toward the old road.'\n"
        "  && ctx.room.level === 7\n"
        "  && ctx.room.sectorType === 'City'\n"
        "  && Array.isArray(ctx.room.flags)\n"
        "  && ctx.room.flags.join(',') === 'dark,indoors'\n"
        "  && ctx.room.alignment === -2\n"
        "  && ctx.room.light === 1\n"
        "  && ctx.zone.level === 6\n"
        "  && ctx.zone.description === 'The old city zone.'\n"
        "  && ctx.zone.map === 'N-G-S'\n"
        "  && ctx.zone.lifespan === 30\n"
        "  && ctx.zone.age === 4\n"
        "  && ctx.zone.topRoomVnum === 1299\n"
        "  && ctx.zone.x === 8\n"
        "  && ctx.zone.y === -3\n"
        "  && ctx.zone.symbol === 'C'\n"
        "  && ctx.zone.whitePower === 120\n"
        "  && ctx.zone.darkPower === 80\n"
        "  && ctx.zone.magiPower === 35\n"
        "  && ctx.zone.minimumLookLevel === 2\n"
        "  && ctx.zone.resetMode === 1\n"
        "  && ctx.object.room.description === ctx.room.description\n"
        "  && ctx.object.room.sectorType === ctx.room.sectorType\n"
        "  && ctx.object.room.flags.join(',') === ctx.room.flags.join(',')\n"
        "  && ctx.object.room.light === ctx.room.light\n"
        "  && ctx.object.room.zone.description === ctx.zone.description\n"
        "  && ctx.object.room.zone.map === ctx.zone.map\n"
        "  && ctx.object.room.zone.level === ctx.zone.level\n"
        "  && ctx.object.room.zone.whitePower === ctx.zone.whitePower\n"
        "  && ctx.room.zone.darkPower === ctx.zone.darkPower\n"
        "  && ctx.self.room.zone.magiPower === ctx.zone.magiPower;",
        context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, KeepsObjectFlagArraysFrozenAndConstructorSafe)
{
    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "let pushBlocked = false;\n"
        "let extraPushBlocked = false;\n"
        "let indexBlocked = false;\n"
        "let lengthBlocked = false;\n"
        "try { ctx.object.flags.wearFlags.push('hold'); } catch (error) { pushBlocked = true; }\n"
        "try { ctx.object.flags.extraFlags.push('dark'); } catch (error) { extraPushBlocked = true; }\n"
        "try { ctx.object.flags.wearFlags[0] = 'hold'; } catch (error) { indexBlocked = true; }\n"
        "try { ctx.object.flags.extraFlags.length = 0; } catch (error) { lengthBlocked = true; }\n"
        "return ctx.object.flags.wearFlags.join(',') === 'take,wield'\n"
        "  && ctx.object.flags.extraFlags.join(',') === 'glow,magic'\n"
        "  && typeof ctx.object.flags.values === 'undefined'\n"
        "  && Object.isFrozen(ctx.object.flags)\n"
        "  && Object.isFrozen(ctx.object.flags.wearFlags)\n"
        "  && Object.isFrozen(ctx.object.flags.extraFlags)\n"
        "  && pushBlocked && extraPushBlocked && indexBlocked && lengthBlocked\n"
        "  && typeof ctx.object.flags.wearFlags.join.constructor === 'undefined';",
        make_context());

    expect_ok_allows(result);
}

TEST(JsGameRuntime, KeepsRoomFlagArraysFrozenAndConstructorSafe)
{
    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "let pushBlocked = false;\n"
        "let indexBlocked = false;\n"
        "let lengthBlocked = false;\n"
        "try { ctx.room.flags.push('death'); } catch (error) { pushBlocked = true; }\n"
        "try { ctx.room.flags[0] = 'death'; } catch (error) { indexBlocked = true; }\n"
        "try { ctx.room.flags.length = 0; } catch (error) { lengthBlocked = true; }\n"
        "return Array.isArray(ctx.room.flags)\n"
        "  && ctx.room.flags.includes('dark')\n"
        "  && ctx.room.flags.join(',') === 'dark,indoors'\n"
        "  && Object.isFrozen(ctx.room.flags)\n"
        "  && pushBlocked && indexBlocked && lengthBlocked\n"
        "  && typeof ctx.room.flags.constructor === 'undefined'\n"
        "  && typeof ctx.room.flags.join.constructor === 'undefined'\n"
        "  && typeof ctx.room.flags.filter.constructor === 'undefined'\n"
        "  && typeof ctx.room.flags.__proto__.constructor === 'undefined';",
        make_context());

    expect_ok_allows(result);
}

TEST(JsGameRuntime, ModelsNullablePromotedStructGetterSnapshotsAsNull)
{
    JsGameTriggerContextFixture context = make_context();
    context.object.has_action_description = false;
    context.zone.has_description = false;
    context.zone.has_map = false;
    context.room.zone.has_description = false;
    context.room.zone.has_map = false;
    context.object.room.zone.has_description = false;
    context.object.room.zone.has_map = false;

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return ctx.object.actionDescription === null\n"
        "  && ctx.zone.description === null\n"
        "  && ctx.zone.map === null\n"
        "  && ctx.room.zone.description === null\n"
        "  && ctx.object.room.zone.map === null;",
        context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, PreservesEmptyNullablePromotedStructGetterStrings)
{
    JsGameTriggerContextFixture context = make_context();
    context.object.has_action_description = true;
    context.object.action_description = "";
    context.zone.has_description = true;
    context.zone.description = "";
    context.zone.has_map = true;
    context.zone.map = "";

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return ctx.object.actionDescription === ''\n"
        "  && ctx.zone.description === ''\n"
        "  && ctx.zone.map === '';",
        context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, ExposesObjectCarriedByWhenPresent)
{
    JsGameTriggerContextFixture context = make_context();
    context.object.has_room = false;
    context.object.has_carried_by = true;
    context.object.carried_by = context.actor;

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return ctx.object.room === null\n"
        "  && ctx.object.carriedBy !== null\n"
        "  && ctx.object.carriedBy.name === 'Builder'\n"
        "  && ctx.object.carriedBy.rank === 4\n"
        "  && ctx.object.carriedBy.isValid() === true\n"
        "  && ctx.object.wornBy === null;",
        context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, ExposesObjectWornByWhenPresent)
{
    JsGameTriggerContextFixture context = make_context();
    context.object.has_room = false;
    context.object.has_worn_by = true;
    context.object.worn_by = context.self;

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return ctx.object.room === null\n"
        "  && ctx.object.carriedBy === null\n"
        "  && ctx.object.wornBy !== null\n"
        "  && ctx.object.wornBy.name === 'Aldren'\n"
        "  && ctx.object.wornBy.room.zone.vnum === 12\n"
        "  && ctx.object.wornBy.isValid() === true;",
        context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, RejectsMutationOfObjectOwnerSnapshots)
{
    JsGameTriggerContextFixture context = make_context();
    context.object.has_carried_by = true;
    context.object.carried_by = context.actor;
    context.object.has_worn_by = true;
    context.object.worn_by = context.self;

    JsGameRuntime runtime;
    JsRuntimeEvalResult assign_result = runtime.evaluate_trigger_body(
        "ctx.object.carriedBy.name = 'changed';\n"
        "return true;",
        context);
    EXPECT_EQ(assign_result.status, JsRuntimeStatus::Error);
    EXPECT_TRUE(assign_result.diagnostic.find("read-only") != std::string::npos ||
                assign_result.diagnostic.find("no setter") != std::string::npos)
        << assign_result.diagnostic;

    JsRuntimeEvalResult proto_result = runtime.evaluate_trigger_body(
        "Object.setPrototypeOf(ctx.object.wornBy, { injected: true });\n"
        "return true;",
        context);
    EXPECT_EQ(proto_result.status, JsRuntimeStatus::Error);
}

TEST(JsGameRuntime, ExposesTriggerSpecificCharacterRoleSnapshots)
{
    JsGameTriggerContextFixture context = make_context();
    context.has_speaker = true;
    context.speaker = context.actor;
    context.has_attacker = true;
    context.attacker = context.actor;
    context.has_victim = true;
    context.victim = context.self;
    context.has_killer = true;
    context.killer = context.actor;
    context.trigger.host_type = "character";

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return ctx.hostType === 'character'\n"
        "  && ctx.speaker.id === 'player:7'\n"
        "  && ctx.attacker.name === 'Builder'\n"
        "  && ctx.victim.name === 'Aldren'\n"
        "  && ctx.killer.name === 'Builder'\n"
        "  && [ctx.self, ctx.speaker, ctx.attacker, ctx.victim, ctx.killer].every(function(character) {\n"
        "    return typeof character.setProfile === 'undefined'\n"
        "      && typeof character.setBaseAbilities === 'undefined'\n"
        "      && typeof character.setCurrentAbilities === 'undefined'\n"
        "      && typeof character.setRolledAbilities === 'undefined'\n"
        "      && typeof character.setPoints === 'undefined'\n"
        "      && typeof character.setSpecials === 'undefined'\n"
        "      && typeof character.setSpecials2 === 'undefined'\n"
        "      && typeof character.setProfessions === 'undefined'\n"
        "      && typeof character.setSpecializations === 'undefined'\n"
        "      && typeof character.setDamageDetails === 'undefined'\n"
        "      && typeof character.setSkill === 'undefined'\n"
        "      && typeof character.setKnowledge === 'undefined'\n"
        "      && typeof character.setRoom === 'undefined'\n"
        "      && typeof character.setAffects === 'undefined'\n"
        "      && typeof character.setEquipmentSlot === 'undefined'\n"
        "      && typeof character.setInventory === 'undefined'\n"
        "      && typeof character.setFollowers === 'undefined'\n"
        "      && typeof character.setMaster === 'undefined'\n"
        "      && typeof character.setMount === 'undefined'\n"
        "      && typeof character.setGroup === 'undefined'\n"
        "      && typeof character.setClassPoints === 'undefined'\n"
        "      && typeof character.setInterruptCount === 'undefined'\n"
        "      && typeof character.setInterruptTime === 'undefined'\n"
        "      && typeof character.setSpecialBusy === 'undefined';\n"
        "  })\n"
        "  && ctx.trigger.hostType === ctx.hostType;",
        context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, ExposesDamageWeaponWhenPresent)
{
    JsGameTriggerContextFixture context = make_context();
    context.has_weapon = true;
    context.weapon = context.object;
    context.trigger.name = "onDamage";
    context.trigger.legacy_name = "ON_DAMAGE";
    context.trigger.legacy_value = 18;

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return ctx.weapon !== null\n"
        "  && ctx.weapon.name === 'silver lever'\n"
        "  && ctx.weapon.room.vnum === 1204\n"
        "  && ctx.weapon.isValid();",
        context);

    expect_ok_allows(result);

    JsRuntimeEvalResult assign_result = runtime.evaluate_trigger_body(
        "ctx.weapon.name = 'changed';\n"
        "return true;",
        context);
    EXPECT_EQ(assign_result.status, JsRuntimeStatus::Error);
    EXPECT_TRUE(assign_result.diagnostic.find("read-only") != std::string::npos ||
                assign_result.diagnostic.find("no setter") != std::string::npos)
        << assign_result.diagnostic;

    JsRuntimeEvalResult proto_result = runtime.evaluate_trigger_body(
        "Object.setPrototypeOf(ctx.weapon, { injected: true });\n"
        "return true;",
        context);
    EXPECT_EQ(proto_result.status, JsRuntimeStatus::Error);
}

TEST(JsGameRuntime, SerializesFalseRoomSunlitState)
{
    JsGameTriggerContextFixture context = make_context();
    context.room.is_sunlit = false;
    context.self.room.is_sunlit = false;
    context.object.room.is_sunlit = false;

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return ctx.room.isSunlit === false\n"
        "  && ctx.self.room.isSunlit === false\n"
        "  && ctx.object.room.isSunlit === false;",
        context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, PreservesBlockingReturnSemantics)
{
    JsGameRuntime runtime;

    JsRuntimeEvalResult result = runtime.evaluate_trigger_body("return false;", make_context());

    EXPECT_EQ(result.status, JsRuntimeStatus::Ok) << result.diagnostic;
    EXPECT_EQ(result.value, JsRuntimeValue::Block);
}

TEST(JsGameRuntime, ExposesScriptResultHelpers)
{
    JsGameRuntime runtime;

    JsRuntimeEvalResult allow_result =
        runtime.evaluate_trigger_body("return RotS.ScriptResult.allow();", make_context());
    JsRuntimeEvalResult block_result =
        runtime.evaluate_trigger_body("return RotS.ScriptResult.block();", make_context());

    expect_ok_allows(allow_result);
    EXPECT_EQ(block_result.status, JsRuntimeStatus::Ok) << block_result.diagnostic;
    EXPECT_EQ(block_result.value, JsRuntimeValue::Block);
}

TEST(JsGameRuntime, KeepsScriptResultHelpersImmutable)
{
    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "try { RotS.ScriptResult.allow = function() { return false; }; } catch (error) {}\n"
        "try { RotS.ScriptResult.extra = true; } catch (error) {}\n"
        "return RotS.ScriptResult.allow() === true && typeof RotS.ScriptResult.extra === 'undefined';",
        make_context());

    expect_ok_allows(result);
}

TEST(JsGameRuntime, ExecutesFirstTextSettersThroughMutationResults)
{
    JsGameTriggerContextFixture context = make_context();
    context.actor.has_room = true;
    context.actor.room = context.room;
    context.has_weapon = true;
    context.weapon.id = "object:weapon";
    context.weapon.name = "iron sword";
    context.weapon.description = "An iron sword rests here.";
    context.weapon.short_description = "an iron sword";
    context.weapon.vnum = 301;
    context.weapon.has_room = true;
    context.weapon.room = context.room;

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "let assignBlocked = false;\n"
        "let defineBlocked = false;\n"
        "try { ctx.object.name = 'unsafe'; } catch (error) { assignBlocked = true; }\n"
        "try { Object.defineProperty(ctx.object, 'name', { value: 'unsafe' }); } catch (error) {\n"
        "  defineBlocked = true;\n"
        "}\n"
        "const objectName = ctx.object.setName('bronze lever');\n"
        "const objectDescription = ctx.object.setDescription('A bronze lever is bolted here.');\n"
        "const objectShort = ctx.object.setShortDescription('a bronze lever');\n"
        "const objectAction = ctx.object.setActionDescription(null);\n"
        "const objectLevelLower = ctx.object.setLevel(0);\n"
        "const objectLevel = ctx.object.setLevel(100);\n"
        "const objectRarityLower = ctx.object.setRarity(0);\n"
        "const objectRarity = ctx.object.setRarity(255);\n"
        "const roomName = ctx.room.setName('Southern Gate');\n"
        "const roomDescription = ctx.room.setDescription('A second gatehouse faces the road.');\n"
        "const roomLevelLower = ctx.room.setLevel(0);\n"
        "const roomLevel = ctx.room.setLevel(100);\n"
        "const roomSector = ctx.room.setSectorType('Water_noswim');\n"
        "const badRoomSectorType = ctx.room.setSectorType(7);\n"
        "const badRoomSectorNull = ctx.room.setSectorType(null);\n"
        "const badRoomSectorUnknown = ctx.room.setSectorType('Unknown');\n"
        "const badRoomSectorLower = ctx.room.setSectorType('water_noswim');\n"
        "const badRoomSectorWhitespace = ctx.room.setSectorType(' Water_noswim');\n"
        "const badRoomSectorTrailing = ctx.room.setSectorType('Water_noswim ');\n"
        "const badRoomSectorDisplay = ctx.room.setSectorType('Water No Swim');\n"
        "const badRoomSectorHyphen = ctx.room.setSectorType('Water-noswim');\n"
        "const badRoomSectorDenseDisplay = ctx.room.setSectorType('Dense Forest');\n"
        "const badRoomSectorDenseCaps = ctx.room.setSectorType('Dense_Forest');\n"
        "const badRoomSectorNumericText = ctx.room.setSectorType('7');\n"
        "const badRoomSectorEmpty = ctx.room.setSectorType('');\n"
        "const zoneName = ctx.zone.setName('New City');\n"
        "const zoneDescription = ctx.zone.setDescription(null);\n"
        "const zoneMap = ctx.zone.setMap('N-G-S-E');\n"
        "const zoneSymbol = ctx.zone.setSymbol('*');\n"
        "const badType = ctx.object.setName(42);\n"
        "const badRange = ctx.room.setName('x'.repeat(300));\n"
        "const badRoomLevelType = ctx.room.setLevel('2');\n"
        "const badRoomLevelFraction = ctx.room.setLevel(1.5);\n"
        "const badRoomLevelNegative = ctx.room.setLevel(-1);\n"
        "const badRoomLevelRange = ctx.room.setLevel(101);\n"
        "const badRoomLevelNaN = ctx.room.setLevel(NaN);\n"
        "const badObjectLevelType = ctx.object.setLevel('2');\n"
        "const badObjectLevelFraction = ctx.object.setLevel(1.5);\n"
        "const badObjectLevelNegative = ctx.object.setLevel(-1);\n"
        "const badObjectLevelRange = ctx.object.setLevel(101);\n"
        "const badObjectLevelNaN = ctx.object.setLevel(NaN);\n"
        "const badObjectRarityType = ctx.object.setRarity('2');\n"
        "const badObjectRarityFraction = ctx.object.setRarity(1.5);\n"
        "const badObjectRarityNegative = ctx.object.setRarity(-1);\n"
        "const badObjectRarityRange = ctx.object.setRarity(256);\n"
        "const badObjectRarityNaN = ctx.object.setRarity(NaN);\n"
        "const badObjectRarityNull = ctx.object.setRarity(null);\n"
        "const blankName = ctx.zone.setName('   ');\n"
        "const badNul = ctx.object.setDescription('bad\\u0000text');\n"
        "const badMapType = ctx.zone.setMap(42);\n"
        "const badMapNul = ctx.zone.setMap('bad\\u0000map');\n"
        "const badMapRange = ctx.zone.setMap('x'.repeat(8193));\n"
        "const badMapTilde = ctx.zone.setMap('bad~map');\n"
        "const badMapHash = ctx.zone.setMap('ok\\n  #31');\n"
        "const badSymbolType = ctx.zone.setSymbol(7);\n"
        "const badSymbolEmpty = ctx.zone.setSymbol('');\n"
        "const badSymbolSpace = ctx.zone.setSymbol(' ');\n"
        "const badSymbolLong = ctx.zone.setSymbol('**');\n"
        "const badSymbolNonAscii = ctx.zone.setSymbol(String.fromCharCode(0xe9));\n"
        "try { Number.isInteger = function() { return false; }; } catch (error) {}\n"
        "try { globalThis.String = function() { return '25'; }; } catch (error) {}\n"
        "const zoneXLower = ctx.zone.setX(0);\n"
        "const zoneX = ctx.zone.setX(25);\n"
        "const zoneYLower = ctx.zone.setY(0);\n"
        "const zoneY = ctx.zone.setY(25);\n"
        "const zoneResetModeLower = ctx.zone.setResetMode(0);\n"
        "const zoneResetMode = ctx.zone.setResetMode(3);\n"
        "const zoneLifespanLower = ctx.zone.setLifespan(1);\n"
        "const zoneLifespan = ctx.zone.setLifespan(10080);\n"
        "const zoneLevelLower = ctx.zone.setLevel(0);\n"
        "const zoneLevel = ctx.zone.setLevel(100);\n"
        "const badXType = ctx.zone.setX('7');\n"
        "const badXFraction = ctx.zone.setX(1.5);\n"
        "const badXNegative = ctx.zone.setX(-1);\n"
        "const badXRange = ctx.zone.setX(26);\n"
        "const badXNaN = ctx.zone.setX(NaN);\n"
        "const badYType = ctx.zone.setY('7');\n"
        "const badYFraction = ctx.zone.setY(1.5);\n"
        "const badYNegative = ctx.zone.setY(-1);\n"
        "const badYRange = ctx.zone.setY(26);\n"
        "const badYNaN = ctx.zone.setY(NaN);\n"
        "const badResetModeType = ctx.zone.setResetMode('2');\n"
        "const badResetModeFraction = ctx.zone.setResetMode(1.5);\n"
        "const badResetModeNegative = ctx.zone.setResetMode(-1);\n"
        "const badResetModeRange = ctx.zone.setResetMode(4);\n"
        "const badResetModeNaN = ctx.zone.setResetMode(NaN);\n"
        "const badLifespanType = ctx.zone.setLifespan('2');\n"
        "const badLifespanFraction = ctx.zone.setLifespan(1.5);\n"
        "const badLifespanZero = ctx.zone.setLifespan(0);\n"
        "const badLifespanNegative = ctx.zone.setLifespan(-1);\n"
        "const badLifespanRange = ctx.zone.setLifespan(10081);\n"
        "const badLifespanNaN = ctx.zone.setLifespan(NaN);\n"
        "const badLevelType = ctx.zone.setLevel('2');\n"
        "const badLevelFraction = ctx.zone.setLevel(1.5);\n"
        "const badLevelNegative = ctx.zone.setLevel(-1);\n"
        "const badLevelRange = ctx.zone.setLevel(101);\n"
        "const badLevelNaN = ctx.zone.setLevel(NaN);\n"
        "const nestedWeapon = ctx.weapon.setName('tempered sword');\n"
        "const nestedWeaponLevel = ctx.weapon.setLevel(77);\n"
        "const nestedWeaponRarity = ctx.weapon.setRarity(201);\n"
        "const nestedObjectRoom = ctx.object.room.setDescription('Nested object room edit.');\n"
        "const nestedObjectRoomLevel = ctx.object.room.setLevel(12);\n"
        "const nestedObjectRoomSector = ctx.object.room.setSectorType('Underwater');\n"
        "const nestedActorRoom = ctx.actor.room.setName('Actor Room Edit');\n"
        "const nestedZone = ctx.room.zone.setName('Nested Zone Edit');\n"
        "const nestedZoneMap = ctx.room.zone.setMap(null);\n"
        "const nestedZoneY = ctx.room.zone.setY(0);\n"
        "const nestedZoneResetMode = ctx.room.zone.setResetMode(2);\n"
        "const nestedZoneLifespan = ctx.room.zone.setLifespan(60);\n"
        "const nestedZoneLevel = ctx.room.zone.setLevel(45);\n"
        "try { badType.code = 'ok'; } catch (error) {}\n"
        "try { Object.defineProperty(badType, 'extra', { value: true }); } catch (error) {}\n"
        "return typeof RotS.MutationResult === 'undefined'\n"
        "  && typeof MutationResult === 'undefined'\n"
        "  && typeof ctx.object.setName === 'function'\n"
        "  && typeof ctx.object.setLevel === 'function'\n"
        "  && typeof ctx.object.setRarity === 'function'\n"
        "  && typeof ctx.object.setFlags === 'undefined'\n"
        "  && typeof ctx.object.setAffects === 'undefined'\n"
        "  && typeof ctx.object.setExtraDescriptions === 'undefined'\n"
        "  && typeof ctx.object.setRoom === 'undefined'\n"
        "  && typeof ctx.object.setCarriedBy === 'undefined'\n"
        "  && typeof ctx.object.setContainer === 'undefined'\n"
        "  && typeof ctx.object.setContents === 'undefined'\n"
        "  && typeof ctx.object.setTouched === 'undefined'\n"
        "  && typeof ctx.object.setLoadedBy === 'undefined'\n"
        "  && typeof ctx.weapon.setName === 'function'\n"
        "  && typeof ctx.weapon.setLevel === 'function'\n"
        "  && typeof ctx.weapon.setRarity === 'function'\n"
        "  && typeof ctx.weapon.setFlags === 'undefined'\n"
        "  && typeof ctx.weapon.setAffects === 'undefined'\n"
        "  && typeof ctx.weapon.setExtraDescriptions === 'undefined'\n"
        "  && typeof ctx.weapon.setRoom === 'undefined'\n"
        "  && typeof ctx.weapon.setCarriedBy === 'undefined'\n"
        "  && typeof ctx.weapon.setContainer === 'undefined'\n"
        "  && typeof ctx.weapon.setContents === 'undefined'\n"
        "  && typeof ctx.weapon.setTouched === 'undefined'\n"
        "  && typeof ctx.weapon.setLoadedBy === 'undefined'\n"
        "  && typeof ctx.actor.setProfile === 'undefined'\n"
        "  && typeof ctx.actor.setBaseAbilities === 'undefined'\n"
        "  && typeof ctx.actor.setCurrentAbilities === 'undefined'\n"
        "  && typeof ctx.actor.setRolledAbilities === 'undefined'\n"
        "  && typeof ctx.actor.setPoints === 'undefined'\n"
        "  && typeof ctx.actor.setSpecials === 'undefined'\n"
        "  && typeof ctx.actor.setSpecials2 === 'undefined'\n"
        "  && typeof ctx.actor.setProfessions === 'undefined'\n"
        "  && typeof ctx.actor.setSpecializations === 'undefined'\n"
        "  && typeof ctx.actor.setDamageDetails === 'undefined'\n"
        "  && typeof ctx.actor.setSkill === 'undefined'\n"
        "  && typeof ctx.actor.setKnowledge === 'undefined'\n"
        "  && typeof ctx.actor.setRoom === 'undefined'\n"
        "  && typeof ctx.actor.setAffects === 'undefined'\n"
        "  && typeof ctx.actor.setEquipmentSlot === 'undefined'\n"
        "  && typeof ctx.actor.setInventory === 'undefined'\n"
        "  && typeof ctx.actor.setFollowers === 'undefined'\n"
        "  && typeof ctx.actor.setMaster === 'undefined'\n"
        "  && typeof ctx.actor.setMount === 'undefined'\n"
        "  && typeof ctx.actor.setGroup === 'undefined'\n"
        "  && typeof ctx.actor.setClassPoints === 'undefined'\n"
        "  && typeof ctx.actor.setInterruptCount === 'undefined'\n"
        "  && typeof ctx.actor.setInterruptTime === 'undefined'\n"
        "  && typeof ctx.actor.setSpecialBusy === 'undefined'\n"
        "  && typeof ctx.object.room.setDescription === 'function'\n"
        "  && typeof ctx.object.room.setLevel === 'function'\n"
        "  && typeof ctx.object.room.setSectorType === 'function'\n"
        "  && typeof ctx.object.room.setExtraDescriptions === 'undefined'\n"
        "  && typeof ctx.object.room.setExit === 'undefined'\n"
        "  && typeof ctx.object.room.setContents === 'undefined'\n"
        "  && typeof ctx.object.room.setCharacters === 'undefined'\n"
        "  && typeof ctx.object.room.setAffects === 'undefined'\n"
        "  && typeof ctx.actor.room.setName === 'function'\n"
        "  && typeof ctx.actor.room.setExtraDescriptions === 'undefined'\n"
        "  && typeof ctx.actor.room.setExit === 'undefined'\n"
        "  && typeof ctx.actor.room.setContents === 'undefined'\n"
        "  && typeof ctx.actor.room.setCharacters === 'undefined'\n"
        "  && typeof ctx.actor.room.setAffects === 'undefined'\n"
        "  && typeof ctx.room.setExtraDescriptions === 'undefined'\n"
        "  && typeof ctx.room.setExit === 'undefined'\n"
        "  && typeof ctx.room.setContents === 'undefined'\n"
        "  && typeof ctx.room.setCharacters === 'undefined'\n"
        "  && typeof ctx.room.setAffects === 'undefined'\n"
        "  && typeof ctx.room.zone.setName === 'function'\n"
        "  && typeof ctx.room.zone.setMap === 'function'\n"
        "  && typeof ctx.room.zone.setSymbol === 'function'\n"
        "  && typeof ctx.room.zone.setX === 'function'\n"
        "  && typeof ctx.room.zone.setY === 'function'\n"
        "  && typeof ctx.room.zone.setResetMode === 'function'\n"
        "  && typeof ctx.room.zone.setLifespan === 'function'\n"
        "  && typeof ctx.room.zone.setLevel === 'function'\n"
        "  && typeof ctx.room.zone.setMinimumLookLevel === 'undefined'\n"
        "  && typeof ctx.room.zone.setAge === 'undefined'\n"
        "  && typeof ctx.room.zone.setTopRoomVnum === 'undefined'\n"
        "  && typeof ctx.room.zone.setVnum === 'undefined'\n"
        "  && typeof ctx.room.zone.setWhitePower === 'undefined'\n"
        "  && typeof ctx.room.zone.setDarkPower === 'undefined'\n"
        "  && typeof ctx.room.zone.setMagiPower === 'undefined'\n"
        "  && typeof ctx.room.zone.setShortDescriptions === 'undefined'\n"
        "  && typeof ctx.room.zone.setExtraDescriptions === 'undefined'\n"
        "  && typeof ctx.room.zone.setMapDescriptions === 'undefined'\n"
        "  && typeof ctx.room.zone.setOwners === 'undefined'\n"
        "  && typeof ctx.room.zone.setResetCommandCount === 'undefined'\n"
        "  && typeof ctx.room.zone.setResetCommands === 'undefined'\n"
        "  && typeof ctx.zone.setMinimumLookLevel === 'undefined'\n"
        "  && typeof ctx.zone.setAge === 'undefined'\n"
        "  && typeof ctx.zone.setTopRoomVnum === 'undefined'\n"
        "  && typeof ctx.zone.setVnum === 'undefined'\n"
        "  && typeof ctx.zone.setWhitePower === 'undefined'\n"
        "  && typeof ctx.zone.setDarkPower === 'undefined'\n"
        "  && typeof ctx.zone.setMagiPower === 'undefined'\n"
        "  && typeof ctx.zone.setShortDescriptions === 'undefined'\n"
        "  && typeof ctx.zone.setExtraDescriptions === 'undefined'\n"
        "  && typeof ctx.zone.setMapDescriptions === 'undefined'\n"
        "  && typeof ctx.zone.setOwners === 'undefined'\n"
        "  && typeof ctx.zone.setResetCommandCount === 'undefined'\n"
        "  && typeof ctx.zone.setResetCommands === 'undefined'\n"
        "  && objectName.ok === true && objectName.code === 'ok'\n"
        "  && objectDescription.ok === true && objectShort.ok === true && objectAction.ok === true\n"
        "  && objectLevelLower.ok === true && objectLevelLower.code === 'ok'\n"
        "  && objectLevelLower.field === 'level'\n"
        "  && objectLevel.ok === true && objectLevel.code === 'ok'\n"
        "  && objectLevel.field === 'level' && objectLevel.message === null\n"
        "  && objectRarityLower.ok === true && objectRarityLower.code === 'ok'\n"
        "  && objectRarityLower.field === 'rarity'\n"
        "  && objectRarity.ok === true && objectRarity.code === 'ok'\n"
        "  && objectRarity.field === 'rarity' && objectRarity.message === null\n"
        "  && roomName.ok === true && roomDescription.ok === true\n"
        "  && roomLevelLower.ok === true && roomLevelLower.code === 'ok'\n"
        "  && roomLevelLower.field === 'level'\n"
        "  && roomLevel.ok === true && roomLevel.code === 'ok'\n"
        "  && roomLevel.field === 'level' && roomLevel.message === null\n"
        "  && roomSector.ok === true && roomSector.code === 'ok'\n"
        "  && roomSector.field === 'sectorType' && roomSector.message === null\n"
        "  && zoneName.ok === true && zoneDescription.ok === true && zoneMap.ok === true\n"
        "  && zoneSymbol.ok === true && zoneSymbol.code === 'ok' && zoneSymbol.field === 'symbol'\n"
        "  && zoneSymbol.message === null\n"
        "  && zoneXLower.ok === true && zoneXLower.code === 'ok' && zoneXLower.field === 'x'\n"
        "  && zoneX.ok === true && zoneX.code === 'ok' && zoneX.field === 'x'\n"
        "  && zoneX.message === null\n"
        "  && zoneYLower.ok === true && zoneYLower.code === 'ok' && zoneYLower.field === 'y'\n"
        "  && zoneY.ok === true && zoneY.code === 'ok' && zoneY.field === 'y'\n"
        "  && zoneY.message === null\n"
        "  && zoneResetModeLower.ok === true && zoneResetModeLower.code === 'ok'\n"
        "  && zoneResetModeLower.field === 'resetMode'\n"
        "  && zoneResetMode.ok === true && zoneResetMode.code === 'ok'\n"
        "  && zoneResetMode.field === 'resetMode' && zoneResetMode.message === null\n"
        "  && zoneLifespanLower.ok === true && zoneLifespanLower.code === 'ok'\n"
        "  && zoneLifespanLower.field === 'lifespan'\n"
        "  && zoneLifespan.ok === true && zoneLifespan.code === 'ok'\n"
        "  && zoneLifespan.field === 'lifespan' && zoneLifespan.message === null\n"
        "  && zoneLevelLower.ok === true && zoneLevelLower.code === 'ok'\n"
        "  && zoneLevelLower.field === 'level'\n"
        "  && zoneLevel.ok === true && zoneLevel.code === 'ok'\n"
        "  && zoneLevel.field === 'level' && zoneLevel.message === null\n"
        "  && badType.ok === false && badType.code === 'invalid-value' && badType.field === 'name'\n"
        "  && badRange.ok === false && badRange.code === 'out-of-range'\n"
        "  && blankName.ok === false && blankName.code === 'invalid-value'\n"
        "  && badNul.ok === false && badNul.code === 'invalid-value'\n"
        "  && badMapType.ok === false && badMapType.field === 'map'\n"
        "  && badMapNul.ok === false && badMapNul.code === 'invalid-value'\n"
        "  && badMapRange.ok === false && badMapRange.code === 'out-of-range'\n"
        "  && badMapTilde.ok === false && badMapTilde.field === 'map'\n"
        "  && badMapHash.ok === false && badMapHash.field === 'map'\n"
        "  && [badSymbolType, badSymbolEmpty, badSymbolSpace, badSymbolLong, badSymbolNonAscii]\n"
        "    .every((result) => result.ok === false && result.code === 'invalid-value'\n"
        "      && result.field === 'symbol' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badRoomLevelType, badRoomLevelFraction, badRoomLevelNaN]\n"
        "    .every((result) => result.ok === false && result.code === 'invalid-value'\n"
        "      && result.field === 'level' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badRoomLevelNegative, badRoomLevelRange]\n"
        "    .every((result) => result.ok === false && result.code === 'out-of-range'\n"
        "      && result.field === 'level' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badRoomSectorType, badRoomSectorNull, badRoomSectorUnknown, badRoomSectorLower, badRoomSectorWhitespace, badRoomSectorTrailing, badRoomSectorDisplay, badRoomSectorHyphen, badRoomSectorDenseDisplay, badRoomSectorDenseCaps, badRoomSectorNumericText, badRoomSectorEmpty]\n"
        "    .every((result) => result.ok === false && result.code === 'invalid-value'\n"
        "      && result.field === 'sectorType' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badObjectLevelType, badObjectLevelFraction, badObjectLevelNaN]\n"
        "    .every((result) => result.ok === false && result.code === 'invalid-value'\n"
        "      && result.field === 'level' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badObjectLevelNegative, badObjectLevelRange]\n"
        "    .every((result) => result.ok === false && result.code === 'out-of-range'\n"
        "      && result.field === 'level' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badObjectRarityType, badObjectRarityFraction, badObjectRarityNaN, badObjectRarityNull]\n"
        "    .every((result) => result.ok === false && result.code === 'invalid-value'\n"
        "      && result.field === 'rarity' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badObjectRarityNegative, badObjectRarityRange]\n"
        "    .every((result) => result.ok === false && result.code === 'out-of-range'\n"
        "      && result.field === 'rarity' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badXType, badXFraction, badXNaN]\n"
        "    .every((result) => result.ok === false && result.code === 'invalid-value'\n"
        "      && result.field === 'x' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badXNegative, badXRange]\n"
        "    .every((result) => result.ok === false && result.code === 'out-of-range'\n"
        "      && result.field === 'x' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badYType, badYFraction, badYNaN]\n"
        "    .every((result) => result.ok === false && result.code === 'invalid-value'\n"
        "      && result.field === 'y' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badYNegative, badYRange]\n"
        "    .every((result) => result.ok === false && result.code === 'out-of-range'\n"
        "      && result.field === 'y' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badResetModeType, badResetModeFraction, badResetModeNaN]\n"
        "    .every((result) => result.ok === false && result.code === 'invalid-value'\n"
        "      && result.field === 'resetMode' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badResetModeNegative, badResetModeRange]\n"
        "    .every((result) => result.ok === false && result.code === 'out-of-range'\n"
        "      && result.field === 'resetMode' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badLifespanType, badLifespanFraction, badLifespanNaN]\n"
        "    .every((result) => result.ok === false && result.code === 'invalid-value'\n"
        "      && result.field === 'lifespan' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badLifespanZero, badLifespanNegative, badLifespanRange]\n"
        "    .every((result) => result.ok === false && result.code === 'out-of-range'\n"
        "      && result.field === 'lifespan' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badLevelType, badLevelFraction, badLevelNaN]\n"
        "    .every((result) => result.ok === false && result.code === 'invalid-value'\n"
        "      && result.field === 'level' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badLevelNegative, badLevelRange]\n"
        "    .every((result) => result.ok === false && result.code === 'out-of-range'\n"
        "      && result.field === 'level' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && nestedWeapon.ok === true && nestedWeaponLevel.ok === true\n"
        "  && nestedWeaponLevel.code === 'ok' && nestedWeaponLevel.field === 'level'\n"
        "  && nestedWeaponRarity.ok === true && nestedWeaponRarity.code === 'ok'\n"
        "  && nestedWeaponRarity.field === 'rarity'\n"
        "  && nestedObjectRoom.ok === true\n"
        "  && nestedObjectRoomLevel.ok === true && nestedObjectRoomLevel.code === 'ok'\n"
        "  && nestedObjectRoomLevel.field === 'level' && nestedObjectRoomLevel.message === null\n"
        "  && nestedObjectRoomSector.ok === true && nestedObjectRoomSector.code === 'ok'\n"
        "  && nestedObjectRoomSector.field === 'sectorType'\n"
        "  && nestedActorRoom.ok === true && nestedZone.ok === true && nestedZoneMap.ok === true\n"
        "  && nestedZoneY.ok === true && nestedZoneY.code === 'ok'\n"
        "  && nestedZoneY.field === 'y' && nestedZoneY.message === null\n"
        "  && nestedZoneResetMode.ok === true && nestedZoneResetMode.code === 'ok'\n"
        "  && nestedZoneResetMode.field === 'resetMode' && nestedZoneResetMode.message === null\n"
        "  && nestedZoneLifespan.ok === true && nestedZoneLifespan.code === 'ok'\n"
        "  && nestedZoneLifespan.field === 'lifespan' && nestedZoneLifespan.message === null\n"
        "  && nestedZoneLevel.ok === true && nestedZoneLevel.code === 'ok'\n"
        "  && nestedZoneLevel.field === 'level' && nestedZoneLevel.message === null\n"
        "  && Object.isFrozen(objectName) && Object.isFrozen(badType)\n"
        "  && typeof objectName.constructor === 'undefined'\n"
        "  && typeof badType.constructor === 'undefined'\n"
        "  && typeof badType.extra === 'undefined'\n"
        "  && typeof ctx.object.setName.prototype === 'undefined'\n"
        "  && Object.isFrozen(ctx.object.setName)\n"
        "  && ctx.object.name === 'bronze lever'\n"
        "  && ctx.object.description === 'A bronze lever is bolted here.'\n"
        "  && ctx.object.shortDescription === 'a bronze lever'\n"
        "  && ctx.object.actionDescription === null\n"
        "  && ctx.object.flags.level === 100\n"
        "  && ctx.object.flags.rarity === 255\n"
        "  && ctx.room.name === 'Southern Gate'\n"
        "  && ctx.room.description === 'A second gatehouse faces the road.'\n"
        "  && ctx.room.level === 100\n"
        "  && ctx.room.sectorType === 'Water_noswim'\n"
        "  && ctx.zone.name === 'New City'\n"
        "  && ctx.zone.description === null\n"
        "  && ctx.zone.map === 'N-G-S-E'\n"
        "  && ctx.zone.symbol === '*'\n"
        "  && ctx.zone.x === 25\n"
        "  && ctx.zone.resetMode === 3\n"
        "  && ctx.zone.lifespan === 10080\n"
        "  && ctx.zone.level === 100\n"
        "  && ctx.weapon.name === 'tempered sword'\n"
        "  && ctx.weapon.flags.level === 77\n"
        "  && ctx.weapon.flags.rarity === 201\n"
        "  && ctx.object.room.description === 'Nested object room edit.'\n"
        "  && ctx.object.room.level === 12\n"
        "  && ctx.object.room.sectorType === 'Underwater'\n"
        "  && ctx.actor.room.name === 'Actor Room Edit'\n"
        "  && ctx.room.zone.name === 'Nested Zone Edit'\n"
        "  && ctx.room.zone.map === null\n"
        "  && ctx.room.zone.y === 0\n"
        "  && ctx.room.zone.resetMode === 2\n"
        "  && ctx.room.zone.lifespan === 60\n"
        "  && ctx.room.zone.level === 45\n"
        "  && assignBlocked\n"
        "  && defineBlocked;",
        context);

    expect_ok_allows(result);

    expect_ok_allows(runtime.evaluate_trigger_body("return ctx.object.name === 'silver lever';", context));
}

TEST(JsGameRuntime, DoesNotExposeInternalMutationEnvelopeToScripts)
{
    JsGameRuntime runtime;
    JsRuntimeEvalResult body_result = runtime.evaluate_trigger_body(
        "let bodyBlocked = false;\n"
        "try { __rotsMutations.push({ targetType: 'object' }); } catch (error) { bodyBlocked = true; }\n"
        "return bodyBlocked\n"
        "  && typeof __rotsJsonStringify === 'undefined'\n"
        "  && typeof __rotsAttachTextSetter === 'undefined'\n"
        "  && typeof __rotsAttachSymbolSetter === 'undefined'\n"
        "  && typeof __rotsAttachCoordinateSetter === 'undefined'\n"
        "  && typeof __rotsAttachResetModeSetter === 'undefined'\n"
        "  && typeof __rotsAttachLifespanSetter === 'undefined'\n"
        "  && typeof __rotsAttachLevelSetter === 'undefined'\n"
        "  && typeof __rotsAttachObjectLevelSetter === 'undefined'\n"
        "  && typeof __rotsAttachObjectRaritySetter === 'undefined'\n"
        "  && typeof __rotsAttachSectorTypeSetter === 'undefined'\n"
        "  && typeof __rotsValidateRaritySetter === 'undefined'\n"
        "  && typeof __rotsValidateSectorTypeSetter === 'undefined'\n"
        "  && typeof __rotsValidateTextSetter === 'undefined'\n"
        "  && typeof __rotsValidateSymbolSetter === 'undefined'\n"
        "  && typeof __rotsValidateCoordinateSetter === 'undefined'\n"
        "  && typeof __rotsValidateResetModeSetter === 'undefined'\n"
        "  && typeof __rotsValidateLifespanSetter === 'undefined'\n"
        "  && typeof __rotsValidateLevelSetter === 'undefined';",
        make_context());
    JsRuntimeEvalResult package_result = runtime.evaluate_trigger_package_handler(
        "exports.onEnter = function(ctx) {\n"
        "  let packageBlocked = false;\n"
        "  try { __rotsMutations.push({ targetType: 'object' }); } catch (error) { packageBlocked = true; }\n"
        "  return packageBlocked\n"
        "    && typeof __rotsJsonStringify === 'undefined'\n"
        "    && typeof __rotsAttachTextSetter === 'undefined'\n"
        "    && typeof __rotsAttachSymbolSetter === 'undefined'\n"
        "    && typeof __rotsAttachCoordinateSetter === 'undefined'\n"
        "    && typeof __rotsAttachResetModeSetter === 'undefined'\n"
        "    && typeof __rotsAttachLifespanSetter === 'undefined'\n"
        "    && typeof __rotsAttachLevelSetter === 'undefined'\n"
        "    && typeof __rotsAttachObjectLevelSetter === 'undefined'\n"
        "    && typeof __rotsAttachObjectRaritySetter === 'undefined'\n"
        "    && typeof __rotsAttachSectorTypeSetter === 'undefined'\n"
        "    && typeof __rotsValidateRaritySetter === 'undefined'\n"
        "    && typeof __rotsValidateSectorTypeSetter === 'undefined'\n"
        "    && typeof __rotsValidateTextSetter === 'undefined'\n"
        "    && typeof __rotsValidateSymbolSetter === 'undefined'\n"
        "    && typeof __rotsValidateCoordinateSetter === 'undefined'\n"
        "    && typeof __rotsValidateResetModeSetter === 'undefined'\n"
        "    && typeof __rotsValidateLifespanSetter === 'undefined'\n"
        "    && typeof __rotsValidateLevelSetter === 'undefined';\n"
        "};\n",
        "onEnter", make_context());

    expect_ok_allows(body_result);
    expect_ok_allows(package_result);
}

TEST(JsGameRuntime, RoomSectorTypeSetterAcceptsEveryCanonicalLiveSectorName)
{
    ASSERT_NE(sector_types, nullptr);
    ASSERT_GT(num_of_sector_types, 0);

    JsGameRuntime runtime;
    for (int sector = 0; sector < num_of_sector_types; ++sector) {
        ASSERT_NE(sector_types[sector], nullptr) << sector;
        const std::string sector_name = sector_types[sector];
        ASSERT_NE(sector_name, "Unknown") << sector;
        JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
            "const result = ctx.room.setSectorType('" + sector_name + "');\n"
            "return result.ok === true && result.code === 'ok' && result.field === 'sectorType'\n"
            "  && result.message === null && ctx.room.sectorType === '" +
                sector_name + "';",
            make_context());
        EXPECT_EQ(result.status, JsRuntimeStatus::Ok) << sector_name << ": " << result.diagnostic;
        EXPECT_EQ(result.value, JsRuntimeValue::Allow) << sector_name;
    }

    JsRuntimeEvalResult unknown_result = runtime.evaluate_trigger_body(
        "const result = ctx.room.setSectorType('Unknown');\n"
        "return result.ok === false && result.code === 'invalid-value'\n"
        "  && result.field === 'sectorType' && ctx.room.sectorType === 'City';",
        make_context());
    expect_ok_allows(unknown_result);
}

TEST(JsGameRuntime, RejectsExcessiveSetterMutationCounts)
{
    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "for (let index = 0; index < 65; index += 1) ctx.object.setDescription('edit ' + index);\n"
        "return true;",
        make_context());

    EXPECT_EQ(result.status, JsRuntimeStatus::Error);
    EXPECT_TRUE(result.mutations.empty());
}

TEST(JsGameRuntime, ExposesNoOpConsoleLogForOfflineParity)
{
    JsGameRuntime runtime;
    JsRuntimeEvalResult result =
        runtime.evaluate_trigger_body("console.log('builder fixture note'); return true;", make_context());

    expect_ok_allows(result);
}

TEST(JsGameRuntime, NormalizesWrapperReturnValues)
{
    JsGameRuntime runtime;

    expect_ok_allows(runtime.evaluate_trigger_body("return true;", make_context()));
    expect_ok_allows(runtime.evaluate_trigger_body("return undefined;", make_context()));
    expect_ok_allows(runtime.evaluate_trigger_body("return {};", make_context()));

    EXPECT_EQ(runtime.evaluate_trigger_body("return 0;", make_context()).value, JsRuntimeValue::Block);
    EXPECT_EQ(runtime.evaluate_trigger_body("return '';", make_context()).value, JsRuntimeValue::Block);
}

TEST(JsGameRuntime, RejectsWrapperBreakoutAttempts)
{
    JsGameRuntime runtime;

    JsRuntimeEvalResult result =
        runtime.evaluate_trigger_body("})(ctx); false; (function(ctx) {", make_context());

    EXPECT_EQ(result.status, JsRuntimeStatus::Error);
    EXPECT_NE(result.diagnostic.find("structurally valid"), std::string::npos);
}

TEST(JsGameRuntime, RejectsMutationOfInjectedContext)
{
    JsGameRuntime runtime;

    JsRuntimeEvalResult assign_result = runtime.evaluate_trigger_body(
        "ctx.self.name = 'changed';\n"
        "return true;",
        make_context());

    EXPECT_EQ(assign_result.status, JsRuntimeStatus::Error);
    EXPECT_NE(assign_result.diagnostic.find("read-only"), std::string::npos)
        << assign_result.diagnostic;

    EXPECT_EQ(runtime.evaluate_trigger_body("ctx.added = true; return true;", make_context()).status,
        JsRuntimeStatus::Error);
    EXPECT_EQ(runtime.evaluate_trigger_body("delete ctx.trigger.name; return true;", make_context()).status,
        JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body(
                      "Object.defineProperty(ctx.trigger, 'name', { value: 'changed' }); return true;",
                      make_context())
                  .status,
        JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("Object.setPrototypeOf(ctx.self, {}); return true;",
                      make_context())
                  .status,
        JsRuntimeStatus::Error);
}

TEST(JsGameRuntime, ContextObjectsHaveNoMutablePrototypeSurface)
{
    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "try { Object.prototype.pointer = 'polluted'; } catch (error) {}\n"
        "try { Object.defineProperty(Object.prototype, 'raw', { get() { return 'polluted'; } }); }"
        " catch (error) {}\n"
        "return !('pointer' in ctx.self)\n"
        "  && !('raw' in ctx.room)\n"
        "  && Object.getPrototypeOf(ctx) === null\n"
        "  && Object.getPrototypeOf(ctx.self) === null\n"
        "  && Object.getPrototypeOf(ctx.self.currentAbilities) === null\n"
        "  && Object.getPrototypeOf(ctx.trigger) === null\n"
        "  && Object.isFrozen(ctx)\n"
        "  && Object.isFrozen(ctx.self)\n"
        "  && Object.isFrozen(ctx.self.currentAbilities)\n"
        "  && Object.isFrozen(ctx.trigger);",
        make_context());

    expect_ok_allows(result);
}

TEST(JsGameRuntime, RejectsMutationOfNestedAbilitySnapshots)
{
    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "ctx.self.currentAbilities.strength = 1;\n"
        "return true;",
        make_context());

    EXPECT_EQ(result.status, JsRuntimeStatus::Error);
}

TEST(JsGameRuntime, ModelsMissingHandlesAsNull)
{
    JsGameTriggerContextFixture context = make_context();
    context.has_self = false;
    context.has_actor = false;
    context.has_speaker = false;
    context.has_attacker = false;
    context.has_victim = false;
    context.has_killer = false;
    context.has_object = false;
    context.has_weapon = false;
    context.has_room = false;
    context.has_zone = false;
    context.has_text = false;
    context.has_wear_slot = false;
    context.has_command = false;
    context.has_args = false;
    context.has_tick = false;
    context.has_direction = false;
    context.has_reverse_direction = false;

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return ctx.self === null\n"
        "  && ctx.actor === null\n"
        "  && ctx.speaker === null\n"
        "  && ctx.attacker === null\n"
        "  && ctx.victim === null\n"
        "  && ctx.killer === null\n"
        "  && ctx.object === null\n"
        "  && ctx.weapon === null\n"
        "  && ctx.room === null\n"
        "  && ctx.zone === null\n"
        "  && ctx.text === null\n"
        "  && ctx.wearSlot === null\n"
        "  && ctx.command === null\n"
        "  && ctx.args === null\n"
        "  && ctx.target === null\n"
        "  && ctx.tick === null\n"
        "  && ctx.direction === null\n"
        "  && ctx.reverseDirection === null\n"
        "  && ctx.targ1 === null\n"
        "  && ctx.targ2 === null\n"
        "  && Array.isArray(ctx.targetTypes)\n"
        "  && ctx.targetTypes.length === 0\n"
        "  && Object.isFrozen(ctx.targetTypes)\n"
        "  && Object.getPrototypeOf(ctx.targetTypes) === Array.prototype\n"
        "  && typeof ctx.targetTypes.constructor === 'undefined'\n"
        "  && ctx.dying === null;",
        context);

    expect_ok_allows(result);

    JsRuntimeEvalResult target_types_mutation_result =
        runtime.evaluate_trigger_body("ctx.targetTypes[0] = 'char'; return true;", context);
    EXPECT_EQ(target_types_mutation_result.status, JsRuntimeStatus::Error);

    JsRuntimeEvalResult dereference_result =
        runtime.evaluate_trigger_body("return ctx.self.name === 'Aldren';", context);
    EXPECT_EQ(dereference_result.status, JsRuntimeStatus::Error);
}

TEST(JsGameRuntime, ExposesWearSlotWhenPresent)
{
    JsGameTriggerContextFixture context = make_context();
    context.has_wear_slot = true;
    context.wear_slot = "head";
    context.trigger.name = "onWear";
    context.trigger.legacy_name = "ON_WEAR";
    context.trigger.legacy_value = 21;

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return ctx.wearSlot === 'head' && Object.isFrozen(ctx);", context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, ExposesScalarCommandAndMovementPayloadsWhenPresent)
{
    JsGameTriggerContextFixture context = make_context();
    context.has_command = true;
    context.command = "open";
    context.has_args = true;
    context.args = "north gate";
    context.has_tick = true;
    context.tick = 42;
    context.has_direction = true;
    context.direction = "north";
    context.has_reverse_direction = true;
    context.reverse_direction = "south";

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return ctx.command === 'open'\n"
        "  && ctx.args === 'north gate'\n"
        "  && ctx.tick === 42\n"
        "  && ctx.direction === 'north'\n"
        "  && ctx.reverseDirection === 'south'\n"
        "  && ctx.target === null\n"
        "  && ctx.dying === null;",
        context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, ExposesTypedTargetsWhenPresent)
{
    JsGameTriggerContextFixture context = make_context();
    context.has_targ1 = true;
    context.targ1.type = "character";
    context.targ1.has_character = true;
    context.targ1.character = context.actor;
    context.targ1.character.id = "targ1";
    context.has_targ2 = true;
    context.targ2.type = "object";
    context.targ2.has_object = true;
    context.targ2.object = context.object;
    context.targ2.object.id = "targ2";
    context.has_target = true;
    context.target = context.targ1;
    context.target.character.id = "target";
    context.target_types = { "character", "object" };
    context.has_dying = true;
    context.dying = context.self;
    context.dying.id = "dying";

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "const characterSetterNames = [\n"
        "  'setProfile', 'setBaseAbilities', 'setCurrentAbilities', 'setRolledAbilities',\n"
        "  'setPoints', 'setSpecials', 'setSpecials2', 'setProfessions',\n"
        "  'setSpecializations', 'setDamageDetails', 'setSkill', 'setKnowledge',\n"
        "  'setRoom', 'setAffects', 'setEquipmentSlot', 'setInventory', 'setFollowers',\n"
        "  'setMaster', 'setMount', 'setGroup', 'setClassPoints', 'setInterruptCount',\n"
        "  'setInterruptTime', 'setSpecialBusy'\n"
        "];\n"
        "const objectSetterNames = [\n"
        "  'setFlags', 'setAffects', 'setExtraDescriptions', 'setContents'\n"
        "];\n"
        "const roomSetterNames = [\n"
        "  'setExtraDescriptions', 'setExit', 'setContents', 'setCharacters', 'setAffects'\n"
        "];\n"
        "return ctx.target.name === 'Builder'\n"
        "  && ctx.target.id === 'target'\n"
        "  && ctx.targ1.name === 'Builder'\n"
        "  && ctx.targ1.id === 'targ1'\n"
        "  && ctx.targ2.name === 'silver lever'\n"
        "  && ctx.targ2.id === 'targ2'\n"
        "  && ctx.dying.name === 'Aldren'\n"
        "  && ctx.dying.id === 'dying'\n"
        "  && ctx.targetTypes[0] === 'character'\n"
        "  && ctx.targetTypes[1] === 'object'\n"
        "  && Object.isFrozen(ctx.target)\n"
        "  && Object.isFrozen(ctx.targ1)\n"
        "  && Object.isFrozen(ctx.targ2)\n"
        "  && Object.isFrozen(ctx.dying)\n"
        "  && Object.isFrozen(ctx.targetTypes)\n"
        "  && Object.getPrototypeOf(ctx.targ1) === null\n"
        "  && Object.getPrototypeOf(ctx.targetTypes) === Array.prototype\n"
        "  && [ctx.target, ctx.targ1, ctx.dying].every(function(character) {\n"
        "    return characterSetterNames.every(function(name) {\n"
        "      return typeof character[name] === 'undefined';\n"
        "    });\n"
        "  })\n"
        "  && objectSetterNames.every(function(name) {\n"
        "    return typeof ctx.targ2[name] === 'undefined';\n"
        "  })\n"
        "  && roomSetterNames.every(function(name) {\n"
        "    return typeof ctx.targ2.room[name] === 'undefined';\n"
        "  })\n"
        "  && typeof ctx.targ1.constructor === 'undefined';",
        context);

    expect_ok_allows(result);

    JsGameTriggerContextFixture room_context = make_context();
    room_context.has_target = true;
    room_context.target.type = "room";
    room_context.target.has_room = true;
    room_context.target.room = room_context.room;
    room_context.target.room.id = "target-room";
    room_context.has_targ2 = true;
    room_context.targ2.type = "room";
    room_context.targ2.has_room = true;
    room_context.targ2.room = room_context.room;
    room_context.targ2.room.id = "targ2-room";

    JsRuntimeEvalResult room_result = runtime.evaluate_trigger_body(
        "const roomSetterNames = [\n"
        "  'setExtraDescriptions', 'setExit', 'setContents', 'setCharacters', 'setAffects'\n"
        "];\n"
        "return ctx.target.id === 'target-room'\n"
        "  && ctx.targ2.id === 'targ2-room'\n"
        "  && Object.isFrozen(ctx.target)\n"
        "  && Object.isFrozen(ctx.targ2)\n"
        "  && [ctx.target, ctx.targ2].every(function(room) {\n"
        "    return roomSetterNames.every(function(name) {\n"
        "      return typeof room[name] === 'undefined';\n"
        "    });\n"
        "  });",
        room_context);

    expect_ok_allows(room_result);

    EXPECT_EQ(runtime.evaluate_trigger_body("ctx.targ1.name = 'mutated'; return true;", context).status,
        JsRuntimeStatus::Error);
    EXPECT_EQ(runtime.evaluate_trigger_body("ctx.targetTypes[0] = 'room'; return true;", context).status,
        JsRuntimeStatus::Error);
    EXPECT_EQ(
        runtime.evaluate_trigger_body("ctx.targ1.room.name = 'mutated'; return true;", context).status,
        JsRuntimeStatus::Error);
}

TEST(JsGameRuntime, TreatsZeroTickAsPresent)
{
    JsGameTriggerContextFixture context = make_context();
    context.has_tick = true;
    context.tick = 0;

    JsGameRuntime runtime;
    JsRuntimeEvalResult result =
        runtime.evaluate_trigger_body("return ctx.tick === 0;", context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, EscapesFixtureStringsBeforeEvaluation)
{
    JsGameTriggerContextFixture context = make_context();
    context.self.name = "self \"quoted\" \\ name";
    context.actor.race = "race\x01value";
    context.object.name = "object\nname";
    context.room.name = "room\rname";
    context.zone.name = "zone\tname";
    context.trigger.name = "trigger `name`";
    context.trigger.legacy_name = "ON_\"PULL\"";
    context.text = "line one\r\nline \"two\" \\\\ end";
    context.has_command = true;
    context.command = "cmd\nname";
    context.has_args = true;
    context.args = "arg \"quoted\"";
    context.has_direction = true;
    context.direction = "north\r";
    context.has_reverse_direction = true;
    context.reverse_direction = "south\t";

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return ctx.self.name === 'self \"quoted\" \\\\ name'\n"
        "  && ctx.actor.race.charCodeAt(4) === 1\n"
        "  && ctx.object.name === 'object\\nname'\n"
        "  && ctx.room.name === 'room\\rname'\n"
        "  && ctx.zone.name === 'zone\\tname'\n"
        "  && ctx.trigger.name === 'trigger `name`'\n"
        "  && ctx.trigger.handlerName === 'trigger `name`'\n"
        "  && ctx.trigger.kind === 'legacy'\n"
        "  && ctx.trigger.legacyName === 'ON_\"PULL\"'\n"
        "  && ctx.text.indexOf('line one\\r\\n') === 0\n"
        "  && ctx.text.includes('line \"two\"')\n"
        "  && ctx.text.includes('\\\\\\\\ end')\n"
        "  && ctx.command === 'cmd\\nname'\n"
        "  && ctx.args === 'arg \"quoted\"'\n"
        "  && ctx.direction === 'north\\r'\n"
        "  && ctx.reverseDirection === 'south\\t';",
        context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, InheritsRuntimeInstructionLimits)
{
    JsRuntimeLimits limits;
    limits.instruction_budget = 64;
    JsGameRuntime runtime(limits);

    JsRuntimeEvalResult result = runtime.evaluate_trigger_body("while (true) {}", make_context());

    EXPECT_EQ(result.status, JsRuntimeStatus::Interrupted);
}

TEST(JsGameRuntime, DoesNotPersistScriptStateAcrossEvaluations)
{
    JsGameRuntime runtime;

    JsRuntimeEvalResult first = runtime.evaluate_trigger_body(
        "globalThis.process = { env: {} };\n"
        "try { Object.prototype.pointer = 'polluted'; } catch (error) {}\n"
        "return true;",
        make_context());
    EXPECT_EQ(first.status, JsRuntimeStatus::Ok) << first.diagnostic;

    JsRuntimeEvalResult second = runtime.evaluate_trigger_body(
        "return typeof process === 'undefined' && !('pointer' in ctx.self);", make_context());
    expect_ok_allows(second);
}

TEST(JsGameRuntime, DoesNotExposeRawPointersOrProcessGlobals)
{
    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return typeof ctx.self.pointer === 'undefined'\n"
        "  && typeof ctx.self.address === 'undefined'\n"
        "  && typeof ctx.room.raw === 'undefined'\n"
        "  && typeof ctx.self.constructor === 'undefined'\n"
        "  && typeof ctx.self.isValid.constructor === 'undefined'\n"
        "  && typeof ctx.trigger.toString === 'undefined'\n"
        "  && typeof RotS.ScriptResult.allow.constructor === 'undefined'\n"
        "  && typeof process === 'undefined'\n"
        "  && typeof require === 'undefined'\n"
        "  && typeof ({}).constructor === 'undefined'\n"
        "  && typeof (function() {}).constructor === 'undefined';",
        make_context());

    expect_ok_allows(result);
}

TEST(JsGameRuntime, RedactsThrownActorTextFromDiagnostics)
{
    JsGameTriggerContextFixture context = make_context();
    context.text = "private player text\r\nwith newline";

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body("throw ctx.text;", context);

    EXPECT_EQ(result.status, JsRuntimeStatus::Error);
    EXPECT_EQ(result.diagnostic.find("private player text"), std::string::npos);
    EXPECT_EQ(result.diagnostic.find('\n'), std::string::npos);
    EXPECT_LE(result.diagnostic.size(), 120);
}

TEST(JsGameRuntime, BuildsStableContextLiteral)
{
    std::string literal = js_game_trigger_context_literal(make_context());

    EXPECT_NE(literal.find("\"self\":{\"id\":\"char:1001\""), std::string::npos);
    EXPECT_NE(literal.find("\"experience\":42000"), std::string::npos);
    EXPECT_NE(literal.find("\"rank\":9"), std::string::npos);
    EXPECT_NE(literal.find("\"room\":{\"id\":\"room:1204\""), std::string::npos);
    EXPECT_NE(literal.find("\"isSunlit\":true"), std::string::npos);
    EXPECT_NE(literal.find("\"sectorType\":\"City\""), std::string::npos);
    EXPECT_NE(literal.find("\"flags\":[\"dark\",\"indoors\"]"), std::string::npos);
    EXPECT_NE(literal.find("\"light\":1"), std::string::npos);
    EXPECT_NE(literal.find("\"zone\":{\"id\":\"zone:12\""), std::string::npos);
    EXPECT_NE(literal.find("\"isValid\":function() { return true; }"), std::string::npos);
    EXPECT_NE(literal.find("\"object\":{\"id\":\"object:300\""), std::string::npos);
    EXPECT_NE(literal.find("\"object\":{\"id\":\"object:300\",\"name\":\"silver lever\","
                           "\"description\":\"A silver lever is bolted to the wall.\","
                           "\"shortDescription\":\"a silver lever\","
                           "\"actionDescription\":\"The lever clicks under your hand.\","
                           "\"vnum\":300,"
                           "\"flags\":{\"itemType\":\"weapon\",\"wearFlags\":[\"take\",\"wield\"],"
                           "\"extraFlags\":[\"glow\",\"magic\"],\"level\":12,\"weight\":7,"
                           "\"cost\":450,\"costPerDay\":15,\"timer\":30,\"rarity\":2,"
                           "\"material\":\"metal\"},"
                           "\"room\":{\"id\":\"room:1204\""),
        std::string::npos);
    EXPECT_NE(literal.find("\"description\":\"The old city zone.\""), std::string::npos);
    EXPECT_NE(literal.find("\"map\":\"N-G-S\""), std::string::npos);
    EXPECT_NE(literal.find("\"topRoomVnum\":1299"), std::string::npos);
    EXPECT_NE(literal.find("\"minimumLookLevel\":2"), std::string::npos);
    EXPECT_NE(literal.find("\"resetMode\":1"), std::string::npos);
    EXPECT_NE(literal.find("\"speaker\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"attacker\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"victim\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"killer\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"weapon\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"wearSlot\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"command\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"args\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"target\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"tick\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"direction\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"reverseDirection\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"targ1\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"targ2\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"targetTypes\":[]"), std::string::npos);
    EXPECT_NE(literal.find("\"dying\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"hostType\":\"object\""), std::string::npos);
    EXPECT_NE(literal.find("\"kind\":\"legacy\""), std::string::npos);
    EXPECT_NE(literal.find("\"handlerName\":\"onPull\""), std::string::npos);
    EXPECT_NE(literal.find("\"zone\":{\"id\":\"zone:12\""), std::string::npos);
    EXPECT_NE(literal.find("\"legacyName\":\"ON_PULL\""), std::string::npos);
    EXPECT_EQ(count_occurrences(literal, "\"command\":"), 1);
    EXPECT_EQ(count_occurrences(literal, "\"args\":"), 1);
    EXPECT_EQ(count_occurrences(literal, "\"target\":"), 1);
    EXPECT_EQ(count_occurrences(literal, "\"tick\":"), 1);
    EXPECT_EQ(count_occurrences(literal, "\"direction\":"), 1);
    EXPECT_EQ(count_occurrences(literal, "\"reverseDirection\":"), 1);
    EXPECT_EQ(count_occurrences(literal, "\"targ1\":"), 1);
    EXPECT_EQ(count_occurrences(literal, "\"targ2\":"), 1);
    EXPECT_EQ(count_occurrences(literal, "\"targetTypes\":"), 1);
    EXPECT_EQ(count_occurrences(literal, "\"dying\":"), 1);
    EXPECT_EQ(literal.find("char_data"), std::string::npos);
    EXPECT_EQ(literal.find("obj_data"), std::string::npos);
}

TEST(JsGameRuntime, EmitsMudlleTriggerKindForSpecialCallFlags)
{
    JsGameTriggerContextFixture context = make_context();
    context.trigger.name = "onMudlleCommand";
    context.trigger.legacy_name = "SPECIAL_COMMAND";
    context.trigger.host_type = "mudlleMobile";

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return ctx.trigger.kind === 'mudlle'\n"
        "  && ctx.trigger.handlerName === 'onMudlleCommand'\n"
        "  && ctx.trigger.legacyName === 'SPECIAL_COMMAND';",
        context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, DispatchesCompiledCommonJsExports)
{
    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_package_handler(
        "\"use strict\";\n"
        "Object.defineProperty(exports, \"__esModule\", { value: true });\n"
        "exports.onEnter = onEnter;\n"
        "function onEnter(ctx) { return RotS.ScriptResult.block(); }\n",
        "onEnter", make_context());

    EXPECT_EQ(result.status, JsRuntimeStatus::Ok) << result.diagnostic;
    EXPECT_EQ(result.value, JsRuntimeValue::Block);
}

TEST(JsGameRuntime, PrefersCompiledExportOverGlobalHandlerFallback)
{
    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_package_handler(
        "exports.onEnter = function(ctx) { return RotS.ScriptResult.allow(); };\n"
        "function onEnter(ctx) { return RotS.ScriptResult.block(); }\n",
        "onEnter", make_context());

    expect_ok_allows(result);
}

TEST(JsGameRuntime, RejectsUnsafePackageHandlerNamesBeforeEvaluation)
{
    JsGameRuntime runtime;
    JsRuntimeEvalResult result =
        runtime.evaluate_trigger_package_handler("throw new Error('should not run');", "onEnter(); evil",
            make_context());

    EXPECT_EQ(result.status, JsRuntimeStatus::Error);
    EXPECT_EQ(result.diagnostic, "JavaScript game handler name is not a safe identifier");
}
