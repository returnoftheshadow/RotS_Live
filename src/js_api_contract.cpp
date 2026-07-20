#include "js_api_contract.h"

#include <cstring>

namespace {

constexpr JsApiContractMetadata ContractMetadata = {
    1,
    1,
    "unpublished",
    "rots-js-api-contract-v1-revision-1",
    "unpublished",
    "unpublished",
    "1",
    "Fixture and live trigger execution expose frozen read-only context data and pure result "
    "helpers; side-effect host bindings remain deferred.",
};

constexpr JsApiMember CharacterMembers[] = {
    {"id", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Stable invocation-local handle id for diagnostics and offline fixtures."},
    {"vnum", JsApiMemberKind::Property, "number | null", "", true, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Prototype vnum when the character has one; player characters may not have a builder vnum."},
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
    {"isValid", JsApiMemberKind::Method, "() => boolean", "boolean", false, false,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedPureHelper, "pure",
     "Checks whether this invocation-local handle still points at a live entity."},
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

constexpr JsApiMember RoomMembers[] = {
    {"id", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Stable invocation-local room handle id for diagnostics and offline fixtures."},
    {"vnum", JsApiMemberKind::Property, "number", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Public room vnum."},
    {"name", JsApiMemberKind::Property, "string", "", false, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Room display name."},
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
     "Character whose speech was heard, when available."},
    {"text", JsApiMemberKind::Property, "string | null", "", true, false, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Sanitized text payload for speech or command-style triggers."},
    {"attacker", JsApiMemberKind::Property, "Character | null", "", true, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Damage source when the combat call site provides one."},
    {"weapon", JsApiMemberKind::Property, "GameObject | null", "", true, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Weapon object involved in damage, or null."},
    {"wearSlot", JsApiMemberKind::Property, "string | null", "", true, false, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Equipment slot name for wear triggers when known."},
    {"command", JsApiMemberKind::Property, "string | null", "", true, false, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Command name for ASIMA/Mudlle command and target call flags."},
    {"args", JsApiMemberKind::Property, "string | null", "", true, false, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only", "Sanitized command argument string."},
    {"target", JsApiMemberKind::Property, "Character | GameObject | Room | null", "", true, true,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Primary command or special-procedure target when available."},
    {"tick", JsApiMemberKind::Property, "number | null", "", true, false, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Server tick or heartbeat metadata for periodic hooks when available."},
    {"direction", JsApiMemberKind::Property, "string | null", "", true, false,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Movement direction for enter-room special hooks when available."},
    {"reverseDirection", JsApiMemberKind::Property, "string | null", "", true, false,
     JsApiSideEffect::None, JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Reverse movement direction for enter-room special hooks when available."},
    {"continuation", JsApiMemberKind::Property, "never", "", false, false, JsApiSideEffect::None,
     JsApiMemberStatus::Unsupported, "unsupported",
     "JavaScript continuations are not part of v1; SPECIAL_DELAY remains unsupported."},
    {"targ1", JsApiMemberKind::Property, "unknown | null", "", true, true, JsApiSideEffect::None,
     JsApiMemberStatus::Deferred, "deferred",
     "Legacy target slot whose safe typed shape is deferred."},
    {"targ2", JsApiMemberKind::Property, "unknown | null", "", true, true, JsApiSideEffect::None,
     JsApiMemberStatus::Deferred, "deferred",
     "Second legacy target slot whose safe typed shape is deferred."},
    {"targetTypes", JsApiMemberKind::Property, "readonly string[]", "", false, false,
     JsApiSideEffect::None, JsApiMemberStatus::Deferred, "deferred",
     "Legacy target type names after a safe mapping is defined."},
    {"victim", JsApiMemberKind::Property, "Character | null", "", true, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Victim character for damage/death special hooks when available."},
    {"dying", JsApiMemberKind::Property, "Character | null", "", true, true, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedReadOnly, "read-only",
     "Dying character for death special hooks when distinct from self."},
};

constexpr JsApiMember ScriptResultMembers[] = {
    {"allow", JsApiMemberKind::Method, "() => true", "true", false, false, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedPureHelper, "pure",
     "Explicit helper for allowing a blocking trigger to continue."},
    {"block", JsApiMemberKind::Method, "() => false", "false", false, false, JsApiSideEffect::None,
     JsApiMemberStatus::PlannedPureHelper, "pure",
     "Explicit helper for blocking a blocking trigger."},
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
    {"Player", JsApiTypeKind::Interface, "Character", "Read-only player character handle.",
     PlayerMembers, sizeof(PlayerMembers) / sizeof(PlayerMembers[0])},
    {"Mob", JsApiTypeKind::Interface, "Character", "Read-only non-player mobile handle.",
     MobMembers, sizeof(MobMembers) / sizeof(MobMembers[0])},
    {"GameObject", JsApiTypeKind::Interface, "", "Read-only object handle.", GameObjectMembers,
     sizeof(GameObjectMembers) / sizeof(GameObjectMembers[0])},
    {"Room", JsApiTypeKind::Interface, "", "Read-only room handle.", RoomMembers,
     sizeof(RoomMembers) / sizeof(RoomMembers[0])},
    {"Zone", JsApiTypeKind::Interface, "", "Read-only zone handle.", ZoneMembers,
     sizeof(ZoneMembers) / sizeof(ZoneMembers[0])},
    {"TriggerInfo", JsApiTypeKind::Interface, "", "Manifest-backed trigger metadata.",
     TriggerInfoMembers, sizeof(TriggerInfoMembers) / sizeof(TriggerInfoMembers[0])},
    {"ScriptContext", JsApiTypeKind::Interface, "",
     "Per-invocation trigger context. Handles are not valid across invocations.",
     ScriptContextMembers, sizeof(ScriptContextMembers) / sizeof(ScriptContextMembers[0])},
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
