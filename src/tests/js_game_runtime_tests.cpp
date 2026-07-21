#include "../js_game_runtime.h"

#include <gtest/gtest.h>

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
        "  && ctx.self.isValid() === true\n"
        "  && ctx.self.room.vnum === 1204\n"
        "  && ctx.self.room.isSunlit === true\n"
        "  && ctx.self.room.zone.name === 'Old City'\n"
        "  && ctx.actor.race === 'Elf'\n"
        "  && ctx.actor.experience === 31000\n"
        "  && ctx.actor.rank === 4\n"
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
        "  && ctx.zone.minimumLookLevel === 2\n"
        "  && ctx.zone.resetMode === 1\n"
        "  && ctx.object.room.description === ctx.room.description\n"
        "  && ctx.object.room.sectorType === ctx.room.sectorType\n"
        "  && ctx.object.room.flags.join(',') === ctx.room.flags.join(',')\n"
        "  && ctx.object.room.light === ctx.room.light\n"
        "  && ctx.object.room.zone.description === ctx.zone.description\n"
        "  && ctx.object.room.zone.map === ctx.zone.map\n"
        "  && ctx.object.room.zone.level === ctx.zone.level;",
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
        "const roomName = ctx.room.setName('Southern Gate');\n"
        "const roomDescription = ctx.room.setDescription('A second gatehouse faces the road.');\n"
        "const zoneName = ctx.zone.setName('New City');\n"
        "const zoneDescription = ctx.zone.setDescription(null);\n"
        "const zoneMap = ctx.zone.setMap('N-G-S-E');\n"
        "const badType = ctx.object.setName(42);\n"
        "const badRange = ctx.room.setName('x'.repeat(300));\n"
        "const blankName = ctx.zone.setName('   ');\n"
        "const badNul = ctx.object.setDescription('bad\\u0000text');\n"
        "const badMapType = ctx.zone.setMap(42);\n"
        "const badMapNul = ctx.zone.setMap('bad\\u0000map');\n"
        "const badMapRange = ctx.zone.setMap('x'.repeat(8193));\n"
        "const badMapTilde = ctx.zone.setMap('bad~map');\n"
        "const badMapHash = ctx.zone.setMap('ok\\n  #31');\n"
        "const nestedWeapon = ctx.weapon.setName('tempered sword');\n"
        "const nestedObjectRoom = ctx.object.room.setDescription('Nested object room edit.');\n"
        "const nestedActorRoom = ctx.actor.room.setName('Actor Room Edit');\n"
        "const nestedZone = ctx.room.zone.setName('Nested Zone Edit');\n"
        "const nestedZoneMap = ctx.room.zone.setMap(null);\n"
        "try { badType.code = 'ok'; } catch (error) {}\n"
        "try { Object.defineProperty(badType, 'extra', { value: true }); } catch (error) {}\n"
        "return typeof RotS.MutationResult === 'undefined'\n"
        "  && typeof MutationResult === 'undefined'\n"
        "  && typeof ctx.object.setName === 'function'\n"
        "  && typeof ctx.weapon.setName === 'function'\n"
        "  && typeof ctx.object.room.setDescription === 'function'\n"
        "  && typeof ctx.actor.room.setName === 'function'\n"
        "  && typeof ctx.room.zone.setName === 'function'\n"
        "  && typeof ctx.room.zone.setMap === 'function'\n"
        "  && !('setLevel' in ctx.zone)\n"
        "  && !('setX' in ctx.zone)\n"
        "  && !('setY' in ctx.zone)\n"
        "  && !('setSymbol' in ctx.zone)\n"
        "  && objectName.ok === true && objectName.code === 'ok'\n"
        "  && objectDescription.ok === true && objectShort.ok === true && objectAction.ok === true\n"
        "  && roomName.ok === true && roomDescription.ok === true\n"
        "  && zoneName.ok === true && zoneDescription.ok === true && zoneMap.ok === true\n"
        "  && badType.ok === false && badType.code === 'invalid-value' && badType.field === 'name'\n"
        "  && badRange.ok === false && badRange.code === 'out-of-range'\n"
        "  && blankName.ok === false && blankName.code === 'invalid-value'\n"
        "  && badNul.ok === false && badNul.code === 'invalid-value'\n"
        "  && badMapType.ok === false && badMapType.field === 'map'\n"
        "  && badMapNul.ok === false && badMapNul.code === 'invalid-value'\n"
        "  && badMapRange.ok === false && badMapRange.code === 'out-of-range'\n"
        "  && badMapTilde.ok === false && badMapTilde.field === 'map'\n"
        "  && badMapHash.ok === false && badMapHash.field === 'map'\n"
        "  && nestedWeapon.ok === true && nestedObjectRoom.ok === true\n"
        "  && nestedActorRoom.ok === true && nestedZone.ok === true && nestedZoneMap.ok === true\n"
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
        "  && ctx.room.name === 'Southern Gate'\n"
        "  && ctx.room.description === 'A second gatehouse faces the road.'\n"
        "  && ctx.zone.name === 'New City'\n"
        "  && ctx.zone.description === null\n"
        "  && ctx.zone.map === 'N-G-S-E'\n"
        "  && ctx.weapon.name === 'tempered sword'\n"
        "  && ctx.object.room.description === 'Nested object room edit.'\n"
        "  && ctx.actor.room.name === 'Actor Room Edit'\n"
        "  && ctx.room.zone.name === 'Nested Zone Edit'\n"
        "  && ctx.room.zone.map === null\n"
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
        "return bodyBlocked && typeof __rotsJsonStringify === 'undefined';",
        make_context());
    JsRuntimeEvalResult package_result = runtime.evaluate_trigger_package_handler(
        "exports.onEnter = function(ctx) {\n"
        "  let packageBlocked = false;\n"
        "  try { __rotsMutations.push({ targetType: 'object' }); } catch (error) { packageBlocked = true; }\n"
        "  return packageBlocked && typeof __rotsJsonStringify === 'undefined';\n"
        "};\n",
        "onEnter", make_context());

    expect_ok_allows(body_result);
    expect_ok_allows(package_result);
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
        "  && Object.getPrototypeOf(ctx.trigger) === null\n"
        "  && Object.isFrozen(ctx)\n"
        "  && Object.isFrozen(ctx.self)\n"
        "  && Object.isFrozen(ctx.trigger);",
        make_context());

    expect_ok_allows(result);
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
        "  && typeof ctx.targ1.constructor === 'undefined';",
        context);

    expect_ok_allows(result);

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
