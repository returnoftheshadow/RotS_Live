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
    context.self.room.vnum = 1204;
    context.self.room.has_zone = true;
    context.self.room.zone.id = "zone:12";
    context.self.room.zone.name = "Old City";
    context.self.room.zone.vnum = 12;

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
    context.object.vnum = 300;

    context.has_room = true;
    context.room.id = "room:1204";
    context.room.name = "Northern Gate";
    context.room.vnum = 1204;
    context.room.has_zone = true;
    context.room.zone.id = "zone:12";
    context.room.zone.name = "Old City";
    context.room.zone.vnum = 12;

    context.has_zone = true;
    context.zone.id = "zone:12";
    context.zone.name = "Old City";
    context.zone.vnum = 12;

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
        "  && ctx.self.room.zone.name === 'Old City'\n"
        "  && ctx.actor.race === 'Elf'\n"
        "  && ctx.actor.experience === 31000\n"
        "  && ctx.actor.rank === 4\n"
        "  && ctx.object.vnum === 300\n"
        "  && ctx.object.isValid() === true\n"
        "  && ctx.room.name === 'Northern Gate'\n"
        "  && ctx.room.zone.vnum === 12\n"
        "  && ctx.room.isValid() === true\n"
        "  && ctx.zone.vnum === 12\n"
        "  && ctx.trigger.name === 'onPull'\n"
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
    context.has_object = false;
    context.has_room = false;
    context.has_zone = false;
    context.has_text = false;

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return ctx.self === null\n"
        "  && ctx.actor === null\n"
        "  && ctx.object === null\n"
        "  && ctx.room === null\n"
        "  && ctx.zone === null\n"
        "  && ctx.text === null;",
        context);

    expect_ok_allows(result);

    JsRuntimeEvalResult dereference_result =
        runtime.evaluate_trigger_body("return ctx.self.name === 'Aldren';", context);
    EXPECT_EQ(dereference_result.status, JsRuntimeStatus::Error);
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

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return ctx.self.name === 'self \"quoted\" \\\\ name'\n"
        "  && ctx.actor.race.charCodeAt(4) === 1\n"
        "  && ctx.object.name === 'object\\nname'\n"
        "  && ctx.room.name === 'room\\rname'\n"
        "  && ctx.zone.name === 'zone\\tname'\n"
        "  && ctx.trigger.name === 'trigger `name`'\n"
        "  && ctx.trigger.legacyName === 'ON_\"PULL\"'\n"
        "  && ctx.text.indexOf('line one\\r\\n') === 0\n"
        "  && ctx.text.includes('line \"two\"')\n"
        "  && ctx.text.includes('\\\\\\\\ end');",
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
    EXPECT_NE(literal.find("\"zone\":{\"id\":\"zone:12\""), std::string::npos);
    EXPECT_NE(literal.find("\"isValid\":function() { return true; }"), std::string::npos);
    EXPECT_NE(literal.find("\"object\":{\"id\":\"object:300\""), std::string::npos);
    EXPECT_NE(literal.find("\"room\":{\"id\":\"room:1204\""), std::string::npos);
    EXPECT_NE(literal.find("\"zone\":{\"id\":\"zone:12\""), std::string::npos);
    EXPECT_NE(literal.find("\"legacyName\":\"ON_PULL\""), std::string::npos);
    EXPECT_EQ(literal.find("char_data"), std::string::npos);
    EXPECT_EQ(literal.find("obj_data"), std::string::npos);
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
