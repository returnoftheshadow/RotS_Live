#include "js_api_contract.h"

#include <cstring>

namespace {

constexpr JsApiContractMetadata ContractMetadata = {
    1,
    2,
    "unpublished",
    "rots-js-api-contract-v1-revision-2",
    "unpublished-2",
    "unpublished-2",
    "1",
    "Fixture and live trigger execution expose frozen read-only context data, pure result helpers, "
    "and a type-only mutation result contract; side-effect host bindings remain deferred.",
};

constexpr JsApiMember CharacterMembers[] = {
    {"id", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Stable invocation-local handle id for diagnostics and offline fixtures."},
    {"vnum", JsApiMemberKind::Property, "number | null", "", true, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Prototype vnum when the character has one; player characters may not have a builder vnum."},
    {"prototypeVnum", JsApiMemberKind::Property, "number | null", "", true, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Mobile prototype vnum when the character is an NPC, or null when unavailable."},
    {"name", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Display name visible to the script author for messages and conditions."},
    {"level", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Current character level."},
    {"experience", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Current experience value, matching the legacy CHx_EXP script parameter."},
    {"rank", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Current fame-war ranking value, matching the legacy CHx_RANK script parameter."},
    {"race", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Readable race name from the server race table."},
    {"room", JsApiMemberKind::Property, "Room | null", "", true, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Current room handle, or null when the character is not in a valid room."},
    {"isNpc", JsApiMemberKind::Property, "boolean", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "True when the handle refers to an NPC."},
    {"isPlayer", JsApiMemberKind::Property, "boolean", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "True when the handle refers to a player character."},
    {"hitPoints", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Current hit point value."},
    {"maxHitPoints", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Maximum hit point value after current modifiers."},
    {"classPoints", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Character-creation class point value copied into the invocation snapshot."},
    {"interruptCount", JsApiMemberKind::Property, "number", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Current interrupt count copied into the invocation snapshot for combat/casting diagnostics."},
    {"interruptTime", JsApiMemberKind::Property, "number", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Countdown before interrupt count decays copied into the invocation snapshot."},
    {"specialBusy", JsApiMemberKind::Property, "boolean", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "True when a special procedure is busy in the invocation snapshot."},
    {"baseAbilities", JsApiMemberKind::Property, "AbilityScores", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Frozen read-only snapshot of base ability scores before active affects, equipment, and "
     "other temporary recalculation modifiers."},
    {"currentAbilities", JsApiMemberKind::Property, "AbilityScores", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Frozen read-only snapshot of currently modified ability scores after active affects, "
     "equipment, and recalculation helpers have been applied."},
    {"rolledAbilities", JsApiMemberKind::Property, "AbilityScores", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Frozen read-only snapshot of rolled character creation ability scores."},
    {"points", JsApiMemberKind::Property, "CharacterPoints", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Frozen read-only snapshot of body-part hit values, carried gold, experience, spirit, regen, "
     "offense, damage, defense, encumbrance, willpower, and spell combat values."},
    {"specials", JsApiMemberKind::Property, "CharacterSpecials", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Frozen read-only snapshot of safe runtime character state. Pointer, alias, union, procedure, "
     "memory-list, and immortal poof-string internals are not exposed."},
    {"specials2", JsApiMemberKind::Property, "CharacterSpecials2", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Frozen read-only snapshot of additional persistent character state. Persistent identity, "
     "owner ids, raw roleplay/teaching bitvectors, and authentication failure counters are not "
     "exposed."},
    {"professions", JsApiMemberKind::Property, "readonly Profession[]", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Frozen read-only profession progression snapshot for mage, mystic, ranger, and warrior. "
     "Color settings, specialization state, and direct profession mutation are not exposed."},
    {"specializations", JsApiMemberKind::Property, "SpecializationData", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Frozen read-only specialization summary. Raw runtime specialization subclass state, "
     "targets, off-hand object pointers, timestamps, and direct mutation are not exposed."},
    {"isValid", JsApiMemberKind::Method, "() => boolean", "boolean", false, false,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedPureHelper, "pure",
     "Checks whether this invocation-local handle still points at a live entity."},
};

constexpr JsApiMember CharacterPointsMembers[] = {
    {"bodypartHits", JsApiMemberKind::Property, "readonly number[]", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Per-body-part hit values copied from the invocation snapshot."},
    {"gold", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Gold carried by the character. This exact economy value is intentionally exposed read-only "
     "for trigger parity and must not become writable without audit and authorization semantics."},
    {"experience", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Experience stored in character point data."},
    {"spirit", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Current spirit point value."},
    {"manaRegen", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Current mana regeneration modifier."},
    {"healthRegen", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Current health regeneration modifier."},
    {"moveRegen", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Current movement regeneration modifier."},
    {"offense", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Normal-tactics offensive bonus value."},
    {"damage", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Normal-tactics damage bonus value."},
    {"energyRegen", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Combat energy regeneration value."},
    {"parry", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Normal-tactics parry value."},
    {"dodge", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Normal-tactics dodge value."},
    {"encumbrance", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Current encumbrance value used by combat/casting/skill calculations."},
    {"willpower", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Mental-combat willpower value."},
    {"spellPenetration", JsApiMemberKind::Property, "number", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Current spell penetration value."},
    {"spellPower", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Current spell power value."},
};

constexpr JsApiMember CharacterSpecialsMembers[] = {
    {"isFighting", JsApiMemberKind::Property, "boolean", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "True when the character has a live fighting target pointer in the invocation snapshot."},
    {"isHunting", JsApiMemberKind::Property, "boolean", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "True when the character has a live hunting target pointer in the invocation snapshot."},
    {"hasMemory", JsApiMemberKind::Property, "boolean", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "True when the character has a non-empty NPC memory list without exposing the list entries."},
    {"position", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Current position name."},
    {"defaultPosition", JsApiMemberKind::Property, "string", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Default position name used by NPC posture enforcement."},
    {"carryWeight", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Total carried weight counter."},
    {"wornWeight", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Worn equipment weight counter."},
    {"encumbranceWeight", JsApiMemberKind::Property, "number", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Encumbrance weight counter used by skill/casting calculations."},
    {"carryItems", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Carried item count."},
    {"timer", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Runtime idle/update timer value."},
    {"wasInRoom", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Stored previous room index for linkdead state."},
    {"energy", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Current combat energy value."},
    {"currentParry", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Current parry split value."},
    {"lastDirection", JsApiMemberKind::Property, "string | null", "", true, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Last movement direction name, or null when no valid direction is stored."},
    {"attackType", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "NPC attack type bitvector value."},
    {"scriptNumber", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Runtime script vnum marker."},
    {"currentBodypart", JsApiMemberKind::Property, "number", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Current body-part index used by combat messaging."},
    {"tactics", JsApiMemberKind::Property, "string | null", "", true, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Player tactics name, or null for NPCs because the source field is overloaded."},
    {"promptNumber", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Prompt template number."},
    {"promptValue", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Prompt value inserted into prompt text."},
    {"homeZone", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Zone where this mobile was loaded."},
    {"loadLine", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Zone reset line that loaded this mobile."},
};

constexpr JsApiMember CharacterConditionsMembers[] = {
    {"drunk", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Drunk condition counter."},
    {"full", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Fullness condition counter."},
    {"thirst", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Thirst condition counter."},
};

constexpr JsApiMember CharacterSpecials2Members[] = {
    {"loadRoom", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Saved load room vnum."},
    {"spellsToLearn", JsApiMemberKind::Property, "number", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Practice sessions available to learn spells or skills."},
    {"alignment", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Character alignment value."},
    {"actFlags", JsApiMemberKind::Property, "readonly string[]", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Canonical player or NPC act flag names in server order."},
    {"preferenceFlags", JsApiMemberKind::Property, "readonly string[]", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Canonical preference flag names in server order."},
    {"wimpLevel", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Wimpy flee hit-point threshold."},
    {"freezeLevel", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Level of the immortal freeze marker."},
    {"savingThrow", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Saving throw modifier."},
    {"rawPerception", JsApiMemberKind::Property, "number", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Raw unclamped perception value."},
    {"perception", JsApiMemberKind::Property, "number", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Effective perception value."},
    {"conditions", JsApiMemberKind::Property, "CharacterConditions", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Frozen read-only condition counters."},
    {"miniLevel", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Minimum level metadata."},
    {"maxMiniLevel", JsApiMemberKind::Property, "number", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Maximum mini-level metadata."},
    {"morale", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Morale metadata value."},
    {"rerolls", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Reroll count."},
    {"legEncumbrance", JsApiMemberKind::Property, "number", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Leg encumbrance value used by movement and dodge calculations."},
    {"retiredOn", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Retirement timestamp marker."},
    {"hideFlags", JsApiMemberKind::Property, "readonly string[]", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Canonical hide flag names in server order."},
    {"tactics", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Normalized tactics name."},
    {"shooting", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Normalized shooting speed name."},
    {"casting", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Normalized casting speed name."},
    {"twoHanded", JsApiMemberKind::Property, "boolean", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "True when two-handed mode is active."},
};

constexpr JsApiMember AbilityScoresMembers[] = {
    {"strength", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Strength score in this ability snapshot."},
    {"intelligence", JsApiMemberKind::Property, "number", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Intelligence score in this ability snapshot."},
    {"willpower", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Willpower score in this ability snapshot."},
    {"dexterity", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Dexterity score in this ability snapshot."},
    {"constitution", JsApiMemberKind::Property, "number", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Constitution score in this ability snapshot."},
    {"leadership", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Leadership score in this ability snapshot."},
};

constexpr JsApiMember PlayerMembers[] = {
    {"accountName", JsApiMemberKind::Property, "string | null", "", true, true,
     JsApiSideEffect::None, JsApiMemberStatus::Deferred, "deferred",
     "Account identifiers are deferred until the script API has an explicit exposure policy; "
     "use display-safe character fields instead."},
    {"rank", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Current player rank value."},
};

constexpr JsApiMember MobMembers[] = {
    {"prototypeVnum", JsApiMemberKind::Property, "number | null", "", true, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Mobile prototype vnum backing this NPC, or null when the prototype cannot be resolved."},
};

constexpr JsApiMember GameObjectMembers[] = {
    {"id", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Stable invocation-local object handle id for diagnostics and offline fixtures."},
    {"vnum", JsApiMemberKind::Property, "number | null", "", true, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Object prototype vnum."},
    {"name", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Object display name."},
    {"description", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Room-visible object description copied into the invocation snapshot."},
    {"shortDescription", JsApiMemberKind::Property, "string", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Short object description used when carried, worn, or listed in inventories."},
    {"actionDescription", JsApiMemberKind::Property, "string | null", "", true, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Optional use/action description text copied into the invocation snapshot when present."},
    {"flags", JsApiMemberKind::Property, "ObjectFlags", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Structured read-only object flag snapshot. It exposes symbolic item type, wear flags, extra "
     "flags, material, and scalar economy/timer fields without exposing the "
     "legacy flag storage or bitvectors."},
    {"room", JsApiMemberKind::Property, "Room | null", "", true, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Direct room containing the object, or null when carried, worn, nested, or invalid."},
    {"carriedBy", JsApiMemberKind::Property, "Character | null", "", true, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Live character carrying the object, or null when worn, in a room, nested, stale, or invalid."},
    {"wornBy", JsApiMemberKind::Property, "Character | null", "", true, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Live character wearing the object, or null when carried, in a room, nested, stale, or invalid."},
    {"isValid", JsApiMemberKind::Method, "() => boolean", "boolean", false, false,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedPureHelper, "pure",
     "Checks whether this invocation-local handle still points at a live object."},
};

constexpr JsApiMember ObjectFlagsMembers[] = {
    {"itemType", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Symbolic object type name, or Unknown when the loaded type is outside the server vocabulary."},
    {"wearFlags", JsApiMemberKind::Property, "readonly string[]", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Builder-safe symbolic wear flag names in canonical server order."},
    {"extraFlags", JsApiMemberKind::Property, "readonly string[]", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Builder-safe symbolic extra flag names in canonical server order, excluding unnamed bit "
     "positions."},
    {"level", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Object level from the loaded object flags."},
    {"weight", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Effective object weight clamped by server rules."},
    {"cost", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Object sale cost."},
    {"costPerDay", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Object rent cost per real day."},
    {"timer", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Object timer value."},
    {"rarity", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Object rarity value."},
    {"material", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Symbolic material name, or Unknown when the loaded material is outside the server vocabulary."},
};

constexpr JsApiMember RoomMembers[] = {
    {"id", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Stable invocation-local room handle id for diagnostics and offline fixtures."},
    {"vnum", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Public room vnum."},
    {"name", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Room display name."},
    {"description", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Room long description copied into the invocation snapshot."},
    {"level", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Room level value."},
    {"sectorType", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Readable sector type name copied from the loaded room snapshot, or Unknown when the loaded "
     "sector value is outside the server vocabulary."},
    {"flags", JsApiMemberKind::Property, "readonly string[]", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Builder-safe room flag names copied from the loaded room snapshot. The list excludes "
     "pathfinding scratch bits such as BFS_MARK and unnamed/internal bit positions."},
    {"alignment", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Room alignment value."},
    {"light", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Current room light-source count copied from the loaded room snapshot."},
    {"isSunlit", JsApiMemberKind::Property, "boolean", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "True when the room currently passes the legacy SCRIPT_IF_ROOM_SUNLIT check."},
    {"zone", JsApiMemberKind::Property, "Zone | null", "", true, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Zone handle for the room, or null when unavailable."},
    {"isValid", JsApiMemberKind::Method, "() => boolean", "boolean", false, false,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedPureHelper, "pure",
     "Checks whether this invocation-local handle still points at a loaded room."},
};

constexpr JsApiMember ZoneMembers[] = {
    {"vnum", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Zone vnum."},
    {"name", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Zone display name."},
    {"description", JsApiMemberKind::Property, "string | null", "", true, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Optional zone description copied into the invocation snapshot when present."},
    {"map", JsApiMemberKind::Property, "string | null", "", true, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Optional zone map text copied into the invocation snapshot when present."},
    {"level", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Zone level value."},
    {"lifespan", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Minutes between zone reset checks from the loaded zone table."},
    {"age", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Current zone age in minutes from the loaded zone table."},
    {"topRoomVnum", JsApiMemberKind::Property, "number", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Upper room vnum bound for the loaded zone."},
    {"x", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Zone map x coordinate."},
    {"y", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Zone map y coordinate."},
    {"symbol", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Single-character zone map symbol."},
    {"whitePower", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "White-side zone power copied from the loaded zone table."},
    {"darkPower", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Dark-side zone power copied from the loaded zone table."},
    {"magiPower", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Magi-side zone power copied from the loaded zone table."},
    {"minimumLookLevel", JsApiMemberKind::Property, "number", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Minimum level required by legacy zone map/detail display paths."},
    {"resetMode", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Legacy reset mode value from the loaded zone table."},
};

constexpr JsApiMember TriggerInfoMembers[] = {
    {"kind", JsApiMemberKind::Property, "'legacy' | 'mudlle'", "", false, false,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Trigger source family used by the server manifest."},
    {"legacyValue", JsApiMemberKind::Property, "number", "", false, false, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Legacy numeric trigger id or ASIMA/Mudlle call flag value."},
    {"legacyName", JsApiMemberKind::Property, "string", "", false, false, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Legacy manifest name such as ON_ENTER or SPECIAL_COMMAND."},
    {"name", JsApiMemberKind::Property, "string", "", false, false, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Compatibility alias for the JavaScript handler name. Prefer handlerName in new scripts."},
    {"handlerName", JsApiMemberKind::Property, "string", "", false, false, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "JavaScript handler name declared by the server trigger manifest."},
    {"hostType", JsApiMemberKind::Property, "'character' | 'object' | 'room' | 'mudlleMobile'", "",
     false, false, JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Host category selected for this invocation."},
    {"blocksGameplay", JsApiMemberKind::Property, "boolean", "", false, false,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "True when false/block return values can stop the underlying game action."},
};

constexpr JsApiMember ScriptContextMembers[] = {
    {"self", JsApiMemberKind::Property, "Character | GameObject | Room | null", "", true, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Script host handle for the current trigger invocation."},
    {"actor", JsApiMemberKind::Property, "Character | null", "", true, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Character that caused the trigger when the call site provides one."},
    {"room", JsApiMemberKind::Property, "Room | null", "", true, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Room relevant to the trigger, or null when unavailable."},
    {"zone", JsApiMemberKind::Property, "Zone | null", "", true, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Zone relevant to the trigger, or null when unavailable."},
    {"trigger", JsApiMemberKind::Property, "TriggerInfo", "", false, false, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Manifest-backed trigger metadata for this invocation."},
    {"hostType", JsApiMemberKind::Property, "'character' | 'object' | 'room' | 'mudlleMobile'", "",
     false, false, JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Host category selected for this invocation."},
    {"killer", JsApiMemberKind::Property, "Character | null", "", true, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Killer or death actor when the legacy call site can provide it; otherwise null."},
    {"object", JsApiMemberKind::Property, "GameObject | null", "", true, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Object relevant to object-host or receive/eat/drink/wear/pull triggers."},
    {"speaker", JsApiMemberKind::Property, "Character | null", "", true, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Speech source role snapshot for hear triggers when the legacy call site provides it."},
    {"text", JsApiMemberKind::Property, "string | null", "", true, false, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Sanitized text payload for speech or command-style triggers."},
    {"attacker", JsApiMemberKind::Property, "Character | null", "", true, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Damage source role snapshot for ON_DAMAGE triggers when the combat call site provides it."},
    {"weapon", JsApiMemberKind::Property, "GameObject | null", "", true, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Weapon object snapshot for ON_DAMAGE when the attacker is wielding a live object, or null."},
    {"wearSlot", JsApiMemberKind::Property, "string | null", "", true, false, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Requested equipment slot name for ON_WEAR when the legacy wear path provides it, or null."},
    {"command", JsApiMemberKind::Property, "string | null", "", true, false, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Command name for Mudlle SPECIAL_COMMAND and SPECIAL_TARGET mobile hooks when available; "
     "null for other hooks until their live dispatchers provide command payload backing."},
    {"args", JsApiMemberKind::Property, "string | null", "", true, false, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Sanitized command argument string for Mudlle SPECIAL_COMMAND and SPECIAL_TARGET mobile "
     "hooks when available; null for other hooks until their live dispatchers provide command "
     "payload backing."},
    {"target", JsApiMemberKind::Property, "Character | GameObject | Room | null", "", true, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Primary typed target when Mudlle SPECIAL_TARGET/SPECIAL_DAMAGE/SPECIAL_DEATH backing can "
     "derive one safely; otherwise null."},
    {"tick", JsApiMemberKind::Property, "number | null", "", true, false, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Server pulse value for Mudlle SPECIAL_SELF heartbeat hooks when live-backed; null for "
     "non-periodic trigger paths."},
    {"direction", JsApiMemberKind::Property, "string | null", "", true, false,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Movement direction for Mudlle SPECIAL_ENTER mobile hooks when available; null for other "
     "hooks until their live dispatchers provide movement payload backing."},
    {"reverseDirection", JsApiMemberKind::Property, "string | null", "", true, false,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Reverse movement direction for Mudlle SPECIAL_ENTER mobile hooks when available; null for "
     "other hooks until their live dispatchers provide movement payload backing."},
    {"continuation", JsApiMemberKind::Property, "never", "", false, false, JsApiSideEffect::None,
     JsApiMemberStatus::Unsupported, "unsupported",
     "JavaScript continuations are not part of v1; SPECIAL_DELAY remains unsupported and this "
     "field is not emitted for supported runtime hooks."},
    {"targ1", JsApiMemberKind::Property, "Character | GameObject | Room | null", "", true, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "First Mudlle legacy target slot as a safe typed handle when the slot is a live character, "
     "object, or room; unsupported target kinds remain null."},
    {"targ2", JsApiMemberKind::Property, "Character | GameObject | Room | null", "", true, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Second Mudlle legacy target slot as a safe typed handle when the slot is a live character, "
     "object, or room; unsupported target kinds remain null."},
    {"targetTypes", JsApiMemberKind::Property, "readonly string[]", "", false, false,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Legacy Mudlle target type names in slot order. The array is always frozen, and unsupported "
     "target kinds are named without exposing raw union payloads."},
    {"victim", JsApiMemberKind::Property, "Character | null", "", true, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Damage victim role snapshot for ON_DAMAGE triggers when the combat call site provides it."},
    {"dying", JsApiMemberKind::Property, "Character | null", "", true, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Dying character for Mudlle SPECIAL_DEATH when that legacy special path invokes JavaScript; "
     "otherwise null."},
};

constexpr JsApiMember MutationResultMembers[] = {
    {"ok", JsApiMemberKind::Property, "boolean", "", false, false, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "True when a future validated setter applies the requested change."},
    {"code", JsApiMemberKind::Property,
     "'ok' | 'invalid-value' | 'out-of-range' | 'not-authorized' | 'stale-handle' | "
     "'unsupported' | 'deferred'",
     "", false, false, JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Stable machine-readable result code. Detailed authorization and audit diagnostics stay in "
     "server logs, not script-visible result values."},
    {"message", JsApiMemberKind::Property, "string | null", "", true, false,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Sanitized builder-facing detail text, or null when no safe detail is available. Messages "
     "are bounded, single-line, and must not include file paths, private identifiers, or internal "
     "field names."},
    {"field", JsApiMemberKind::Property, "string | null", "", true, false,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Public API field or setter argument name related to the result, or null for whole-operation "
     "results."},
};

constexpr JsApiMember ScriptResultMembers[] = {
    {"allow", JsApiMemberKind::Method, "() => true", "true", false, false, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedPureHelper, "pure",
     "Explicit helper for allowing a blocking trigger to continue."},
    {"block", JsApiMemberKind::Method, "() => false", "false", false, false, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedPureHelper, "pure",
     "Explicit helper for blocking a blocking trigger."},
};

constexpr JsApiMember ProfessionMembers[] = {
    {"key", JsApiMemberKind::Property, "'mage' | 'mystic' | 'ranger' | 'warrior'", "", false,
     true, JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Stable lowercase profession key."},
    {"name", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Builder-facing profession display name."},
    {"level", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Current profession level."},
    {"points", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Profession point/coefficient value used by legacy progression calculations."},
    {"coefficient", JsApiMemberKind::Property, "number", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Alias of the stored profession coefficient value; 100 means a full profession coefficient."},
    {"experience", JsApiMemberKind::Property, "number", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Experience tracked for this profession."},
};

constexpr JsApiMember SpecializationDataMembers[] = {
    {"selectedId", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Persisted selected specialization id from the character's profession storage."},
    {"selectedKey", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Stable key for the persisted selection."},
    {"selectedName", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Builder-facing name for the persisted selected specialization."},
    {"currentId", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Runtime current specialization id after specialization state has been initialized."},
    {"currentKey", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Stable key for the runtime current state."},
    {"currentName", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Builder-facing name for the runtime current specialization state."},
    {"isMageSpecialization", JsApiMemberKind::Property, "boolean", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "True when the runtime current specialization is one of the mage-specialization families."},
    {"hasRuntimeState", JsApiMemberKind::Property, "boolean", "", false, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "True when runtime specialization helper state has been allocated for this invocation."},
};

constexpr JsApiMember ScriptMembers[] = {
    {"sendToCharacter", JsApiMemberKind::Method, "(target: Character, text: string) => void",
     "void", false, true, JsApiSideEffect::Output, JsApiMemberStatus::Deferred, "deferred",
     "Output helper candidate; deferred until permission, recursion, and audit rules exist."},
    {"sendToRoom", JsApiMemberKind::Method, "(room: Room, text: string) => void", "void", false,
     true, JsApiSideEffect::Output, JsApiMemberStatus::Deferred, "deferred",
     "Room output helper candidate; deferred until visibility and recursion rules exist."},
    {"loadMob", JsApiMemberKind::Method, "(vnum: number) => never", "never", false, false,
     JsApiSideEffect::WorldMutation, JsApiMemberStatus::Unsupported, "unsupported",
     "World creation is not exposed to builder JavaScript in v1."},
    {"extractCharacter", JsApiMemberKind::Method, "(target: Character) => never", "never", false,
     true, JsApiSideEffect::WorldMutation, JsApiMemberStatus::Unsupported, "unsupported",
     "Entity extraction is not exposed to builder JavaScript in v1."},
};

constexpr JsApiType ApiTypes[] = {
    {"Character", JsApiTypeKind::Interface, "", "Read-only character handle.", CharacterMembers,
     sizeof(CharacterMembers) / sizeof(CharacterMembers[0])},
    {"AbilityScores", JsApiTypeKind::Interface, "",
     "Frozen read-only character ability score snapshot.", AbilityScoresMembers,
     sizeof(AbilityScoresMembers) / sizeof(AbilityScoresMembers[0])},
    {"CharacterPoints", JsApiTypeKind::Interface, "", "Frozen read-only character point snapshot.",
     CharacterPointsMembers, sizeof(CharacterPointsMembers) / sizeof(CharacterPointsMembers[0])},
    {"CharacterSpecials", JsApiTypeKind::Interface, "",
     "Frozen read-only safe character runtime-state snapshot.", CharacterSpecialsMembers,
     sizeof(CharacterSpecialsMembers) / sizeof(CharacterSpecialsMembers[0])},
    {"CharacterConditions", JsApiTypeKind::Interface, "",
     "Frozen read-only character condition counter snapshot.", CharacterConditionsMembers,
     sizeof(CharacterConditionsMembers) / sizeof(CharacterConditionsMembers[0])},
    {"CharacterSpecials2", JsApiTypeKind::Interface, "",
     "Frozen read-only additional character state snapshot.", CharacterSpecials2Members,
     sizeof(CharacterSpecials2Members) / sizeof(CharacterSpecials2Members[0])},
    {"Profession", JsApiTypeKind::Interface, "",
     "Frozen read-only character profession progression entry.", ProfessionMembers,
     sizeof(ProfessionMembers) / sizeof(ProfessionMembers[0])},
    {"SpecializationData", JsApiTypeKind::Interface, "",
     "Frozen read-only character specialization summary.", SpecializationDataMembers,
     sizeof(SpecializationDataMembers) / sizeof(SpecializationDataMembers[0])},
    {"Player", JsApiTypeKind::Interface, "Character", "Read-only player character handle.",
     PlayerMembers, sizeof(PlayerMembers) / sizeof(PlayerMembers[0])},
    {"Mob", JsApiTypeKind::Interface, "Character", "Read-only non-player mobile handle.",
     MobMembers, sizeof(MobMembers) / sizeof(MobMembers[0])},
    {"GameObject", JsApiTypeKind::Interface, "", "Read-only object handle.", GameObjectMembers,
     sizeof(GameObjectMembers) / sizeof(GameObjectMembers[0])},
    {"ObjectFlags", JsApiTypeKind::Interface, "", "Read-only object flag snapshot.",
     ObjectFlagsMembers, sizeof(ObjectFlagsMembers) / sizeof(ObjectFlagsMembers[0])},
    {"Room", JsApiTypeKind::Interface, "", "Read-only room handle.", RoomMembers,
     sizeof(RoomMembers) / sizeof(RoomMembers[0])},
    {"Zone", JsApiTypeKind::Interface, "", "Read-only zone handle.", ZoneMembers,
     sizeof(ZoneMembers) / sizeof(ZoneMembers[0])},
    {"TriggerInfo", JsApiTypeKind::Interface, "", "Manifest-backed trigger metadata.",
     TriggerInfoMembers, sizeof(TriggerInfoMembers) / sizeof(TriggerInfoMembers[0])},
    {"ScriptContext", JsApiTypeKind::Interface, "",
     "Per-invocation trigger context. Handles are not valid across invocations.",
     ScriptContextMembers, sizeof(ScriptContextMembers) / sizeof(ScriptContextMembers[0])},
    {"MutationResult", JsApiTypeKind::Interface, "",
     "Type-only result returned by future validated setter methods. It does not make any setter "
     "callable by itself.",
     MutationResultMembers, sizeof(MutationResultMembers) / sizeof(MutationResultMembers[0])},
    {"ScriptResult", JsApiTypeKind::Namespace, "", "Pure return-value helpers.",
     ScriptResultMembers, sizeof(ScriptResultMembers) / sizeof(ScriptResultMembers[0])},
    {"Script", JsApiTypeKind::Namespace, "",
     "Deferred or unsupported side-effect helpers. Nothing here is callable in v1.", ScriptMembers,
     sizeof(ScriptMembers) / sizeof(ScriptMembers[0])},
};

} // namespace

const JsApiContractMetadata &js_api_contract_metadata() { return ContractMetadata; }

const JsApiType *js_api_contract_types() { return ApiTypes; }

std::size_t js_api_contract_type_count() { return sizeof(ApiTypes) / sizeof(ApiTypes[0]); }

const JsApiType *find_js_api_contract_type(const char *name) {
    if (!name)
        return nullptr;
    for (std::size_t index = 0; index < js_api_contract_type_count(); ++index) {
        if (std::strcmp(ApiTypes[index].name, name) == 0)
            return &ApiTypes[index];
    }
    return nullptr;
}

const JsApiMember *find_js_api_contract_member(const JsApiType &type, const char *name) {
    if (!name)
        return nullptr;
    for (std::size_t index = 0; index < type.member_count; ++index) {
        if (std::strcmp(type.members[index].name, name) == 0)
            return &type.members[index];
    }
    return nullptr;
}

const char *js_api_type_kind_name(JsApiTypeKind kind) {
    switch (kind) {
    case JsApiTypeKind::Class:
        return "class";
    case JsApiTypeKind::Interface:
        return "interface";
    case JsApiTypeKind::Namespace:
        return "namespace";
    }
    return "unknown";
}

const char *js_api_member_kind_name(JsApiMemberKind kind) {
    switch (kind) {
    case JsApiMemberKind::Property:
        return "property";
    case JsApiMemberKind::Method:
        return "method";
    }
    return "unknown";
}

const char *js_api_member_status_name(JsApiMemberStatus status) {
    switch (status) {
    case JsApiMemberStatus::PlannedReadOnly:
        return "planned-read-only";
    case JsApiMemberStatus::PlannedPureHelper:
        return "planned-pure-helper";
    case JsApiMemberStatus::Deferred:
        return "deferred";
    case JsApiMemberStatus::Unsupported:
        return "unsupported";
    }
    return "unknown";
}

const char *js_api_side_effect_name(JsApiSideEffect side_effect) {
    switch (side_effect) {
    case JsApiSideEffect::None:
        return "none";
    case JsApiSideEffect::Output:
        return "output";
    case JsApiSideEffect::Mutation:
        return "mutation";
    case JsApiSideEffect::WorldMutation:
        return "world-mutation";
    }
    return "unknown";
}
