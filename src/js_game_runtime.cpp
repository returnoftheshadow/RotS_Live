#include "js_game_runtime.h"

#include <cctype>
#include <sstream>

namespace {

constexpr std::size_t MaxGameDiagnosticLength = 120;

std::string js_quote(const std::string& value)
{
    std::ostringstream out;
    out << '"';
    for (unsigned char ch : value) {
        switch (ch) {
        case '\\':
            out << "\\\\";
            break;
        case '"':
            out << "\\\"";
            break;
        case '\b':
            out << "\\b";
            break;
        case '\f':
            out << "\\f";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (ch < 0x20 || ch >= 0x80) {
                static const char* hex = "0123456789abcdef";
                out << "\\x" << hex[(ch >> 4) & 0x0f] << hex[ch & 0x0f];
            } else {
                out << ch;
            }
            break;
        }
    }
    out << '"';
    return out.str();
}

const char* js_bool(bool value)
{
    return value ? "true" : "false";
}

std::string nullable_literal(bool present, const std::string& literal)
{
    return present ? literal : "null";
}

std::string zone_literal(const JsGameZoneFixture& zone)
{
    std::ostringstream out;
    out << "{"
        << "\"id\":" << js_quote(zone.id) << ","
        << "\"name\":" << js_quote(zone.name) << ","
        << "\"description\":" << nullable_literal(zone.has_description, js_quote(zone.description))
        << ","
        << "\"map\":" << nullable_literal(zone.has_map, js_quote(zone.map)) << ","
        << "\"vnum\":" << zone.vnum << ","
        << "\"level\":" << zone.level << ","
        << "\"lifespan\":" << zone.lifespan << ","
        << "\"age\":" << zone.age << ","
        << "\"topRoomVnum\":" << zone.top_room_vnum << ","
        << "\"x\":" << zone.x << ","
        << "\"y\":" << zone.y << ","
        << "\"symbol\":" << js_quote(zone.symbol) << ","
        << "\"minimumLookLevel\":" << zone.minimum_look_level << ","
        << "\"resetMode\":" << zone.reset_mode << "}";
    return out.str();
}

std::string room_literal(const JsGameRoomFixture& room)
{
    std::ostringstream out;
    out << "{"
        << "\"id\":" << js_quote(room.id) << ","
        << "\"name\":" << js_quote(room.name) << ","
        << "\"description\":" << js_quote(room.description) << ","
        << "\"vnum\":" << room.vnum << ","
        << "\"level\":" << room.level << ","
        << "\"alignment\":" << room.alignment << ","
        << "\"isSunlit\":" << js_bool(room.is_sunlit) << ","
        << "\"zone\":" << nullable_literal(room.has_zone, zone_literal(room.zone)) << ","
        << "\"isValid\":function() { return true; }"
        << "}";
    return out.str();
}

std::string character_literal(const JsGameCharacterFixture& character)
{
    std::ostringstream out;
    out << "{"
        << "\"id\":" << js_quote(character.id) << ","
        << "\"name\":" << js_quote(character.name) << ","
        << "\"race\":" << js_quote(character.race) << ","
        << "\"vnum\":";
    if (character.vnum >= 0)
        out << character.vnum;
    else
        out << "null";
    out << ","
        << "\"prototypeVnum\":";
    if (character.prototype_vnum >= 0)
        out << character.prototype_vnum;
    else
        out << "null";
    out << ","
        << "\"level\":" << character.level << ","
        << "\"experience\":" << character.experience << ","
        << "\"rank\":" << character.rank << ","
        << "\"hitPoints\":" << character.hit_points << ","
        << "\"maxHitPoints\":" << character.max_hit_points << ","
        << "\"isNpc\":" << js_bool(character.is_npc) << ","
        << "\"isPlayer\":" << js_bool(!character.is_npc) << ","
        << "\"room\":" << nullable_literal(character.has_room, room_literal(character.room)) << ","
        << "\"isValid\":function() { return true; }"
        << "}";
    return out.str();
}

std::string object_literal(const JsGameObjectFixture& object)
{
    std::ostringstream out;
    out << "{"
        << "\"id\":" << js_quote(object.id) << ","
        << "\"name\":" << js_quote(object.name) << ","
        << "\"description\":" << js_quote(object.description) << ","
        << "\"shortDescription\":" << js_quote(object.short_description) << ","
        << "\"actionDescription\":"
        << nullable_literal(object.has_action_description, js_quote(object.action_description)) << ","
        << "\"vnum\":";
    if (object.vnum >= 0)
        out << object.vnum;
    else
        out << "null";
    out << ","
        << "\"room\":" << nullable_literal(object.has_room, room_literal(object.room)) << ","
        << "\"carriedBy\":"
        << nullable_literal(object.has_carried_by, character_literal(object.carried_by)) << ","
        << "\"wornBy\":" << nullable_literal(object.has_worn_by, character_literal(object.worn_by))
        << ","
        << "\"isValid\":function() { return true; }"
        << "}";
    return out.str();
}

std::string trigger_literal(const JsGameTriggerFixture& trigger)
{
    const char* trigger_kind = trigger.legacy_name.rfind("SPECIAL_", 0) == 0 ? "mudlle" : "legacy";
    std::ostringstream out;
    out << "{"
        << "\"kind\":" << js_quote(trigger_kind) << ","
        << "\"name\":" << js_quote(trigger.name) << ","
        << "\"handlerName\":" << js_quote(trigger.name) << ","
        << "\"legacyName\":" << js_quote(trigger.legacy_name) << ","
        << "\"hostType\":" << js_quote(trigger.host_type) << ","
        << "\"legacyValue\":" << trigger.legacy_value << ","
        << "\"blocksGameplay\":" << js_bool(trigger.blocks_gameplay) << "}";
    return out.str();
}

std::string target_literal(const JsGameTargetFixture& target)
{
    if (target.has_character)
        return character_literal(target.character);
    if (target.has_object)
        return object_literal(target.object);
    if (target.has_room)
        return room_literal(target.room);
    return "null";
}

std::string target_types_literal(const std::vector<std::string>& target_types)
{
    std::ostringstream out;
    out << "[";
    for (std::size_t index = 0; index < target_types.size(); ++index) {
        if (index > 0)
            out << ",";
        out << js_quote(target_types[index]);
    }
    out << "]";
    return out.str();
}

bool source_has_unsafe_wrapper_boundary(const std::string& source)
{
    int brace_depth = 0;
    int paren_depth = 0;
    int bracket_depth = 0;
    char string_quote = '\0';
    bool escaped = false;
    bool in_line_comment = false;
    bool in_block_comment = false;

    for (std::size_t i = 0; i < source.size(); ++i) {
        char ch = source[i];
        char next = i + 1 < source.size() ? source[i + 1] : '\0';

        if (in_line_comment) {
            if (ch == '\n' || ch == '\r')
                in_line_comment = false;
            continue;
        }

        if (in_block_comment) {
            if (ch == '*' && next == '/') {
                in_block_comment = false;
                ++i;
            }
            continue;
        }

        if (string_quote != '\0') {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == string_quote) {
                string_quote = '\0';
            }
            continue;
        }

        if (ch == '/' && next == '/') {
            in_line_comment = true;
            ++i;
            continue;
        }

        if (ch == '/' && next == '*') {
            in_block_comment = true;
            ++i;
            continue;
        }

        if (ch == '\'' || ch == '"' || ch == '`') {
            string_quote = ch;
            continue;
        }

        if (ch == '{') {
            ++brace_depth;
        } else if (ch == '}') {
            if (brace_depth == 0)
                return true;
            --brace_depth;
        } else if (ch == '(') {
            ++paren_depth;
        } else if (ch == ')') {
            if (paren_depth == 0)
                return true;
            --paren_depth;
        } else if (ch == '[') {
            ++bracket_depth;
        } else if (ch == ']') {
            if (bracket_depth == 0)
                return true;
            --bracket_depth;
        }
    }

    return string_quote != '\0' || in_block_comment || brace_depth != 0 || paren_depth != 0 || bracket_depth != 0;
}

bool is_identifier_start(unsigned char ch)
{
    return std::isalpha(ch) || ch == '_' || ch == '$';
}

bool is_identifier_continue(unsigned char ch)
{
    return is_identifier_start(ch) || std::isdigit(ch);
}

bool is_safe_handler_identifier(const std::string& handler_name)
{
    if (handler_name.empty())
        return false;
    if (!is_identifier_start(static_cast<unsigned char>(handler_name[0])))
        return false;
    for (std::size_t index = 1; index < handler_name.size(); ++index) {
        if (!is_identifier_continue(static_cast<unsigned char>(handler_name[index])))
            return false;
    }
    return true;
}

std::string trigger_context_preamble(const JsGameTriggerContextFixture& context)
{
    std::ostringstream wrapped;
    wrapped << "'use strict';\n"
            << "delete Object.prototype.constructor;\n"
            << "const __rotsFunctionPrototype = Object.getPrototypeOf(function() {});\n"
            << "delete __rotsFunctionPrototype.constructor;\n"
            << "Object.freeze(Object.prototype);\n"
            << "Object.freeze(__rotsFunctionPrototype);\n"
            << "function __rotsDeepFreeze(value) {\n"
            << "  if (value && (typeof value === 'object' || typeof value === 'function') && !Object.isFrozen(value)) {\n"
            << "    if (typeof value === 'object') Object.setPrototypeOf(value, null);\n"
            << "    Object.freeze(value);\n"
            << "    for (const key of Object.keys(value)) __rotsDeepFreeze(value[key]);\n"
            << "  }\n"
            << "  return value;\n"
            << "}\n"
            << "const console = __rotsDeepFreeze({ log: function() { return undefined; } });\n"
            << "const RotS = __rotsDeepFreeze({\n"
            << "  ScriptResult: {\n"
            << "    allow: function() { return true; },\n"
            << "    block: function() { return false; }\n"
            << "  }\n"
            << "});\n"
            << "const ctx = __rotsDeepFreeze(" << js_game_trigger_context_literal(context) << ");\n";
    return wrapped.str();
}

JsRuntimeEvalResult sanitize_game_result(JsRuntimeEvalResult result)
{
    if (result.status == JsRuntimeStatus::Error && result.diagnostic.size() > MaxGameDiagnosticLength)
        result.diagnostic.resize(MaxGameDiagnosticLength);
    if (result.status == JsRuntimeStatus::Error && result.diagnostic.find("TypeError:") != 0 && result.diagnostic.find("SyntaxError:") != 0) {
        result.diagnostic = "JavaScript game script failed";
    }
    return result;
}

} // namespace

JsGameRuntime::JsGameRuntime(const JsRuntimeLimits& limits)
    : m_limits(limits)
{
}

JsRuntimeEvalResult JsGameRuntime::evaluate_trigger_body(const std::string& source,
    const JsGameTriggerContextFixture& context, const char* filename)
{
    if (source_has_unsafe_wrapper_boundary(source)) {
        JsRuntimeEvalResult result;
        result.status = JsRuntimeStatus::Error;
        result.diagnostic = "JavaScript game script body is not structurally valid";
        return result;
    }

    std::ostringstream wrapped;
    wrapped << trigger_context_preamble(context)
            << "(function(ctx) {\n"
            << "  'use strict';\n"
            << source << "\n"
            << "})(ctx);";

    JsRuntime runtime(m_limits);
    return sanitize_game_result(runtime.evaluate(wrapped.str(), filename));
}

JsRuntimeEvalResult JsGameRuntime::evaluate_trigger_package_handler(
    const std::string& package_source, const std::string& handler_name,
    const JsGameTriggerContextFixture& context, const char* filename)
{
    if (!is_safe_handler_identifier(handler_name)) {
        JsRuntimeEvalResult result;
        result.status = JsRuntimeStatus::Error;
        result.diagnostic = "JavaScript game handler name is not a safe identifier";
        return result;
    }

    std::ostringstream wrapped;
    wrapped << trigger_context_preamble(context)
            << "const exports = Object.create(null);\n"
            << package_source << "\n"
            << "const __rotsHandler = typeof exports." << handler_name << " === 'function' ? exports."
            << handler_name << "\n"
            << "  : typeof " << handler_name << " === 'function' ? " << handler_name
            << " : undefined;\n"
            << "if (typeof __rotsHandler !== 'function') throw new TypeError('JavaScript game handler is not callable');\n"
            << "__rotsHandler(ctx);";

    JsRuntime runtime(m_limits);
    return sanitize_game_result(runtime.evaluate(wrapped.str(), filename));
}

std::string js_game_trigger_context_literal(const JsGameTriggerContextFixture& context)
{
    std::ostringstream out;
    out << "{"
        << "\"self\":"
        << nullable_literal(context.has_self, character_literal(context.self)) << ","
        << "\"actor\":"
        << nullable_literal(context.has_actor, character_literal(context.actor)) << ","
        << "\"speaker\":"
        << nullable_literal(context.has_speaker, character_literal(context.speaker)) << ","
        << "\"attacker\":"
        << nullable_literal(context.has_attacker, character_literal(context.attacker)) << ","
        << "\"victim\":"
        << nullable_literal(context.has_victim, character_literal(context.victim)) << ","
        << "\"killer\":"
        << nullable_literal(context.has_killer, character_literal(context.killer)) << ","
        << "\"object\":"
        << nullable_literal(context.has_object, object_literal(context.object)) << ","
        << "\"weapon\":"
        << nullable_literal(context.has_weapon, object_literal(context.weapon)) << ","
        << "\"room\":"
        << nullable_literal(context.has_room, room_literal(context.room)) << ","
        << "\"zone\":"
        << nullable_literal(context.has_zone, zone_literal(context.zone)) << ","
        << "\"text\":" << nullable_literal(context.has_text, js_quote(context.text)) << ","
        << "\"wearSlot\":" << nullable_literal(context.has_wear_slot, js_quote(context.wear_slot))
        << ","
        << "\"command\":" << nullable_literal(context.has_command, js_quote(context.command)) << ","
        << "\"args\":" << nullable_literal(context.has_args, js_quote(context.args)) << ","
        << "\"target\":"
        << nullable_literal(context.has_target, target_literal(context.target)) << ","
        << "\"tick\":";
    if (context.has_tick)
        out << context.tick;
    else
        out << "null";
    out << ","
        << "\"direction\":"
        << nullable_literal(context.has_direction, js_quote(context.direction)) << ","
        << "\"reverseDirection\":"
        << nullable_literal(context.has_reverse_direction, js_quote(context.reverse_direction)) << ","
        << "\"targ1\":"
        << nullable_literal(context.has_targ1, target_literal(context.targ1)) << ","
        << "\"targ2\":"
        << nullable_literal(context.has_targ2, target_literal(context.targ2)) << ","
        << "\"targetTypes\":" << target_types_literal(context.target_types) << ","
        << "\"dying\":"
        << nullable_literal(context.has_dying, character_literal(context.dying)) << ","
        << "\"hostType\":" << js_quote(context.trigger.host_type) << ","
        << "\"trigger\":" << trigger_literal(context.trigger) << "}";
    return out.str();
}
