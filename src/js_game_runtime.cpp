#include "js_game_runtime.h"

#include <sstream>

namespace {

constexpr std::size_t MaxGameDiagnosticLength = 120;

std::string js_quote(const std::string &value)
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
                static const char *hex = "0123456789abcdef";
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

const char *js_bool(bool value)
{
    return value ? "true" : "false";
}

std::string character_literal(const JsGameCharacterFixture &character)
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
        << "\"level\":" << character.level << ","
        << "\"hitPoints\":" << character.hit_points << ","
        << "\"maxHitPoints\":" << character.max_hit_points << ","
        << "\"isNpc\":" << js_bool(character.is_npc) << ","
        << "\"isPlayer\":" << js_bool(!character.is_npc) << "}";
    return out.str();
}

std::string object_literal(const JsGameObjectFixture &object)
{
    std::ostringstream out;
    out << "{"
        << "\"id\":" << js_quote(object.id) << ","
        << "\"name\":" << js_quote(object.name) << ","
        << "\"vnum\":";
    if (object.vnum >= 0)
        out << object.vnum;
    else
        out << "null";
    out << "}";
    return out.str();
}

std::string room_literal(const JsGameRoomFixture &room)
{
    std::ostringstream out;
    out << "{"
        << "\"id\":" << js_quote(room.id) << ","
        << "\"name\":" << js_quote(room.name) << ","
        << "\"vnum\":" << room.vnum << "}";
    return out.str();
}

std::string zone_literal(const JsGameZoneFixture &zone)
{
    std::ostringstream out;
    out << "{"
        << "\"id\":" << js_quote(zone.id) << ","
        << "\"name\":" << js_quote(zone.name) << ","
        << "\"vnum\":" << zone.vnum << "}";
    return out.str();
}

std::string trigger_literal(const JsGameTriggerFixture &trigger)
{
    std::ostringstream out;
    out << "{"
        << "\"name\":" << js_quote(trigger.name) << ","
        << "\"legacyName\":" << js_quote(trigger.legacy_name) << ","
        << "\"hostType\":" << js_quote(trigger.host_type) << ","
        << "\"legacyValue\":" << trigger.legacy_value << ","
        << "\"blocksGameplay\":" << js_bool(trigger.blocks_gameplay) << "}";
    return out.str();
}

std::string nullable_literal(bool present, const std::string &literal)
{
    return present ? literal : "null";
}

bool source_has_unsafe_wrapper_boundary(const std::string &source)
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

    return string_quote != '\0' || in_block_comment || brace_depth != 0 || paren_depth != 0 ||
           bracket_depth != 0;
}

} // namespace

JsGameRuntime::JsGameRuntime(const JsRuntimeLimits &limits)
    : m_limits(limits)
{
}

JsRuntimeEvalResult JsGameRuntime::evaluate_trigger_body(const std::string &source,
    const JsGameTriggerContextFixture &context, const char *filename)
{
    if (source_has_unsafe_wrapper_boundary(source)) {
        JsRuntimeEvalResult result;
        result.status = JsRuntimeStatus::Error;
        result.diagnostic = "JavaScript game script body is not structurally valid";
        return result;
    }

    std::ostringstream wrapped;
    wrapped << "'use strict';\n"
            << "delete Object.prototype.constructor;\n"
            << "const __rotsFunctionPrototype = Object.getPrototypeOf(function() {});\n"
            << "delete __rotsFunctionPrototype.constructor;\n"
            << "Object.freeze(Object.prototype);\n"
            << "Object.freeze(__rotsFunctionPrototype);\n"
            << "function __rotsDeepFreeze(value) {\n"
            << "  if (value && typeof value === 'object' && !Object.isFrozen(value)) {\n"
            << "    Object.setPrototypeOf(value, null);\n"
            << "    Object.freeze(value);\n"
            << "    for (const key of Object.keys(value)) __rotsDeepFreeze(value[key]);\n"
            << "  }\n"
            << "  return value;\n"
            << "}\n"
            << "const ctx = __rotsDeepFreeze(" << js_game_trigger_context_literal(context) << ");\n"
            << "(function(ctx) {\n"
            << "  'use strict';\n"
            << source << "\n"
            << "})(ctx);";

    JsRuntime runtime(m_limits);
    JsRuntimeEvalResult result = runtime.evaluate(wrapped.str(), filename);
    if (result.status == JsRuntimeStatus::Error && result.diagnostic.size() > MaxGameDiagnosticLength)
        result.diagnostic.resize(MaxGameDiagnosticLength);
    if (result.status == JsRuntimeStatus::Error && result.diagnostic.find("TypeError:") != 0 &&
        result.diagnostic.find("SyntaxError:") != 0) {
        result.diagnostic = "JavaScript game script failed";
    }
    return result;
}

std::string js_game_trigger_context_literal(const JsGameTriggerContextFixture &context)
{
    std::ostringstream out;
    out << "{"
        << "\"self\":"
        << nullable_literal(context.has_self, character_literal(context.self)) << ","
        << "\"actor\":"
        << nullable_literal(context.has_actor, character_literal(context.actor)) << ","
        << "\"object\":"
        << nullable_literal(context.has_object, object_literal(context.object)) << ","
        << "\"room\":"
        << nullable_literal(context.has_room, room_literal(context.room)) << ","
        << "\"zone\":"
        << nullable_literal(context.has_zone, zone_literal(context.zone)) << ","
        << "\"text\":" << nullable_literal(context.has_text, js_quote(context.text)) << ","
        << "\"trigger\":" << trigger_literal(context.trigger) << "}";
    return out.str();
}
