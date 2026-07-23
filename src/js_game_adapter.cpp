#include "js_game_adapter.h"

#include "db.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"
#include "zone.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

extern char *sector_types[];
extern char num_of_sector_types;
extern char *dirs[];
extern char *tactics[];
extern char *affected_bits[];
extern char *apply_types[];

namespace {

constexpr std::size_t MaxAdapterStringLength = 512;
constexpr std::size_t MaxAdapterTextLength = 1024;
constexpr int MaxExtraDescriptionSnapshotNodes = 32;
constexpr int MaxObjectContainerMembershipNodes = 100;
constexpr int MaxObjectContentsSnapshotNodes = 100;

struct CharacterProfessionField {
    int index;
    const char *key;
    const char *name;
};

constexpr CharacterProfessionField CharacterProfessionFields[] = {
    {PROF_MAGE, "mage", "Mage"},
    {PROF_CLERIC, "mystic", "Mystic"},
    {PROF_RANGER, "ranger", "Ranger"},
    {PROF_WARRIOR, "warrior", "Warrior"},
};

struct CharacterSpecializationField {
    int id;
    const char *key;
    const char *name;
};

constexpr CharacterSpecializationField CharacterSpecializationFields[] = {
    {game_types::PS_None, "nothing", "nothing"},
    {game_types::PS_Fire, "fire", "fire"},
    {game_types::PS_Cold, "cold", "cold"},
    {game_types::PS_Regeneration, "regeneration", "regeneration"},
    {game_types::PS_Protection, "protection", "protection"},
    {game_types::PS_Animals, "animals", "animals"},
    {game_types::PS_Stealth, "stealth", "stealth"},
    {game_types::PS_WildFighting, "wildFighting", "wild fighting"},
    {game_types::PS_Teleportation, "teleportation", "teleportation"},
    {game_types::PS_Illusion, "illusion", "illusion"},
    {game_types::PS_Lightning, "lightning", "lightning"},
    {game_types::PS_Guardian, "guardian", "guardian"},
    {game_types::PS_HeavyFighting, "heavyFighting", "heavy fighting"},
    {game_types::PS_LightFighting, "lightFighting", "light fighting"},
    {game_types::PS_Defender, "defending", "defending"},
    {game_types::PS_Archery, "archery", "archery"},
    {game_types::PS_Darkness, "darkness", "darkness"},
    {game_types::PS_Arcane, "arcane", "arcane"},
    {game_types::PS_WeaponMaster, "weaponMastery", "weapon mastery"},
    {game_types::PS_BattleMage, "battleMagic", "battle magic"},
};

constexpr const char *EquipmentSlotNames[] = {
    "light", "fingerRight", "fingerLeft", "neck1",  "neck2",     "body",  "head",       "legs",
    "feet",  "hands",       "arms",       "shield", "aboutBody", "waist", "wristRight", "wristLeft",
    "wield", "hold",        "back",       "belt1",  "belt2",     "belt3"};

template <typename T>
bool pointer_in_table(const T *candidate, const T *const *table, std::size_t count) {
    if (candidate == nullptr)
        return false;
    if (table == nullptr)
        return false;
    return std::find(table, table + count, candidate) != table + count;
}

std::string copy_c_string(const char *value, std::size_t max_length = MaxAdapterStringLength) {
    if (value == nullptr)
        return "";
    return std::string(value, strnlen(value, max_length));
}

std::string table_name_at(char *const *table, int index, int max_entries) {
    if (table == nullptr || index < 0 || index >= max_entries)
        return "Unknown";
    const std::string name = copy_c_string(table[index]);
    if (name.empty() || name == "\n")
        return "Unknown";
    return name;
}

bool character_is_npc(const char_data &character) {
    return (character.specials2.act & MOB_ISNPC) != 0;
}

std::string skill_profession_key(int profession) {
    switch (profession) {
    case PROF_MAGE:
        return "mage";
    case PROF_CLERIC:
        return "mystic";
    case PROF_RANGER:
        return "ranger";
    case PROF_WARRIOR:
        return "warrior";
    case PROF_GENERAL:
        return "general";
    default:
        return "unknown";
    }
}

const CharacterSpecializationField &character_specialization_field(int id) {
    for (const CharacterSpecializationField &field : CharacterSpecializationFields) {
        if (field.id == id)
            return field;
    }
    static constexpr CharacterSpecializationField UnknownSpecialization = {-1, "Unknown",
                                                                           "Unknown"};
    return UnknownSpecialization;
}

std::pair<std::string, std::string> damage_source_metadata(int source_id) {
    if (source_id >= TYPE_HIT && source_id <= TYPE_CRUSH) {
        const attack_hit_type &hit_text = get_hit_text(source_id);
        return {"attack", copy_c_string(hit_text.singular)};
    }

    if (source_id >= 0 && source_id < MAX_SKILLS) {
        const skill_data *skills = get_skill_array();
        if (skills != nullptr)
            return {"skill", copy_c_string(skills[source_id].name)};
    }

    return {"unknown", "Unknown"};
}

std::vector<std::string> affect_bitvector_names(long bitvector) {
    std::vector<std::string> names;
    for (int bit_index = 0; bit_index < 32; ++bit_index) {
        if ((bitvector & (1L << bit_index)) == 0)
            continue;
        const std::string name = table_name_at(affected_bits, bit_index, 32);
        if (name != "Unknown")
            names.push_back(name);
    }
    return names;
}

std::string table_name_or_unknown(char **table, int value) {
    if (table == nullptr || value < 0)
        return "Unknown";
    for (int index = 0; table[index] != nullptr && table[index][0] != '\n'; ++index) {
        if (index == value)
            return table[index];
    }
    return "Unknown";
}

std::string table_name_or_empty(char **table, int value) {
    if (table == nullptr || value < 0)
        return "";
    for (int index = 0; table[index] != nullptr && table[index][0] != '\n'; ++index) {
        if (index == value)
            return table[index];
    }
    return "";
}

std::string character_tactics_name(const char_data &character) {
    if (character_is_npc(character))
        return "";
    const int value = character.specials.tactics;
    return value >= TACTICS_DEFENSIVE && value <= TACTICS_BERSERK
               ? table_name_or_empty(tactics, value - 1)
               : "";
}

std::string character_position_name(int position) {
    switch (position) {
    case POSITION_DEAD:
        return "Dead";
    case POSITION_SHAPING:
        return "Shaping";
    case POSITION_INCAP:
        return "Incapacitated";
    case POSITION_STUNNED:
        return "Stunned";
    case POSITION_SLEEPING:
        return "Sleeping";
    case POSITION_RESTING:
        return "Resting";
    case POSITION_SITTING:
        return "Sitting";
    case POSITION_FIGHTING:
        return "Fighting";
    case POSITION_STANDING:
        return "Standing";
    default:
        return "Unknown";
    }
}

std::string character_name(const char_data &character) {
    if (character_is_npc(character))
        return copy_c_string(character.player.short_descr);
    return copy_c_string(character.player.name);
}

std::string race_name(const char_data &character, const JsGameAdapterOptions &options) {
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

int character_vnum(const char_data &character, const JsGameAdapterOptions &options) {
    if (!character_is_npc(character) || character.nr < 0 || options.mobile_index == nullptr ||
        static_cast<std::size_t>(character.nr) >= options.mobile_index_count) {
        return -1;
    }
    return options.mobile_index[character.nr].virt;
}

int object_vnum(const obj_data &object, const JsGameAdapterOptions &options) {
    if (object.item_number < 0 || options.object_index == nullptr ||
        static_cast<std::size_t>(object.item_number) >= options.object_index_count) {
        return -1;
    }
    return options.object_index[object.item_number].virt;
}

std::string character_id(const char_data &character, const JsGameAdapterOptions &options) {
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

std::string object_id(const obj_data &object, const JsGameAdapterOptions &options) {
    std::ostringstream out;
    out << "object";
    int vnum = object_vnum(object, options);
    if (vnum >= 0)
        out << ":" << vnum;
    else
        out << ":unresolved";
    return out.str();
}

bool room_is_dark(const room_data &room) {
    return !room.light && (IS_SET(room.room_flags, DARK) ||
                           ((room.sector_type != SECT_INSIDE && room.sector_type != SECT_CITY) &&
                            weather_info.sunlight == SUN_DARK));
}

bool room_is_sunlit(const room_data &room) {
    return (weather_info.sunlight == SUN_LIGHT || weather_info.sunlight == SUN_RISE) &&
           !room_is_dark(room);
}

std::string room_sector_type_name(int sector_type) {
    if (sector_type >= 0 && sector_type < num_of_sector_types && sector_types != nullptr &&
        sector_types[sector_type] != nullptr) {
        return copy_c_string(sector_types[sector_type]);
    }

    return "Unknown";
}

struct RoomFlagName {
    long bit;
    const char *name;
};

constexpr RoomFlagName RoomFlagNames[] = {
    {DARK, "dark"},
    {DEATH, "death"},
    {NO_MOB, "noMob"},
    {INDOORS, "indoors"},
    {NORIDE, "noRide"},
    {PERMAFFECT, "permanentAffect"},
    {SHADOWY, "shadowy"},
    {NO_MAGIC, "noMagic"},
    {TUNNEL, "tunnel"},
    {PRIVATE, "private"},
    {GODROOM, "godRoom"},
    {DRINK_WATER, "drinkWater"},
    {DRINK_POISON, "drinkPoison"},
    {SECURITYROOM, "securityRoom"},
    {PEACEROOM, "peaceRoom"},
    {NO_TELEPORT, "noTeleport"},
    {HIDE_VNUM, "hideVnum"},
};

std::vector<std::string> room_flag_names(long room_flags) {
    std::vector<std::string> flags;
    for (const RoomFlagName &entry : RoomFlagNames) {
        if (IS_SET(room_flags, entry.bit))
            flags.emplace_back(entry.name);
    }
    return flags;
}

struct IntName {
    int value;
    const char *name;
};

struct LongFlagName {
    long bit;
    const char *name;
};

constexpr LongFlagName ExitFlagNames[] = {
    {EX_ISDOOR, "door"},       {EX_CLOSED, "closed"},        {EX_LOCKED, "locked"},
    {EX_NOFLEE, "noFlee"},     {EX_RSLOCKED, "resetLocked"}, {EX_PICKPROOF, "pickproof"},
    {EX_DOORISHEAVY, "heavy"}, {EX_NOBREAK, "noBreak"},      {EX_NO_LOOK, "noLook"},
    {EX_ISHIDDEN, "hidden"},   {EX_ISBROKEN, "broken"},      {EX_NORIDE, "noRide"},
    {EX_NOBLINK, "noBlink"},   {EX_LEVER, "lever"},          {EX_NOWALK, "noWalk"},
};

constexpr IntName ObjectTypeNames[] = {
    {ITEM_LIGHT, "light"},
    {ITEM_SCROLL, "scroll"},
    {ITEM_WAND, "wand"},
    {ITEM_STAFF, "staff"},
    {ITEM_WEAPON, "weapon"},
    {ITEM_FIREWEAPON, "fireWeapon"},
    {ITEM_MISSILE, "missile"},
    {ITEM_TREASURE, "treasure"},
    {ITEM_ARMOR, "armor"},
    {ITEM_POTION, "potion"},
    {ITEM_WORN, "worn"},
    {ITEM_OTHER, "other"},
    {ITEM_TRASH, "trash"},
    {ITEM_TRAP, "trap"},
    {ITEM_CONTAINER, "container"},
    {ITEM_NOTE, "note"},
    {ITEM_DRINKCON, "drinkContainer"},
    {ITEM_KEY, "key"},
    {ITEM_FOOD, "food"},
    {ITEM_MONEY, "money"},
    {ITEM_PEN, "pen"},
    {ITEM_BOAT, "boat"},
    {ITEM_FOUNTAIN, "fountain"},
    {ITEM_SHIELD, "shield"},
    {ITEM_LEVER, "lever"},
};

constexpr LongFlagName ObjectWearFlagNames[] = {
    {ITEM_TAKE, "take"},          {ITEM_WEAR_FINGER, "finger"},   {ITEM_WEAR_NECK, "neck"},
    {ITEM_WEAR_BODY, "body"},     {ITEM_WEAR_HEAD, "head"},       {ITEM_WEAR_LEGS, "legs"},
    {ITEM_WEAR_FEET, "feet"},     {ITEM_WEAR_HANDS, "hands"},     {ITEM_WEAR_ARMS, "arms"},
    {ITEM_WEAR_SHIELD, "shield"}, {ITEM_WEAR_ABOUT, "aboutBody"}, {ITEM_WEAR_WAISTE, "waist"},
    {ITEM_WEAR_WRIST, "wrist"},   {ITEM_WIELD, "wield"},          {ITEM_HOLD, "hold"},
    {ITEM_THROW, "throw"},        {ITEM_WEAR_BACK, "back"},       {ITEM_WEAR_BELT, "belt"},
};

constexpr LongFlagName ObjectExtraFlagNames[] = {
    {ITEM_GLOW, "glow"},
    {ITEM_HUM, "hum"},
    {ITEM_DARK, "dark"},
    {ITEM_BREAKABLE, "breakable"},
    {ITEM_EVIL, "evil"},
    {ITEM_INVISIBLE, "invisible"},
    {ITEM_MAGIC, "magic"},
    {ITEM_NODROP, "noDrop"},
    {ITEM_BROKEN, "broken"},
    {ITEM_ANTI_GOOD, "antiGood"},
    {ITEM_ANTI_EVIL, "antiEvil"},
    {ITEM_ANTI_NEUTRAL, "antiNeutral"},
    {ITEM_NORENT, "noRent"},
    {ITEM_NOINVIS, "noInvis"},
    {ITEM_WILLPOWER, "willpower"},
    {ITEM_IMM, "immortalOnly"},
    {ITEM_HUMAN, "human"},
    {ITEM_DWARF, "dwarf"},
    {ITEM_WOODELF, "woodElf"},
    {ITEM_HOBBIT, "hobbit"},
    {ITEM_BEORNING, "beorning"},
    {ITEM_URUK, "uruk"},
    {ITEM_ORC, "orc"},
    {ITEM_MOBORC, "mobOrc"},
    {ITEM_MAGUS, "magus"},
    {ITEM_OLOGHAI, "ologHai"},
    {ITEM_HARADRIM, "haradrim"},
    {ITEM_STAY_ZONE, "stayZone"},
};

constexpr LongFlagName MobFlagNames[] = {
    {MOB_SPEC, "specialProcedure"},
    {MOB_SENTINEL, "sentinel"},
    {MOB_SCAVENGER, "scavenger"},
    {MOB_ISNPC, "isNpc"},
    {MOB_NOBASH, "noBash"},
    {MOB_AGGRESSIVE, "aggressive"},
    {MOB_STAY_ZONE, "stayZone"},
    {MOB_WIMPY, "wimpy"},
    {MOB_STAY_TYPE, "stayType"},
    {MOB_MOUNT, "mount"},
    {MOB_CAN_SWIM, "canSwim"},
    {MOB_MEMORY, "memory"},
    {MOB_HELPER, "helper"},
    {MOB_AGGRESSIVE_EVIL, "aggressiveEvil"},
    {MOB_AGGRESSIVE_NEUTRAL, "aggressiveNeutral"},
    {MOB_AGGRESSIVE_GOOD, "aggressiveGood"},
    {MOB_BODYGUARD, "bodyguard"},
    {MOB_SHADOW, "shadow"},
    {MOB_SWITCHING, "switching"},
    {MOB_NORECALC, "noRecalc"},
    {MOB_FAST, "active"},
    {MOB_PET, "pet"},
    {MOB_HUNTER, "hunter"},
    {MOB_ORC_FRIEND, "orcFriend"},
    {MOB_RACE_GUARD, "raceGuard"},
    {MOB_ASSISTANT, "assistant"},
    {MOB_GUARDIAN, "guardian"},
};

constexpr LongFlagName PlayerFlagNames[] = {
    {PLR_IS_NCHANGED, "isNchanged"},
    {PLR_FROZEN, "frozen"},
    {PLR_DONTSET, "dontSet"},
    {PLR_WRITING, "writing"},
    {PLR_MAILING, "mailing"},
    {PLR_CRASH, "crash"},
    {PLR_SITEOK, "siteOk"},
    {PLR_NOSHOUT, "noShout"},
    {PLR_NOTITLE, "noTitle"},
    {PLR_DELETED, "deleted"},
    {PLR_LOADROOM, "loadRoom"},
    {PLR_NOWIZLIST, "noWizlist"},
    {PLR_NODELETE, "noDelete"},
    {PLR_INVSTART, "invisibleStart"},
    {PLR_RETIRED, "retired"},
    {PLR_SHAPING, "shaping"},
    {PLR_WR_FINISH, "writingFinished"},
    {PLR_ISSHADOW, "isShadow"},
    {PLR_ISAFK, "isAfk"},
    {PLR_INCOGNITO, "incognito"},
    {PLR_WAS_KITTED, "wasKitted"},
};

constexpr LongFlagName PreferenceFlagNames[] = {
    {PRF_BRIEF, "brief"},
    {PRF_COMPACT, "compact"},
    {PRF_NARRATE, "narrate"},
    {PRF_NOTELL, "noTell"},
    {PRF_MENTAL, "mental"},
    {PRF_SWIM, "swim"},
    {PRF_PROMPT, "prompt"},
    {PRF_DISPTEXT, "displayText"},
    {PRF_NOHASSLE, "noHassle"},
    {PRF_SUMMONABLE, "summonable"},
    {PRF_ECHO, "echo"},
    {PRF_HOLYLIGHT, "holyLight"},
    {PRF_COLOR, "color"},
    {PRF_SING, "sing"},
    {PRF_WIZ, "wiz"},
    {PRF_LOG1, "log1"},
    {PRF_LOG2, "log2"},
    {PRF_LOG3, "log3"},
    {PRF_CHAT, "chat"},
    {PRF_ROOMFLAGS, "roomFlags"},
    {PRF_SPAM, "spam"},
    {PRF_MSDP, "msdp"},
    {PRF_WRAP, "wrap"},
    {PRF_LATIN1, "latin1"},
    {PRF_SPINNER, "spinner"},
    {PRF_INV_SORT1, "inventorySort1"},
    {PRF_INV_SORT2, "inventorySort2"},
    {PRF_ADVANCED_VIEW, "advancedView"},
    {PRF_ADVANCED_PROMPT, "advancedPrompt"},
};

constexpr LongFlagName HideFlagNames[] = {
    {HIDING_WELL, "hidingWell"},
    {HIDING_SNUCK_IN, "snuckIn"},
};

constexpr IntName ObjectMaterialNames[] = {
    {0, "usual"},    {1, "cloth"}, {2, "leather"}, {3, "chain"},  {4, "metal"},
    {5, "wood"},     {6, "stone"}, {7, "crystal"}, {8, "gold"},   {9, "silver"},
    {10, "mithril"}, {11, "fur"},  {12, "glass"},  {13, "plant"},
};

constexpr IntName CharacterTacticNames[] = {
    {TACTICS_DEFENSIVE, "defensive"},   {TACTICS_CAREFUL, "careful"}, {TACTICS_NORMAL, "normal"},
    {TACTICS_AGGRESSIVE, "aggressive"}, {TACTICS_BERSERK, "berserk"},
};

constexpr IntName CharacterShootingNames[] = {
    {SHOOTING_SLOW, "slow"},
    {SHOOTING_NORMAL, "normal"},
    {SHOOTING_FAST, "fast"},
};

constexpr IntName CharacterCastingNames[] = {
    {CASTING_SLOW, "slow"},
    {CASTING_NORMAL, "normal"},
    {CASTING_FAST, "fast"},
};

std::string named_value(int value, const IntName *names, std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        if (names[index].value == value)
            return names[index].name;
    }
    return "Unknown";
}

std::vector<std::string> named_flags(long bitvector, const LongFlagName *names, std::size_t count) {
    std::vector<std::string> flags;
    for (std::size_t index = 0; index < count; ++index) {
        if (IS_SET(bitvector, names[index].bit))
            flags.emplace_back(names[index].name);
    }
    return flags;
}

std::vector<std::string> exit_flag_names(int exit_info) {
    return named_flags(exit_info, ExitFlagNames, std::size(ExitFlagNames));
}

std::vector<std::string> character_act_flags(const char_data &character) {
    return character_is_npc(character)
               ? named_flags(character.specials2.act, MobFlagNames, std::size(MobFlagNames))
               : named_flags(character.specials2.act, PlayerFlagNames, std::size(PlayerFlagNames));
}

JsGameObjectFlagsFixture object_flags_fixture(const obj_flag_data &flags) {
    JsGameObjectFlagsFixture fixture;
    fixture.item_type = named_value(flags.type_flag, ObjectTypeNames, std::size(ObjectTypeNames));
    fixture.wear_flags =
        named_flags(flags.wear_flags, ObjectWearFlagNames, std::size(ObjectWearFlagNames));
    fixture.extra_flags =
        named_flags(flags.extra_flags, ObjectExtraFlagNames, std::size(ObjectExtraFlagNames));
    fixture.level = flags.level;
    fixture.weight = flags.get_weight();
    fixture.cost = flags.cost;
    fixture.cost_per_day = flags.cost_per_day;
    fixture.timer = flags.timer;
    fixture.rarity = flags.rarity;
    fixture.material =
        named_value(flags.material, ObjectMaterialNames, std::size(ObjectMaterialNames));
    return fixture;
}

std::vector<JsGameObjectAffectFixture>
object_affects_fixture(const obj_affected_type (&affected)[MAX_OBJ_AFFECT]) {
    std::vector<JsGameObjectAffectFixture> fixtures;
    for (int slot_index = 0; slot_index < MAX_OBJ_AFFECT; ++slot_index) {
        const obj_affected_type &affect = affected[slot_index];
        if (affect.location == APPLY_NONE && affect.modifier == 0)
            continue;

        JsGameObjectAffectFixture fixture;
        fixture.slot_index = slot_index;
        fixture.location = affect.location;
        fixture.location_name = table_name_at(apply_types, affect.location, 40);
        fixture.modifier = affect.modifier;
        fixtures.push_back(std::move(fixture));
    }
    return fixtures;
}

std::vector<JsGameExtraDescriptionFixture>
extra_descriptions_fixture(const extra_descr_data *extra_descriptions) {
    std::vector<JsGameExtraDescriptionFixture> fixtures;
    std::vector<const extra_descr_data *> seen_nodes;
    int nodes_visited = 0;
    for (const extra_descr_data *node = extra_descriptions;
         node != nullptr && nodes_visited < MaxExtraDescriptionSnapshotNodes; node = node->next) {
        if (std::find(seen_nodes.begin(), seen_nodes.end(), node) != seen_nodes.end())
            break;
        seen_nodes.push_back(node);
        ++nodes_visited;

        JsGameExtraDescriptionFixture fixture;
        fixture.keyword = copy_c_string(node->keyword);
        fixture.description = copy_c_string(node->description, MaxAdapterTextLength);
        fixtures.push_back(std::move(fixture));
    }
    return fixtures;
}

std::vector<JsGameRoomExitFixture> room_exits_fixture(const room_data &room_data,
                                                      const JsGameAdapterOptions &options) {
    std::vector<JsGameRoomExitFixture> fixtures;
    for (int direction_index = 0; direction_index < NUM_OF_DIRS; ++direction_index) {
        const room_direction_data *exit = room_data.dir_option[direction_index];
        if (exit == nullptr)
            continue;

        JsGameRoomExitFixture fixture;
        fixture.direction_index = direction_index;
        fixture.direction = dirs != nullptr && dirs[direction_index] != nullptr
                                ? copy_c_string(dirs[direction_index])
                                : "unknown";
        fixture.has_to_room_vnum = js_game_adapter_room_is_valid(exit->to_room, options);
        if (fixture.has_to_room_vnum)
            fixture.to_room_vnum = options.world[exit->to_room].number;
        fixture.keyword = copy_c_string(exit->keyword);
        fixture.description = copy_c_string(exit->general_description, MaxAdapterTextLength);
        fixture.key_vnum = exit->key;
        fixture.width = exit->exit_width;
        fixture.flags = exit_flag_names(exit->exit_info);
        fixtures.push_back(std::move(fixture));
    }
    return fixtures;
}

bool object_is_worn_by(const obj_data *object, const char_data *carrier) {
    if (object == nullptr || carrier == nullptr)
        return false;

    return std::find(carrier->equipment, carrier->equipment + MAX_WEAR, object) !=
           carrier->equipment + MAX_WEAR;
}

bool object_is_directly_contained_by(const obj_data *object, const obj_data *container) {
    if (object == nullptr || container == nullptr || object == container)
        return false;
    std::vector<const obj_data *> seen_nodes;
    int nodes_visited = 0;
    for (const obj_data *node = container->contains;
         node != nullptr && nodes_visited < MaxObjectContainerMembershipNodes;
         node = node->next_content) {
        if (std::find(seen_nodes.begin(), seen_nodes.end(), node) != seen_nodes.end())
            break;
        seen_nodes.push_back(node);
        ++nodes_visited;
        if (node == object)
            return true;
    }
    return false;
}

bool shallow_object_fixture(const obj_data *object, const JsGameAdapterOptions &options,
                            JsGameEquipmentObjectFixture *fixture) {
    if (fixture == nullptr || !js_game_adapter_is_live_object(object, options))
        return false;

    const char *display_name =
        object->short_description != nullptr ? object->short_description : object->name;
    fixture->id = object_id(*object, options);
    fixture->name = copy_c_string(display_name);
    fixture->description = copy_c_string(object->description);
    fixture->short_description = copy_c_string(display_name);
    fixture->has_action_description = object->action_description != nullptr;
    fixture->action_description = copy_c_string(object->action_description);
    fixture->vnum = object_vnum(*object, options);
    fixture->flags = object_flags_fixture(object->obj_flags);
    fixture->affects = object_affects_fixture(object->affected);
    fixture->extra_descriptions = extra_descriptions_fixture(object->ex_description);
    fixture->touched = object->touched != 0;
    fixture->has_room = false;
    return true;
}

bool object_is_carried_by(const obj_data *object, const char_data *carrier) {
    if (object == nullptr || carrier == nullptr)
        return false;

    for (const obj_data *carried = carrier->carrying; carried != nullptr;
         carried = carried->next_content) {
        if (carried == object)
            return true;
    }
    return false;
}

std::vector<JsGameEquipmentObjectFixture>
object_contents_fixture(const obj_data *container, const JsGameAdapterOptions &options) {
    std::vector<JsGameEquipmentObjectFixture> contents;
    std::vector<const obj_data *> seen_nodes;
    int nodes_visited = 0;
    for (const obj_data *node = container != nullptr ? container->contains : nullptr;
         node != nullptr && nodes_visited < MaxObjectContentsSnapshotNodes;
         node = node->next_content) {
        if (std::find(seen_nodes.begin(), seen_nodes.end(), node) != seen_nodes.end())
            break;
        seen_nodes.push_back(node);
        ++nodes_visited;
        if (node == container || !js_game_adapter_is_live_object(node, options))
            break;
        if (node->in_obj != container || node->in_room != NOWHERE || node->carried_by != nullptr)
            continue;
        JsGameEquipmentObjectFixture fixture;
        if (shallow_object_fixture(node, options, &fixture))
            contents.push_back(std::move(fixture));
    }
    return contents;
}

bool character_has_follower(const char_data *leader, const char_data *follower,
                            const JsGameAdapterOptions &options) {
    if (leader == nullptr || follower == nullptr)
        return false;

    constexpr int MaxFollowerSnapshotNodes = 100;
    int nodes_visited = 0;
    std::vector<const follow_type *> seen_nodes;
    for (const follow_type *node = leader->followers;
         node != nullptr && nodes_visited < MaxFollowerSnapshotNodes; node = node->next) {
        if (std::find(seen_nodes.begin(), seen_nodes.end(), node) != seen_nodes.end())
            break;
        seen_nodes.push_back(node);
        ++nodes_visited;
        if (!js_game_adapter_is_live_character(node->follower, options))
            continue;
        if (node->follower == follower && node->fol_number == follower->abs_number)
            return true;
    }
    return false;
}

bool character_mount_has_rider(const char_data *mount, const char_data *rider,
                               const JsGameAdapterOptions &options) {
    if (mount == nullptr || rider == nullptr)
        return false;

    constexpr int MaxRiderSnapshotNodes = 100;
    int nodes_visited = 0;
    std::vector<const char_data *> seen_riders;
    for (const char_data *node = mount->mount_data.rider;
         node != nullptr && nodes_visited < MaxRiderSnapshotNodes;
         node = node->mount_data.next_rider) {
        if (!js_game_adapter_is_live_character(node, options))
            return false;
        if (std::find(seen_riders.begin(), seen_riders.end(), node) != seen_riders.end())
            break;
        seen_riders.push_back(node);
        ++nodes_visited;
        if (node == rider && node->mount_data.mount == mount &&
            node->mount_data.mount_number == mount->abs_number)
            return true;
        if (node->mount_data.next_rider != nullptr) {
            if (!js_game_adapter_is_live_character(node->mount_data.next_rider, options))
                break;
            if (node->mount_data.next_rider_number != node->mount_data.next_rider->abs_number)
                break;
        }
    }
    return false;
}

bool character_reference_fixture(const char_data *character, const JsGameAdapterOptions &options,
                                 JsGameCharacterReferenceFixture *fixture) {
    if (fixture == nullptr || !js_game_adapter_is_live_character(character, options))
        return false;

    fixture->id = character_id(*character, options);
    fixture->name = copy_c_string(GET_NAME(character));
    fixture->race = race_name(*character, options);
    fixture->vnum = character_vnum(*character, options);
    fixture->prototype_vnum =
        character_is_npc(*character) ? character_vnum(*character, options) : -1;
    fixture->level = GET_LEVEL(character);
    fixture->is_npc = character_is_npc(*character);
    return true;
}

bool mount_fixture(const char_data *character, const JsGameAdapterOptions &options,
                   JsGameMountFixture *fixture) {
    if (fixture == nullptr || character == nullptr)
        return false;

    *fixture = JsGameMountFixture{};
    if (js_game_adapter_is_live_character(character->mount_data.mount, options) &&
        character->mount_data.mount_number == character->mount_data.mount->abs_number &&
        character_mount_has_rider(character->mount_data.mount, character, options)) {
        fixture->has_mount =
            character_reference_fixture(character->mount_data.mount, options, &fixture->mount);
        fixture->is_riding = fixture->has_mount;
    }
    if (js_game_adapter_is_live_character(character->mount_data.rider, options) &&
        character->mount_data.rider_number == character->mount_data.rider->abs_number &&
        character->mount_data.rider->mount_data.mount == character &&
        character->mount_data.rider->mount_data.mount_number == character->abs_number) {
        fixture->has_rider =
            character_reference_fixture(character->mount_data.rider, options, &fixture->rider);
        fixture->is_mounted = fixture->has_rider;
    }
    if (fixture->is_riding &&
        js_game_adapter_is_live_character(character->mount_data.next_rider, options) &&
        character->mount_data.next_rider_number == character->mount_data.next_rider->abs_number &&
        character->mount_data.next_rider->mount_data.mount == character->mount_data.mount &&
        character->mount_data.next_rider->mount_data.mount_number ==
            character->mount_data.mount->abs_number) {
        fixture->has_next_rider = character_reference_fixture(character->mount_data.next_rider,
                                                              options, &fixture->next_rider);
    }
    return true;
}

bool equipment_object_fixture(const obj_data *object, const char_data *wearer,
                              const JsGameAdapterOptions &options,
                              JsGameEquipmentObjectFixture *fixture) {
    if (fixture == nullptr || !js_game_adapter_is_live_object(object, options))
        return false;
    if (wearer == nullptr || object->carried_by != wearer || object->in_room != NOWHERE ||
        object->in_obj != nullptr || !object_is_worn_by(object, wearer))
        return false;

    return shallow_object_fixture(object, options, fixture);
}

bool inventory_object_fixture(const obj_data *object, const char_data *carrier,
                              const JsGameAdapterOptions &options,
                              JsGameEquipmentObjectFixture *fixture) {
    if (fixture == nullptr || !js_game_adapter_is_live_object(object, options))
        return false;
    if (carrier == nullptr || object->carried_by != carrier || object->in_room != NOWHERE ||
        object->in_obj != nullptr || !object_is_carried_by(object, carrier) ||
        object_is_worn_by(object, carrier))
        return false;

    return shallow_object_fixture(object, options, fixture);
}

int room_index_for_pointer(const room_data *room, const JsGameAdapterOptions &options) {
    if (room == nullptr || options.world == nullptr)
        return -1;
    for (std::size_t index = 0; index < options.world_count; ++index) {
        if (&options.world[index] == room)
            return static_cast<int>(index);
    }
    return -1;
}

const char *target_type_name(int target_type) {
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
                                   JsGameTargetFixture *fixture) {
    if (fixture == nullptr ||
        !js_game_adapter_character_fixture(character, options, &fixture->character))
        return false;
    fixture->type = "character";
    fixture->has_character = true;
    fixture->character.id = "target";
    return true;
}

bool target_fixture_from_object(const obj_data *object, const JsGameAdapterOptions &options,
                                JsGameTargetFixture *fixture) {
    if (fixture == nullptr || !js_game_adapter_object_fixture(object, options, &fixture->object))
        return false;
    fixture->type = "object";
    fixture->has_object = true;
    fixture->object.id = "target";
    return true;
}

bool target_fixture_from_room(int room, const JsGameAdapterOptions &options,
                              JsGameTargetFixture *fixture) {
    if (fixture == nullptr || !js_game_adapter_room_fixture(room, options, &fixture->room))
        return false;
    fixture->type = "room";
    fixture->has_room = true;
    fixture->room.id = "target";
    return true;
}

bool target_fixture_from_target_data(const target_data *target, const JsGameAdapterOptions &options,
                                     JsGameTargetFixture *fixture) {
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

void set_target_fixture_id(JsGameTargetFixture &fixture, const char *id) {
    if (fixture.has_character)
        fixture.character.id = id;
    if (fixture.has_object)
        fixture.object.id = id;
    if (fixture.has_room)
        fixture.room.id = id;
}

const char *wear_slot_name(int wear_slot) {
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

bool js_game_adapter_is_live_character(const char_data *character,
                                       const JsGameAdapterOptions &options) {
    return pointer_in_table(character, options.live_characters, options.live_character_count);
}

bool js_game_adapter_is_live_object(const obj_data *object, const JsGameAdapterOptions &options) {
    return pointer_in_table(object, options.live_objects, options.live_object_count);
}

bool js_game_adapter_room_is_valid(int room, const JsGameAdapterOptions &options) {
    if (options.world == nullptr || room < 0)
        return false;
    if (options.world_count > 0)
        return static_cast<std::size_t>(room) < options.world_count;
    return options.top_of_world >= 0 && room <= options.top_of_world;
}

bool js_game_adapter_character_fixture(const char_data *character,
                                       const JsGameAdapterOptions &options,
                                       JsGameCharacterFixture *fixture) {
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
    fixture->class_points = character->classpoints;
    fixture->interrupt_count = character->interrupt_count;
    fixture->interrupt_time = character->interrupt_time;
    fixture->special_busy = character->spec_busy;
    fixture->profile.name = copy_c_string(character->player.name);
    fixture->profile.short_description = character->player.short_descr != nullptr
                                             ? copy_c_string(character->player.short_descr)
                                             : fixture->profile.name;
    fixture->profile.has_long_description = character->player.long_descr != nullptr;
    fixture->profile.long_description = copy_c_string(character->player.long_descr);
    fixture->profile.has_description = character->player.description != nullptr;
    fixture->profile.description = copy_c_string(character->player.description);
    fixture->profile.has_title = character->player.title != nullptr;
    fixture->profile.title = copy_c_string(character->player.title);
    fixture->profile.has_death_cry = character->player.death_cry != nullptr;
    fixture->profile.death_cry = copy_c_string(character->player.death_cry);
    fixture->profile.has_death_cry2 = character->player.death_cry2 != nullptr;
    fixture->profile.death_cry2 = copy_c_string(character->player.death_cry2);
    fixture->profile.corpse_number = character->player.corpse_num;
    fixture->profile.race_id = character->player.race;
    fixture->profile.sex = character->player.sex;
    fixture->profile.body_type = character->player.bodytype;
    fixture->profile.profession = character->player.prof;
    fixture->profile.level = character->player.level;
    fixture->profile.language = character->player.language;
    fixture->profile.hometown = character->player.hometown;
    fixture->profile.birth_epoch_seconds = static_cast<std::int64_t>(character->player.time.birth);
    fixture->profile.logon_epoch_seconds = static_cast<std::int64_t>(character->player.time.logon);
    fixture->profile.played_seconds = character->player.time.played;
    fixture->profile.weight = character->player.weight;
    fixture->profile.height = character->player.height;
    fixture->profile.ranking = character->player.ranking;
    fixture->profile.talks.clear();
    fixture->profile.talks.reserve(MAX_TOUNGE);
    for (int index = 0; index < MAX_TOUNGE; ++index)
        fixture->profile.talks.push_back(character->player.talks[index]);
    fixture->base_abilities.strength = character->abilities.str;
    fixture->base_abilities.intelligence = character->abilities.intel;
    fixture->base_abilities.willpower = character->abilities.wil;
    fixture->base_abilities.dexterity = character->abilities.dex;
    fixture->base_abilities.constitution = character->abilities.con;
    fixture->base_abilities.leadership = character->abilities.lea;
    fixture->current_abilities.strength = character->tmpabilities.str;
    fixture->current_abilities.intelligence = character->tmpabilities.intel;
    fixture->current_abilities.willpower = character->tmpabilities.wil;
    fixture->current_abilities.dexterity = character->tmpabilities.dex;
    fixture->current_abilities.constitution = character->tmpabilities.con;
    fixture->current_abilities.leadership = character->tmpabilities.lea;
    fixture->rolled_abilities.strength = character->constabilities.str;
    fixture->rolled_abilities.intelligence = character->constabilities.intel;
    fixture->rolled_abilities.willpower = character->constabilities.wil;
    fixture->rolled_abilities.dexterity = character->constabilities.dex;
    fixture->rolled_abilities.constitution = character->constabilities.con;
    fixture->rolled_abilities.leadership = character->constabilities.lea;
    fixture->points.bodypart_hits.clear();
    fixture->points.bodypart_hits.reserve(MAX_BODYPARTS);
    for (int index = 0; index < MAX_BODYPARTS; ++index)
        fixture->points.bodypart_hits.push_back(character->points.bodypart_hit[index]);
    fixture->points.gold = character->points.gold;
    fixture->points.experience = character->points.exp;
    fixture->points.spirit = character->points.spirit;
    fixture->points.mana_regen = character->points.mana_regen;
    fixture->points.health_regen = character->points.health_regen;
    fixture->points.move_regen = character->points.move_regen;
    fixture->points.offense = character->points.OB;
    fixture->points.damage = character->points.damage;
    fixture->points.energy_regen = character->points.ENE_regen;
    fixture->points.parry = character->points.parry;
    fixture->points.dodge = character->points.dodge;
    fixture->points.encumbrance = character->points.encumb;
    fixture->points.willpower = character->points.willpower;
    fixture->points.spell_penetration = character->points.spell_pen;
    fixture->points.spell_power = character->points.spell_power;
    fixture->specials.is_fighting = character->specials.fighting != nullptr;
    fixture->specials.is_hunting = character->specials.hunting != nullptr;
    fixture->specials.has_memory = character->specials.memory != nullptr;
    fixture->specials.position = character_position_name(character->specials.position);
    fixture->specials.default_position = character_position_name(character->specials.default_pos);
    fixture->specials.carry_weight = character->specials.carry_weight;
    fixture->specials.worn_weight = character->specials.worn_weight;
    fixture->specials.encumbrance_weight = character->specials.encumb_weight;
    fixture->specials.carry_items = character->specials.carry_items;
    fixture->specials.timer = character->specials.timer;
    fixture->specials.was_in_room = character->specials.was_in_room;
    fixture->specials.energy = character->specials.ENERGY;
    fixture->specials.current_parry = character->specials.current_parry;
    fixture->specials.last_direction =
        table_name_or_empty(dirs, character->specials.last_direction);
    fixture->specials.attack_type = character->specials.attack_type;
    fixture->specials.script_number = character->specials.script_number;
    fixture->specials.current_bodypart = character->specials.current_bodypart;
    fixture->specials.tactics = character_tactics_name(*character);
    fixture->specials.prompt_number = character->specials.prompt_number;
    fixture->specials.prompt_value = character->specials.prompt_value;
    fixture->specials.home_zone = character->specials.homezone;
    fixture->specials.load_line = character->specials.load_line;
    fixture->specials2.load_room = character->specials2.load_room;
    fixture->specials2.spells_to_learn = character->specials2.spells_to_learn;
    fixture->specials2.alignment = character->specials2.alignment;
    fixture->specials2.act_flags = character_act_flags(*character);
    fixture->specials2.preference_flags =
        named_flags(character->specials2.pref, PreferenceFlagNames, std::size(PreferenceFlagNames));
    fixture->specials2.wimp_level = character->specials2.wimp_level;
    fixture->specials2.freeze_level = character->specials2.freeze_level;
    fixture->specials2.saving_throw = character->specials2.saving_throw;
    fixture->specials2.raw_perception = character->specials2.rawPerception;
    fixture->specials2.perception = character->specials2.perception;
    fixture->specials2.conditions.drunk = character->specials2.conditions[0];
    fixture->specials2.conditions.full = character->specials2.conditions[1];
    fixture->specials2.conditions.thirst = character->specials2.conditions[2];
    fixture->specials2.mini_level = character->specials2.mini_level;
    fixture->specials2.max_mini_level = character->specials2.max_mini_level;
    fixture->specials2.morale = character->specials2.morale;
    fixture->specials2.rerolls = character->specials2.rerolls;
    fixture->specials2.leg_encumbrance = character->specials2.leg_encumb;
    fixture->specials2.retired_on = character->specials2.retiredon;
    fixture->specials2.hide_flags =
        named_flags(character->specials2.hide_flags, HideFlagNames, std::size(HideFlagNames));
    fixture->specials2.tactics = named_value(character->specials2.tactics, CharacterTacticNames,
                                             std::size(CharacterTacticNames));
    fixture->specials2.shooting = named_value(character->specials2.shooting, CharacterShootingNames,
                                              std::size(CharacterShootingNames));
    fixture->specials2.casting = named_value(character->specials2.casting, CharacterCastingNames,
                                             std::size(CharacterCastingNames));
    fixture->specials2.two_handed = character->specials2.two_handed != 0;
    fixture->professions.clear();
    if (character->profs != nullptr) {
        fixture->professions.reserve(std::size(CharacterProfessionFields));
        for (const CharacterProfessionField &profession : CharacterProfessionFields) {
            JsGameProfessionFixture profession_fixture;
            profession_fixture.key = profession.key;
            profession_fixture.name = profession.name;
            profession_fixture.level = character->profs->prof_level[profession.index];
            profession_fixture.points = character->profs->prof_coof[profession.index];
            profession_fixture.coefficient = character->profs->prof_coof[profession.index];
            profession_fixture.experience = character->profs->prof_exp[profession.index];
            fixture->professions.push_back(std::move(profession_fixture));
        }
    }
    const CharacterSpecializationField &selected_specialization = character_specialization_field(
        character->profs != nullptr ? character->profs->specialization : game_types::PS_None);
    const CharacterSpecializationField &current_specialization =
        character_specialization_field(character->extra_specialization_data.get_current_spec());
    fixture->specializations.selected_id = selected_specialization.id;
    fixture->specializations.selected_key = selected_specialization.key;
    fixture->specializations.selected_name = selected_specialization.name;
    fixture->specializations.current_id = current_specialization.id;
    fixture->specializations.current_key = current_specialization.key;
    fixture->specializations.current_name = current_specialization.name;
    fixture->specializations.is_mage_specialization =
        character->extra_specialization_data.is_mage_spec();
    fixture->specializations.has_runtime_state =
        character->extra_specialization_data.current_spec_info != nullptr;
    fixture->damage_details.entries.clear();
    fixture->damage_details.elapsed_combat_seconds =
        character->damage_details.get_elapsed_combat_seconds();
    fixture->damage_details.total_damage = 0;
    for (const auto &damage_entry : character->damage_details.get_damage_map()) {
        fixture->damage_details.total_damage += damage_entry.second.get_total_damage();
    }
    const double combat_seconds = std::max(fixture->damage_details.elapsed_combat_seconds, 0.5);
    fixture->damage_details.damage_per_second =
        static_cast<double>(fixture->damage_details.total_damage) / combat_seconds;
    fixture->damage_details.entries.reserve(character->damage_details.get_damage_map().size());
    for (const auto &damage_entry : character->damage_details.get_damage_map()) {
        JsGameDamageEntryFixture entry_fixture;
        const auto [source_kind, source_name] = damage_source_metadata(damage_entry.first);
        entry_fixture.source_id = damage_entry.first;
        entry_fixture.source_kind = source_kind;
        entry_fixture.source_name = source_name;
        entry_fixture.instance_count = damage_entry.second.get_instance_count();
        entry_fixture.total_damage = damage_entry.second.get_total_damage();
        entry_fixture.largest_damage = damage_entry.second.get_largest_damage();
        entry_fixture.average_damage =
            entry_fixture.instance_count > 0
                ? static_cast<double>(entry_fixture.total_damage) / entry_fixture.instance_count
                : 0;
        entry_fixture.percent_of_total = fixture->damage_details.total_damage > 0
                                             ? (static_cast<double>(entry_fixture.total_damage) /
                                                fixture->damage_details.total_damage) *
                                                   100
                                             : 0;
        fixture->damage_details.entries.push_back(std::move(entry_fixture));
    }
    fixture->skills.clear();
    if (character->skills != nullptr) {
        const skill_data *skill_table = get_skill_array();
        if (skill_table != nullptr) {
            for (int skill_id = 0; skill_id < MAX_SKILLS; ++skill_id) {
                const int practice = character->skills[skill_id];
                const std::string name = copy_c_string(skill_table[skill_id].name);
                if (practice <= 0 || name.empty())
                    continue;
                JsGameSkillValueFixture skill_fixture;
                skill_fixture.id = skill_id;
                skill_fixture.name = name;
                skill_fixture.profession = skill_profession_key(skill_table[skill_id].type);
                skill_fixture.level = skill_table[skill_id].level;
                skill_fixture.practice = practice;
                skill_fixture.minimum_position = skill_table[skill_id].minimum_position;
                skill_fixture.mana_cost = skill_table[skill_id].min_usesmana;
                skill_fixture.beats = skill_table[skill_id].beats;
                skill_fixture.targets = skill_table[skill_id].targets;
                skill_fixture.learn_difficulty = skill_table[skill_id].learn_diff;
                skill_fixture.learn_type = skill_table[skill_id].learn_type;
                skill_fixture.is_fast = skill_table[skill_id].is_fast != 0;
                skill_fixture.specialization = skill_table[skill_id].skill_spec;
                fixture->skills.push_back(std::move(skill_fixture));
            }
        }
    }
    fixture->knowledge.clear();
    if (character->knowledge != nullptr) {
        const skill_data *skill_table = get_skill_array();
        if (skill_table != nullptr) {
            for (int skill_id = 0; skill_id < MAX_SKILLS; ++skill_id) {
                const int knowledge = character->knowledge[skill_id];
                const std::string name = copy_c_string(skill_table[skill_id].name);
                if (knowledge <= 0 || name.empty())
                    continue;
                JsGameKnowledgeValueFixture knowledge_fixture;
                knowledge_fixture.id = skill_id;
                knowledge_fixture.name = name;
                knowledge_fixture.profession = skill_profession_key(skill_table[skill_id].type);
                knowledge_fixture.level = skill_table[skill_id].level;
                knowledge_fixture.knowledge = knowledge;
                knowledge_fixture.minimum_position = skill_table[skill_id].minimum_position;
                knowledge_fixture.mana_cost = skill_table[skill_id].min_usesmana;
                knowledge_fixture.beats = skill_table[skill_id].beats;
                knowledge_fixture.targets = skill_table[skill_id].targets;
                knowledge_fixture.learn_difficulty = skill_table[skill_id].learn_diff;
                knowledge_fixture.learn_type = skill_table[skill_id].learn_type;
                knowledge_fixture.is_fast = skill_table[skill_id].is_fast != 0;
                knowledge_fixture.specialization = skill_table[skill_id].skill_spec;
                fixture->knowledge.push_back(std::move(knowledge_fixture));
            }
        }
    }
    fixture->affects.clear();
    const skill_data *skill_table = get_skill_array();
    int affect_count = 0;
    for (const affected_type *affect = character->affected;
         affect != nullptr && affect_count < MAX_AFFECT; affect = affect->next, ++affect_count) {
        JsGameAffectFixture affect_fixture;
        affect_fixture.type = affect->type;
        if (skill_table != nullptr && affect->type >= 0 && affect->type < MAX_SKILLS) {
            const std::string name = copy_c_string(skill_table[affect->type].name);
            if (!name.empty())
                affect_fixture.name = name;
        }
        affect_fixture.duration = affect->duration;
        affect_fixture.time_phase = affect->time_phase;
        affect_fixture.modifier = affect->modifier;
        affect_fixture.location = affect->location;
        affect_fixture.location_name = table_name_at(apply_types, affect->location, 40);
        affect_fixture.bitvector = affect->bitvector;
        affect_fixture.bitvector_names = affect_bitvector_names(affect->bitvector);
        affect_fixture.counter = affect->counter;
        fixture->affects.push_back(std::move(affect_fixture));
    }
    fixture->equipment.clear();
    fixture->equipment.reserve(MAX_WEAR);
    for (int slot = 0; slot < MAX_WEAR; ++slot) {
        JsGameEquipmentSlotFixture slot_fixture;
        slot_fixture.slot_index = slot;
        slot_fixture.slot_name = slot < static_cast<int>(std::size(EquipmentSlotNames))
                                     ? EquipmentSlotNames[slot]
                                     : "unknown";
        slot_fixture.has_object = equipment_object_fixture(character->equipment[slot], character,
                                                           options, &slot_fixture.object);
        fixture->equipment.push_back(std::move(slot_fixture));
    }
    fixture->inventory.clear();
    constexpr int MaxInventorySnapshotItems = 100;
    int inventory_nodes_visited = 0;
    std::vector<const obj_data *> seen_inventory_nodes;
    for (const obj_data *carried = character->carrying;
         carried != nullptr && inventory_nodes_visited < MaxInventorySnapshotItems;
         carried = carried->next_content) {
        if (std::find(seen_inventory_nodes.begin(), seen_inventory_nodes.end(), carried) !=
            seen_inventory_nodes.end())
            break;
        seen_inventory_nodes.push_back(carried);
        ++inventory_nodes_visited;
        JsGameEquipmentObjectFixture inventory_fixture;
        if (inventory_object_fixture(carried, character, options, &inventory_fixture)) {
            fixture->inventory.push_back(std::move(inventory_fixture));
        }
    }
    fixture->followers.clear();
    constexpr int MaxFollowerSnapshotNodes = 100;
    int follower_nodes_visited = 0;
    std::vector<const follow_type *> seen_follower_nodes;
    std::vector<const char_data *> seen_followers;
    for (const follow_type *follower = character->followers;
         follower != nullptr && follower_nodes_visited < MaxFollowerSnapshotNodes;
         follower = follower->next) {
        if (std::find(seen_follower_nodes.begin(), seen_follower_nodes.end(), follower) !=
            seen_follower_nodes.end())
            break;
        seen_follower_nodes.push_back(follower);
        ++follower_nodes_visited;
        if (!js_game_adapter_is_live_character(follower->follower, options) ||
            follower->fol_number != follower->follower->abs_number ||
            follower->follower->master != character)
            continue;
        if (std::find(seen_followers.begin(), seen_followers.end(), follower->follower) !=
            seen_followers.end())
            continue;
        seen_followers.push_back(follower->follower);
        JsGameCharacterReferenceFixture follower_fixture;
        if (character_reference_fixture(follower->follower, options, &follower_fixture))
            fixture->followers.push_back(std::move(follower_fixture));
    }
    fixture->has_master = false;
    if (js_game_adapter_is_live_character(character->master, options) &&
        character_has_follower(character->master, character, options))
        fixture->has_master =
            character_reference_fixture(character->master, options, &fixture->master);
    mount_fixture(character, options, &fixture->mount);
    fixture->is_npc = character_is_npc(*character);
    fixture->has_room = js_game_adapter_room_fixture(character->in_room, options, &fixture->room);
    return true;
}

bool js_game_adapter_object_fixture(const obj_data *object, const JsGameAdapterOptions &options,
                                    JsGameObjectFixture *fixture) {
    if (fixture == nullptr || !js_game_adapter_is_live_object(object, options))
        return false;

    const char *display_name =
        object->short_description != nullptr ? object->short_description : object->name;
    fixture->id = object_id(*object, options);
    fixture->name = copy_c_string(display_name);
    fixture->description = copy_c_string(object->description);
    fixture->short_description = copy_c_string(display_name);
    fixture->has_action_description = object->action_description != nullptr;
    fixture->action_description = copy_c_string(object->action_description);
    fixture->vnum = object_vnum(*object, options);
    fixture->flags = object_flags_fixture(object->obj_flags);
    fixture->affects = object_affects_fixture(object->affected);
    fixture->extra_descriptions = extra_descriptions_fixture(object->ex_description);
    fixture->has_container = false;
    if (object->in_room == NOWHERE && js_game_adapter_is_live_object(object->in_obj, options) &&
        object_is_directly_contained_by(object, object->in_obj)) {
        fixture->has_container =
            shallow_object_fixture(object->in_obj, options, &fixture->container);
    }
    fixture->contents = object_contents_fixture(object, options);
    fixture->touched = object->touched != 0;
    fixture->has_room = js_game_adapter_room_fixture(object->in_room, options, &fixture->room);

    fixture->has_carried_by = false;
    fixture->has_worn_by = false;
    if (object->in_room == NOWHERE && object->in_obj == nullptr &&
        js_game_adapter_is_live_character(object->carried_by, options)) {
        if (object_is_worn_by(object, object->carried_by)) {
            fixture->has_worn_by =
                js_game_adapter_character_fixture(object->carried_by, options, &fixture->worn_by);
        } else if (object_is_carried_by(object, object->carried_by)) {
            fixture->has_carried_by = js_game_adapter_character_fixture(object->carried_by, options,
                                                                        &fixture->carried_by);
        }
    }
    return true;
}

bool js_game_adapter_room_fixture(int room, const JsGameAdapterOptions &options,
                                  JsGameRoomFixture *fixture) {
    if (fixture == nullptr || !js_game_adapter_room_is_valid(room, options))
        return false;

    const room_data &room_data = options.world[room];
    fixture->id = "room:" + std::to_string(room_data.number);
    fixture->name = copy_c_string(room_data.name);
    fixture->description = copy_c_string(room_data.description);
    fixture->vnum = room_data.number;
    fixture->level = room_data.level;
    fixture->sector_type = room_sector_type_name(room_data.sector_type);
    fixture->flags = room_flag_names(room_data.room_flags);
    fixture->extra_descriptions = extra_descriptions_fixture(room_data.ex_description);
    fixture->exits = room_exits_fixture(room_data, options);
    fixture->alignment = room_data.alignment;
    fixture->light = room_data.light;
    fixture->is_sunlit = room_is_sunlit(room_data);
    fixture->has_zone = js_game_adapter_zone_fixture(room_data.zone, options, &fixture->zone);
    return true;
}

bool js_game_adapter_zone_fixture(int zone, const JsGameAdapterOptions &options,
                                  JsGameZoneFixture *fixture) {
    if (fixture == nullptr || options.zones == nullptr || zone < 0 ||
        static_cast<std::size_t>(zone) >= options.zone_count) {
        return false;
    }

    const zone_data &zone_data = options.zones[zone];
    fixture->id = "zone:" + std::to_string(zone_data.number);
    fixture->name = copy_c_string(zone_data.name);
    fixture->has_description = zone_data.description != nullptr;
    fixture->description = copy_c_string(zone_data.description);
    fixture->has_map = zone_data.map != nullptr;
    fixture->map = copy_c_string(zone_data.map);
    fixture->vnum = zone_data.number;
    fixture->level = zone_data.level;
    fixture->lifespan = zone_data.lifespan;
    fixture->age = zone_data.age;
    fixture->top_room_vnum = zone_data.top;
    fixture->x = zone_data.x;
    fixture->y = zone_data.y;
    fixture->symbol = zone_data.symbol == '\0' ? "" : std::string(1, zone_data.symbol);
    fixture->white_power = zone_data.white_power;
    fixture->dark_power = zone_data.dark_power;
    fixture->magi_power = zone_data.magi_power;
    fixture->minimum_look_level = zone_data.min_level_look;
    fixture->reset_mode = zone_data.reset_mode;
    return true;
}

JsGameTriggerContextFixture js_game_adapter_context_fixture(const JsGameAdapterContextInput &input,
                                                            const JsGameAdapterOptions &options) {
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
    const bool has_explicit_target = input.target_character != nullptr ||
                                     input.target_object != nullptr || input.target_room >= 0;
    if (input.target_character != nullptr)
        context.has_target =
            target_fixture_from_character(input.target_character, options, &context.target);
    else if (input.target_object != nullptr)
        context.has_target =
            target_fixture_from_object(input.target_object, options, &context.target);
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
