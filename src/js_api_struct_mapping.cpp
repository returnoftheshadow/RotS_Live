#include "js_api_struct_mapping.h"

#include <cstring>

namespace {

constexpr const char *ImplementedReadOnly = "implemented-read-only-getter";
constexpr const char *ReadOnly = "planned-read-only-getter";
constexpr const char *SetterPlanned = "planned-validated-setter";
constexpr const char *SetterImplemented = "implemented-validated-setter";
constexpr const char *Deferred = "deferred";
constexpr const char *Internal = "internal-only";
constexpr const char *Unsupported = "unsupported";

constexpr JsApiStructFieldMapping FieldMappings[] = {
    {JsApiStructOwner::CharData, "char_data", "abs_number", "internalIndex", "getInternalIndex",
     "setInternalIndex", "number", false, Internal, Unsupported,
     "Internal control-array index for diagnostics only; no builder getter is emitted.",
     "Setting the internal control-array index from JavaScript is unsupported.", "none",
     "Implementation identity; not stable across loads."},
    {JsApiStructOwner::CharData, "char_data", "player_index", "playerIndex", "getPlayerIndex",
     "setPlayerIndex", "number", false, Internal, Unsupported,
     "Internal player-table index for diagnostics only; no builder getter is emitted.",
     "Setting the player-table index from JavaScript is unsupported.", "none",
     "May reveal server storage layout."},
    {JsApiStructOwner::CharData, "char_data", "nr", "prototypeVnum", "getPrototypeVnum",
     "setPrototypeVnum", "number | null", true, ImplementedReadOnly, Unsupported,
     "Returns the mobile prototype vnum when this character is an NPC and the prototype can be "
     "resolved; players return null.",
     "Changing a live character's prototype from JavaScript is unsupported.", "none",
     "Already exposed as Mob.prototypeVnum for implemented snapshots."},
    {JsApiStructOwner::CharData, "char_data", "in_room", "room", "getRoom", "setRoom",
     "Room | null", true, ImplementedReadOnly, Deferred,
     "Returns the current room handle when the character is in a loaded room; otherwise null.",
     "Moving a character requires a validated movement/teleport API and is deferred.", "mutation",
     "Setter must preserve movement triggers, mounts, followers, and visibility rules."},
    {JsApiStructOwner::CharData, "char_data", "player", "profile", "getProfile", "setProfile",
     "CharacterProfile", false, Deferred, Deferred,
     "Planned structured getter for public identity fields such as name, level, race, and title.",
     "Profile mutation is deferred until each editable subfield has validation and audit rules.",
     "mutation", "Nested struct; do not expose as a raw object."},
    {JsApiStructOwner::CharData, "char_data", "abilities", "baseAbilities", "getBaseAbilities",
     "setBaseAbilities", "AbilityScores", false, Deferred, Deferred,
     "Planned read-only snapshot of rolled/base ability scores.",
     "Ability-score writes are deferred until range, recalculation, and persistence rules exist.",
     "mutation", "Nested struct."},
    {JsApiStructOwner::CharData, "char_data", "tmpabilities", "currentAbilities",
     "getCurrentAbilities", "setCurrentAbilities", "AbilityScores", false, Deferred, Deferred,
     "Planned read-only snapshot of currently modified ability scores.",
     "Temporary ability writes are deferred until affect and recalculation interactions are "
     "mapped.",
     "mutation", "Nested struct."},
    {JsApiStructOwner::CharData, "char_data", "constabilities", "rolledAbilities",
     "getRolledAbilities", "setRolledAbilities", "AbilityScores", false, Deferred, Deferred,
     "Planned read-only snapshot of rolled character creation abilities.",
     "Rolled ability writes are deferred and should usually remain administrative only.",
     "mutation", "Nested struct."},
    {JsApiStructOwner::CharData, "char_data", "points", "points", "getPoints", "setPoints",
     "CharacterPoints", false, Deferred, Deferred,
     "Planned structured getter for hit points, movement, mana, armor, dodge, and related values.",
     "Point writes are deferred until clamping, recalculation, death, and combat rules are mapped.",
     "mutation", "Nested struct."},
    {JsApiStructOwner::CharData, "char_data", "specials", "specials", "getSpecials", "setSpecials",
     "CharacterSpecials", false, Deferred, Deferred,
     "Planned structured getter for safe public character state flags.",
     "Special-state writes are deferred; many fields affect combat, position, timers, and scripts.",
     "mutation", "Nested struct."},
    {JsApiStructOwner::CharData, "char_data", "specials2", "specials2", "getSpecials2",
     "setSpecials2", "CharacterSpecials2", false, Deferred, Deferred,
     "Planned structured getter for additional safe public character state.",
     "Specials2 writes are deferred until each subfield is classified.", "mutation",
     "Nested struct."},
    {JsApiStructOwner::CharData, "char_data", "profs", "professions", "getProfessions",
     "setProfessions", "readonly Profession[]", true, Deferred, Deferred,
     "Planned snapshot of profession coefficients when available.",
     "Profession writes are deferred until persistence and skill recalculation are defined.",
     "mutation", "Pointer-owned nested data."},
    {JsApiStructOwner::CharData, "char_data", "extra_specialization_data", "specializations",
     "getSpecializations", "setSpecializations", "SpecializationData", false, Deferred, Deferred,
     "Planned snapshot of specialization state.",
     "Specialization writes are deferred until class-specific invariants are documented.",
     "mutation", "Nested data."},
    {JsApiStructOwner::CharData, "char_data", "damage_details", "damageDetails", "getDamageDetails",
     "setDamageDetails", "DamageDetails", false, Deferred, Deferred,
     "Planned snapshot of current damage bookkeeping for combat triggers.",
     "Damage bookkeeping writes are deferred and likely restricted to combat engine helpers.",
     "mutation", "Combat-internal data."},
    {JsApiStructOwner::CharData, "char_data", "skills", "skills", "getSkills", "setSkill",
     "readonly SkillValue[]", true, Deferred, Deferred,
     "Planned read-only skill-practice snapshot.",
     "Skill writes require a validated setSkill helper and are deferred.", "mutation",
     "Raw byte pointer must never be exposed."},
    {JsApiStructOwner::CharData, "char_data", "knowledge", "knowledge", "getKnowledge",
     "setKnowledge", "readonly SkillValue[]", true, Deferred, Deferred,
     "Planned read-only computed knowledge snapshot.",
     "Knowledge writes are deferred; values are normally derived from practice data.", "mutation",
     "Raw byte pointer must never be exposed."},
    {JsApiStructOwner::CharData, "char_data", "affected", "affects", "getAffects", "setAffects",
     "readonly Affect[]", true, Deferred, Deferred, "Planned read-only list of active affects.",
     "Affect mutation needs explicit add/remove helpers and is deferred.", "world-mutation",
     "Linked list pointer must never be exposed."},
    {JsApiStructOwner::CharData, "char_data", "equipment", "equipment", "getEquipment",
     "setEquipmentSlot", "readonly (GameObject | null)[]", false, Deferred, Deferred,
     "Planned equipment-slot snapshot using safe object handles.",
     "Equipment writes require validated wear/remove helpers and trigger parity.", "world-mutation",
     "Object pointer array must never be exposed."},
    {JsApiStructOwner::CharData, "char_data", "carrying", "inventory", "getInventory",
     "setInventory", "readonly GameObject[]", true, Deferred, Unsupported,
     "Planned inventory snapshot using safe object handles.",
     "Replacing the raw inventory list from JavaScript is unsupported; use explicit inventory "
     "helpers when they exist.",
     "world-mutation", "Linked list pointer must never be exposed."},
    {JsApiStructOwner::CharData, "char_data", "desc", "descriptor", "getDescriptor",
     "setDescriptor", "never", true, Internal, Unsupported,
     "Descriptor/session data is internal and no builder getter is emitted.",
     "Descriptor mutation from JavaScript is unsupported.", "none", "Raw descriptor pointer."},
    {JsApiStructOwner::CharData, "char_data", "next_in_room", "nextInRoom", "getNextInRoom",
     "setNextInRoom", "never", true, Internal, Unsupported,
     "Room linked-list traversal state is internal and no builder getter is emitted.",
     "Linked-list mutation from JavaScript is unsupported.", "none", "Raw list pointer."},
    {JsApiStructOwner::CharData, "char_data", "next", "next", "getNext", "setNext", "never", true,
     Internal, Unsupported,
     "Global character-list traversal state is internal and no builder getter is emitted.",
     "Linked-list mutation from JavaScript is unsupported.", "none", "Raw list pointer."},
    {JsApiStructOwner::CharData, "char_data", "next_fighting", "nextFighting", "getNextFighting",
     "setNextFighting", "never", true, Internal, Unsupported,
     "Combat-list traversal state is internal and no builder getter is emitted.",
     "Combat-list mutation from JavaScript is unsupported.", "none", "Raw list pointer."},
    {JsApiStructOwner::CharData, "char_data", "next_fast_update", "nextFastUpdate",
     "getNextFastUpdate", "setNextFastUpdate", "never", true, Internal, Unsupported,
     "Fast-update traversal state is internal and no builder getter is emitted.",
     "Fast-update list mutation from JavaScript is unsupported.", "none", "Raw list pointer."},
    {JsApiStructOwner::CharData, "char_data", "followers", "followers", "getFollowers",
     "setFollowers", "readonly Character[]", true, Deferred, Unsupported,
     "Planned read-only follower snapshot using safe character handles.",
     "Replacing the follower linked list from JavaScript is unsupported.", "world-mutation",
     "Use explicit follow helpers if added later."},
    {JsApiStructOwner::CharData, "char_data", "master", "master", "getMaster", "setMaster",
     "Character | null", true, Deferred, Deferred,
     "Planned safe handle for the character being followed.",
     "Master changes are deferred until follow/unfollow semantics and loop prevention are mapped.",
     "world-mutation", "Raw character pointer must never be exposed."},
    {JsApiStructOwner::CharData, "char_data", "master_number", "masterNumber", "getMasterNumber",
     "setMasterNumber", "number", false, Internal, Unsupported,
     "Internal persisted master id; no builder getter is emitted.",
     "Changing the persisted master id directly is unsupported.", "none", "Implementation field."},
    {JsApiStructOwner::CharData, "char_data", "mount_data", "mount", "getMount", "setMount",
     "MountData", false, Deferred, Deferred, "Planned mount-state snapshot.",
     "Mount writes are deferred until ride/dismount rules are mapped.", "world-mutation",
     "Nested struct."},
    {JsApiStructOwner::CharData, "char_data", "group", "group", "getGroup", "setGroup",
     "Group | null", true, Deferred, Unsupported, "Planned read-only group snapshot.",
     "Replacing a group pointer from JavaScript is unsupported.", "world-mutation",
     "Raw group pointer must never be exposed."},
    {JsApiStructOwner::CharData, "char_data", "temp", "temporaryData", "getTemporaryData",
     "setTemporaryData", "never", true, Internal, Unsupported,
     "Opaque temporary implementation data is internal and no builder getter is emitted.",
     "Opaque temporary pointer mutation from JavaScript is unsupported.", "none", "void pointer."},
    {JsApiStructOwner::CharData, "char_data", "delay", "delay", "getDelay", "setDelay", "never",
     false, Internal, Unsupported,
     "Legacy wait/continuation state is internal and no builder getter is emitted.",
     "JavaScript continuations are unsupported in v1.", "none", "Nested wait state."},
    {JsApiStructOwner::CharData, "char_data", "next_die", "nextDying", "getNextDying",
     "setNextDying", "never", true, Internal, Unsupported,
     "Death-list traversal state is internal and no builder getter is emitted.",
     "Death-list mutation from JavaScript is unsupported.", "none", "Raw list pointer."},
    {JsApiStructOwner::CharData, "char_data", "classpoints", "classPoints", "getClassPoints",
     "setClassPoints", "number", false, Deferred, Deferred,
     "Planned read-only character-creation class point value.",
     "Class-point writes are deferred and likely administrative only.", "mutation",
     "Character creation bookkeeping."},
    {JsApiStructOwner::CharData, "char_data", "interrupt_count", "interruptCount",
     "getInterruptCount", "setInterruptCount", "number", false, Deferred, Deferred,
     "Planned read-only interrupt count for combat/casting diagnostics.",
     "Interrupt count writes are deferred until caster AI semantics are mapped.", "mutation",
     "Combat AI bookkeeping."},
    {JsApiStructOwner::CharData, "char_data", "interrupt_time", "interruptTime", "getInterruptTime",
     "setInterruptTime", "number", false, Deferred, Deferred,
     "Planned read-only countdown before interrupt count decays.",
     "Interrupt timer writes are deferred until caster AI semantics are mapped.", "mutation",
     "Combat AI bookkeeping."},
    {JsApiStructOwner::CharData, "char_data", "spec_busy", "specialBusy", "isSpecialBusy",
     "setSpecialBusy", "boolean", false, Deferred, Deferred,
     "Planned read-only flag showing whether a special procedure is busy.",
     "Special busy writes are deferred until procedure reentrancy rules are mapped.", "mutation",
     "Special-procedure state."},

    {JsApiStructOwner::ObjData, "obj_data", "item_number", "vnum", "getVnum", "setVnum",
     "number | null", true, ImplementedReadOnly, Unsupported,
     "Returns the object prototype vnum when the prototype can be resolved; otherwise null.",
     "Changing a live object's prototype from JavaScript is unsupported.", "none",
     "Already exposed as GameObject.vnum for implemented snapshots."},
    {JsApiStructOwner::ObjData, "obj_data", "in_room", "room", "getRoom", "setRoom", "Room | null",
     true, ImplementedReadOnly, Deferred,
     "Returns the direct containing room when the object is in a room; carried, worn, nested, or "
     "invalid objects return null.",
     "Moving objects requires explicit load/move/extract helpers and is deferred.",
     "world-mutation", "Setter must preserve container, carrier, and room contents invariants."},
    {JsApiStructOwner::ObjData, "obj_data", "obj_flags", "flags", "getFlags", "setFlags",
     "ObjectFlags", false, ImplementedReadOnly, Deferred,
     "Returns a structured read-only object flag snapshot with symbolic type, wear flags, extra "
     "flags, material, and scalar economy/timer fields.",
     "Object flag writes are deferred until every subfield has validation.", "mutation",
     "Legacy item-type value slots remain intentionally absent until each item type has a "
     "documented builder-facing domain."},
    {JsApiStructOwner::ObjData, "obj_data", "affected", "affects", "getAffects", "setAffects",
     "readonly ObjectAffect[]", false, Deferred, Deferred,
     "Deferred fixed-slot equipment modifier snapshot. Future entries must use named apply "
     "locations, integer modifiers, canonical ordering, and omit empty slots.",
     "Object affect writes are deferred until equipment recalculation and apply-location "
     "validation rules are mapped.",
     "mutation", "Fixed-size equipment modifier slots; no builder getter is emitted yet."},
    {JsApiStructOwner::ObjData, "obj_data", "name", "name", "getName", "setName", "string", false,
     ImplementedReadOnly, SetterImplemented,
     "Returns the object's keyword/name string for builder conditions and diagnostics.",
     "Updates the invocation snapshot object keyword/name after type, nonblank, length, and "
     "unsupported-character checks, and applies to live owned memory only when dispatch provides "
     "target-scoped persistent setter authority.",
     "mutation", "Persistent application requires target-scoped dispatch mutation authority context."},
    {JsApiStructOwner::ObjData, "obj_data", "description", "description", "getDescription",
     "setDescription", "string", false, ImplementedReadOnly, SetterImplemented,
     "Returns the room-visible object description copied into the invocation snapshot.",
     "Updates the invocation snapshot object description after type, length, and "
     "unsupported-character checks, and applies to live owned memory only when dispatch provides "
     "target-scoped persistent setter authority.",
     "mutation", "Persistent application requires target-scoped dispatch mutation authority context."},
    {JsApiStructOwner::ObjData, "obj_data", "short_description", "shortDescription",
     "getShortDescription", "setShortDescription", "string", false, ImplementedReadOnly, SetterImplemented,
     "Returns the carried/worn short description copied into the invocation snapshot.",
     "Updates the invocation snapshot short description after type, length, and "
     "unsupported-character checks, and applies to live owned memory only when dispatch provides "
     "target-scoped persistent setter authority.",
     "mutation", "Persistent application requires target-scoped dispatch mutation authority context."},
    {JsApiStructOwner::ObjData, "obj_data", "action_description", "actionDescription",
     "getActionDescription", "setActionDescription", "string | null", true, ImplementedReadOnly, SetterImplemented,
     "Returns the optional use/action text copied into the invocation snapshot when present.",
     "Updates or clears the invocation snapshot action description after nullability, type, "
     "length, and unsupported-character checks, and applies to live owned memory only when "
     "dispatch provides target-scoped persistent setter authority.",
     "mutation", "Persistent application requires target-scoped dispatch mutation authority context."},
    {JsApiStructOwner::ObjData, "obj_data", "ex_description", "extraDescriptions",
     "getExtraDescriptions", "setExtraDescriptions", "readonly ExtraDescription[]", true, Deferred,
     Deferred,
     "Deferred read-only snapshot of object extra descriptions. Future entries must expose only "
     "builder text fields such as keywords and description text, with bounded list size.",
     "Extra-description writes are deferred until list ownership, text length, and sanitization "
     "rules are mapped.",
     "mutation", "Linked-list storage is not exposed to builders."},
    {JsApiStructOwner::ObjData, "obj_data", "carried_by", "carriedBy", "getCarriedBy",
     "setCarriedBy", "Character | null", true, ImplementedReadOnly, Deferred,
     "Returns the live character carrying the object when applicable; otherwise null.",
     "Carrier changes require explicit inventory helpers and are deferred.", "world-mutation",
     "Already exposed as GameObject.carriedBy for implemented snapshots."},
    {JsApiStructOwner::ObjData, "obj_data", "owner", "ownerId", "getOwnerId", "setOwnerId", "never",
     false, Internal, Unsupported,
     "Object owner id is sensitive authorization data and no builder getter is emitted.",
     "Owner id mutation from JavaScript is unsupported; use server authorization flows instead.",
     "none", "May reveal account/player identity policy."},
    {JsApiStructOwner::ObjData, "obj_data", "in_obj", "container", "getContainer", "setContainer",
     "GameObject | null", true, Deferred, Deferred,
     "Deferred safe handle for the containing object. Getter needs liveness checks, cycle guards, "
     "and depth limits before nested containers are exposed.",
     "Container changes require explicit object movement helpers and are deferred.",
     "world-mutation", "Object storage links are not exposed to builders."},
    {JsApiStructOwner::ObjData, "obj_data", "contains", "contents", "getContents", "setContents",
     "readonly GameObject[]", true, Deferred, Unsupported,
     "Deferred read-only snapshot of nested object contents. Getter needs bounded traversal, "
     "canonical ordering, stale-object filtering, and cycle protection.",
     "Replacing the contents list from JavaScript is unsupported; use explicit movement helpers "
     "when they exist.",
     "world-mutation", "Linked-list storage is not exposed to builders."},
    {JsApiStructOwner::ObjData, "obj_data", "next_content", "nextContent", "getNextContent",
     "setNextContent", "never", true, Internal, Unsupported,
     "Container, room, and inventory traversal state is internal; builders should use future "
     "bounded contents snapshots instead.",
     "Linked-list mutation from JavaScript is unsupported.", "none", "Internal traversal link."},
    {JsApiStructOwner::ObjData, "obj_data", "next", "next", "getNext", "setNext", "never", true,
     Internal, Unsupported,
     "Global object traversal state is internal and no builder getter is emitted.",
     "Global-list mutation from JavaScript is unsupported.", "none", "Internal traversal link."},
    {JsApiStructOwner::ObjData, "obj_data", "touched", "touched", "wasTouched", "setTouched",
     "boolean", false, Deferred, Deferred,
     "Deferred read-only boolean showing whether a player has touched the object. Getter remains "
     "deferred until persistence behavior and builder-visible gameplay meaning are confirmed.",
     "Touched-state writes are deferred until persistence and gameplay meaning are confirmed.",
     "mutation", "Integer flag will be normalized to boolean before exposure."},
    {JsApiStructOwner::ObjData, "obj_data", "loaded_by", "loadedBy", "getLoadedBy", "setLoadedBy",
     "number", false, Internal, Unsupported,
     "Immortal loader id is administrative audit data and no builder getter is emitted by default.",
     "Loader id mutation from JavaScript is unsupported.", "none", "Administrative audit data."},

    {JsApiStructOwner::RoomData, "room_data", "number", "vnum", "getVnum", "setVnum", "number",
     false, ImplementedReadOnly, Unsupported, "Returns the public room vnum.",
     "Changing a loaded room vnum from JavaScript is unsupported.", "none",
     "Already exposed as Room.vnum."},
    {JsApiStructOwner::RoomData, "room_data", "zone", "zone", "getZone", "setZone", "Zone | null",
     true, ImplementedReadOnly, Unsupported,
     "Returns the room's zone handle when the zone table contains it; otherwise null.",
     "Moving a room between zones from JavaScript is unsupported.", "none",
     "Already exposed as Room.zone."},
    {JsApiStructOwner::RoomData, "room_data", "level", "level", "getLevel", "setLevel", "number",
     false, ImplementedReadOnly, Deferred, "Returns the room level value.",
     "Room level writes are deferred.",
     "mutation", ""},
    {JsApiStructOwner::RoomData, "room_data", "sector_type", "sectorType", "getSectorType",
     "setSectorType", "string", false, ImplementedReadOnly, Deferred, "Returns the readable sector type name.",
     "Sector writes are deferred until movement costs and "
     "visibility effects are mapped.",
     "mutation", "Invalid loaded sector values are exposed as Unknown, not raw integers."},
    {JsApiStructOwner::RoomData, "room_data", "name", "name", "getName", "setName", "string", false,
     ImplementedReadOnly, SetterImplemented, "Returns the room display name.",
     "Updates the invocation snapshot room display name after type, nonblank, length, and "
     "unsupported-character checks, and applies to live owned memory only when dispatch provides "
     "target-scoped persistent setter authority.",
     "mutation", "Persistent application requires target-scoped dispatch mutation authority context."},
    {JsApiStructOwner::RoomData, "room_data", "description", "description", "getDescription",
     "setDescription", "string", false, ImplementedReadOnly, SetterImplemented,
     "Returns the room long description copied into the invocation snapshot.",
     "Updates the invocation snapshot room long description after type, length, and "
     "unsupported-character checks, and applies to live owned memory only when dispatch provides "
     "target-scoped persistent setter authority.",
     "mutation", "Persistent application requires target-scoped dispatch mutation authority context."},
    {JsApiStructOwner::RoomData, "room_data", "ex_description", "extraDescriptions",
     "getExtraDescriptions", "setExtraDescriptions", "readonly ExtraDescription[]", true, Deferred,
     Deferred, "Planned read-only extra-description snapshot.",
     "Extra-description writes are deferred until list ownership rules are mapped.", "mutation",
     "Linked list pointer must never be exposed."},
    {JsApiStructOwner::RoomData, "room_data", "dir_option", "exits", "getExits", "setExit",
     "readonly RoomExit[]", false, Deferred, Deferred,
     "Planned read-only room exit snapshot by direction.",
     "Exit writes require explicit setExit/removeExit helpers and are deferred.", "world-mutation",
     "Pointer array must never be exposed."},
    {JsApiStructOwner::RoomData, "room_data", "room_track", "tracks", "getTracks", "setTracks",
     "readonly RoomTrack[]", false, Internal, Unsupported,
     "Room tracking data is internal unless a future tracking API is designed.",
     "Raw track mutation from JavaScript is unsupported.", "none", "Fixed internal array."},
    {JsApiStructOwner::RoomData, "room_data", "room_flags", "flags", "getFlags", "setFlags",
     "readonly string[]", false, ImplementedReadOnly, Deferred,
     "Returns builder-safe room flag names, excluding BFS_MARK and unnamed/internal bits.",
     "Room flag writes are deferred until flag-domain "
     "validation and side effects are mapped.",
     "mutation", "Raw bitvector is filtered before reaching JavaScript."},
    {JsApiStructOwner::RoomData, "room_data", "alignment", "alignment", "getAlignment",
     "setAlignment", "number", false, ImplementedReadOnly, Deferred, "Returns the room alignment value.",
     "Room alignment writes are deferred.", "mutation", ""},
    {JsApiStructOwner::RoomData, "room_data", "light", "light", "getLight", "setLight", "number",
     false, ImplementedReadOnly, Unsupported, "Returns the current room light-source count.",
     "Direct light counter writes are unsupported; use "
     "object/light mechanics instead.",
     "mutation", "Derived counter."},
    {JsApiStructOwner::RoomData, "room_data", "bfs_dir", "bfsDirection", "getBfsDirection",
     "setBfsDirection", "never", false, Internal, Unsupported,
     "Pathfinding scratch direction is internal.", "Pathfinding scratch mutation is unsupported.",
     "none", "Temporary traversal field."},
    {JsApiStructOwner::RoomData, "room_data", "bfs_next", "bfsNext", "getBfsNext", "setBfsNext",
     "never", true, Internal, Unsupported, "Pathfinding scratch pointer is internal.",
     "Pathfinding scratch mutation is unsupported.", "none", "Raw traversal pointer."},
    {JsApiStructOwner::RoomData, "room_data", "funct", "specialProcedure", "getSpecialProcedure",
     "setSpecialProcedure", "never", true, Internal, Unsupported,
     "Special procedure function pointer is internal.", "Function pointer mutation is unsupported.",
     "none", "Raw function pointer."},
    {JsApiStructOwner::RoomData, "room_data", "contents", "contents", "getContents", "setContents",
     "readonly GameObject[]", true, Deferred, Unsupported,
     "Planned read-only snapshot of objects directly in the room.",
     "Replacing the room contents linked list from JavaScript is unsupported.", "world-mutation",
     "Use explicit object movement helpers if added later."},
    {JsApiStructOwner::RoomData, "room_data", "people", "characters", "getCharacters",
     "setCharacters", "readonly Character[]", true, Deferred, Unsupported,
     "Planned read-only snapshot of characters currently in the room.",
     "Replacing the room people linked list from JavaScript is unsupported.", "world-mutation",
     "Use explicit movement helpers if added later."},
    {JsApiStructOwner::RoomData, "room_data", "affected", "affects", "getAffects", "setAffects",
     "readonly Affect[]", true, Deferred, Deferred, "Planned read-only room affect snapshot.",
     "Room affect writes require explicit helpers and "
     "are deferred.",
     "world-mutation", "Linked list pointer must never be exposed."},
    {JsApiStructOwner::RoomData, "room_data", "bleed_track", "bleedTracks", "getBleedTracks",
     "setBleedTracks", "readonly BleedTrack[]", false, Internal, Unsupported,
     "Bleed tracking data is internal unless a future tracking API is designed.",
     "Raw bleed-track mutation from JavaScript is unsupported.", "none", "Fixed internal array."},

    {JsApiStructOwner::ZoneData, "zone_data", "name", "name", "getName", "setName", "string", false,
     ImplementedReadOnly, SetterImplemented, "Returns the zone display name.",
     "Updates the invocation snapshot zone display name after type, nonblank, length, and "
     "unsupported-character checks, and applies to live owned memory only when dispatch provides "
     "target-scoped persistent setter authority.",
     "mutation", "Persistent application requires target-scoped dispatch mutation authority context."},
    {JsApiStructOwner::ZoneData, "zone_data", "description", "description", "getDescription",
     "setDescription", "string | null", true, ImplementedReadOnly, SetterImplemented,
     "Returns the optional zone description copied into the invocation snapshot when present.",
     "Updates or clears the invocation snapshot zone description after nullability, type, length, "
     "and unsupported-character checks, and applies to live owned memory only when dispatch "
     "provides target-scoped persistent setter authority.",
     "mutation", "Persistent application requires target-scoped dispatch mutation authority context."},
    {JsApiStructOwner::ZoneData, "zone_data", "map", "map", "getMap", "setMap", "string | null",
     true, ImplementedReadOnly, SetterImplemented,
     "Returns the optional zone map text copied into the invocation snapshot when present.",
     "Updates or clears the invocation snapshot zone map text after nullability, type, length, "
     "and unsupported-character checks, and applies to live owned memory only when dispatch "
     "provides target-scoped persistent setter authority.",
     "mutation", "Persistent application requires target-scoped dispatch mutation authority context."},
    {JsApiStructOwner::ZoneData, "zone_data", "lifespan", "lifespan", "getLifespan", "setLifespan",
     "number", false, ImplementedReadOnly, Deferred, "Returns the minutes between zone reset checks.",
     "Zone reset lifespan writes are deferred "
     "until reset scheduling rules are mapped.",
     "mutation", "Reset-scheduling scalar; defer until pulse/reset side effects are explicit."},
    {JsApiStructOwner::ZoneData, "zone_data", "age", "age", "getAge", "setAge", "number", false,
     ImplementedReadOnly, Unsupported, "Returns the current zone age in minutes.",
     "Direct age writes are unsupported; reset scheduling should own this value.", "mutation", ""},
    {JsApiStructOwner::ZoneData, "zone_data", "top", "topRoomVnum", "getTopRoomVnum",
     "setTopRoomVnum", "number", false, ImplementedReadOnly, Unsupported,
     "Returns the upper room vnum for the zone.",
     "Changing zone room bounds from "
     "JavaScript is unsupported.",
     "none", "World topology field."},
    {JsApiStructOwner::ZoneData, "zone_data", "x", "x", "getX", "setX", "number", false, ImplementedReadOnly,
     SetterPlanned, "Returns the zone map x coordinate.",
     "Planned setter for the zone map x coordinate. It must validate a bounded integer map "
     "coordinate, require target-scoped persistent setter authority, and preserve map layout "
     "constraints before it becomes callable.",
     "mutation", "Low-risk scalar candidate; implementation deferred until coordinate range is pinned."},
    {JsApiStructOwner::ZoneData, "zone_data", "y", "y", "getY", "setY", "number", false, ImplementedReadOnly,
     SetterPlanned, "Returns the zone map y coordinate.",
     "Planned setter for the zone map y coordinate. It must validate a bounded integer map "
     "coordinate, require target-scoped persistent setter authority, and preserve map layout "
     "constraints before it becomes callable.",
     "mutation", "Low-risk scalar candidate; implementation deferred until coordinate range is pinned."},
    {JsApiStructOwner::ZoneData, "zone_data", "symbol", "symbol", "getSymbol", "setSymbol",
     "string", false, ImplementedReadOnly, SetterImplemented,
     "Returns the single-character zone map symbol.",
     "Updates the invocation snapshot single-character printable ASCII zone map symbol after empty, "
     "multi-character, control, whitespace-only, and non-ASCII checks, and applies to live owned "
     "memory only when dispatch provides target-scoped persistent setter authority.",
     "mutation", "Persistent application requires target-scoped dispatch mutation authority context."},
    {JsApiStructOwner::ZoneData, "zone_data", "level", "level", "getLevel", "setLevel", "number",
     false, ImplementedReadOnly, Deferred, "Returns the zone level value.",
     "Zone level writes are deferred until builder ownership and balance rules are mapped.",
     "mutation", "Balance-sensitive scalar; defer until gameplay impact and authority rules are explicit."},
    {JsApiStructOwner::ZoneData, "zone_data", "white_power", "whitePower", "getWhitePower",
     "setWhitePower", "number", false, Deferred, Unsupported,
     "Planned read-only White-side zone power.",
     "Direct power writes are unsupported unless a "
     "future faction-power API owns recalculation.",
     "mutation", "Derived gameplay state."},
    {JsApiStructOwner::ZoneData, "zone_data", "dark_power", "darkPower", "getDarkPower",
     "setDarkPower", "number", false, Deferred, Unsupported,
     "Planned read-only Dark-side zone power.",
     "Direct power writes are unsupported unless a "
     "future faction-power API owns recalculation.",
     "mutation", "Derived gameplay state."},
    {JsApiStructOwner::ZoneData, "zone_data", "magi_power", "magiPower", "getMagiPower",
     "setMagiPower", "number", false, Deferred, Unsupported,
     "Planned read-only Magi-side zone power.",
     "Direct power writes are unsupported unless a "
     "future faction-power API owns recalculation.",
     "mutation", "Derived gameplay state."},
    {JsApiStructOwner::ZoneData, "zone_data", "zone_short_description", "shortDescriptions",
     "getShortDescriptions", "setShortDescriptions", "readonly ExtraDescription[]", true, Deferred,
     Deferred, "Planned read-only zone short-description list.",
     "Zone short-description writes are deferred until list ownership rules are mapped.",
     "mutation", "Linked list pointer must never be exposed."},
    {JsApiStructOwner::ZoneData, "zone_data", "zone_description", "extraDescriptions",
     "getExtraDescriptions", "setExtraDescriptions", "readonly ExtraDescription[]", true, Deferred,
     Deferred, "Planned read-only zone extra-description list.",
     "Zone extra-description writes are deferred until list ownership rules are mapped.",
     "mutation", "Linked list pointer must never be exposed."},
    {JsApiStructOwner::ZoneData, "zone_data", "zone_map", "mapDescriptions", "getMapDescriptions",
     "setMapDescriptions", "readonly ExtraDescription[]", true, Deferred, Deferred,
     "Planned read-only zone map-description list.",
     "Zone map-description writes are deferred until list ownership rules are mapped.", "mutation",
     "Linked list pointer must never be exposed."},
    {JsApiStructOwner::ZoneData, "zone_data", "min_level_look", "minimumLookLevel",
     "getMinimumLookLevel", "setMinimumLookLevel", "number", false, ImplementedReadOnly, Deferred,
     "Returns the minimum level required to inspect zone map/details.",
     "Minimum look level writes are deferred until level range, immortal visibility, and builder "
     "authority rules are mapped.",
     "mutation", "Visibility-gating scalar; defer until product range and permission semantics are pinned."},
    {JsApiStructOwner::ZoneData, "zone_data", "owners", "owners", "getOwners", "setOwners", "never",
     true, Internal, Unsupported,
     "Zone owner list is sensitive authorization data and no builder getter is emitted.",
     "Owner-list mutation from JavaScript is unsupported; use server authorization flows instead.",
     "none", "Linked list pointer must never be exposed."},
    {JsApiStructOwner::ZoneData, "zone_data", "reset_mode", "resetMode", "getResetMode",
     "setResetMode", "number", false, ImplementedReadOnly, Deferred, "Returns the legacy zone reset mode.",
     "Reset mode writes are deferred until reset semantics "
     "and allowed domain values are documented.",
     "mutation", "Reset-sensitive scalar; defer until scheduling and reset side effects are explicit."},
    {JsApiStructOwner::ZoneData, "zone_data", "number", "vnum", "getVnum", "setVnum", "number",
     false, ImplementedReadOnly, Unsupported, "Returns the public zone vnum.",
     "Changing a loaded zone vnum from JavaScript is unsupported.", "none",
     "Already exposed as Zone.vnum."},
    {JsApiStructOwner::ZoneData, "zone_data", "cmdno", "resetCommandCount", "getResetCommandCount",
     "setResetCommandCount", "number", false, Internal, Unsupported,
     "Reset command count is internal and no builder getter is emitted by default.",
     "Reset command count mutation from JavaScript is unsupported.", "none", "Reset loader data."},
    {JsApiStructOwner::ZoneData, "zone_data", "cmd", "resetCommands", "getResetCommands",
     "setResetCommands", "never", true, Internal, Unsupported,
     "Raw reset command table is internal and no builder getter is emitted.",
     "Raw reset command mutation from JavaScript is unsupported.", "none", "Raw command pointer."},
};

} // namespace

const JsApiStructFieldMapping *js_api_struct_field_mappings() { return FieldMappings; }

std::size_t js_api_struct_field_mapping_count() {
    return sizeof(FieldMappings) / sizeof(FieldMappings[0]);
}

const char *js_api_struct_owner_name(JsApiStructOwner owner) {
    switch (owner) {
    case JsApiStructOwner::CharData:
        return "char_data";
    case JsApiStructOwner::ObjData:
        return "obj_data";
    case JsApiStructOwner::RoomData:
        return "room_data";
    case JsApiStructOwner::ZoneData:
        return "zone_data";
    }
    return "unknown";
}

std::size_t js_api_struct_field_mapping_count_for_owner(JsApiStructOwner owner) {
    std::size_t count = 0;
    for (std::size_t index = 0; index < js_api_struct_field_mapping_count(); ++index) {
        if (FieldMappings[index].owner == owner)
            ++count;
    }
    return count;
}

const JsApiStructFieldMapping *find_js_api_struct_field_mapping(JsApiStructOwner owner,
                                                                const char *source_field) {
    if (!source_field)
        return nullptr;
    for (std::size_t index = 0; index < js_api_struct_field_mapping_count(); ++index) {
        if (FieldMappings[index].owner == owner &&
            std::strcmp(FieldMappings[index].source_field, source_field) == 0)
            return &FieldMappings[index];
    }
    return nullptr;
}
