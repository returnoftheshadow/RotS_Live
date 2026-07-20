#ifndef JS_GAME_ADAPTER_H
#define JS_GAME_ADAPTER_H

#include "js_game_runtime.h"

#include <cstddef>

struct char_data;
struct index_data;
struct obj_data;
struct room_data;
struct zone_data;

struct JsGameAdapterOptions {
    const char_data *const *live_characters = nullptr;
    std::size_t live_character_count = 0;

    const obj_data *const *live_objects = nullptr;
    std::size_t live_object_count = 0;

    const room_data *world = nullptr;
    std::size_t world_count = 0;
    int top_of_world = -1;

    const index_data *mobile_index = nullptr;
    std::size_t mobile_index_count = 0;

    const index_data *object_index = nullptr;
    std::size_t object_index_count = 0;

    const zone_data *zones = nullptr;
    std::size_t zone_count = 0;

    const char *const *race_names = nullptr;
    std::size_t race_name_count = 0;
};

struct JsGameAdapterContextInput {
    const char_data *self = nullptr;
    const char_data *actor = nullptr;
    const char_data *speaker = nullptr;
    const char_data *attacker = nullptr;
    const char_data *victim = nullptr;
    const char_data *killer = nullptr;
    const obj_data *object = nullptr;
    const obj_data *weapon = nullptr;
    int room = -1;
    const char *text = nullptr;
    int wear_slot = -1;
    JsGameTriggerFixture trigger;
};

bool js_game_adapter_is_live_character(
    const char_data *character, const JsGameAdapterOptions &options);
bool js_game_adapter_is_live_object(const obj_data *object, const JsGameAdapterOptions &options);
bool js_game_adapter_room_is_valid(int room, const JsGameAdapterOptions &options);

bool js_game_adapter_character_fixture(const char_data *character,
    const JsGameAdapterOptions &options, JsGameCharacterFixture *fixture);
bool js_game_adapter_object_fixture(
    const obj_data *object, const JsGameAdapterOptions &options, JsGameObjectFixture *fixture);
bool js_game_adapter_room_fixture(
    int room, const JsGameAdapterOptions &options, JsGameRoomFixture *fixture);
bool js_game_adapter_zone_fixture(
    int zone, const JsGameAdapterOptions &options, JsGameZoneFixture *fixture);

JsGameTriggerContextFixture js_game_adapter_context_fixture(
    const JsGameAdapterContextInput &input, const JsGameAdapterOptions &options);

#endif
