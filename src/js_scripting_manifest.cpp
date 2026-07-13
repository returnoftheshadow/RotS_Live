#include "js_scripting_manifest.h"

#include "interpre.h"
#include "script.h"

namespace {

constexpr unsigned CharacterHost = JS_SCRIPTING_HOST_CHARACTER;
constexpr unsigned ObjectHost = JS_SCRIPTING_HOST_OBJECT;
constexpr unsigned RoomHost = JS_SCRIPTING_HOST_ROOM;
constexpr unsigned MudlleMobileHost = JS_SCRIPTING_HOST_MUDLLE_MOBILE;

constexpr JsScriptingManifestMetadata ManifestMetadata = {
    1,
    1,
    1,
    "rots-js-manifest-v1-trigger-catalog-1",
    "unpublished",
    "QuickJS",
    "2026-06-04",
    "2026-06-04",
    "no-modules,no-fs,no-process,no-network,no-persistence,no-async",
    "unpublished",
};

constexpr JsScriptingManifestEntry ManifestEntries[] = {
    { JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER, "ON_ENTER", "onEnter",
        JsScriptingSupportStatus::Deferred, JsScriptingBuilderStatus::Deferred,
        CharacterHost | ObjectHost | RoomHost, false, false, false, false,
        JsScriptingExceptionPolicy::FailOpen,
        "room hook, then room occupants, then room objects while previous handlers allow",
        "self, actor, room, trigger, hostType",
        "Room context is readable, but room-owned JavaScript publishing is deferred because room "
        "script storage is not implemented." },
    { JsScriptingManifestKind::LegacyScriptTrigger, ON_BEFORE_ENTER, "ON_BEFORE_ENTER",
        "onBeforeEnter", JsScriptingSupportStatus::Deferred, JsScriptingBuilderStatus::Deferred,
        CharacterHost, false, false, true, false, JsScriptingExceptionPolicy::FailClosed,
        "room occupants other than the entrant, short-circuiting on block",
        "self, actor, room, trigger, hostType",
        "False/block preserves the legacy movement-prevention behavior." },
    { JsScriptingManifestKind::LegacyScriptTrigger, ON_BEFORE_DIE, "ON_BEFORE_DIE", "onBeforeDie",
        JsScriptingSupportStatus::Reserved, JsScriptingBuilderStatus::Reserved, CharacterHost, false,
        false, true, false, JsScriptingExceptionPolicy::RejectAtPublish,
        "not dispatched by the inspected legacy .scr trigger path", "self, killer, trigger, hostType",
        "Defined in script.h but not currently called by call_trigger(); keep reserved until a "
        "product decision enables it." },
    { JsScriptingManifestKind::LegacyScriptTrigger, ON_DIE, "ON_DIE", "onDie",
        JsScriptingSupportStatus::Deferred, JsScriptingBuilderStatus::Deferred, CharacterHost, false,
        false, true, false, JsScriptingExceptionPolicy::FailClosed,
        "dying character script before death continues", "self, killer, trigger, hostType",
        "Legacy trigger_char_die() does not pass killer context today; JavaScript must decide "
        "compatibility before enabling." },
    { JsScriptingManifestKind::LegacyScriptTrigger, ON_RECEIVE, "ON_RECEIVE", "onReceive",
        JsScriptingSupportStatus::Deferred, JsScriptingBuilderStatus::Deferred, CharacterHost, false,
        false, false, false, JsScriptingExceptionPolicy::FailOpen,
        "receiver character script when an object is received",
        "self, actor, object, trigger, hostType",
        "Receiver is self, giver is actor, received item is object." },
    { JsScriptingManifestKind::LegacyScriptTrigger, ON_EXAMINE_OBJECT, "ON_EXAMINE_OBJECT",
        "onExamineObject", JsScriptingSupportStatus::Deferred, JsScriptingBuilderStatus::Deferred,
        ObjectHost, false, false, false, false, JsScriptingExceptionPolicy::FailOpen,
        "object script from the look/examine path", "self, object, actor, trigger, hostType",
        "Object host is exposed as self and object in generated TypeScript docs." },
    { JsScriptingManifestKind::LegacyScriptTrigger, ON_HEAR_SAY, "ON_HEAR_SAY", "onHearSay",
        JsScriptingSupportStatus::Deferred, JsScriptingBuilderStatus::Deferred, CharacterHost, false,
        false, false, false, JsScriptingExceptionPolicy::FailOpen,
        "character hear helper; legacy helper also checks ON_HEAR_YELL",
        "self, speaker, text, trigger, hostType",
        "Preserve the say/yell compatibility quirk for parity unless a later manifest revision "
        "intentionally splits the handlers." },
    { JsScriptingManifestKind::LegacyScriptTrigger, ON_DAMAGE, "ON_DAMAGE", "onDamage",
        JsScriptingSupportStatus::Deferred, JsScriptingBuilderStatus::Deferred,
        CharacterHost | ObjectHost, false, false, true, false, JsScriptingExceptionPolicy::FailClosed,
        "victim character script first, then wielded object script if damage remains allowed",
        "self, object, attacker, weapon, trigger, hostType",
        "False/block prevents damage and prevents downstream weapon-object dispatch." },
    { JsScriptingManifestKind::LegacyScriptTrigger, ON_EAT, "ON_EAT", "onEat",
        JsScriptingSupportStatus::Deferred, JsScriptingBuilderStatus::Deferred, ObjectHost, false,
        false, false, false, JsScriptingExceptionPolicy::FailOpen, "object script from eat path",
        "self, object, actor, trigger, hostType",
        "Object host is exposed as self and object in generated TypeScript docs." },
    { JsScriptingManifestKind::LegacyScriptTrigger, ON_DRINK, "ON_DRINK", "onDrink",
        JsScriptingSupportStatus::Deferred, JsScriptingBuilderStatus::Deferred, ObjectHost, false,
        false, false, false, JsScriptingExceptionPolicy::FailOpen, "object script from drink path",
        "self, object, actor, trigger, hostType",
        "Object host is exposed as self and object in generated TypeScript docs." },
    { JsScriptingManifestKind::LegacyScriptTrigger, ON_WEAR, "ON_WEAR", "onWear",
        JsScriptingSupportStatus::Deferred, JsScriptingBuilderStatus::Deferred, ObjectHost, false,
        false, true, false, JsScriptingExceptionPolicy::FailClosed,
        "object script before equipment is worn", "self, object, actor, wearSlot, trigger, hostType",
        "False/block prevents the wear action." },
    { JsScriptingManifestKind::LegacyScriptTrigger, ON_PULL, "ON_PULL", "onPull",
        JsScriptingSupportStatus::Deferred, JsScriptingBuilderStatus::Deferred, ObjectHost, false,
        false, true, false, JsScriptingExceptionPolicy::FailClosed,
        "object script before lever pull completes", "self, object, actor, trigger, hostType",
        "False/block prevents the pull action." },
    { JsScriptingManifestKind::LegacyScriptTrigger, ON_HEAR_YELL, "ON_HEAR_YELL", "onHearYell",
        JsScriptingSupportStatus::Deferred, JsScriptingBuilderStatus::Deferred, CharacterHost, false,
        false, false, false, JsScriptingExceptionPolicy::FailOpen,
        "character hear helper; legacy helper also checks ON_HEAR_SAY",
        "self, speaker, text, trigger, hostType",
        "Preserve the say/yell compatibility quirk for parity unless a later manifest revision "
        "intentionally splits the handlers." },

    { JsScriptingManifestKind::MudlleCallFlag, SPECIAL_COMMAND, "SPECIAL_COMMAND",
        "onSpecialCommand", JsScriptingSupportStatus::Deferred, JsScriptingBuilderStatus::Deferred,
        MudlleMobileHost, false, true, false, true, JsScriptingExceptionPolicy::FailOpen,
        "special dispatcher command checks for mobile programs and hard-coded specials",
        "self, actor, command, args, target, room, trigger, hostType",
        "True/handled consumes normal command flow; broader object/room hard-coded-special bridging "
        "is deferred." },
    { JsScriptingManifestKind::MudlleCallFlag, SPECIAL_SELF, "SPECIAL_SELF", "onSpecialSelf",
        JsScriptingSupportStatus::Deferred, JsScriptingBuilderStatus::Deferred, MudlleMobileHost,
        false, true, false, true, JsScriptingExceptionPolicy::FailOpen,
        "periodic mobile activity path", "self, room, tick, trigger, hostType",
        "Requires strict tick budget before enablement." },
    { JsScriptingManifestKind::MudlleCallFlag, SPECIAL_ENTER, "SPECIAL_ENTER", "onSpecialEnter",
        JsScriptingSupportStatus::Deferred, JsScriptingBuilderStatus::Deferred, MudlleMobileHost,
        false, true, false, true, JsScriptingExceptionPolicy::FailOpen,
        "special dispatcher enter-room checks",
        "self, actor, direction, reverseDirection, room, trigger, hostType",
        "Caller-specific handled semantics must be documented before enablement." },
    { JsScriptingManifestKind::MudlleCallFlag, SPECIAL_DELAY, "SPECIAL_DELAY", "onSpecialDelay",
        JsScriptingSupportStatus::Unsupported, JsScriptingBuilderStatus::Unsupported, MudlleMobileHost,
        false, false, false, true, JsScriptingExceptionPolicy::RejectAtPublish,
        "legacy ASIMA delayed continuation path", "self, continuation, trigger, hostType",
        "JavaScript continuations are unsupported until a separate audited state model exists." },
    { JsScriptingManifestKind::MudlleCallFlag, SPECIAL_TARGET, "SPECIAL_TARGET", "onSpecialTarget",
        JsScriptingSupportStatus::Deferred, JsScriptingBuilderStatus::Deferred, MudlleMobileHost,
        false, true, false, true, JsScriptingExceptionPolicy::FailOpen,
        "command targeting special path",
        "self, actor, command, args, targ1, targ2, targetTypes, trigger, hostType",
        "Target data lifetime and nullability must be specified before enablement." },
    { JsScriptingManifestKind::MudlleCallFlag, SPECIAL_DAMAGE, "SPECIAL_DAMAGE", "onSpecialDamage",
        JsScriptingSupportStatus::Deferred, JsScriptingBuilderStatus::Deferred, MudlleMobileHost,
        false, true, true, true, JsScriptingExceptionPolicy::FailClosed,
        "special-procedure damage hook path", "self, attacker, victim, target, trigger, hostType",
        "Must remain distinct from .scr ON_DAMAGE in diagnostics and tests." },
    { JsScriptingManifestKind::MudlleCallFlag, SPECIAL_DEATH, "SPECIAL_DEATH", "onSpecialDeath",
        JsScriptingSupportStatus::Deferred, JsScriptingBuilderStatus::Deferred, MudlleMobileHost,
        false, true, true, true, JsScriptingExceptionPolicy::FailClosed,
        "special-procedure death hook path", "self, actor, dying, target, trigger, hostType",
        "Must remain distinct from .scr ON_DIE in diagnostics and tests." },
    { JsScriptingManifestKind::MudlleCallFlag, SPECIAL_NONE, "SPECIAL_NONE", "",
        JsScriptingSupportStatus::Unsupported, JsScriptingBuilderStatus::Unsupported, 0, false, false,
        false, false, JsScriptingExceptionPolicy::RejectAtPublish, "no-callflag/default path", "",
        "Not a builder JavaScript trigger unless a specific existing hard-coded-special behavior is "
        "deliberately ported." },
};

constexpr JsScriptingApiPermissionEntry ApiPermissionEntries[] = {
    { SCRIPT_DO_HIT, "SCRIPT_DO_HIT", "hit", JsScriptingApiPermissionStatus::Unsupported,
        "direct combat mutation is not part of the v1 deny-by-default API" },
    { SCRIPT_DO_FLEE, "SCRIPT_DO_FLEE", "flee", JsScriptingApiPermissionStatus::Unsupported,
        "forced movement is deferred until action-loop and liveness tests exist" },
    { SCRIPT_LOAD_MOB, "SCRIPT_LOAD_MOB", "loadMob", JsScriptingApiPermissionStatus::Unsupported,
        "world creation requires zone permission and rollback design" },
    { SCRIPT_TELEPORT_CHAR, "SCRIPT_TELEPORT_CHAR", "teleportCharacter",
        JsScriptingApiPermissionStatus::Unsupported,
        "broad movement mutation is deferred until permission and compensation rules exist" },
    { SCRIPT_EXTRACT_CHAR, "SCRIPT_EXTRACT_CHAR", "extractCharacter",
        JsScriptingApiPermissionStatus::Unsupported,
        "entity extraction is unsafe until stale-handle behavior is fully tested" },
    { SCRIPT_SET_EXIT_STATE, "SCRIPT_SET_EXIT_STATE", "setExitState",
        JsScriptingApiPermissionStatus::Unsupported,
        "exit mutation is deferred until room/zone ownership policy exists" },
    { SCRIPT_RAW_KILL, "SCRIPT_RAW_KILL", "rawKill", JsScriptingApiPermissionStatus::Unsupported,
        "direct kill mutation is not exposed to builder JavaScript" },
    { SCRIPT_DO_GIVE, "SCRIPT_DO_GIVE", "give", JsScriptingApiPermissionStatus::Unsupported,
        "inventory transfer is deferred until partial-failure behavior is specified" },
    { SCRIPT_LOAD_OBJ, "SCRIPT_LOAD_OBJ", "loadObject", JsScriptingApiPermissionStatus::Unsupported,
        "world creation requires zone permission and rollback design" },
    { SCRIPT_OBJ_TO_CHAR, "SCRIPT_OBJ_TO_CHAR", "objectToCharacter",
        JsScriptingApiPermissionStatus::Unsupported,
        "inventory mutation is deferred until liveness and permission checks exist" },
    { SCRIPT_OBJ_TO_ROOM, "SCRIPT_OBJ_TO_ROOM", "objectToRoom",
        JsScriptingApiPermissionStatus::Unsupported,
        "room mutation is deferred until zone ownership checks exist" },
    { SCRIPT_CHANGE_EXIT_TO, "SCRIPT_CHANGE_EXIT_TO", "changeExitTo",
        JsScriptingApiPermissionStatus::Unsupported,
        "exit topology mutation is deferred until zone policy exists" },
    { SCRIPT_EXTRACT_OBJ, "SCRIPT_EXTRACT_OBJ", "extractObject",
        JsScriptingApiPermissionStatus::Unsupported,
        "object extraction is unsafe until stale-handle behavior is fully tested" },
    { SCRIPT_DO_DROP, "SCRIPT_DO_DROP", "drop", JsScriptingApiPermissionStatus::Unsupported,
        "inventory mutation is deferred until partial-failure behavior is specified" },
    { SCRIPT_DO_REMOVE, "SCRIPT_DO_REMOVE", "remove", JsScriptingApiPermissionStatus::Unsupported,
        "equipment mutation is deferred until action recursion tests exist" },
    { SCRIPT_DO_WEAR, "SCRIPT_DO_WEAR", "wear", JsScriptingApiPermissionStatus::Unsupported,
        "equipment mutation is deferred until action recursion tests exist" },
    { SCRIPT_DO_SOCIAL, "SCRIPT_DO_SOCIAL", "social", JsScriptingApiPermissionStatus::Unsupported,
        "command execution helpers need an audited allowlist before exposure" },
    { SCRIPT_DO_WAIT, "SCRIPT_DO_WAIT", "wait", JsScriptingApiPermissionStatus::Unsupported,
        "JavaScript continuations are unsupported in v1" },
    { SCRIPT_GAIN_EXP, "SCRIPT_GAIN_EXP", "gainExperience",
        JsScriptingApiPermissionStatus::Unsupported,
        "player progression mutation requires a separate permission model" },
    { SCRIPT_TELEPORT_CHAR_X, "SCRIPT_TELEPORT_CHAR_X", "teleportCharacterOnly",
        JsScriptingApiPermissionStatus::Unsupported,
        "broad movement mutation is deferred until permission and compensation rules exist" },
    { SCRIPT_OBJ_FROM_ROOM, "SCRIPT_OBJ_FROM_ROOM", "objectFromRoom",
        JsScriptingApiPermissionStatus::Unsupported,
        "room inventory mutation is deferred until zone ownership checks exist" },
    { SCRIPT_OBJ_FROM_CHAR, "SCRIPT_OBJ_FROM_CHAR", "objectFromCharacter",
        JsScriptingApiPermissionStatus::Unsupported,
        "inventory mutation is deferred until liveness and permission checks exist" },
    { SCRIPT_EQUIP_CHAR, "SCRIPT_EQUIP_CHAR", "equipCharacter",
        JsScriptingApiPermissionStatus::Unsupported,
        "equipment mutation is deferred until action recursion tests exist" },
    { SCRIPT_TELEPORT_CHAR_XL, "SCRIPT_TELEPORT_CHAR_XL", "teleportCharacterToRoomHandle",
        JsScriptingApiPermissionStatus::Unsupported,
        "broad movement mutation is deferred until permission and compensation rules exist" },
    { SCRIPT_LOAD_OBJ_X, "SCRIPT_LOAD_OBJ_X", "loadObjectFromObject",
        JsScriptingApiPermissionStatus::Unsupported,
        "nested object creation requires ownership, limit, and rollback design" },
};

} // namespace

const JsScriptingManifestMetadata& js_scripting_manifest_metadata() { return ManifestMetadata; }

const JsScriptingManifestEntry* js_scripting_manifest_entries() { return ManifestEntries; }

std::size_t js_scripting_manifest_entry_count()
{
    return sizeof(ManifestEntries) / sizeof(ManifestEntries[0]);
}

const JsScriptingManifestEntry* find_js_scripting_manifest_entry(JsScriptingManifestKind kind,
    int legacy_value)
{
    for (std::size_t index = 0; index < js_scripting_manifest_entry_count(); ++index) {
        const JsScriptingManifestEntry& entry = ManifestEntries[index];
        if (entry.kind == kind && entry.legacy_value == legacy_value)
            return &entry;
    }
    return nullptr;
}

bool js_scripting_manifest_entry_publishable(const JsScriptingManifestEntry& entry)
{
    (void)entry;
    return false;
}

bool js_scripting_manifest_host_publishable(JsScriptingManifestKind kind, int legacy_value,
    JsScriptingHostFlag host_flag)
{
    const JsScriptingManifestEntry* entry = find_js_scripting_manifest_entry(kind, legacy_value);
    if (!entry)
        return false;
    if (!js_scripting_manifest_entry_publishable(*entry))
        return false;
    if ((entry->host_flags & host_flag) == 0)
        return false;
    if (host_flag == JS_SCRIPTING_HOST_ROOM && !entry->room_owned_scripts_publishable)
        return false;
    return true;
}

const JsScriptingApiPermissionEntry* js_scripting_api_permission_entries()
{
    return ApiPermissionEntries;
}

std::size_t js_scripting_api_permission_entry_count()
{
    return sizeof(ApiPermissionEntries) / sizeof(ApiPermissionEntries[0]);
}

const JsScriptingApiPermissionEntry* find_js_scripting_api_permission_entry(int legacy_command)
{
    for (std::size_t index = 0; index < js_scripting_api_permission_entry_count(); ++index) {
        const JsScriptingApiPermissionEntry& entry = ApiPermissionEntries[index];
        if (entry.legacy_command == legacy_command)
            return &entry;
    }
    return nullptr;
}

bool js_scripting_api_permission_is_allowed(int legacy_command)
{
    const JsScriptingApiPermissionEntry* entry = find_js_scripting_api_permission_entry(legacy_command);
    if (!entry)
        return false;
    return false;
}

const char* js_scripting_manifest_kind_name(JsScriptingManifestKind kind)
{
    switch (kind) {
    case JsScriptingManifestKind::LegacyScriptTrigger:
        return "legacy-script-trigger";
    case JsScriptingManifestKind::MudlleCallFlag:
        return "mudlle-call-flag";
    }
    return "unknown";
}

const char* js_scripting_support_status_name(JsScriptingSupportStatus status)
{
    switch (status) {
    case JsScriptingSupportStatus::Deferred:
        return "deferred";
    case JsScriptingSupportStatus::Reserved:
        return "reserved";
    case JsScriptingSupportStatus::Unsupported:
        return "unsupported";
    }
    return "unknown";
}

const char* js_scripting_builder_status_name(JsScriptingBuilderStatus status)
{
    switch (status) {
    case JsScriptingBuilderStatus::Deferred:
        return "deferred";
    case JsScriptingBuilderStatus::Reserved:
        return "reserved";
    case JsScriptingBuilderStatus::Unsupported:
        return "unsupported";
    }
    return "unknown";
}

const char* js_scripting_exception_policy_name(JsScriptingExceptionPolicy policy)
{
    switch (policy) {
    case JsScriptingExceptionPolicy::FailClosed:
        return "fail-closed";
    case JsScriptingExceptionPolicy::FailOpen:
        return "fail-open";
    case JsScriptingExceptionPolicy::RejectAtPublish:
        return "reject-at-publish";
    }
    return "unknown";
}

const char* js_scripting_api_permission_status_name(JsScriptingApiPermissionStatus status)
{
    switch (status) {
    case JsScriptingApiPermissionStatus::Deferred:
        return "deferred";
    case JsScriptingApiPermissionStatus::Unsupported:
        return "unsupported";
    }
    return "unknown";
}
