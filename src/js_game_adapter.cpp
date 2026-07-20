#include "js_game_adapter.h"

#include "db.h"
#include "structs.h"
#include "utils.h"
#include "zone.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <string>

namespace {

constexpr std::size_t MaxAdapterStringLength = 512;
constexpr std::size_t MaxAdapterTextLength = 1024;

template <typename T>
bool pointer_in_table(const T *candidate, const T *const *table, std::size_t count)
{
    if (candidate == nullptr)
        return false;
    if (table == nullptr)
        return false;
    return std::find(table, table + count, candidate) != table + count;
}

std::string copy_c_string(const char *value, std::size_t max_length = MaxAdapterStringLength)
{
    if (value == nullptr)
        return "";
    return std::string(value, strnlen(value, max_length));
}

bool character_is_npc(const char_data &character)
{
    return (character.specials2.act & MOB_ISNPC) != 0;
}

std::string character_name(const char_data &character)
{
    if (character_is_npc(character))
        return copy_c_string(character.player.short_descr);
    return copy_c_string(character.player.name);
}

std::string race_name(const char_data &character, const JsGameAdapterOptions &options)
{
    int race = character.player.race;
    if (options.race_names != nullptr && race >= 0 &&
        static_cast<std::size_t>(race) < options.race_name_count &&
        options.race_names[race] != nullptr) {
        return options.race_names[race];
    }

    std::ostringstream out;
    out << "race:" << race;
    return out.str();
}

int character_vnum(const char_data &character, const JsGameAdapterOptions &options)
{
    if (!character_is_npc(character) || character.nr < 0 || options.mobile_index == nullptr ||
        static_cast<std::size_t>(character.nr) >= options.mobile_index_count) {
        return -1;
    }
    return options.mobile_index[character.nr].virt;
}

int object_vnum(const obj_data &object, const JsGameAdapterOptions &options)
{
    if (object.item_number < 0 || options.object_index == nullptr ||
        static_cast<std::size_t>(object.item_number) >= options.object_index_count) {
        return -1;
    }
    return options.object_index[object.item_number].virt;
}

std::string character_id(const char_data &character, const JsGameAdapterOptions &options)
{
    std::ostringstream out;
    if (character_is_npc(character)) {
        out << "mob";
        int vnum = character_vnum(character, options);
        if (vnum >= 0)
            out << ":" << vnum;
        else
            out << ":unresolved";
    } else {
        out << "player";
    }
    return out.str();
}

std::string object_id(const obj_data &object, const JsGameAdapterOptions &options)
{
    std::ostringstream out;
    out << "object";
    int vnum = object_vnum(object, options);
    if (vnum >= 0)
        out << ":" << vnum;
    else
        out << ":unresolved";
    return out.str();
}

bool room_is_dark(const room_data &room)
{
    return !room.light &&
        (IS_SET(room.room_flags, DARK) ||
            ((room.sector_type != SECT_INSIDE && room.sector_type != SECT_CITY) &&
                weather_info.sunlight == SUN_DARK));
}

bool room_is_sunlit(const room_data &room)
{
    return (weather_info.sunlight == SUN_LIGHT || weather_info.sunlight == SUN_RISE) &&
        !room_is_dark(room);
}

bool object_is_worn_by(const obj_data *object, const char_data *carrier)
{
    if (object == nullptr || carrier == nullptr)
        return false;

    return std::find(carrier->equipment, carrier->equipment + MAX_WEAR, object) !=
        carrier->equipment + MAX_WEAR;
}

bool object_is_carried_by(const obj_data *object, const char_data *carrier)
{
    if (object == nullptr || carrier == nullptr)
        return false;

    for (const obj_data *carried = carrier->carrying; carried != nullptr;
         carried = carried->next_content) {
        if (carried == object)
            return true;
    }
    return false;
}

} // namespace

bool js_game_adapter_is_live_character(
    const char_data *character, const JsGameAdapterOptions &options)
{
    return pointer_in_table(character, options.live_characters, options.live_character_count);
}

bool js_game_adapter_is_live_object(const obj_data *object, const JsGameAdapterOptions &options)
{
    return pointer_in_table(object, options.live_objects, options.live_object_count);
}

bool js_game_adapter_room_is_valid(int room, const JsGameAdapterOptions &options)
{
    if (options.world == nullptr || room < 0)
        return false;
    if (options.world_count > 0)
        return static_cast<std::size_t>(room) < options.world_count;
    return options.top_of_world >= 0 && room <= options.top_of_world;
}

bool js_game_adapter_character_fixture(const char_data *character,
    const JsGameAdapterOptions &options, JsGameCharacterFixture *fixture)
{
    if (fixture == nullptr || !js_game_adapter_is_live_character(character, options))
        return false;

    fixture->id = character_id(*character, options);
    fixture->name = character_name(*character);
    fixture->race = race_name(*character, options);
    fixture->vnum = character_vnum(*character, options);
    fixture->prototype_vnum = fixture->vnum;
    fixture->level = character->player.level;
    fixture->experience = character->points.exp;
    fixture->rank = character->player.ranking;
    fixture->hit_points = character->tmpabilities.hit;
    fixture->max_hit_points = character->abilities.hit;
    fixture->is_npc = character_is_npc(*character);
    fixture->has_room = js_game_adapter_room_fixture(character->in_room, options, &fixture->room);
    return true;
}

bool js_game_adapter_object_fixture(
    const obj_data *object, const JsGameAdapterOptions &options, JsGameObjectFixture *fixture)
{
    if (fixture == nullptr || !js_game_adapter_is_live_object(object, options))
        return false;

    fixture->id = object_id(*object, options);
    fixture->name = copy_c_string(object->short_description != nullptr ? object->short_description : object->name);
    fixture->vnum = object_vnum(*object, options);
    fixture->has_room = js_game_adapter_room_fixture(object->in_room, options, &fixture->room);

    fixture->has_carried_by = false;
    fixture->has_worn_by = false;
    if (object->in_room == NOWHERE && object->in_obj == nullptr &&
        js_game_adapter_is_live_character(object->carried_by, options)) {
        if (object_is_worn_by(object, object->carried_by)) {
            fixture->has_worn_by =
                js_game_adapter_character_fixture(object->carried_by, options, &fixture->worn_by);
        } else if (object_is_carried_by(object, object->carried_by)) {
            fixture->has_carried_by =
                js_game_adapter_character_fixture(object->carried_by, options, &fixture->carried_by);
        }
    }
    return true;
}

bool js_game_adapter_room_fixture(
    int room, const JsGameAdapterOptions &options, JsGameRoomFixture *fixture)
{
    if (fixture == nullptr || !js_game_adapter_room_is_valid(room, options))
        return false;

    const room_data &room_data = options.world[room];
    fixture->id = "room:" + std::to_string(room_data.number);
    fixture->name = copy_c_string(room_data.name);
    fixture->vnum = room_data.number;
    fixture->is_sunlit = room_is_sunlit(room_data);
    fixture->has_zone = js_game_adapter_zone_fixture(room_data.zone, options, &fixture->zone);
    return true;
}

bool js_game_adapter_zone_fixture(
    int zone, const JsGameAdapterOptions &options, JsGameZoneFixture *fixture)
{
    if (fixture == nullptr || options.zones == nullptr || zone < 0 ||
        static_cast<std::size_t>(zone) >= options.zone_count) {
        return false;
    }

    const zone_data &zone_data = options.zones[zone];
    fixture->id = "zone:" + std::to_string(zone_data.number);
    fixture->name = copy_c_string(zone_data.name);
    fixture->vnum = zone_data.number;
    return true;
}

JsGameTriggerContextFixture js_game_adapter_context_fixture(
    const JsGameAdapterContextInput &input, const JsGameAdapterOptions &options)
{
    JsGameTriggerContextFixture context;
    context.has_self = js_game_adapter_character_fixture(input.self, options, &context.self);
    context.has_actor = js_game_adapter_character_fixture(input.actor, options, &context.actor);
    context.has_object = js_game_adapter_object_fixture(input.object, options, &context.object);
    if (context.has_self)
        context.self.id = "self";
    if (context.has_actor)
        context.actor.id = "actor";
    if (context.has_object)
        context.object.id = "object";
    context.has_room = js_game_adapter_room_fixture(input.room, options, &context.room);

    if (context.has_room) {
        context.has_zone =
            js_game_adapter_zone_fixture(options.world[input.room].zone, options, &context.zone);
    }

    context.has_text = input.text != nullptr;
    if (input.text != nullptr)
        context.text = copy_c_string(input.text, MaxAdapterTextLength);
    context.trigger = input.trigger;
    return context;
}
