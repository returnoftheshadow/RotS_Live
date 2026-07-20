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

int room_index_for_pointer(const room_data *room, const JsGameAdapterOptions &options)
{
    if (room == nullptr || options.world == nullptr)
        return -1;
    for (std::size_t index = 0; index < options.world_count; ++index) {
        if (&options.world[index] == room)
            return static_cast<int>(index);
    }
    return -1;
}

const char *target_type_name(int target_type)
{
    switch (target_type) {
    case TARGET_CHAR:
        return "character";
    case TARGET_OBJ:
        return "object";
    case TARGET_ROOM:
        return "room";
    case TARGET_TEXT:
        return "text";
    case TARGET_GOLD:
        return "gold";
    case TARGET_DIR:
        return "direction";
    case TARGET_IN:
        return "in";
    case TARGET_ALL:
        return "all";
    case TARGET_VALUE:
        return "value";
    case TARGET_OTHER:
        return "other";
    case TARGET_IGNORE:
        return "ignore";
    case TARGET_NONE:
        return "none";
    default:
        return "unknown";
    }
}

bool target_fixture_from_character(const char_data *character, const JsGameAdapterOptions &options,
    JsGameTargetFixture *fixture)
{
    if (fixture == nullptr ||
        !js_game_adapter_character_fixture(character, options, &fixture->character))
        return false;
    fixture->type = "character";
    fixture->has_character = true;
    fixture->character.id = "target";
    return true;
}

bool target_fixture_from_object(
    const obj_data *object, const JsGameAdapterOptions &options, JsGameTargetFixture *fixture)
{
    if (fixture == nullptr || !js_game_adapter_object_fixture(object, options, &fixture->object))
        return false;
    fixture->type = "object";
    fixture->has_object = true;
    fixture->object.id = "target";
    return true;
}

bool target_fixture_from_room(
    int room, const JsGameAdapterOptions &options, JsGameTargetFixture *fixture)
{
    if (fixture == nullptr || !js_game_adapter_room_fixture(room, options, &fixture->room))
        return false;
    fixture->type = "room";
    fixture->has_room = true;
    fixture->room.id = "target";
    return true;
}

bool target_fixture_from_target_data(const target_data *target,
    const JsGameAdapterOptions &options, JsGameTargetFixture *fixture)
{
    if (target == nullptr || fixture == nullptr)
        return false;
    switch (target->type) {
    case TARGET_CHAR:
        if (!js_game_adapter_is_live_character(target->ptr.ch, options) ||
            GET_ABS_NUM(target->ptr.ch) != target->ch_num)
            return false;
        return target_fixture_from_character(target->ptr.ch, options, fixture);
    case TARGET_OBJ:
        return target_fixture_from_object(target->ptr.obj, options, fixture);
    case TARGET_ROOM:
        return target_fixture_from_room(room_index_for_pointer(target->ptr.room, options), options,
            fixture);
    default:
        fixture->type = target_type_name(target->type);
        return false;
    }
}

void set_target_fixture_id(JsGameTargetFixture &fixture, const char *id)
{
    if (fixture.has_character)
        fixture.character.id = id;
    if (fixture.has_object)
        fixture.object.id = id;
    if (fixture.has_room)
        fixture.room.id = id;
}

const char *wear_slot_name(int wear_slot)
{
    switch (wear_slot) {
    case WEAR_LIGHT:
        return "light";
    case WEAR_FINGER_R:
        return "fingerRight";
    case WEAR_FINGER_L:
        return "fingerLeft";
    case WEAR_NECK_1:
        return "neck1";
    case WEAR_NECK_2:
        return "neck2";
    case WEAR_BODY:
        return "body";
    case WEAR_HEAD:
        return "head";
    case WEAR_LEGS:
        return "legs";
    case WEAR_FEET:
        return "feet";
    case WEAR_HANDS:
        return "hands";
    case WEAR_ARMS:
        return "arms";
    case WEAR_SHIELD:
        return "shield";
    case WEAR_ABOUT:
        return "aboutBody";
    case WEAR_WAISTE:
        return "waist";
    case WEAR_WRIST_R:
        return "wristRight";
    case WEAR_WRIST_L:
        return "wristLeft";
    case WIELD:
        return "wield";
    case HOLD:
        return "hold";
    case WEAR_BACK:
        return "back";
    case WEAR_BELT_1:
        return "belt1";
    case WEAR_BELT_2:
        return "belt2";
    case WEAR_BELT_3:
        return "belt3";
    default:
        return nullptr;
    }
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

    const char *display_name =
        object->short_description != nullptr ? object->short_description : object->name;
    fixture->id = object_id(*object, options);
    fixture->name = copy_c_string(display_name);
    fixture->description = copy_c_string(object->description);
    fixture->short_description = copy_c_string(display_name);
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
    fixture->description = copy_c_string(room_data.description);
    fixture->vnum = room_data.number;
    fixture->level = room_data.level;
    fixture->alignment = room_data.alignment;
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
    fixture->level = zone_data.level;
    return true;
}

JsGameTriggerContextFixture js_game_adapter_context_fixture(
    const JsGameAdapterContextInput &input, const JsGameAdapterOptions &options)
{
    JsGameTriggerContextFixture context;
    context.has_self = js_game_adapter_character_fixture(input.self, options, &context.self);
    context.has_actor = js_game_adapter_character_fixture(input.actor, options, &context.actor);
    context.has_speaker =
        js_game_adapter_character_fixture(input.speaker, options, &context.speaker);
    context.has_attacker =
        js_game_adapter_character_fixture(input.attacker, options, &context.attacker);
    context.has_victim = js_game_adapter_character_fixture(input.victim, options, &context.victim);
    context.has_killer = js_game_adapter_character_fixture(input.killer, options, &context.killer);
    context.has_object = js_game_adapter_object_fixture(input.object, options, &context.object);
    context.has_weapon = js_game_adapter_object_fixture(input.weapon, options, &context.weapon);
    if (context.has_self)
        context.self.id = "self";
    if (context.has_actor)
        context.actor.id = "actor";
    if (context.has_speaker)
        context.speaker.id = "speaker";
    if (context.has_attacker)
        context.attacker.id = "attacker";
    if (context.has_victim)
        context.victim.id = "victim";
    if (context.has_killer)
        context.killer.id = "killer";
    if (context.has_object)
        context.object.id = "object";
    if (context.has_weapon)
        context.weapon.id = "weapon";
    context.has_room = js_game_adapter_room_fixture(input.room, options, &context.room);

    if (context.has_room) {
        context.has_zone =
            js_game_adapter_zone_fixture(options.world[input.room].zone, options, &context.zone);
    }

    context.has_text = input.text != nullptr;
    if (input.text != nullptr)
        context.text = copy_c_string(input.text, MaxAdapterTextLength);
    if (const char *slot_name = wear_slot_name(input.wear_slot)) {
        context.has_wear_slot = true;
        context.wear_slot = slot_name;
    }
    context.has_command = input.command != nullptr;
    if (input.command != nullptr)
        context.command = copy_c_string(input.command, MaxAdapterTextLength);
    context.has_args = input.args != nullptr;
    if (input.args != nullptr)
        context.args = copy_c_string(input.args, MaxAdapterTextLength);
    context.has_tick = input.has_tick;
    context.tick = input.tick;
    context.has_direction = input.direction != nullptr;
    if (input.direction != nullptr)
        context.direction = copy_c_string(input.direction, MaxAdapterTextLength);
    context.has_reverse_direction = input.reverse_direction != nullptr;
    if (input.reverse_direction != nullptr)
        context.reverse_direction = copy_c_string(input.reverse_direction, MaxAdapterTextLength);
    const bool has_explicit_target = input.target_character != nullptr || input.target_object != nullptr ||
        input.target_room >= 0;
    if (input.target_character != nullptr)
        context.has_target =
            target_fixture_from_character(input.target_character, options, &context.target);
    else if (input.target_object != nullptr)
        context.has_target = target_fixture_from_object(input.target_object, options, &context.target);
    else if (input.target_room >= 0)
        context.has_target = target_fixture_from_room(input.target_room, options, &context.target);
    context.has_targ1 = target_fixture_from_target_data(input.targ1, options, &context.targ1);
    context.has_targ2 = target_fixture_from_target_data(input.targ2, options, &context.targ2);
    if (context.has_targ1)
        set_target_fixture_id(context.targ1, "targ1");
    if (context.has_targ2)
        set_target_fixture_id(context.targ2, "targ2");
    if (!has_explicit_target && !context.has_target) {
        if (context.has_targ1) {
            context.has_target = true;
            context.target = context.targ1;
        } else if (context.has_targ2) {
            context.has_target = true;
            context.target = context.targ2;
        }
    }
    if (context.has_target)
        set_target_fixture_id(context.target, "target");
    if (input.targ1 != nullptr)
        context.target_types.push_back(target_type_name(input.targ1->type));
    if (input.targ2 != nullptr)
        context.target_types.push_back(target_type_name(input.targ2->type));
    context.has_dying = js_game_adapter_character_fixture(input.dying, options, &context.dying);
    if (context.has_dying)
        context.dying.id = "dying";
    context.trigger = input.trigger;
    return context;
}
