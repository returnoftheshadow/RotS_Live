#ifndef JS_GAME_RUNTIME_H
#define JS_GAME_RUNTIME_H

#include "js_runtime.h"

#include <string>

struct JsGameZoneFixture {
    std::string id;
    std::string name;
    int vnum = 0;
};

struct JsGameRoomFixture {
    std::string id;
    std::string name;
    int vnum = 0;
    bool is_sunlit = false;

    bool has_zone = false;
    JsGameZoneFixture zone;
};

struct JsGameCharacterFixture {
    std::string id;
    std::string name;
    std::string race;
    int vnum = -1;
    int prototype_vnum = -1;
    int level = 0;
    int experience = 0;
    int rank = 0;
    int hit_points = 0;
    int max_hit_points = 0;
    bool is_npc = false;

    bool has_room = false;
    JsGameRoomFixture room;
};

struct JsGameObjectFixture {
    std::string id;
    std::string name;
    int vnum = 0;

    bool has_room = false;
    JsGameRoomFixture room;

    bool has_carried_by = false;
    JsGameCharacterFixture carried_by;

    bool has_worn_by = false;
    JsGameCharacterFixture worn_by;
};

struct JsGameTriggerFixture {
    std::string name;
    std::string legacy_name;
    std::string host_type;
    int legacy_value = 0;
    bool blocks_gameplay = false;
};

struct JsGameTriggerContextFixture {
    bool has_self = false;
    JsGameCharacterFixture self;

    bool has_actor = false;
    JsGameCharacterFixture actor;

    bool has_speaker = false;
    JsGameCharacterFixture speaker;

    bool has_attacker = false;
    JsGameCharacterFixture attacker;

    bool has_victim = false;
    JsGameCharacterFixture victim;

    bool has_object = false;
    JsGameObjectFixture object;

    bool has_weapon = false;
    JsGameObjectFixture weapon;

    bool has_room = false;
    JsGameRoomFixture room;

    bool has_zone = false;
    JsGameZoneFixture zone;

    bool has_text = false;
    std::string text;

    bool has_wear_slot = false;
    std::string wear_slot;

    JsGameTriggerFixture trigger;
};

class JsGameRuntime {
  public:
    explicit JsGameRuntime(const JsRuntimeLimits &limits = {});

    JsRuntimeEvalResult evaluate_trigger_body(const std::string &source,
        const JsGameTriggerContextFixture &context, const char *filename = "game-script.js");
    JsRuntimeEvalResult evaluate_trigger_package_handler(const std::string &package_source,
        const std::string &handler_name, const JsGameTriggerContextFixture &context,
        const char *filename = "game-script.js");

  private:
    JsRuntimeLimits m_limits;
};

std::string js_game_trigger_context_literal(const JsGameTriggerContextFixture &context);

#endif
