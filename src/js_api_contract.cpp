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
    { "id", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Stable invocation-local handle id for diagnostics and offline fixtures." },
    { "vnum", JsApiMemberKind::Property, "number | null", "", true, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Prototype vnum when the character has one; player characters may not have a builder vnum." },
    { "prototypeVnum", JsApiMemberKind::Property, "number | null", "", true, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Mobile prototype vnum when the character is an NPC, or null when unavailable." },
    { "name", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Display name visible to the script author for messages and conditions." },
    { "level", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Current character level." },
    { "experience", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Current experience value, matching the legacy CHx_EXP script parameter." },
    { "rank", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Current fame-war ranking value, matching the legacy CHx_RANK script parameter." },
    { "race", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Readable race name from the server race table." },
    { "room", JsApiMemberKind::Property, "Room | null", "", true, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Current room handle, or null when the character is not in a valid room." },
    { "isNpc", JsApiMemberKind::Property, "boolean", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "True when the handle refers to an NPC." },
    { "isPlayer", JsApiMemberKind::Property, "boolean", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "True when the handle refers to a player character." },
    { "hitPoints", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Current hit point value." },
    { "maxHitPoints", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Maximum hit point value after current modifiers." },
    { "classPoints", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Character-creation class point value copied into the invocation snapshot." },
    { "interruptCount", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Current interrupt count copied into the invocation snapshot for combat/casting diagnostics." },
    { "interruptTime", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Countdown before interrupt count decays copied into the invocation snapshot." },
    { "specialBusy", JsApiMemberKind::Property, "boolean", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "True when a special procedure is busy in the invocation snapshot." },
    { "profile", JsApiMemberKind::Property, "CharacterProfile", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Frozen read-only snapshot of public character identity, description, demographic, language, "
        "age, and display metadata. Nullable live text is copied as bounded strings." },
    { "baseAbilities", JsApiMemberKind::Property, "AbilityScores", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Frozen read-only snapshot of base ability scores before active affects, equipment, and "
        "other temporary recalculation modifiers." },
    { "currentAbilities", JsApiMemberKind::Property, "AbilityScores", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Frozen read-only snapshot of currently modified ability scores after active affects, "
        "equipment, and recalculation helpers have been applied." },
    { "rolledAbilities", JsApiMemberKind::Property, "AbilityScores", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Frozen read-only snapshot of rolled character creation ability scores." },
    { "points", JsApiMemberKind::Property, "CharacterPoints", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Frozen read-only snapshot of body-part hit values, carried gold, experience, spirit, regen, "
        "offense, damage, defense, encumbrance, willpower, and spell combat values." },
    { "specials", JsApiMemberKind::Property, "CharacterSpecials", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Frozen read-only snapshot of safe runtime character state. Pointer, alias, union, procedure, "
        "memory-list, and immortal poof-string internals are not exposed." },
    { "specials2", JsApiMemberKind::Property, "CharacterSpecials2", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Frozen read-only snapshot of additional persistent character state. Persistent identity, "
        "owner ids, raw roleplay/teaching bitvectors, and authentication failure counters are not "
        "exposed." },
    { "professions", JsApiMemberKind::Property, "readonly Profession[]", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Frozen read-only profession progression snapshot for mage, mystic, ranger, and warrior. "
        "Color settings, specialization state, and direct profession mutation are not exposed." },
    { "specializations", JsApiMemberKind::Property, "SpecializationData", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Frozen read-only specialization summary. Raw runtime specialization subclass state, "
        "targets, off-hand object pointers, timestamps, and direct mutation are not exposed." },
    { "damageDetails", JsApiMemberKind::Property, "DamageDetails", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Frozen read-only combat damage summary. It exposes aggregate and per-source counters only; "
        "combat ownership, threat, XP sharing, timers, and cleanup state are not exposed." },
    { "skills", JsApiMemberKind::Property, "readonly SkillValue[]", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Frozen read-only snapshot of trained skill practice entries. Raw skill storage and direct "
        "training mutation are not exposed." },
    { "knowledge", JsApiMemberKind::Property, "readonly KnowledgeValue[]", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Frozen read-only snapshot of computed skill knowledge entries. Raw knowledge storage and "
        "direct recalculation mutation are not exposed." },
    { "affects", JsApiMemberKind::Property, "readonly Affect[]", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Frozen read-only active affect snapshot. Raw linked-list pointers and direct add/remove "
        "mutation are not exposed." },
    { "equipment", JsApiMemberKind::Property, "readonly EquipmentSlot[]", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Frozen read-only equipment snapshot with one entry for each live wear slot. Raw object "
        "pointer slots and direct wear/remove mutation are not exposed." },
    { "inventory", JsApiMemberKind::Property, "readonly InventoryObjectSnapshot[]", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Frozen read-only carried inventory snapshot capped to the first 100 top-level carried "
        "objects. Raw linked-list pointers, nested contents, owner handles, and direct inventory "
        "mutation are not exposed." },
    { "followers", JsApiMemberKind::Property, "readonly CharacterRelationshipSnapshot[]", "", false,
        true, JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Frozen read-only follower snapshot capped to the first 100 follower-list nodes. Raw "
        "follow_type nodes, recursive character handles, and direct follow mutation are not exposed." },
    { "master", JsApiMemberKind::Property, "CharacterRelationshipSnapshot | null", "", true, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Frozen read-only snapshot of the character being followed when the master pointer is live "
        "and reciprocally owns this follower, or null." },
    { "mount", JsApiMemberKind::Property, "MountData", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Frozen read-only mount-state snapshot. Relationship fields are shallow character snapshots "
        "and are null when stored mount/rider pointers fail live reciprocal validation." },
    { "isValid", JsApiMemberKind::Method, "() => boolean", "boolean", false, false,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedPureHelper, "pure",
        "Checks whether this invocation-local handle still points at a live entity." },
};

constexpr JsApiMember CharacterRelationshipSnapshotMembers[] = {
    { "id", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Opaque invocation-local character snapshot id." },
    { "name", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Visible character display name." },
    { "race", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Builder-facing race name when known." },
    { "vnum", JsApiMemberKind::Property, "number | null", "", true, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Mobile vnum for NPC relationship snapshots, or null for players/unresolved prototypes." },
    { "prototypeVnum", JsApiMemberKind::Property, "number | null", "", true, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Mobile prototype vnum when available, or null." },
    { "level", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Current character level." },
    { "isNpc", JsApiMemberKind::Property, "boolean", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "True when this snapshot represents an NPC." },
    { "isPlayer", JsApiMemberKind::Property, "boolean", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "True when this snapshot represents a player character." },
    { "isValid", JsApiMemberKind::Method, "() => boolean", "boolean", false, false,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedPureHelper, "pure",
        "Returns true for the invocation-local relationship snapshot." },
};

constexpr JsApiMember CharacterProfileMembers[] = {
    { "name", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Copied character name." },
    { "shortDescription", JsApiMemberKind::Property, "string", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Copied short description, falling back to name when the source is missing." },
    { "longDescription", JsApiMemberKind::Property, "string | null", "", true, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Copied long/look description, or null when the source pointer is absent." },
    { "description", JsApiMemberKind::Property, "string | null", "", true, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Copied extra description, or null when the source pointer is absent." },
    { "title", JsApiMemberKind::Property, "string | null", "", true, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Copied player or NPC title, or null when absent." },
    { "deathCry", JsApiMemberKind::Property, "string | null", "", true, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Copied NPC death cry visible in the death room, or null when absent." },
    { "deathCry2", JsApiMemberKind::Property, "string | null", "", true, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Copied NPC death cry visible to adjacent rooms, or null when absent." },
    { "corpseNumber", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Corpse object virtual number metadata." },
    { "raceId", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Raw race id copied for exact comparisons." },
    { "sex", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Legacy sex id." },
    { "bodyType", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Legacy body type id." },
    { "profession", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Primary profession id." },
    { "level", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Character level copied from player data." },
    { "language", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Current spoken language skill id." },
    { "hometown", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Legacy hometown zone id." },
    { "birthEpochSeconds", JsApiMemberKind::Property, "number", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Birth timestamp intentionally exposed as read-only Unix epoch seconds for age logic." },
    { "logonEpochSeconds", JsApiMemberKind::Property, "number", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Last logon timestamp intentionally exposed as read-only Unix epoch seconds for builder "
        "conditions." },
    { "playedSeconds", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Accumulated played time in seconds." },
    { "weight", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Character weight metadata." },
    { "height", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Character height metadata." },
    { "ranking", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Fame-war ranking copied from player data." },
    { "talks", JsApiMemberKind::Property, "readonly number[]", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Copied legacy tongue proficiency slots." },
};

constexpr JsApiMember MountDataMembers[] = {
    { "mount", JsApiMemberKind::Property, "CharacterRelationshipSnapshot | null", "", true, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Character being ridden by this character, or null when this character is not riding a live "
        "reciprocal mount." },
    { "rider", JsApiMemberKind::Property, "CharacterRelationshipSnapshot | null", "", true, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "First character riding this character, or null when this character is not currently "
        "mounted." },
    { "nextRider", JsApiMemberKind::Property, "CharacterRelationshipSnapshot | null", "", true, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Next rider in the same mount chain for this character, or null when there is no validated "
        "next rider." },
    { "isRiding", JsApiMemberKind::Property, "boolean", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "True when the mount relationship points at a live reciprocal mount." },
    { "isMounted", JsApiMemberKind::Property, "boolean", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "True when at least one live reciprocal rider is mounted on this character." },
};

constexpr JsApiMember CharacterPointsMembers[] = {
    { "bodypartHits", JsApiMemberKind::Property, "readonly number[]", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Per-body-part hit values copied from the invocation snapshot." },
    { "gold", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Gold carried by the character. This exact economy value is intentionally exposed read-only "
        "for trigger parity and must not become writable without audit and authorization semantics." },
    { "experience", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Experience stored in character point data." },
    { "spirit", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Current spirit point value." },
    { "manaRegen", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Current mana regeneration modifier." },
    { "healthRegen", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Current health regeneration modifier." },
    { "moveRegen", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Current movement regeneration modifier." },
    { "offense", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Normal-tactics offensive bonus value." },
    { "damage", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Normal-tactics damage bonus value." },
    { "energyRegen", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Combat energy regeneration value." },
    { "parry", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Normal-tactics parry value." },
    { "dodge", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Normal-tactics dodge value." },
    { "encumbrance", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Current encumbrance value used by combat/casting/skill calculations." },
    { "willpower", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Mental-combat willpower value." },
    { "spellPenetration", JsApiMemberKind::Property, "number", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Current spell penetration value." },
    { "spellPower", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Current spell power value." },
};

constexpr JsApiMember CharacterSpecialsMembers[] = {
    { "isFighting", JsApiMemberKind::Property, "boolean", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "True when the character has a live fighting target pointer in the invocation snapshot." },
    { "isHunting", JsApiMemberKind::Property, "boolean", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "True when the character has a live hunting target pointer in the invocation snapshot." },
    { "hasMemory", JsApiMemberKind::Property, "boolean", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "True when the character has a non-empty NPC memory list without exposing the list entries." },
    { "position", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Current position name." },
    { "defaultPosition", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Default position name used by NPC posture enforcement." },
    { "carryWeight", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Total carried weight counter." },
    { "wornWeight", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Worn equipment weight counter." },
    { "encumbranceWeight", JsApiMemberKind::Property, "number", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Encumbrance weight counter used by skill/casting calculations." },
    { "carryItems", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Carried item count." },
    { "timer", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Runtime idle/update timer value." },
    { "wasInRoom", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Stored previous room index for linkdead state." },
    { "energy", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Current combat energy value." },
    { "currentParry", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Current parry split value." },
    { "lastDirection", JsApiMemberKind::Property, "string | null", "", true, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Last movement direction name, or null when no valid direction is stored." },
    { "attackType", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "NPC attack type bitvector value." },
    { "scriptNumber", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Runtime script vnum marker." },
    { "currentBodypart", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Current body-part index used by combat messaging." },
    { "tactics", JsApiMemberKind::Property, "string | null", "", true, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Player tactics name, or null for NPCs because the source field is overloaded." },
    { "promptNumber", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Prompt template number." },
    { "promptValue", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Prompt value inserted into prompt text." },
    { "homeZone", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Zone where this mobile was loaded." },
    { "loadLine", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Zone reset line that loaded this mobile." },
};

constexpr JsApiMember CharacterConditionsMembers[] = {
    { "drunk", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Drunk condition counter." },
    { "full", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Fullness condition counter." },
    { "thirst", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Thirst condition counter." },
};

constexpr JsApiMember CharacterSpecials2Members[] = {
    { "loadRoom", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Saved load room vnum." },
    { "spellsToLearn", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Practice sessions available to learn spells or skills." },
    { "alignment", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Character alignment value." },
    { "actFlags", JsApiMemberKind::Property, "readonly string[]", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Canonical player or NPC act flag names in server order." },
    { "preferenceFlags", JsApiMemberKind::Property, "readonly string[]", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Canonical preference flag names in server order." },
    { "wimpLevel", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Wimpy flee hit-point threshold." },
    { "freezeLevel", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Level of the immortal freeze marker." },
    { "savingThrow", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Saving throw modifier." },
    { "rawPerception", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Raw unclamped perception value." },
    { "perception", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Effective perception value." },
    { "conditions", JsApiMemberKind::Property, "CharacterConditions", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Frozen read-only condition counters." },
    { "miniLevel", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Minimum level metadata." },
    { "maxMiniLevel", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Maximum mini-level metadata." },
    { "morale", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Morale metadata value." },
    { "rerolls", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Reroll count." },
    { "legEncumbrance", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Leg encumbrance value used by movement and dodge calculations." },
    { "retiredOn", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Retirement timestamp marker." },
    { "hideFlags", JsApiMemberKind::Property, "readonly string[]", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Canonical hide flag names in server order." },
    { "tactics", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Normalized tactics name." },
    { "shooting", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Normalized shooting speed name." },
    { "casting", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Normalized casting speed name." },
    { "twoHanded", JsApiMemberKind::Property, "boolean", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "True when two-handed mode is active." },
};

constexpr JsApiMember AbilityScoresMembers[] = {
    { "strength", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Strength score in this ability snapshot." },
    { "intelligence", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Intelligence score in this ability snapshot." },
    { "willpower", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Willpower score in this ability snapshot." },
    { "dexterity", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Dexterity score in this ability snapshot." },
    { "constitution", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Constitution score in this ability snapshot." },
    { "leadership", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Leadership score in this ability snapshot." },
};

constexpr JsApiMember PlayerMembers[] = {
    { "accountName", JsApiMemberKind::Property, "string | null", "", true, true,
        JsApiSideEffect::None, JsApiMemberStatus::Deferred, "deferred",
        "Account identifiers are deferred until the script API has an explicit exposure policy; "
        "use display-safe character fields instead." },
    { "rank", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Current player rank value." },
};

constexpr JsApiMember MobMembers[] = {
    { "prototypeVnum", JsApiMemberKind::Property, "number | null", "", true, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Mobile prototype vnum backing this NPC, or null when the prototype cannot be resolved." },
};

constexpr JsApiMember GameObjectMembers[] = {
    { "id", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Stable invocation-local object handle id for diagnostics and offline fixtures." },
    { "vnum", JsApiMemberKind::Property, "number | null", "", true, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Object prototype vnum." },
    { "name", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Object display name." },
    { "description", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Room-visible object description copied into the invocation snapshot." },
    { "shortDescription", JsApiMemberKind::Property, "string", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Short object description used when carried, worn, or listed in inventories." },
    { "actionDescription", JsApiMemberKind::Property, "string | null", "", true, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Optional use/action description text copied into the invocation snapshot when present." },
    { "flags", JsApiMemberKind::Property, "ObjectFlags", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Structured read-only object flag snapshot. It exposes symbolic item type, wear flags, extra "
        "flags, material, and scalar economy/timer fields without exposing the "
        "legacy flag storage or bitvectors." },
    { "affects", JsApiMemberKind::Property, "readonly ObjectAffect[]", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Frozen read-only fixed-slot object affect snapshot. Empty none-location plus zero-modifier "
        "slots "
        "are omitted; malformed legacy nonzero slots are still exposed for diagnostics." },
    { "extraDescriptions", JsApiMemberKind::Property, "readonly ExtraDescription[]", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Frozen read-only object extra-description entries copied from the live linked list with "
        "bounded "
        "entry count and text length." },
    { "container", JsApiMemberKind::Property, "EquipmentObjectSnapshot | null", "", true, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Shallow read-only object snapshot containing this object, or null when the object is not "
        "nested "
        "in another live object with reciprocal contents membership." },
    { "contents", JsApiMemberKind::Property, "readonly EquipmentObjectSnapshot[]", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Frozen read-only direct contents snapshot. Entries are shallow object snapshots copied from "
        "live reciprocal contained-object links with bounded cycle-safe traversal." },
    { "touched", JsApiMemberKind::Property, "boolean", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Read-only runtime/player-interaction marker. It is true when legacy object state records any "
        "nonzero touched value and cannot be changed directly from JavaScript." },
    { "room", JsApiMemberKind::Property, "Room | null", "", true, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Direct room containing the object, or null when carried, worn, nested, or invalid." },
    { "carriedBy", JsApiMemberKind::Property, "Character | null", "", true, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Live character carrying the object, or null when worn, in a room, nested, stale, or "
        "invalid." },
    { "wornBy", JsApiMemberKind::Property, "Character | null", "", true, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Live character wearing the object, or null when carried, in a room, nested, stale, or "
        "invalid." },
    { "isValid", JsApiMemberKind::Method, "() => boolean", "boolean", false, false,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedPureHelper, "pure",
        "Checks whether this invocation-local handle still points at a live object." },
};

constexpr JsApiMember ObjectFlagsMembers[] = {
    { "itemType", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Symbolic object type name, or Unknown when the loaded type is outside the server "
        "vocabulary." },
    { "wearFlags", JsApiMemberKind::Property, "readonly string[]", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Builder-safe symbolic wear flag names in canonical server order." },
    { "extraFlags", JsApiMemberKind::Property, "readonly string[]", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Builder-safe symbolic extra flag names in canonical server order, excluding unnamed bit "
        "positions." },
    { "level", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Object level from the loaded object flags." },
    { "weight", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Effective object weight clamped by server rules." },
    { "cost", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Object sale cost." },
    { "costPerDay", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Object rent cost per real day." },
    { "timer", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Object timer value." },
    { "rarity", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Object rarity value." },
    { "material", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Symbolic material name, or Unknown when the loaded material is outside the server "
        "vocabulary." },
};

constexpr JsApiMember RoomMembers[] = {
    { "id", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Stable invocation-local room handle id for diagnostics and offline fixtures." },
    { "vnum", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Public room vnum." },
    { "name", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Room display name." },
    { "description", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Room long description copied into the invocation snapshot." },
    { "level", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Room level value." },
    { "sectorType", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Readable sector type name copied from the loaded room snapshot, or Unknown when the loaded "
        "sector value is outside the server vocabulary." },
    { "flags", JsApiMemberKind::Property, "readonly string[]", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Builder-safe room flag names copied from the loaded room snapshot. The list excludes "
        "pathfinding scratch bits such as BFS_MARK and unnamed/internal bit positions." },
    { "extraDescriptions", JsApiMemberKind::Property, "readonly ExtraDescription[]", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Bounded read-only room extra-description entries copied from the loaded linked list." },
    { "exits", JsApiMemberKind::Property, "readonly RoomExit[]", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Frozen read-only room exit entries copied by direction from loaded room exits." },
    { "contents", JsApiMemberKind::Property, "readonly EquipmentObjectSnapshot[]", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Frozen read-only direct room object snapshots copied from the room contents linked list. "
        "Entries are shallow object snapshots without ownership handles or recursive contents." },
    { "characters", JsApiMemberKind::Property, "readonly CharacterRelationshipSnapshot[]", "", false,
        true, JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Frozen read-only shallow occupant snapshots copied from the room people linked list. "
        "Entries omit recursive followers, master, mount, group, inventory, equipment, and setters." },
    { "affects", JsApiMemberKind::Property, "readonly Affect[]", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Frozen read-only room affect snapshots copied from the room affect linked list." },
    { "alignment", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Room alignment value." },
    { "light", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Current room light-source count copied from the loaded room snapshot." },
    { "isSunlit", JsApiMemberKind::Property, "boolean", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "True when the room currently passes the legacy SCRIPT_IF_ROOM_SUNLIT check." },
    { "zone", JsApiMemberKind::Property, "Zone | null", "", true, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Zone handle for the room, or null when unavailable." },
    { "isValid", JsApiMemberKind::Method, "() => boolean", "boolean", false, false,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedPureHelper, "pure",
        "Checks whether this invocation-local handle still points at a loaded room." },
};

constexpr JsApiMember ZoneMembers[] = {
    { "vnum", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Zone vnum." },
    { "name", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Zone display name." },
    { "description", JsApiMemberKind::Property, "string | null", "", true, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Optional zone description copied into the invocation snapshot when present." },
    { "map", JsApiMemberKind::Property, "string | null", "", true, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Optional zone map text copied into the invocation snapshot when present." },
    { "level", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Zone level value." },
    { "lifespan", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Minutes between zone reset checks from the loaded zone table." },
    { "age", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Current zone age in minutes from the loaded zone table." },
    { "topRoomVnum", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Upper room vnum bound for the loaded zone." },
    { "x", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Zone map x coordinate." },
    { "y", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Zone map y coordinate." },
    { "symbol", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Single-character zone map symbol." },
    { "whitePower", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "White-side zone power copied from the loaded zone table." },
    { "darkPower", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Dark-side zone power copied from the loaded zone table." },
    { "magiPower", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Magi-side zone power copied from the loaded zone table." },
    { "minimumLookLevel", JsApiMemberKind::Property, "number", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Minimum level required by legacy zone map/detail display paths." },
    { "resetMode", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Legacy reset mode value from the loaded zone table." },
};

constexpr JsApiMember TriggerInfoMembers[] = {
    { "kind", JsApiMemberKind::Property, "'legacy' | 'mudlle'", "", false, false,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Trigger source family used by the server manifest." },
    { "legacyValue", JsApiMemberKind::Property, "number", "", false, false, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Legacy numeric trigger id or ASIMA/Mudlle call flag value." },
    { "legacyName", JsApiMemberKind::Property, "string", "", false, false, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Legacy manifest name such as ON_ENTER or SPECIAL_COMMAND." },
    { "name", JsApiMemberKind::Property, "string", "", false, false, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Compatibility alias for the JavaScript handler name. Prefer handlerName in new scripts." },
    { "handlerName", JsApiMemberKind::Property, "string", "", false, false, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "JavaScript handler name declared by the server trigger manifest." },
    { "hostType", JsApiMemberKind::Property, "'character' | 'object' | 'room' | 'mudlleMobile'", "",
        false, false, JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Host category selected for this invocation." },
    { "blocksGameplay", JsApiMemberKind::Property, "boolean", "", false, false,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "True when false/block return values can stop the underlying game action." },
};

constexpr JsApiMember ScriptContextMembers[] = {
    { "self", JsApiMemberKind::Property, "Character | GameObject | Room | null", "", true, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Script host handle for the current trigger invocation." },
    { "actor", JsApiMemberKind::Property, "Character | null", "", true, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Character that caused the trigger when the call site provides one." },
    { "room", JsApiMemberKind::Property, "Room | null", "", true, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Room relevant to the trigger, or null when unavailable." },
    { "zone", JsApiMemberKind::Property, "Zone | null", "", true, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Zone relevant to the trigger, or null when unavailable." },
    { "trigger", JsApiMemberKind::Property, "TriggerInfo", "", false, false, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Manifest-backed trigger metadata for this invocation." },
    { "hostType", JsApiMemberKind::Property, "'character' | 'object' | 'room' | 'mudlleMobile'", "",
        false, false, JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Host category selected for this invocation." },
    { "killer", JsApiMemberKind::Property, "Character | null", "", true, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Killer or death actor when the legacy call site can provide it; otherwise null." },
    { "object", JsApiMemberKind::Property, "GameObject | null", "", true, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Object relevant to object-host or receive/eat/drink/wear/pull triggers." },
    { "speaker", JsApiMemberKind::Property, "Character | null", "", true, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Speech source role snapshot for hear triggers when the legacy call site provides it." },
    { "text", JsApiMemberKind::Property, "string | null", "", true, false, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Sanitized text payload for speech or command-style triggers." },
    { "attacker", JsApiMemberKind::Property, "Character | null", "", true, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Damage source role snapshot for ON_DAMAGE triggers when the combat call site provides it." },
    { "weapon", JsApiMemberKind::Property, "GameObject | null", "", true, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Weapon object snapshot for ON_DAMAGE when the attacker is wielding a live object, or null." },
    { "wearSlot", JsApiMemberKind::Property, "string | null", "", true, false, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Requested equipment slot name for ON_WEAR when the legacy wear path provides it, or null." },
    { "command", JsApiMemberKind::Property, "string | null", "", true, false, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Command name for Mudlle SPECIAL_COMMAND and SPECIAL_TARGET mobile hooks when available; "
        "null for other hooks until their live dispatchers provide command payload backing." },
    { "args", JsApiMemberKind::Property, "string | null", "", true, false, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Sanitized command argument string for Mudlle SPECIAL_COMMAND and SPECIAL_TARGET mobile "
        "hooks when available; null for other hooks until their live dispatchers provide command "
        "payload backing." },
    { "target", JsApiMemberKind::Property, "Character | GameObject | Room | null", "", true, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Primary typed target when Mudlle SPECIAL_TARGET/SPECIAL_DAMAGE/SPECIAL_DEATH backing can "
        "derive one safely; otherwise null." },
    { "tick", JsApiMemberKind::Property, "number | null", "", true, false, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Server pulse value for Mudlle SPECIAL_SELF heartbeat hooks when live-backed; null for "
        "non-periodic trigger paths." },
    { "direction", JsApiMemberKind::Property, "string | null", "", true, false,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Movement direction for Mudlle SPECIAL_ENTER mobile hooks when available; null for other "
        "hooks until their live dispatchers provide movement payload backing." },
    { "reverseDirection", JsApiMemberKind::Property, "string | null", "", true, false,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Reverse movement direction for Mudlle SPECIAL_ENTER mobile hooks when available; null for "
        "other hooks until their live dispatchers provide movement payload backing." },
    { "continuation", JsApiMemberKind::Property, "never", "", false, false, JsApiSideEffect::None,
        JsApiMemberStatus::Unsupported, "unsupported",
        "JavaScript continuations are not part of v1; SPECIAL_DELAY remains unsupported and this "
        "field is not emitted for supported runtime hooks." },
    { "targ1", JsApiMemberKind::Property, "Character | GameObject | Room | null", "", true, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "First Mudlle legacy target slot as a safe typed handle when the slot is a live character, "
        "object, or room; unsupported target kinds remain null." },
    { "targ2", JsApiMemberKind::Property, "Character | GameObject | Room | null", "", true, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Second Mudlle legacy target slot as a safe typed handle when the slot is a live character, "
        "object, or room; unsupported target kinds remain null." },
    { "targetTypes", JsApiMemberKind::Property, "readonly string[]", "", false, false,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Legacy Mudlle target type names in slot order. The array is always frozen, and unsupported "
        "target kinds are named without exposing raw union payloads." },
    { "victim", JsApiMemberKind::Property, "Character | null", "", true, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Damage victim role snapshot for ON_DAMAGE triggers when the combat call site provides it." },
    { "dying", JsApiMemberKind::Property, "Character | null", "", true, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Dying character for Mudlle SPECIAL_DEATH when that legacy special path invokes JavaScript; "
        "otherwise null." },
};

constexpr JsApiMember MutationResultMembers[] = {
    { "ok", JsApiMemberKind::Property, "boolean", "", false, false, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "True when a validated setter or command helper accepts the requested change." },
    { "code", JsApiMemberKind::Property,
        "'ok' | 'invalid-value' | 'out-of-range' | 'not-authorized' | 'stale-handle' | "
        "'unsupported' | 'deferred' | 'invalid-target' | 'not-carried' | 'no-drop' | "
        "'inventory-full' | 'too-heavy' | 'audit-rejected' | 'not-found' | 'already-waiting' | "
        "'no-recipient'",
        "", false, false, JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Stable machine-readable result code. Detailed authorization and audit diagnostics stay in "
        "server logs, not script-visible result values." },
    { "message", JsApiMemberKind::Property, "string | null", "", true, false, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Sanitized builder-facing detail text, or null when no safe detail is available. Messages "
        "are bounded, single-line, and must not include file paths, private identifiers, or internal "
        "field names." },
    { "field", JsApiMemberKind::Property, "string | null", "", true, false, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Public API field or setter argument name related to the result, or null for whole-operation "
        "results." },
};

constexpr JsApiMember ScriptResultMembers[] = {
    { "allow", JsApiMemberKind::Method, "() => true", "true", false, false, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedPureHelper, "pure",
        "Explicit helper for allowing a blocking trigger to continue." },
    { "block", JsApiMemberKind::Method, "() => false", "false", false, false, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedPureHelper, "pure",
        "Explicit helper for blocking a blocking trigger." },
};

constexpr JsApiMember ProfessionMembers[] = {
    { "key", JsApiMemberKind::Property, "'mage' | 'mystic' | 'ranger' | 'warrior'", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Stable lowercase profession key." },
    { "name", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Builder-facing profession display name." },
    { "level", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Current profession level." },
    { "points", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Profession point/coefficient value used by legacy progression calculations." },
    { "coefficient", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Alias of the stored profession coefficient value; 100 means a full profession coefficient." },
    { "experience", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Experience tracked for this profession." },
};

constexpr JsApiMember SpecializationDataMembers[] = {
    { "selectedId", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Persisted selected specialization id from the character's profession storage." },
    { "selectedKey", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Stable key for the persisted selection." },
    { "selectedName", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Builder-facing name for the persisted selected specialization." },
    { "currentId", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Runtime current specialization id after specialization state has been initialized." },
    { "currentKey", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Stable key for the runtime current state." },
    { "currentName", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Builder-facing name for the runtime current specialization state." },
    { "isMageSpecialization", JsApiMemberKind::Property, "boolean", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "True when the runtime current specialization is one of the mage-specialization families." },
    { "hasRuntimeState", JsApiMemberKind::Property, "boolean", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "True when runtime specialization helper state has been allocated for this invocation." },
};

constexpr JsApiMember DamageDetailsMembers[] = {
    { "elapsedCombatSeconds", JsApiMemberKind::Property, "number", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Elapsed combat seconds tracked by the combat engine for this character's current report." },
    { "totalDamage", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Total recorded damage across all tracked sources." },
    { "damagePerSecond", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Total damage divided by combat time, using the same minimum half-second divisor as the "
        "legacy damage report." },
    { "entries", JsApiMemberKind::Property, "readonly DamageEntry[]", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Frozen per-source damage entries ordered by the underlying source id." },
};

constexpr JsApiMember DamageEntryMembers[] = {
    { "sourceId", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Skill id or attack type id used by the combat engine as the damage source." },
    { "sourceKind", JsApiMemberKind::Property, "'skill' | 'attack' | 'unknown'", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Classifies whether the source id resolved to a skill, attack type, or unknown id." },
    { "sourceName", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Builder-facing skill or attack name for the source id." },
    { "instanceCount", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Number of recorded damage instances for this source." },
    { "totalDamage", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Total recorded damage for this source." },
    { "largestDamage", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Largest single recorded damage value for this source." },
    { "averageDamage", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Average recorded damage for this source." },
    { "percentOfTotal", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Percent of the character's total recorded damage represented by this source." },
};

constexpr JsApiMember SkillValueMembers[] = {
    { "id", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Server skill id from the bounded skill table." },
    { "name", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Builder-facing skill name." },
    { "profession", JsApiMemberKind::Property,
        "'general' | 'mage' | 'mystic' | 'ranger' | 'warrior' | 'unknown'", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Profession family that owns the skill entry." },
    { "level", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Profession level normally required by the live skill table." },
    { "practice", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Practice count stored on the character for this skill." },
    { "minimumPosition", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Minimum live position required by the skill table." },
    { "manaCost", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Base mana or spirit cost from the live skill table." },
    { "beats", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Base wait beats from the live skill table." },
    { "targets", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Raw target mask copied read-only from the live skill table for diagnostics." },
    { "learnDifficulty", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Learning difficulty value from the live skill table." },
    { "learnType", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Learning type code from the live skill table." },
    { "isFast", JsApiMemberKind::Property, "boolean", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "True when the live skill table marks this skill as fast-updating." },
    { "specialization", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Specialization id required by the live skill table, or the no-specialization id." },
};

constexpr JsApiMember KnowledgeValueMembers[] = {
    { "id", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Server skill id from the bounded skill table." },
    { "name", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Builder-facing skill name." },
    { "profession", JsApiMemberKind::Property,
        "'general' | 'mage' | 'mystic' | 'ranger' | 'warrior' | 'unknown'", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Profession family that owns the skill entry." },
    { "level", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Profession level normally required by the live skill table." },
    { "knowledge", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Computed knowledge value currently available to the character for this skill." },
    { "minimumPosition", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Minimum live position required by the skill table." },
    { "manaCost", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Base mana or spirit cost from the live skill table." },
    { "beats", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Base wait beats from the live skill table." },
    { "targets", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Raw target mask copied read-only from the live skill table for diagnostics." },
    { "learnDifficulty", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Learning difficulty value from the live skill table." },
    { "learnType", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Learning type code from the live skill table." },
    { "isFast", JsApiMemberKind::Property, "boolean", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "True when the live skill table marks this skill as fast-updating." },
    { "specialization", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Specialization id required by the live skill table, or the no-specialization id." },
};

constexpr JsApiMember AffectMembers[] = {
    { "type", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Server skill or spell id that created this affect." },
    { "name", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Builder-facing skill or spell name when the affect type maps to the live skill table." },
    { "duration", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Remaining affect duration copied from the live affect node." },
    { "timePhase", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Tick phase at which the affect was applied." },
    { "modifier", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Numeric modifier applied to the affect location." },
    { "location", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Raw apply-location id copied read-only for diagnostics." },
    { "locationName", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Builder-facing apply-location name when known." },
    { "bitvector", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Raw affect-flag bitvector copied read-only for diagnostics." },
    { "bitvectorNames", JsApiMemberKind::Property, "readonly string[]", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Builder-facing names for known affect flags set by this affect." },
    { "counter", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Affect-specific counter value copied from the live affect node." },
};

constexpr JsApiMember ObjectAffectMembers[] = {
    { "slotIndex", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Zero-based fixed object affect slot copied from the live object." },
    { "location", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Raw apply-location id copied read-only for diagnostics." },
    { "locationName", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Builder-facing apply-location name when the location id is known." },
    { "modifier", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Numeric modifier applied by this object affect slot." },
};

constexpr JsApiMember ExtraDescriptionMembers[] = {
    { "keyword", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Keyword text used by look/examine commands to match this extra description." },
    { "description", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Description text shown when the keyword matches." },
};

constexpr JsApiMember RoomExitMembers[] = {
    { "directionIndex", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Zero-based live direction index for this exit." },
    { "direction", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Canonical direction name such as north, east, south, west, up, or down." },
    { "toRoomVnum", JsApiMemberKind::Property, "number | null", "", true, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Destination room vnum when the exit points at a loaded room, or null for NOWHERE/stale "
        "exits." },
    { "keyword", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Copied door keyword text used by open, close, lock, and unlock commands." },
    { "description", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Copied look-direction description text." },
    { "keyVnum", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Object vnum of the key, or -1 when none." },
    { "width", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Legacy exit width value copied from the room." },
    { "flags", JsApiMemberKind::Property, "readonly string[]", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Builder-safe symbolic exit flags copied from the live exit bitvector." },
};

constexpr JsApiMember EquipmentSlotMembers[] = {
    { "slotIndex", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Zero-based live wear slot index copied from the fixed MAX_WEAR equipment array." },
    { "slotName", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Builder-facing canonical wear slot name such as head, wield, or belt1." },
    { "object", JsApiMemberKind::Property, "EquipmentObjectSnapshot | null", "", true, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Read-only object snapshot currently worn in this slot, or null when the slot is empty. "
        "The nested object does not expose mutation methods or recursive carriedBy/wornBy ownership "
        "handles." },
};

constexpr JsApiMember EquipmentObjectSnapshotMembers[] = {
    { "id", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Opaque invocation-local object snapshot id for this worn item." },
    { "name", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Object prototype name copied read-only." },
    { "description", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Object room/inventory description copied read-only." },
    { "shortDescription", JsApiMemberKind::Property, "string", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Short object description copied read-only, with the same fallback as GameObject snapshots." },
    { "actionDescription", JsApiMemberKind::Property, "string | null", "", true, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Optional action description copied read-only." },
    { "vnum", JsApiMemberKind::Property, "number | null", "", true, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Resolved object prototype vnum when available, or null." },
    { "flags", JsApiMemberKind::Property, "ObjectFlags", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Filtered object flag/value snapshot copied read-only." },
    { "affects", JsApiMemberKind::Property, "readonly ObjectAffect[]", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Filtered fixed-slot object affect snapshot copied read-only." },
    { "extraDescriptions", JsApiMemberKind::Property, "readonly ExtraDescription[]", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Bounded read-only object extra-description entries copied read-only." },
    { "touched", JsApiMemberKind::Property, "boolean", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Read-only runtime/player-interaction marker copied from the object snapshot." },
    { "room", JsApiMemberKind::Property, "null", "", true, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Always null for equipment snapshots because worn objects are not directly in a room." },
    { "carriedBy", JsApiMemberKind::Property, "null", "", true, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Always null for equipment snapshots to avoid recursive owner handles." },
    { "wornBy", JsApiMemberKind::Property, "null", "", true, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Always null for equipment snapshots to avoid recursive owner handles." },
    { "isValid", JsApiMemberKind::Method, "() => boolean", "boolean", false, false,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedPureHelper, "pure",
        "Returns true for the invocation-local worn-object snapshot." },
};

constexpr JsApiMember InventoryObjectSnapshotMembers[] = {
    { "id", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Opaque invocation-local object snapshot id for this carried item." },
    { "name", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only", "Object prototype name copied read-only." },
    { "description", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Object room/inventory description copied read-only." },
    { "shortDescription", JsApiMemberKind::Property, "string", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Short object description copied read-only, with the same fallback as GameObject snapshots." },
    { "actionDescription", JsApiMemberKind::Property, "string | null", "", true, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Optional action description copied read-only." },
    { "vnum", JsApiMemberKind::Property, "number | null", "", true, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Resolved object prototype vnum when available, or null." },
    { "flags", JsApiMemberKind::Property, "ObjectFlags", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Filtered object flag/value snapshot copied read-only." },
    { "affects", JsApiMemberKind::Property, "readonly ObjectAffect[]", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Filtered fixed-slot object affect snapshot copied read-only." },
    { "extraDescriptions", JsApiMemberKind::Property, "readonly ExtraDescription[]", "", false, true,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Bounded read-only object extra-description entries copied read-only." },
    { "touched", JsApiMemberKind::Property, "boolean", "", false, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Read-only runtime/player-interaction marker copied from the object snapshot." },
    { "room", JsApiMemberKind::Property, "null", "", true, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Always null for inventory snapshots because carried objects are not directly in a room." },
    { "carriedBy", JsApiMemberKind::Property, "null", "", true, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Always null for inventory snapshots to avoid recursive owner handles." },
    { "wornBy", JsApiMemberKind::Property, "null", "", true, true, JsApiSideEffect::None,
        JsApiMemberStatus::PlannedReadOnly, "read-only",
        "Always null for inventory snapshots to avoid recursive owner handles." },
    { "isValid", JsApiMemberKind::Method, "() => boolean", "boolean", false, false,
        JsApiSideEffect::None, JsApiMemberStatus::PlannedPureHelper, "pure",
        "Returns true for the invocation-local carried-object snapshot." },
};

constexpr JsApiMember ScriptMembers[] = {
    { "doWait", JsApiMemberKind::Method, "(pulses: number) => MutationResult", "MutationResult",
        false, false, JsApiSideEffect::Mutation, JsApiMemberStatus::ImplementedSideEffectHelper,
        "builder-zone",
        "Request a bounded legacy-style wait state on the current live character host. Live dispatch "
        "returns inline authority, audit, invalid target, and already-waiting failures before queuing "
        "when possible. V1 does not resume JavaScript continuations after the wait expires." },
    { "doSay", JsApiMemberKind::Method, "(speaker: Character, text: string) => MutationResult",
        "MutationResult", false, true, JsApiSideEffect::Output,
        JsApiMemberStatus::ImplementedSideEffectHelper, "builder-zone",
        "Queue a bounded say action for a live character handle. Live dispatch returns inline target, "
        "recipient, and audit failures before queuing when possible. Text is single-line, length "
        "bounded, and emitted through the command-helper transaction path rather than command text "
        "passthrough." },
    { "doGive", JsApiMemberKind::Method,
        "(giver: Character, recipient: Character, object: GameObject) => MutationResult",
        "MutationResult", false, true, JsApiSideEffect::WorldMutation,
        JsApiMemberStatus::ImplementedSideEffectHelper, "builder-zone",
        "Queue a bounded object-give request between live character and object handles. Live dispatch "
        "requires invocation-local live handles, direct carried-object ownership, target-zone "
        "authority, and transfers through the existing give path. Command-helper audit is still a "
        "follow-up hardening item." },
    { "loadObj", JsApiMemberKind::Method,
        "(vnum: number, target?: Character | Room) => MutationResult", "MutationResult", false, false,
        JsApiSideEffect::WorldMutation, JsApiMemberStatus::ImplementedSideEffectHelper, "builder-zone",
        "Request a bounded object load by prototype vnum. Live dispatch returns inline failure codes "
        "before queuing when possible. Successful `ok` means transaction accepted; apply creates the "
        "object once after recheck. The one-argument form returns an inline result but remains a "
        "no-placement intent until local object variables are designed." },
    { "sendToChar", JsApiMemberKind::Method, "(target: Character, text: string) => MutationResult",
        "MutationResult", false, true, JsApiSideEffect::Output,
        JsApiMemberStatus::ImplementedSideEffectHelper, "builder-zone",
        "Queue bounded text output to a character handle without exposing descriptor or account "
        "objects to builder scripts. Audited live dispatch can expose the coarse `no-recipient` "
        "reachability result, plus target and audit failures, before queuing when possible." },
    { "sendToRoom", JsApiMemberKind::Method, "(room: Room, text: string) => MutationResult",
        "MutationResult", false, true, JsApiSideEffect::Output,
        JsApiMemberStatus::ImplementedSideEffectHelper, "builder-zone",
        "Queue bounded room output for a live room handle. Live dispatch returns inline target, "
        "recipient, and audit failures before queuing when possible." },
    { "sendToRoomExcept", JsApiMemberKind::Method,
        "(room: Room, except: Character, text: string) => MutationResult", "MutationResult", false,
        true, JsApiSideEffect::Output, JsApiMemberStatus::ImplementedSideEffectHelper,
        "builder-zone",
        "Queue bounded room output for a live room handle while excluding one character handle. Live "
        "dispatch returns inline target, recipient, and audit failures before queuing when possible." },
    { "yell", JsApiMemberKind::Method, "(speaker: Character, text: string) => MutationResult",
        "MutationResult", false, true, JsApiSideEffect::Output,
        JsApiMemberStatus::ImplementedSideEffectHelper, "builder-zone",
        "Queue bounded yell-style output for a live character handle without raw command text "
        "passthrough." },
    { "emote", JsApiMemberKind::Method, "(actor: Character, text: string) => MutationResult",
        "MutationResult", false, true, JsApiSideEffect::Output,
        JsApiMemberStatus::ImplementedSideEffectHelper, "builder-zone",
        "Queue bounded emote-style room output for a live character handle without raw command text "
        "passthrough." },
    { "social", JsApiMemberKind::Method,
        "(actor: Character, command: string, target?: Character) => MutationResult",
        "MutationResult", false, true, JsApiSideEffect::Output,
        JsApiMemberStatus::ImplementedSideEffectHelper, "builder-zone",
        "Queue bounded social-style room output for a live character handle and optional character "
        "target. This compatibility helper records the social command name rather than exposing raw "
        "command parser access." },
    { "pageZoneMap", JsApiMemberKind::Method, "(target: Character, zone: Zone) => MutationResult",
        "MutationResult", false, true,
        JsApiSideEffect::Output, JsApiMemberStatus::ImplementedSideEffectHelper, "builder-zone",
        "Queue shaped zone map text for a character handle when the live zone map exists. Missing "
        "zone maps return `not-found`." },
    { "do_wait", JsApiMemberKind::Method, "(pulses: number) => MutationResult", "MutationResult",
        false, false, JsApiSideEffect::Mutation, JsApiMemberStatus::ImplementedSideEffectHelper,
        "builder-zone", "Migration alias for `doWait`; prefer `doWait` in new TypeScript scripts." },
    { "do_say", JsApiMemberKind::Method, "(speaker: Character, text: string) => MutationResult",
        "MutationResult", false, true, JsApiSideEffect::Output,
        JsApiMemberStatus::ImplementedSideEffectHelper, "builder-zone",
        "Migration alias for `doSay`; prefer `doSay` in new TypeScript scripts." },
    { "do_give", JsApiMemberKind::Method,
        "(giver: Character, recipient: Character, object: GameObject) => MutationResult",
        "MutationResult", false, true, JsApiSideEffect::WorldMutation,
        JsApiMemberStatus::ImplementedSideEffectHelper, "builder-zone",
        "Migration alias for `doGive`; prefer `doGive` in new TypeScript scripts." },
    { "load_obj", JsApiMemberKind::Method,
        "(vnum: number, target?: Character | Room) => MutationResult", "MutationResult", false, false,
        JsApiSideEffect::WorldMutation, JsApiMemberStatus::ImplementedSideEffectHelper, "builder-zone",
        "Migration alias for `loadObj`; prefer `loadObj` in new TypeScript scripts." },
    { "send_to_char", JsApiMemberKind::Method, "(target: Character, text: string) => MutationResult",
        "MutationResult", false, true, JsApiSideEffect::Output,
        JsApiMemberStatus::ImplementedSideEffectHelper, "builder-zone",
        "Migration alias for `sendToChar`; prefer `sendToChar` in new TypeScript scripts." },
    { "send_to_room", JsApiMemberKind::Method, "(room: Room, text: string) => MutationResult",
        "MutationResult", false, true, JsApiSideEffect::Output,
        JsApiMemberStatus::ImplementedSideEffectHelper, "builder-zone",
        "Migration alias for `sendToRoom`; prefer `sendToRoom` in new TypeScript scripts." },
    { "send_to_room_x", JsApiMemberKind::Method,
        "(room: Room, except: Character, text: string) => MutationResult", "MutationResult", false,
        true, JsApiSideEffect::Output, JsApiMemberStatus::ImplementedSideEffectHelper,
        "builder-zone",
        "Migration alias for `sendToRoomExcept`; prefer `sendToRoomExcept` in new TypeScript scripts." },
    { "do_yell", JsApiMemberKind::Method, "(speaker: Character, text: string) => MutationResult",
        "MutationResult", false, true, JsApiSideEffect::Output,
        JsApiMemberStatus::ImplementedSideEffectHelper, "builder-zone",
        "Migration alias for `yell`; prefer `yell` in new TypeScript scripts." },
    { "do_emote", JsApiMemberKind::Method, "(actor: Character, text: string) => MutationResult",
        "MutationResult", false, true, JsApiSideEffect::Output,
        JsApiMemberStatus::ImplementedSideEffectHelper, "builder-zone",
        "Migration alias for `emote`; prefer `emote` in new TypeScript scripts." },
    { "do_social", JsApiMemberKind::Method,
        "(actor: Character, command: string, target?: Character) => MutationResult",
        "MutationResult", false, true, JsApiSideEffect::Output,
        JsApiMemberStatus::ImplementedSideEffectHelper, "builder-zone",
        "Migration alias for `social`; prefer `social` in new TypeScript scripts." },
    { "page_zone_map", JsApiMemberKind::Method,
        "(target: Character, zone: Zone) => MutationResult", "MutationResult", false, true,
        JsApiSideEffect::Output, JsApiMemberStatus::ImplementedSideEffectHelper, "builder-zone",
        "Migration alias for `pageZoneMap`; prefer `pageZoneMap` in new TypeScript scripts." },
    { "sendToCharacter", JsApiMemberKind::Method, "(target: Character, text: string) => void",
        "void", false, true, JsApiSideEffect::Output, JsApiMemberStatus::Deferred, "deferred",
        "Output helper candidate; deferred until permission, recursion, and audit rules exist." },
    { "loadMob", JsApiMemberKind::Method, "(vnum: number) => never", "never", false, false,
        JsApiSideEffect::WorldMutation, JsApiMemberStatus::Unsupported, "unsupported",
        "World creation is not exposed to builder JavaScript in v1." },
    { "extractCharacter", JsApiMemberKind::Method, "(target: Character) => never", "never", false,
        true, JsApiSideEffect::WorldMutation, JsApiMemberStatus::Unsupported, "unsupported",
        "Entity extraction is not exposed to builder JavaScript in v1." },
};

constexpr JsApiType ApiTypes[] = {
    { "Character", JsApiTypeKind::Interface, "", "Read-only character handle.", CharacterMembers,
        sizeof(CharacterMembers) / sizeof(CharacterMembers[0]) },
    { "CharacterRelationshipSnapshot", JsApiTypeKind::Interface, "",
        "Frozen read-only shallow character relationship snapshot without mutation helpers.",
        CharacterRelationshipSnapshotMembers,
        sizeof(CharacterRelationshipSnapshotMembers) / sizeof(CharacterRelationshipSnapshotMembers[0]) },
    { "CharacterProfile", JsApiTypeKind::Interface, "",
        "Frozen read-only public character profile snapshot.", CharacterProfileMembers,
        sizeof(CharacterProfileMembers) / sizeof(CharacterProfileMembers[0]) },
    { "MountData", JsApiTypeKind::Interface, "",
        "Frozen read-only character mount relationship snapshot.", MountDataMembers,
        sizeof(MountDataMembers) / sizeof(MountDataMembers[0]) },
    { "AbilityScores", JsApiTypeKind::Interface, "",
        "Frozen read-only character ability score snapshot.", AbilityScoresMembers,
        sizeof(AbilityScoresMembers) / sizeof(AbilityScoresMembers[0]) },
    { "CharacterPoints", JsApiTypeKind::Interface, "", "Frozen read-only character point snapshot.",
        CharacterPointsMembers, sizeof(CharacterPointsMembers) / sizeof(CharacterPointsMembers[0]) },
    { "CharacterSpecials", JsApiTypeKind::Interface, "",
        "Frozen read-only safe character runtime-state snapshot.", CharacterSpecialsMembers,
        sizeof(CharacterSpecialsMembers) / sizeof(CharacterSpecialsMembers[0]) },
    { "CharacterConditions", JsApiTypeKind::Interface, "",
        "Frozen read-only character condition counter snapshot.", CharacterConditionsMembers,
        sizeof(CharacterConditionsMembers) / sizeof(CharacterConditionsMembers[0]) },
    { "CharacterSpecials2", JsApiTypeKind::Interface, "",
        "Frozen read-only additional character state snapshot.", CharacterSpecials2Members,
        sizeof(CharacterSpecials2Members) / sizeof(CharacterSpecials2Members[0]) },
    { "Profession", JsApiTypeKind::Interface, "",
        "Frozen read-only character profession progression entry.", ProfessionMembers,
        sizeof(ProfessionMembers) / sizeof(ProfessionMembers[0]) },
    { "SpecializationData", JsApiTypeKind::Interface, "",
        "Frozen read-only character specialization summary.", SpecializationDataMembers,
        sizeof(SpecializationDataMembers) / sizeof(SpecializationDataMembers[0]) },
    { "DamageDetails", JsApiTypeKind::Interface, "",
        "Frozen read-only character combat damage summary.", DamageDetailsMembers,
        sizeof(DamageDetailsMembers) / sizeof(DamageDetailsMembers[0]) },
    { "DamageEntry", JsApiTypeKind::Interface, "",
        "Frozen read-only per-source combat damage entry.", DamageEntryMembers,
        sizeof(DamageEntryMembers) / sizeof(DamageEntryMembers[0]) },
    { "SkillValue", JsApiTypeKind::Interface, "", "Frozen read-only trained skill practice entry.",
        SkillValueMembers, sizeof(SkillValueMembers) / sizeof(SkillValueMembers[0]) },
    { "KnowledgeValue", JsApiTypeKind::Interface, "",
        "Frozen read-only computed skill knowledge entry.", KnowledgeValueMembers,
        sizeof(KnowledgeValueMembers) / sizeof(KnowledgeValueMembers[0]) },
    { "Affect", JsApiTypeKind::Interface, "", "Frozen read-only active affect entry.", AffectMembers,
        sizeof(AffectMembers) / sizeof(AffectMembers[0]) },
    { "ObjectAffect", JsApiTypeKind::Interface, "",
        "Frozen read-only fixed-slot object affect entry.", ObjectAffectMembers,
        sizeof(ObjectAffectMembers) / sizeof(ObjectAffectMembers[0]) },
    { "ExtraDescription", JsApiTypeKind::Interface, "",
        "Frozen read-only object extra-description entry.", ExtraDescriptionMembers,
        sizeof(ExtraDescriptionMembers) / sizeof(ExtraDescriptionMembers[0]) },
    { "RoomExit", JsApiTypeKind::Interface, "", "Frozen read-only room exit entry.", RoomExitMembers,
        sizeof(RoomExitMembers) / sizeof(RoomExitMembers[0]) },
    { "EquipmentSlot", JsApiTypeKind::Interface, "",
        "Frozen read-only character equipment slot entry.", EquipmentSlotMembers,
        sizeof(EquipmentSlotMembers) / sizeof(EquipmentSlotMembers[0]) },
    { "EquipmentObjectSnapshot", JsApiTypeKind::Interface, "",
        "Frozen read-only worn object snapshot without mutation helpers.",
        EquipmentObjectSnapshotMembers,
        sizeof(EquipmentObjectSnapshotMembers) / sizeof(EquipmentObjectSnapshotMembers[0]) },
    { "InventoryObjectSnapshot", JsApiTypeKind::Interface, "",
        "Frozen read-only carried object snapshot without mutation helpers.",
        InventoryObjectSnapshotMembers,
        sizeof(InventoryObjectSnapshotMembers) / sizeof(InventoryObjectSnapshotMembers[0]) },
    { "Player", JsApiTypeKind::Interface, "Character", "Read-only player character handle.",
        PlayerMembers, sizeof(PlayerMembers) / sizeof(PlayerMembers[0]) },
    { "Mob", JsApiTypeKind::Interface, "Character", "Read-only non-player mobile handle.",
        MobMembers, sizeof(MobMembers) / sizeof(MobMembers[0]) },
    { "GameObject", JsApiTypeKind::Interface, "", "Read-only object handle.", GameObjectMembers,
        sizeof(GameObjectMembers) / sizeof(GameObjectMembers[0]) },
    { "ObjectFlags", JsApiTypeKind::Interface, "", "Read-only object flag snapshot.",
        ObjectFlagsMembers, sizeof(ObjectFlagsMembers) / sizeof(ObjectFlagsMembers[0]) },
    { "Room", JsApiTypeKind::Interface, "", "Read-only room handle.", RoomMembers,
        sizeof(RoomMembers) / sizeof(RoomMembers[0]) },
    { "Zone", JsApiTypeKind::Interface, "", "Read-only zone handle.", ZoneMembers,
        sizeof(ZoneMembers) / sizeof(ZoneMembers[0]) },
    { "TriggerInfo", JsApiTypeKind::Interface, "", "Manifest-backed trigger metadata.",
        TriggerInfoMembers, sizeof(TriggerInfoMembers) / sizeof(TriggerInfoMembers[0]) },
    { "ScriptContext", JsApiTypeKind::Interface, "",
        "Per-invocation trigger context. Handles are not valid across invocations.",
        ScriptContextMembers, sizeof(ScriptContextMembers) / sizeof(ScriptContextMembers[0]) },
    { "MutationResult", JsApiTypeKind::Interface, "",
        "Type-only result returned by validated setter methods and command helpers. It does not make "
        "any setter or helper callable by itself.",
        MutationResultMembers, sizeof(MutationResultMembers) / sizeof(MutationResultMembers[0]) },
    { "ScriptResult", JsApiTypeKind::Namespace, "", "Pure return-value helpers.",
        ScriptResultMembers, sizeof(ScriptResultMembers) / sizeof(ScriptResultMembers[0]) },
    { "Script", JsApiTypeKind::Namespace, "",
        "Validated side-effect helper namespace. CamelCase helpers are the primary TypeScript API; "
        "snake-case names are migration aliases for legacy-derived scripts.",
        ScriptMembers, sizeof(ScriptMembers) / sizeof(ScriptMembers[0]) },
};

} // namespace

const JsApiContractMetadata& js_api_contract_metadata() { return ContractMetadata; }

const JsApiType* js_api_contract_types() { return ApiTypes; }

std::size_t js_api_contract_type_count() { return sizeof(ApiTypes) / sizeof(ApiTypes[0]); }

const JsApiType* find_js_api_contract_type(const char* name)
{
    if (!name)
        return nullptr;
    for (std::size_t index = 0; index < js_api_contract_type_count(); ++index) {
        if (std::strcmp(ApiTypes[index].name, name) == 0)
            return &ApiTypes[index];
    }
    return nullptr;
}

const JsApiMember* find_js_api_contract_member(const JsApiType& type, const char* name)
{
    if (!name)
        return nullptr;
    for (std::size_t index = 0; index < type.member_count; ++index) {
        if (std::strcmp(type.members[index].name, name) == 0)
            return &type.members[index];
    }
    return nullptr;
}

const char* js_api_type_kind_name(JsApiTypeKind kind)
{
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

const char* js_api_member_kind_name(JsApiMemberKind kind)
{
    switch (kind) {
    case JsApiMemberKind::Property:
        return "property";
    case JsApiMemberKind::Method:
        return "method";
    }
    return "unknown";
}

const char* js_api_member_status_name(JsApiMemberStatus status)
{
    switch (status) {
    case JsApiMemberStatus::PlannedReadOnly:
        return "planned-read-only";
    case JsApiMemberStatus::PlannedPureHelper:
        return "planned-pure-helper";
    case JsApiMemberStatus::ImplementedSideEffectHelper:
        return "implemented-side-effect-helper";
    case JsApiMemberStatus::Deferred:
        return "deferred";
    case JsApiMemberStatus::Unsupported:
        return "unsupported";
    }
    return "unknown";
}

const char* js_api_side_effect_name(JsApiSideEffect side_effect)
{
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
