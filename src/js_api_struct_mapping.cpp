#include "js_api_struct_mapping.h"

#include "structs.h"

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
     "Moving a character requires a validated movement/teleport API and is deferred. Direct room "
     "writes would bypass movement triggers, room people lists, combat stop/start behavior, "
     "mount/follower relocation, visibility messages, death rooms, private/security room checks, "
     "and save/load room bookkeeping.",
     "world-mutation",
     "Setter must preserve movement triggers, mounts, followers, and visibility rules."},
    {JsApiStructOwner::CharData, "char_data", "player", "profile", "getProfile", "setProfile",
     "CharacterProfile", false, ImplementedReadOnly, Unsupported,
     "Returns a frozen read-only snapshot of copied public char_player_data fields, including "
     "identity text, description text, demographic ids, language metadata, age timestamps, and "
     "display ranking. Raw char pointers are not exposed.",
     "Whole-profile mutation is unsupported for builder scripts; identity, title, race, level, "
     "description, and account-backed fields need separate audited helpers with persistence and "
     "player/NPC authority rules.",
     "mutation", "Nested struct; do not expose as a raw object."},
    {JsApiStructOwner::CharData, "char_data", "abilities", "baseAbilities", "getBaseAbilities",
     "setBaseAbilities", "AbilityScores", false, ImplementedReadOnly, Unsupported,
     "Returns a frozen read-only snapshot of base ability scores before active affects and "
     "equipment modifiers: strength, intelligence, willpower, dexterity, constitution, and "
     "leadership.",
     "Base ability-score writes are unsupported for builder scripts; any future admin-only helper "
     "must validate score ranges, creation/leveling rules, derived stat recalculation, and "
     "persistence.",
     "mutation", "Nested struct."},
    {JsApiStructOwner::CharData, "char_data", "tmpabilities", "currentAbilities",
     "getCurrentAbilities", "setCurrentAbilities", "AbilityScores", false, ImplementedReadOnly,
     Unsupported,
     "Returns a frozen read-only snapshot of currently modified ability scores: strength, "
     "intelligence, willpower, dexterity, constitution, and leadership.",
     "Current ability writes are unsupported for builder scripts because these values are derived "
     "from base abilities, active affects, equipment, and recalculation helpers.",
     "mutation", "Nested struct."},
    {JsApiStructOwner::CharData, "char_data", "constabilities", "rolledAbilities",
     "getRolledAbilities", "setRolledAbilities", "AbilityScores", false, ImplementedReadOnly,
     Unsupported,
     "Returns a frozen read-only snapshot of rolled character creation ability scores: strength, "
     "intelligence, willpower, dexterity, constitution, and leadership.",
     "Rolled ability writes are unsupported for builder scripts; this is character-creation "
     "history and any future admin-only helper needs audit and persistence rules.",
     "mutation", "Nested struct."},
    {JsApiStructOwner::CharData, "char_data", "points", "points", "getPoints", "setPoints",
     "CharacterPoints", false, ImplementedReadOnly, Unsupported,
     "Returns a frozen read-only snapshot of character point data: body-part hit values, carried "
     "gold, experience, spirit, regen, offense, damage, defense, encumbrance, willpower, and spell "
     "combat values.",
     "Whole-points mutation is unsupported for builder scripts; hit/move/spirit/OB/damage/defense "
     "fields need separate helpers with clamping, death handling, combat state, regen, "
     "encumbrance, and persistence semantics.",
     "mutation", "Nested struct."},
    {JsApiStructOwner::CharData, "char_data", "specials", "specials", "getSpecials", "setSpecials",
     "CharacterSpecials", false, ImplementedReadOnly, Unsupported,
     "Returns a frozen read-only snapshot of safe character runtime state: combat/hunt/memory "
     "presence, position names, carry counters, timers, energy, parry, direction/tactics names, "
     "prompt/load diagnostics, and script number.",
     "Whole-special-state mutation is unsupported for builder scripts because the fields drive "
     "combat targets, position, timers, aliases, prompts, script numbers, hiding, carrying "
     "counters, and procedure state.",
     "mutation", "Nested struct."},
    {JsApiStructOwner::CharData, "char_data", "specials2", "specials2", "getSpecials2",
     "setSpecials2", "CharacterSpecials2", false, ImplementedReadOnly, Unsupported,
     "Returns a frozen read-only snapshot of additional character state: load room, spell "
     "practice count, alignment, player/NPC flags, preferences, wimp/freeze/saving values, "
     "perception, condition counters, mini-levels, morale, rerolls, leg encumbrance, retirement "
     "marker, hide flags, tactics, shooting, casting, and two-handed mode. Persistent identity, "
     "owner ids, raw roleplay/teaching bitvectors, and "
     "authentication failure counters are intentionally not exposed.",
     "Whole-specials2 mutation is unsupported for builder scripts because the fields include "
     "player/NPC flags, preferences, ids, load rooms, conditions, perception, alignment, "
     "teaching, tactics, and other persistence-sensitive state.",
     "mutation", "Nested struct."},
    {JsApiStructOwner::CharData, "char_data", "profs", "professions", "getProfessions",
     "setProfessions", "readonly Profession[]", true, ImplementedReadOnly, Unsupported,
     "Returns a frozen read-only snapshot of public profession progression data for mage, "
     "mystic, ranger, and warrior entries when a character has profession storage: level, "
     "points, coefficient, and experience. Color settings and specialization state are exposed "
     "through separate planned surfaces.",
     "Profession writes are unsupported for builder scripts; any future admin-only helper must "
     "validate profession domains, level progression, skill recalculation, and player-file "
     "persistence.",
     "mutation", "Pointer-owned nested data."},
    {JsApiStructOwner::CharData, "char_data", "extra_specialization_data", "specializations",
     "getSpecializations", "setSpecializations", "SpecializationData", false, ImplementedReadOnly,
     Unsupported,
     "Returns a frozen read-only specialization summary with persisted selected id/key/name, "
     "runtime current id/key/name, mage-specialization classification, and whether runtime "
     "specialization state exists. Raw specialization subclass counters, targets, off-hand "
     "object pointers, and timestamps are intentionally not exposed.",
     "Specialization writes are unsupported for builder scripts; class-specific invariants, "
     "respec rules, combat bonuses, and persistence/audit semantics need explicit admin helpers.",
     "mutation", "Nested data."},
    {JsApiStructOwner::CharData, "char_data", "damage_details", "damageDetails", "getDamageDetails",
     "setDamageDetails", "DamageDetails", false, ImplementedReadOnly, Unsupported,
     "Returns a frozen read-only combat damage summary with elapsed combat seconds, aggregate "
     "damage, damage per second, and per-source id/name/kind/count/total/max/average/percent "
     "entries. Combat ownership, threat, XP sharing, timer internals, and cleanup state are not "
     "exposed.",
     "Damage bookkeeping writes are unsupported for builder scripts; combat participation, XP "
     "sharing, threat, timers, and cleanup must stay owned by combat engine helpers.",
     "mutation", "Combat-internal data."},
    {JsApiStructOwner::CharData, "char_data", "skills", "skills", "getSkills", "setSkill",
     "readonly SkillValue[]", true, ImplementedReadOnly, Unsupported,
     "Returns a frozen read-only trained-skill snapshot with one entry per nonzero practice "
     "value, including skill id, builder-facing name, owning profession, required level, "
     "practice count, live target/position/cost metadata, learning metadata, fast-update flag, "
     "and specialization id. The raw MAX_SKILLS byte pointer is not exposed.",
     "Skill writes are unsupported for builder scripts; any future training/admin helper must "
     "validate skill ids, percent ranges, practice sessions, guild restrictions, derived "
     "knowledge, and player-file persistence.",
     "mutation", "Raw byte pointer must never be exposed."},
    {JsApiStructOwner::CharData, "char_data", "knowledge", "knowledge", "getKnowledge",
     "setKnowledge", "readonly KnowledgeValue[]", true, ImplementedReadOnly, Unsupported,
     "Returns a frozen read-only computed-knowledge snapshot with one entry per nonzero knowledge "
     "value, including skill id, builder-facing name, owning profession, required level, computed "
     "knowledge value, live target/position/cost metadata, learning metadata, fast-update flag, "
     "and specialization id. The raw MAX_SKILLS byte pointer is not exposed.",
     "Knowledge writes are unsupported for builder scripts because values are normally derived "
     "from skills, body type, confusion, teaching, guild limits, and recalculation helpers.",
     "mutation", "Raw byte pointer must never be exposed."},
    {JsApiStructOwner::CharData, "char_data", "affected", "affects", "getAffects", "setAffects",
     "readonly Affect[]", true, ImplementedReadOnly, Deferred,
     "Returns a frozen read-only active-affect snapshot capped at MAX_AFFECT entries, including "
     "spell/skill type id, builder-facing name, duration, tick phase, modifier, APPLY_* location "
     "id/name, AFF_* bitvector id/names, and counter. Raw linked-list pointers are not exposed.",
     "Affect mutation needs explicit add/remove helpers and is deferred because raw list writes "
     "would bypass duration accounting, affect bit recalculation, stat recomputation, combat "
     "side effects, room/mount interactions, and persistence rules.",
     "world-mutation", "Linked list pointer must never be exposed."},
    {JsApiStructOwner::CharData, "char_data", "equipment", "equipment", "getEquipment",
     "setEquipmentSlot", "readonly EquipmentSlot[]", false, ImplementedReadOnly, Deferred,
     "Returns a frozen read-only equipment snapshot with one entry per MAX_WEAR slot, including "
     "the slot index, canonical builder-facing slot name, and a nullable shallow object snapshot. "
     "Raw object pointer slots and recursive wearer/carrier handles are not exposed.",
     "Equipment writes require validated wear/remove helpers and trigger parity. Direct equipment "
     "slot assignment would bypass ON_WEAR JavaScript/legacy triggers, wear restrictions, carried "
     "list transfer, light recounting, apply-affect recalculation, combat stat recalculation, and "
     "player crash-save flags.",
     "world-mutation", "Object pointer array must never be exposed."},
    {JsApiStructOwner::CharData, "char_data", "carrying", "inventory", "getInventory",
     "setInventory", "readonly InventoryObjectSnapshot[]", true, ImplementedReadOnly, Unsupported,
     "Returns a frozen read-only snapshot of up to 100 top-level carried inventory objects using "
     "shallow object snapshots without setter methods, recursive owner handles, nested contents, "
     "or raw linked-list pointers.",
     "Replacing the raw inventory list from JavaScript is unsupported; use explicit inventory "
     "helpers when they exist so carried weight, item ownership, equipment transitions, nested "
     "containers, and player crash-save state stay centralized.",
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
     "setFollowers", "readonly CharacterRelationshipSnapshot[]", true, ImplementedReadOnly,
     Unsupported,
     "Returns frozen read-only shallow snapshots of live reciprocal followers, capped to 100 "
     "visited follower-list nodes.",
     "Replacing the follower linked list from JavaScript is unsupported because follow state "
     "requires master back-pointers, follower caps, charm/orc/tamed behavior, group interactions, "
     "loop prevention, and room movement propagation.",
     "world-mutation", "Exposes no raw follow_type nodes or recursive character handles."},
    {JsApiStructOwner::CharData, "char_data", "master", "master", "getMaster", "setMaster",
     "CharacterRelationshipSnapshot | null", true, ImplementedReadOnly, Deferred,
     "Returns a frozen read-only shallow snapshot of the followed master when the master pointer "
     "is live and the master's follower list reciprocally contains this character.",
     "Master changes are deferred until follow/unfollow semantics, master back-pointers, follower "
     "caps, charm/orc/tamed behavior, group interactions, and loop prevention are mapped.",
     "world-mutation", "Raw character pointer must never be exposed."},
    {JsApiStructOwner::CharData, "char_data", "master_number", "masterNumber", "getMasterNumber",
     "setMasterNumber", "number", false, Internal, Unsupported,
     "Internal persisted master id; no builder getter is emitted.",
     "Changing the persisted master id directly is unsupported.", "none", "Implementation field."},
    {JsApiStructOwner::CharData, "char_data", "mount_data", "mount", "getMount", "setMount",
     "MountData", false, ImplementedReadOnly, Deferred,
     "Returns a frozen read-only mount-state snapshot with live reciprocal mount, rider, and "
     "next-rider relationship snapshots when those pointers pass stored-number validation.",
     "Mount writes are deferred until ride/dismount rules, rider back-pointers, room movement "
     "propagation, carried-weight accounting, combat restrictions, and mount persistence are "
     "mapped.",
     "world-mutation", "Exposes no raw mount_data pointers or recursive character handles."},
    {JsApiStructOwner::CharData, "char_data", "group", "group", "getGroup", "setGroup",
     "Group | null", true, Deferred, Unsupported,
     "Deferred group snapshot until the adapter has a live group registry or equivalent "
     "lifetime token so raw group_data pointers can fail closed before dereference.",
     "Replacing a group pointer from JavaScript is unsupported because group membership requires "
     "leader/member list integrity, follow/master consistency, combat XP sharing, and movement "
     "propagation rules.",
     "world-mutation", "Raw group pointer must never be exposed."},
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
     "setClassPoints", "number", false, ImplementedReadOnly, Unsupported,
     "Returns the character-creation class point value copied into the invocation snapshot.",
     "Class-point writes are unsupported for builder scripts because this is character-creation "
     "bookkeeping, not a builder-facing world script field; any future admin-only mutation needs "
     "account/admin audit and persistence rules.",
     "mutation", "Character creation bookkeeping."},
    {JsApiStructOwner::CharData, "char_data", "interrupt_count", "interruptCount",
     "getInterruptCount", "setInterruptCount", "number", false, ImplementedReadOnly, Unsupported,
     "Returns the current interrupt count copied into the invocation snapshot for combat/casting "
     "diagnostics.",
     "Interrupt count writes are unsupported for builder scripts; any future admin-only helper "
     "must map caster AI, mental/combat interruption, decay timing, and wait-state interactions.",
     "mutation", "Combat AI bookkeeping."},
    {JsApiStructOwner::CharData, "char_data", "interrupt_time", "interruptTime", "getInterruptTime",
     "setInterruptTime", "number", false, ImplementedReadOnly, Unsupported,
     "Returns the countdown before interrupt count decays copied into the invocation snapshot.",
     "Interrupt timer writes are unsupported for builder scripts; any future admin-only helper "
     "must map caster AI, mental/combat interruption, decay timing, and wait-state interactions.",
     "mutation", "Combat AI bookkeeping."},
    {JsApiStructOwner::CharData, "char_data", "spec_busy", "specialBusy", "isSpecialBusy",
     "setSpecialBusy", "boolean", false, ImplementedReadOnly, Unsupported,
     "Returns whether a special procedure is busy in the invocation snapshot.",
     "Special busy writes are unsupported for builder scripts; any future admin-only helper must "
     "map special-procedure reentrancy, trigger dispatch, legacy wait-state, and reset/heartbeat "
     "interactions.",
     "mutation", "Special-procedure state."},

    {JsApiStructOwner::ObjData, "obj_data", "item_number", "vnum", "getVnum", "setVnum",
     "number | null", true, ImplementedReadOnly, Unsupported,
     "Returns the object prototype vnum when the prototype can be resolved; otherwise null.",
     "Changing a live object's prototype from JavaScript is unsupported.", "none",
     "Already exposed as GameObject.vnum for implemented snapshots."},
    {JsApiStructOwner::ObjData, "obj_data", "in_room", "room", "getRoom", "setRoom", "Room | null",
     true, ImplementedReadOnly, Deferred,
     "Returns the direct containing room when the object is in a room; carried, worn, nested, or "
     "invalid objects return null.",
     "Moving objects requires explicit load/move/extract helpers and is deferred. Direct room "
     "writes would bypass handler-maintained carrier, container, room contents, light, mount "
     "carry-weight, player crash-save, and stale linked-list invariants.",
     "world-mutation",
     "Setter must preserve container, carrier, and room contents invariants through audited "
     "movement helpers rather than raw field assignment."},
    {JsApiStructOwner::ObjData, "obj_data", "obj_flags", "flags", "getFlags", "setFlags",
     "ObjectFlags", false, ImplementedReadOnly, Unsupported,
     "Returns a structured read-only object flag snapshot with symbolic type, wear flags, extra "
     "flags, material, and scalar economy/timer fields.",
     "Whole-object flag writes are unsupported for builder scripts; object level and rarity use "
     "the dedicated GameObject.setLevel(value: number) and GameObject.setRarity(value: number) "
     "validated setters, while other flag/value domains need separate named helper APIs.",
     "mutation",
     "Legacy item-type value slots remain intentionally absent until each item type has a "
     "documented builder-facing domain. Object flags.level and flags.rarity are implemented "
     "subfield setters because they have bounded persisted scalar paths."},
    {JsApiStructOwner::ObjData, "obj_data", "affected", "affects", "getAffects", "setAffects",
     "readonly ObjectAffect[]", false, ImplementedReadOnly, Unsupported,
     "Returns a fixed-slot equipment modifier snapshot. Entries use named apply locations, "
     "integer modifiers, canonical slot ordering, and omit empty none-location plus zero-modifier "
     "slots.",
     "Object affect writes are unsupported for builder scripts until a slot-specific helper owns "
     "equipment recalculation, apply-location validation, canonical ordering, and persistence.",
     "mutation",
     "Fixed-size equipment modifier slots are exposed as a read-only diagnostic snapshot."},
    {JsApiStructOwner::ObjData, "obj_data", "name", "name", "getName", "setName", "string", false,
     ImplementedReadOnly, SetterImplemented,
     "Returns the object's keyword/name string for builder conditions and diagnostics.",
     "Updates the invocation snapshot object keyword/name after type, nonblank, length, and "
     "unsupported-character checks, and applies to live owned memory only when dispatch provides "
     "target-scoped persistent setter authority.",
     "mutation",
     "Persistent application requires target-scoped dispatch mutation authority context."},
    {JsApiStructOwner::ObjData, "obj_data", "description", "description", "getDescription",
     "setDescription", "string", false, ImplementedReadOnly, SetterImplemented,
     "Returns the room-visible object description copied into the invocation snapshot.",
     "Updates the invocation snapshot object description after type, length, and "
     "unsupported-character checks, and applies to live owned memory only when dispatch provides "
     "target-scoped persistent setter authority.",
     "mutation",
     "Persistent application requires target-scoped dispatch mutation authority context."},
    {JsApiStructOwner::ObjData, "obj_data", "short_description", "shortDescription",
     "getShortDescription", "setShortDescription", "string", false, ImplementedReadOnly,
     SetterImplemented,
     "Returns the carried/worn short description copied into the invocation snapshot.",
     "Updates the invocation snapshot short description after type, length, and "
     "unsupported-character checks, and applies to live owned memory only when dispatch provides "
     "target-scoped persistent setter authority.",
     "mutation",
     "Persistent application requires target-scoped dispatch mutation authority context."},
    {JsApiStructOwner::ObjData, "obj_data", "action_description", "actionDescription",
     "getActionDescription", "setActionDescription", "string | null", true, ImplementedReadOnly,
     SetterImplemented,
     "Returns the optional use/action text copied into the invocation snapshot when present.",
     "Updates or clears the invocation snapshot action description after nullability, type, "
     "length, and unsupported-character checks, and applies to live owned memory only when "
     "dispatch provides target-scoped persistent setter authority.",
     "mutation",
     "Persistent application requires target-scoped dispatch mutation authority context."},
    {JsApiStructOwner::ObjData, "obj_data", "ex_description", "extraDescriptions",
     "getExtraDescriptions", "setExtraDescriptions", "readonly ExtraDescription[]", false,
     ImplementedReadOnly, Unsupported,
     "Returns bounded read-only object extra-description entries with keyword and description text "
     "copied from the live linked list.",
     "Whole extra-description list writes are unsupported for builder scripts; future "
     "add/update/remove helper APIs must own linked-list allocation, stale-handle protection, "
     "bounded list size, text length, sanitization, persistence, rollback, and audit semantics.",
     "mutation", "Linked-list storage is exposed only as a bounded frozen text snapshot."},
    {JsApiStructOwner::ObjData, "obj_data", "carried_by", "carriedBy", "getCarriedBy",
     "setCarriedBy", "Character | null", true, ImplementedReadOnly, Deferred,
     "Returns the live character carrying the object when applicable; otherwise null.",
     "Carrier changes require explicit inventory helpers and are deferred. Direct carrier writes "
     "would bypass inventory/equipment list updates, carried weight, riding mount carry weight, "
     "player crash-save flags, and worn-vs-carried ownership checks.",
     "world-mutation", "Already exposed as GameObject.carriedBy for implemented snapshots."},
    {JsApiStructOwner::ObjData, "obj_data", "owner", "ownerId", "getOwnerId", "setOwnerId", "never",
     false, Internal, Unsupported,
     "Object owner id is sensitive authorization data and no builder getter is emitted.",
     "Owner id mutation from JavaScript is unsupported; use server authorization flows instead.",
     "none", "May reveal account/player identity policy."},
    {JsApiStructOwner::ObjData, "obj_data", "in_obj", "container", "getContainer", "setContainer",
     "EquipmentObjectSnapshot | null", true, ImplementedReadOnly, Deferred,
     "Returns a shallow read-only snapshot of the containing object when the container is live and "
     "its "
     "contents list reciprocally contains this object within bounded cycle-safe traversal.",
     "Container changes require explicit object movement helpers and are deferred. Direct "
     "container writes would bypass nested weight propagation, capacity/counting rules, decay "
     "movement, cycle prevention, and linked-list ownership.",
     "world-mutation",
     "Nested storage is exposed only as a nullable shallow frozen container snapshot."},
    {JsApiStructOwner::ObjData, "obj_data", "contains", "contents", "getContents", "setContents",
     "readonly EquipmentObjectSnapshot[]", true, ImplementedReadOnly, Unsupported,
     "Returns a bounded shallow read-only snapshot of directly contained live objects whose "
     "container back-pointer reciprocally references this object.",
     "Replacing the contents list from JavaScript is unsupported; use explicit movement helpers "
     "when they exist so nested ownership, capacity, weight propagation, decay extraction, and "
     "cycle guards stay centralized.",
     "world-mutation", "Linked-list storage is exposed only as shallow frozen contents snapshots."},
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
     "boolean", false, ImplementedReadOnly, Unsupported,
     "Returns a read-only boolean showing whether legacy runtime state has marked this object as "
     "touched by a player or administrative object action. Any nonzero stored value is exposed as "
     "true.",
     "Touched-state writes are unsupported for builder scripts because the flag is "
     "runtime/player-interaction state that is initialized on prototype instantiation, changed by "
     "player/admin object actions, and not part of the object shaper's persisted prototype "
     "metadata.",
     "mutation",
     "Runtime/player-interaction state is exposed only as a normalized read-only boolean."},
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
     false, ImplementedReadOnly, SetterImplemented, "Returns the room level value.",
     "Updates the invocation snapshot room level after integer and 0 through 100 inclusive bounds "
     "checks, rejects negative values, values above 100, and fractional or other non-integer "
     "values, and applies to live owned memory only when dispatch provides target-scoped "
     "persistent setter authority. This changes the persisted room-file scalar value used by "
     "legacy same-level room filtering.",
     "mutation",
     "Persistent application requires target-scoped dispatch mutation authority context."},
    {JsApiStructOwner::RoomData, "room_data", "sector_type", "sectorType", "getSectorType",
     "setSectorType", "string", false, ImplementedReadOnly, SetterImplemented,
     "Returns the readable sector type name.",
     "Updates the invocation snapshot room sector type after canonical live sector-name "
     "validation, rejects Unknown, aliases, raw numeric values, and malformed names, and "
     "applies to live owned memory only when dispatch provides target-scoped persistent "
     "setter authority. This changes the persisted room-file sector scalar and can "
     "immediately affect movement cost, swimming, weather/sunlight messaging, tracking, "
     "and MOB_STAY_TYPE behavior.",
     "mutation",
     "Invalid loaded sector values are exposed as Unknown, not raw integers. Persistent "
     "application requires target-scoped dispatch mutation authority context."},
    {JsApiStructOwner::RoomData, "room_data", "name", "name", "getName", "setName", "string", false,
     ImplementedReadOnly, SetterImplemented, "Returns the room display name.",
     "Updates the invocation snapshot room display name after type, nonblank, length, and "
     "unsupported-character checks, and applies to live owned memory only when dispatch provides "
     "target-scoped persistent setter authority.",
     "mutation",
     "Persistent application requires target-scoped dispatch mutation authority context."},
    {JsApiStructOwner::RoomData, "room_data", "description", "description", "getDescription",
     "setDescription", "string", false, ImplementedReadOnly, SetterImplemented,
     "Returns the room long description copied into the invocation snapshot.",
     "Updates the invocation snapshot room long description after type, length, and "
     "unsupported-character checks, and applies to live owned memory only when dispatch provides "
     "target-scoped persistent setter authority.",
     "mutation",
     "Persistent application requires target-scoped dispatch mutation authority context."},
    {JsApiStructOwner::RoomData, "room_data", "ex_description", "extraDescriptions",
     "getExtraDescriptions", "setExtraDescriptions", "readonly ExtraDescription[]", false,
     ImplementedReadOnly, Unsupported,
     "Returns bounded read-only room extra-description entries with keyword and description text "
     "copied from the live linked list.",
     "Whole extra-description list writes are unsupported for builder scripts; future "
     "add/update/remove helper APIs must own linked-list allocation, stale-handle protection, "
     "bounded list size, text length, sanitization, persistence, rollback, and audit semantics.",
     "mutation", "Linked-list storage is exposed only as a bounded frozen text snapshot."},
    {JsApiStructOwner::RoomData, "room_data", "dir_option", "exits", "getExits", "setExit",
     "readonly RoomExit[]", false, ImplementedReadOnly, Deferred,
     "Returns frozen read-only room exit entries copied by direction from loaded room exits.",
     "Exit writes require explicit setExit/removeExit helpers and are deferred until direction, "
     "door, destination-room, reset-command, bidirectional-link, permission, and persistence "
     "semantics are mapped.",
     "world-mutation",
     "Pointer array is exposed only as copied direction, text, destination-vnum, width, key, and "
     "symbolic flag values."},
    {JsApiStructOwner::RoomData, "room_data", "room_track", "tracks", "getTracks", "setTracks",
     "readonly RoomTrack[]", false, Internal, Unsupported,
     "Room tracking data is internal unless a future tracking API is designed.",
     "Raw track mutation from JavaScript is unsupported.", "none", "Fixed internal array."},
    {JsApiStructOwner::RoomData, "room_data", "room_flags", "flags", "getFlags", "setFlags",
     "readonly string[]", false, ImplementedReadOnly, Deferred,
     "Returns builder-safe room flag names, excluding BFS_MARK and unnamed/internal bits.",
     "Room flag writes are deferred until a builder-facing flag vocabulary, additive/removal "
     "helper shape, internal/transient bit exclusions, room-affect synchronization, and "
     "movement, combat, teleport, lighting, drinking, and security side effects are mapped.",
     "mutation",
     "Raw bitvector is filtered before reaching JavaScript. PERMAFFECT may be visible as "
     "read-only permanentAffect metadata when present, but BFS_MARK, PERMAFFECT, and unnamed "
     "bits must never be user-settable through a raw bitvector path."},
    {JsApiStructOwner::RoomData, "room_data", "alignment", "alignment", "getAlignment",
     "setAlignment", "number", false, ImplementedReadOnly, Deferred,
     "Returns the room alignment value.",
     "Room alignment writes are deferred because the inspected room file writer stores the "
     "leading alignment column as 0, the room implementation path does not copy alignment into "
     "the live room record, and no builder-facing persistence or gameplay side-effect policy has "
     "been confirmed for this field.",
     "mutation",
     "Do not make callable until persistence/editing semantics are deliberately added or "
     "confirmed."},
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
     "readonly EquipmentObjectSnapshot[]", false, ImplementedReadOnly, Unsupported,
     "Returns a bounded shallow read-only snapshot of directly contained live objects whose room "
     "index reciprocally references this room.",
     "Replacing the room contents linked list from JavaScript is unsupported; use explicit "
     "object movement/load/extract helpers so room contents, object container/carrier links, "
     "light counters, crash-save flags, and stale-list checks stay centralized.",
     "world-mutation", "Linked-list storage is exposed only as shallow frozen contents snapshots."},
    {JsApiStructOwner::RoomData, "room_data", "people", "characters", "getCharacters",
     "setCharacters", "readonly CharacterRelationshipSnapshot[]", false, ImplementedReadOnly,
     Unsupported,
     "Returns a bounded shallow read-only snapshot of direct live room occupants whose room index "
     "reciprocally references this room.",
     "Replacing the room people linked list from JavaScript is unsupported; use explicit "
     "movement/teleport helpers so character room pointers, combat state, followers, mounts, "
     "visibility, and room security checks stay centralized.",
     "world-mutation", "Linked-list storage is exposed only as shallow frozen occupant snapshots."},
    {JsApiStructOwner::RoomData, "room_data", "affected", "affects", "getAffects", "setAffects",
     "readonly Affect[]", false, ImplementedReadOnly, Unsupported,
     "Returns a bounded shallow read-only snapshot of room affect linked-list entries.",
     "Room affect writes are unsupported for builder scripts until explicit add/remove helpers "
     "own duration accounting, room flag synchronization, movement/combat/light side effects, "
     "and persistence.",
     "world-mutation", "Linked-list storage is exposed only as shallow frozen affect snapshots."},
    {JsApiStructOwner::RoomData, "room_data", "bleed_track", "bleedTracks", "getBleedTracks",
     "setBleedTracks", "readonly BleedTrack[]", false, Internal, Unsupported,
     "Bleed tracking data is internal unless a future tracking API is designed.",
     "Raw bleed-track mutation from JavaScript is unsupported.", "none", "Fixed internal array."},

    {JsApiStructOwner::ZoneData, "zone_data", "name", "name", "getName", "setName", "string", false,
     ImplementedReadOnly, SetterImplemented, "Returns the zone display name.",
     "Updates the invocation snapshot zone display name after type, nonblank, length, and "
     "unsupported-character checks, and applies to live owned memory only when dispatch provides "
     "target-scoped persistent setter authority.",
     "mutation",
     "Persistent application requires target-scoped dispatch mutation authority context."},
    {JsApiStructOwner::ZoneData, "zone_data", "description", "description", "getDescription",
     "setDescription", "string | null", true, ImplementedReadOnly, SetterImplemented,
     "Returns the optional zone description copied into the invocation snapshot when present.",
     "Updates or clears the invocation snapshot zone description after nullability, type, length, "
     "and unsupported-character checks, and applies to live owned memory only when dispatch "
     "provides target-scoped persistent setter authority.",
     "mutation",
     "Persistent application requires target-scoped dispatch mutation authority context."},
    {JsApiStructOwner::ZoneData, "zone_data", "map", "map", "getMap", "setMap", "string | null",
     true, ImplementedReadOnly, SetterImplemented,
     "Returns the optional zone map text copied into the invocation snapshot when present.",
     "Updates or clears the invocation snapshot zone map text after nullability, type, length, "
     "and unsupported-character checks, and applies to live owned memory only when dispatch "
     "provides target-scoped persistent setter authority.",
     "mutation",
     "Persistent application requires target-scoped dispatch mutation authority context."},
    {JsApiStructOwner::ZoneData, "zone_data", "lifespan", "lifespan", "getLifespan", "setLifespan",
     "number", false, ImplementedReadOnly, SetterImplemented,
     "Returns the minutes between zone reset checks.",
     "Updates the invocation snapshot zone reset lifespan after integer and 1 through 10080 "
     "inclusive bounds checks, rejects zero, negative values, values above 10080, and fractional "
     "or "
     "other non-integer values, and applies to live owned memory only when dispatch provides "
     "target-scoped persistent setter authority. This changes the minute threshold used by "
     "legacy zone reset scheduling for reset modes 1, 2, and 3; reset mode 0 still disables "
     "automatic reset aging.",
     "mutation",
     "Persistent application requires target-scoped dispatch mutation authority context."},
    {JsApiStructOwner::ZoneData, "zone_data", "age", "age", "getAge", "setAge", "number", false,
     ImplementedReadOnly, Unsupported, "Returns the current zone age in minutes.",
     "Direct age writes are unsupported; reset scheduling should own this value.", "mutation", ""},
    {JsApiStructOwner::ZoneData, "zone_data", "top", "topRoomVnum", "getTopRoomVnum",
     "setTopRoomVnum", "number", false, ImplementedReadOnly, Unsupported,
     "Returns the upper room vnum for the zone.",
     "Changing zone room bounds from "
     "JavaScript is unsupported.",
     "none", "World topology field."},
    {JsApiStructOwner::ZoneData, "zone_data", "x", "x", "getX", "setX", "number", false,
     ImplementedReadOnly, SetterImplemented, "Returns the zone map x coordinate.",
     "Updates the invocation snapshot zone map x coordinate after integer and 0 through 25 "
     "inclusive bounds checks, rejects negative values, values above 25 such as 26, and "
     "fractional or other non-integer values instead of relying on legacy map clamping, and "
     "applies to live owned memory only when dispatch provides target-scoped persistent setter "
     "authority. Committed global zone writes redraw the cached world map.",
     "mutation",
     "Persistent application requires target-scoped dispatch mutation authority context."},
    {JsApiStructOwner::ZoneData, "zone_data", "y", "y", "getY", "setY", "number", false,
     ImplementedReadOnly, SetterImplemented, "Returns the zone map y coordinate.",
     "Updates the invocation snapshot zone map y coordinate after integer and 0 through 25 "
     "inclusive bounds checks, accept boundary values 0 and 25, reject negative values, "
     "reject values above 25 such as 26, and reject fractional or other non-integer values "
     "instead of addressing outside the map buffer, and applies to live owned memory only when "
     "dispatch provides target-scoped persistent setter authority. Committed global zone writes "
     "redraw the map through the cached world map path.",
     "mutation",
     "Persistent application requires target-scoped dispatch mutation authority context."},
    {JsApiStructOwner::ZoneData, "zone_data", "symbol", "symbol", "getSymbol", "setSymbol",
     "string", false, ImplementedReadOnly, SetterImplemented,
     "Returns the single-character zone map symbol.",
     "Updates the invocation snapshot single-character printable ASCII zone map symbol after "
     "empty, "
     "multi-character, control, whitespace-only, and non-ASCII checks, and applies to live owned "
     "memory only when dispatch provides target-scoped persistent setter authority.",
     "mutation",
     "Persistent application requires target-scoped dispatch mutation authority context."},
    {JsApiStructOwner::ZoneData, "zone_data", "level", "level", "getLevel", "setLevel", "number",
     false, ImplementedReadOnly, SetterImplemented, "Returns the zone level value.",
     "Updates the invocation snapshot zone level after integer and 0 through 100 inclusive bounds "
     "checks, rejects negative values, values above 100, and fractional or other non-integer "
     "values, and applies to live owned memory only when dispatch provides target-scoped "
     "persistent setter authority. This changes the persisted builder-facing zone metadata value "
     "shown by legacy zone inspection and shaping paths.",
     "mutation",
     "Persistent application requires target-scoped dispatch mutation authority context."},
    {JsApiStructOwner::ZoneData, "zone_data", "white_power", "whitePower", "getWhitePower",
     "setWhitePower", "number", false, ImplementedReadOnly, Unsupported,
     "Returns the White-side zone power copied from the loaded zone table.",
     "Direct White-side power writes are unsupported for builder scripts because zone power is "
     "derived from live character movement, race/allegiance power, recalculation sweeps, and "
     "battlefield-control messaging.",
     "mutation", "Derived gameplay state; future faction-power APIs must own recalculation."},
    {JsApiStructOwner::ZoneData, "zone_data", "dark_power", "darkPower", "getDarkPower",
     "setDarkPower", "number", false, ImplementedReadOnly, Unsupported,
     "Returns the Dark-side zone power copied from the loaded zone table.",
     "Direct Dark-side power writes are unsupported for builder scripts because zone power is "
     "derived from live character movement, race/allegiance power, recalculation sweeps, and "
     "battlefield-control messaging.",
     "mutation", "Derived gameplay state; future faction-power APIs must own recalculation."},
    {JsApiStructOwner::ZoneData, "zone_data", "magi_power", "magiPower", "getMagiPower",
     "setMagiPower", "number", false, ImplementedReadOnly, Unsupported,
     "Returns the Magi-side zone power copied from the loaded zone table.",
     "Direct Magi-side power writes are unsupported for builder scripts because zone power is "
     "derived from live character movement, race/allegiance power, recalculation sweeps, and "
     "battlefield-control messaging.",
     "mutation", "Derived gameplay state; future faction-power APIs must own recalculation."},
    {JsApiStructOwner::ZoneData, "zone_data", "zone_short_description", "shortDescriptions",
     "getShortDescriptions", "setShortDescriptions", "readonly ExtraDescription[]", true, Deferred,
     Unsupported, "Planned read-only zone short-description list.",
     "Whole zone short-description list writes are unsupported for builder scripts; future "
     "add/update/remove helper APIs must own linked-list allocation, stale-handle protection, "
     "bounded list size, text length, sanitization, persistence, rollback, and audit semantics.",
     "mutation", "Linked list pointer must never be exposed."},
    {JsApiStructOwner::ZoneData, "zone_data", "zone_description", "extraDescriptions",
     "getExtraDescriptions", "setExtraDescriptions", "readonly ExtraDescription[]", true, Deferred,
     Unsupported, "Planned read-only zone extra-description list.",
     "Whole zone extra-description list writes are unsupported for builder scripts; future "
     "add/update/remove helper APIs must own linked-list allocation, stale-handle protection, "
     "bounded list size, text length, sanitization, persistence, rollback, and audit semantics.",
     "mutation", "Linked list pointer must never be exposed."},
    {JsApiStructOwner::ZoneData, "zone_data", "zone_map", "mapDescriptions", "getMapDescriptions",
     "setMapDescriptions", "readonly ExtraDescription[]", true, Deferred, Unsupported,
     "Planned read-only zone map-description list.",
     "Whole zone map-description list writes are unsupported for builder scripts; future "
     "add/update/remove helper APIs must own linked-list allocation, stale-handle protection, "
     "bounded list size, text length, sanitization, persistence, rollback, and audit semantics.",
     "mutation", "Linked list pointer must never be exposed."},
    {JsApiStructOwner::ZoneData, "zone_data", "min_level_look", "minimumLookLevel",
     "getMinimumLookLevel", "setMinimumLookLevel", "number", false, ImplementedReadOnly, Deferred,
     "Returns the minimum level required to inspect zone map/details.",
     "Minimum look level writes are deferred because the inspected legacy zone loader, saver, and "
     "shaper paths do not currently persist or edit this value with the other zone scalar fields.",
     "mutation",
     "Visibility-gating scalar; defer until a persisted builder edit path and visibility semantics "
     "are explicit."},
    {JsApiStructOwner::ZoneData, "zone_data", "owners", "owners", "getOwners", "setOwners", "never",
     true, Internal, Unsupported,
     "Zone owner list is sensitive authorization data and no builder getter is emitted.",
     "Owner-list mutation from JavaScript is unsupported; use server authorization flows instead.",
     "none", "Linked list pointer must never be exposed."},
    {JsApiStructOwner::ZoneData, "zone_data", "reset_mode", "resetMode", "getResetMode",
     "setResetMode", "number", false, ImplementedReadOnly, SetterImplemented,
     "Returns the legacy zone reset mode.",
     "Updates the invocation snapshot legacy zone reset mode after integer and 0 through 3 "
     "inclusive bounds checks, rejects negative values, values above 3, and fractional or other "
     "non-integer values, and applies to live owned memory only when dispatch provides "
     "target-scoped persistent setter authority. Reset mode 0 disables automatic reset aging, "
     "1 resets only when the zone is empty, 2 resets whenever the lifespan expires, and 3 uses "
     "the legacy mixed empty-or-extended-lifespan reset rule.",
     "mutation",
     "Persistent application requires target-scoped dispatch mutation authority context."},
    {JsApiStructOwner::ZoneData, "zone_data", "number", "vnum", "getVnum", "setVnum", "number",
     false, ImplementedReadOnly, Unsupported, "Returns the public zone vnum.",
     "Changing a loaded zone vnum from JavaScript is unsupported.", "none",
     "Already exposed as Zone.vnum."},
    {JsApiStructOwner::ZoneData, "zone_data", "cmdno", "resetCommandCount", "getResetCommandCount",
     "setResetCommandCount", "number", false, Internal, Unsupported,
     "Reset command count is internal and no builder getter is emitted by default.",
     "Reset command count mutation from JavaScript is unsupported because reset scripts must be "
     "edited through explicit reset-command helpers that validate command type, room/object/mob "
     "vnums, if-flag ordering, max counts, and zone persistence as one unit.",
     "none", "Reset loader data."},
    {JsApiStructOwner::ZoneData, "zone_data", "cmd", "resetCommands", "getResetCommands",
     "setResetCommands", "never", true, Internal, Unsupported,
     "Raw reset command table is internal and no builder getter is emitted.",
     "Raw reset command mutation from JavaScript is unsupported because reset scripts must be "
     "edited through explicit reset-command helpers that validate command type, room/object/mob "
     "vnums, if-flag ordering, max counts, and zone persistence as one unit.",
     "none", "Raw command pointer."},
};

constexpr JsApiDeferredHelperPlan DeferredHelperPlans[] = {
    {"helper-guardrail-foundation", 10, "Helper API guardrail foundation",
     "char_data.in_room|obj_data.in_room|room_data.room_flags|room_data.dir_option",
     "Shared helper-method registration, generated documentation rules, live/offline mutation "
     "envelope policy, and raw whole-field setter absence gates before any new helper is callable.",
     "Requires target-scoped authority, live target tokens, stale-handle denial, mutation-count "
     "limits, audit-before-mutation policy, no-partial-write rollback, and sanitized "
     "MutationResult diagnostics.",
     "Offline fixtures must exercise the same helper names, authority failures, raw setter "
     "absence, stale-handle errors, mutation-count limits, and atomic rollback behavior.",
     "Cover generated typings/docs/manifest/fallback/runtime parity, raw setInventory/setContents/"
     "setFlags/setExtraDescriptions absence, wrong-zone authority, stale handles, thrown scripts, "
     "and mixed-batch atomicity.",
     "This foundation prevents helper work from reopening direct struct assignment through docs-only "
     "setter names."},
    {"room-flags", 20, "Room flag helper API",
     "room_data.room_flags|room_data.affected",
     "Named add/remove/replace helpers over a filtered builder-facing room-flag vocabulary.",
     "Requires target-scoped room authority, zone ownership, internal/transient bit exclusion, "
     "and explicit PERMAFFECT/room-affect synchronization policy.",
     "Offline fixtures must use the same generated flag vocabulary and reject raw numeric "
     "bitvectors or unnamed bits before local execution.",
     "Cover allowed flags, BFS_MARK/PERMAFFECT/unnamed-bit rejection, room-affect sync, "
     "authorization failures, stale rooms, and atomic mixed batches.",
     "First priority because flags are common builder needs but raw bit writes would affect "
     "movement, combat, teleport, lighting, drinking, and security behavior."},
    {"room-exits", 30, "Room exit helper API",
     "room_data.dir_option",
     "Directional set/update/remove helpers that own destination, door, keyword, key, width, "
     "flag, and optional bidirectional-link semantics.",
     "Requires target-scoped room/zone authority, destination-zone policy, reset-command impact "
     "checks, and explicit audit for topology changes.",
     "Offline fixtures must emulate canonical direction names, NOWHERE/null destinations, door "
     "flags, and bidirectional-link diagnostics without raw dir_option pointers.",
     "Cover invalid directions, stale destination vnums, private/locked door flags, rollback, "
     "reset-command conflicts, and partial-update atomicity.",
     "Exit writes are high-impact world topology changes and must stay out of generic setters."},
    {"object-value-domains", 40, "Object flag and value-domain helpers",
     "obj_data.obj_flags",
     "Item-type-specific helpers for material, wear flags, extra flags, weight, cost, rent, "
     "timer policy, and value slots such as weapon, container, light, liquid, trap, and lever "
     "domains.",
     "Requires object/zone authority, item-type compatibility, economy/combat/equipment "
     "recalculation policy, and transient/internal bit exclusion.",
     "Offline fixtures must load generated finite vocabularies and item-type schemas instead of "
     "accepting raw value arrays or numeric bitvectors.",
     "Cover malformed value arrays, wrong item-type helpers, economy bounds, combat-affecting "
     "weapon fields, equipment restriction recalculation, and nested object authority.",
     "Keeps raw obj_flags storage absent while enabling safe builder-facing object authoring."},
    {"affects", 50, "Character, room, and object affect helpers",
     "char_data.affected|room_data.affected|obj_data.affected",
     "Add/remove/update helpers for active affects and fixed object affect slots using generated "
     "spell/skill, apply-location, and affect-flag vocabularies.",
     "Requires target authority, duration/tick accounting, stat/flag recomputation, equipment "
     "and room side-effect policy, persistence scope, and audit.",
     "Offline fixtures must emulate affect normalization, generated vocabularies, caps, and "
     "MutationResult diagnostics without exposing linked-list nodes.",
     "Cover unknown apply locations, invalid durations, duplicate affects, recalculation hooks, "
     "room flag synchronization, stale targets, and batch rollback.",
     "Affects touch derived stats and flags, so helpers must own recalculation before mutation."},
    {"inventory-equipment-object-movement", 60,
     "Reward, custody, inventory, equipment, and object movement helpers",
     "char_data.equipment|char_data.carrying|obj_data.in_room|obj_data.carried_by|obj_data.in_obj|obj_data.contains|room_data.contents",
     "Modern helpers such as giveReward, exchangeReceivedObject, findInventoryObject, "
     "findEquippedObject, findRoomObject, cloneObjectFrom, stashObject, moveObjectToRoom, "
     "extractObject, wearObject, removeObject, and container helpers that own reward issuance, "
     "clone-from-handle flows, custody transfer, lookup, list transfer, object liveness, capacity, "
     "weight, light, equipment, and trigger semantics.",
     "Requires actor/target authority, zone ownership, reciprocal list validation, crash-save "
     "policy, prototype-backed source-object liveness, explicit prototype versus live-instance copy "
     "policy for LOAD_OBJ_X-style clones, explicit inventory/equipment/room lookup domains, "
     "bounded equipment-slot validation before equipment array reads, direct-room-content "
     "ownership, rejected unknown vnums, absent and multiple-match result codes, nested-container "
     "cycle prevention, ON_WEAR/receive trigger ordering, and batch preflight plus "
     "audit-before-mutation ordering for multi-reward, lookup-driven, clone, or exchange-table "
     "flows before any input object is consumed.",
     "Offline fixtures must model list membership, shallow snapshots, typed inventory/equipment/"
     "room lookup results, hidden lookup catalogs, hidden accepted custody and clone state, reward "
     "capacity and weight preflight, wear slots, container capacity, and trigger side effects "
     "without recursive mutable handles.",
     "Cover reward handoff, default item return, multi-reward no-partial rollback, stale objects, "
     "LOAD_OBJ_X explicit-source clone rejection for stale or prototype-less objects, "
     "ASSIGN_INV direct versus recursive inventory lookup policy, ASSIGN_EQ slot bounds before "
     "equipment reads, ASSIGN_ROOM direct-content lookup, duplicate list membership, cycles, wrong "
     "owner, absent or multiple lookup matches, nested container exclusions, weight/capacity "
     "limits, wear restriction failures, trigger blocks, and atomic extraction rollback.",
     "Groups the linked object surfaces that would corrupt live lists if exposed as field assignment; "
     "legacy temp object slots should become local TypeScript variables plus named helper results."},
    {"character-movement-relationships", 70, "Character movement and relationship helpers",
     "char_data.in_room|char_data.followers|char_data.master|char_data.mount_data|char_data.group",
     "Move/teleport/follow/unfollow/mount/dismount/group helpers with explicit relationship and "
     "movement-trigger semantics.",
     "Requires character authority, room/zone policy, visibility/security checks, reciprocal "
     "relationship validation, follower caps, loop prevention, and movement propagation rules.",
     "Offline fixtures must emulate role relationships, room membership, mount/follower links, "
     "group lifetime tokens, and blocking trigger results.",
     "Cover death/private/security rooms, trigger-blocked movement, stale relationship handles, "
     "looped follow graphs, mount rider propagation, and group lifetime invalidation.",
     "Raw character relationship writes would bypass movement and social invariants."},
    {"zone-reset-authoring", 80, "Zone reset-command helper API",
     "zone_data.cmd|zone_data.cmdno|zone_data.top",
     "Reset-command add/update/remove/reorder helpers that validate command type, if-flag "
     "ordering, room/object/mobile vnums, max counts, and zone persistence as one unit.",
     "Requires zone authority, workspace/server identity binding, staged-package audit, stale "
     "base checksum checks, and retention-aware rollback.",
     "Offline fixtures must validate reset command schemas and deterministic ordering without "
     "exposing raw command arrays.",
     "Cover command ordering, invalid vnums, max-count bounds, stale base checks, rollback, "
     "persistence failures, and mixed valid/invalid batch atomicity.",
     "Zone reset data is persistent world-building state and should be helper-owned."},
    {"zone-descriptions-and-visibility", 90, "Zone description and visibility helpers",
     "zone_data.zone_short_description|zone_data.zone_description|zone_data.zone_map|zone_data.min_level_look",
     "Add/update/remove helpers for zone description lists plus a persisted minimum-look-level "
     "helper once the legacy save/edit path is confirmed.",
     "Requires zone authority, text bounds/sanitization, linked-list ownership, visibility "
     "policy, persistence support, and audit.",
     "Offline fixtures must emulate list caps, text normalization, null handling, and visibility "
     "diagnostics without exposing linked-list nodes.",
     "Cover overlong text, terminators, duplicate keywords, stale list entries, missing "
     "persistence support, and visibility boundary values.",
     "Keeps linked-list writes narrow while preserving builder authoring needs."},
    {"zone-faction-power", 100, "Zone faction-power helper API",
     "zone_data.white_power|zone_data.dark_power|zone_data.magi_power",
     "Admin-only recalculate/adjust helpers for zone faction power instead of direct scalar "
     "assignment.",
     "Requires admin or explicit zone-control authority, race/allegiance recalculation policy, "
     "battlefield messaging policy, and audit.",
     "Offline fixtures must distinguish read-only faction power from admin-only hypothetical "
     "changes and reject direct setters.",
     "Cover non-admin rejection, recalculation drift, bounded adjustments, messaging side effects, "
     "and stale zone snapshots.",
     "Faction power is derived live state and should not become a builder raw setter."},
    {"profile-admin", 110, "Character profile, progression, and admin helpers",
     "char_data.player|char_data.abilities|char_data.tmpabilities|char_data.constabilities|char_data.points|char_data.profs|char_data.extra_specialization_data|char_data.skills|char_data.knowledge",
     "Separate audited admin/training helpers for identity text, descriptions, race/level, "
     "ability scores, points, professions, specializations, skills, and knowledge recalculation.",
     "Requires account/admin authority, player-vs-NPC policy, level/progression rules, derived "
     "stat recalculation, guild/practice restrictions, player-file persistence, and audit.",
     "Offline fixtures must keep these surfaces read-only for normal builder scripts and expose "
     "only generated admin-helper typings once server authority exists.",
     "Cover auth failures, NPC/player split, invalid skill ids, level bounds, derived stat drift, "
     "player-file persistence failure, and audit-before-mutation ordering.",
     "Lowest initial priority because these are admin/player-data operations rather than zone "
     "script authoring primitives."},
};

constexpr JsApiHelperMutationGateRequirement HelperMutationGateRequirements[] = {
    {"opaque-target-tokens", "Opaque live target tokens",
     "Helper mutation envelopes must identify live targets through server-issued opaque handle ids "
     "or role ids only, bound to invocation id, package id/checksum, handler, host type, target "
     "kind, live identity/generation, authorized zone, and expiry; scripts must not supply "
     "pointers, table indexes, raw vnums, or owner ids as authorization facts.",
     "Offline fixtures may emulate the same opaque ids for deterministic testing, but they must keep "
     "them as fixture-local handles and must not treat raw numbers as proof of authority.",
     "Cover forged ids, raw numeric ids, copied ids from another target type, and ids for handles that "
     "were never present in the trigger context."},
    {"target-scoped-authority", "Target-scoped authority",
     "Each helper must resolve the live target and verify it belongs to the authenticated builder's "
     "authorized zone, host type, or narrower target scope before preparing any mutation.",
     "Offline runs should report the same authority failure shape without blocking unauthenticated "
     "editing or local fixture execution.",
     "Cover wrong-zone room, object-in-wrong-zone, nested contained object, room-owned zone, and "
     "missing builder authority cases."},
    {"stale-handle-denial", "Stale-handle denial",
     "Helper target resolution must reject stale, moved, extracted, invalid, or type-mismatched "
     "handles, including generation/version mismatches, rather than falling back to a live role or "
     "global lookup.",
     "Offline fixtures must be able to mark handles stale or moved so builders can test rejection "
     "paths locally.",
     "Cover extracted objects, moved objects, invalid rooms, stale characters, stale zones, and "
     "explicit stale targets with valid fallback roles available."},
    {"per-helper-mutation-limit", "Per-helper mutation limit",
     "Helper APIs must stay within the runtime mutation-envelope cap and any helper-specific item "
     "count cap before dispatch prepares mutations; malformed calls must fail before enqueue and "
     "before audit.",
     "Offline emulation must enforce the same caps and return deterministic diagnostics for over-large "
     "batches.",
     "Cover excessive helper calls, excessive list arguments, repeated calls in one trigger, and mixed "
     "setter/helper batches."},
    {"sanitized-mutation-result", "Sanitized MutationResult",
     "Script-visible helper results must use the public MutationResult shape with stable codes and "
     "builder-safe messages only; authorization evidence, account ids, live paths, and raw target "
     "metadata stay server-side, and helper-specific codes such as not-authorized and stale-handle "
     "must not leak internal evidence.",
     "Offline MutationResult objects must stay frozen, prototype-free, and shaped like server results "
     "so IntelliSense and tests match production behavior.",
     "Cover thrown helper errors, denied authority, invalid values, unsupported target states, and "
     "attempts to mutate result objects or inspect hidden evidence."},
    {"audit-before-apply", "Audit-before-apply policy",
     "Persistent helper mutations must have a durable audit decision recorded or explicitly staged "
     "after validation and before live state changes are applied; audit failure must leave live "
     "state unchanged.",
     "Offline runs should expose audit requirements as readiness diagnostics, not as a server "
     "dependency for local editing.",
     "Cover audit write failure, missing request id, missing builder identity, redacted audit "
     "diagnostics, and no live-state changes after audit failure."},
    {"atomic-no-partial-write", "Atomic no-partial-write behavior",
     "Dispatch must validate all helper mutations and capture rollback-safe pending changes before "
     "applying any live write; one rejected helper in a batch rejects the whole batch across room, "
     "object, zone, and affect helper families.",
     "Offline emulation must keep fixture state unchanged when any helper in the batch fails.",
     "Cover valid-then-invalid batches, invalid-then-valid batches, rollback after allocation failure, "
     "world-map redraw only after commit, and mixed scalar/helper setter batches."},
};

constexpr const char *RoomFlagHelperAllowedFlags =
    "dark|death|noMob|indoors|noRide|shadowy|noMagic|tunnel|private|godRoom|drinkWater|"
    "drinkPoison|securityRoom|peaceRoom|noTeleport|hideVnum";

constexpr const char *RoomFlagHelperExcludedFlags =
    "BFS_MARK|PERMAFFECT|permanentAffect|unnamed-room-flag-bits";

constexpr const char *RoomFlagHelperBuilderZoneFlags =
    "dark|noMob|indoors|noRide|shadowy|noMagic|tunnel|drinkWater|drinkPoison|peaceRoom|"
    "hideVnum";

constexpr const char *RoomFlagHelperAdminOnlyFlags =
    "death|private|godRoom|securityRoom|noTeleport";

constexpr const char *RoomFlagHelperBlockedFlags =
    "BFS_MARK|PERMAFFECT|permanentAffect|unnamed-room-flag-bits";

constexpr JsApiRoomFlagHelperOperation RoomFlagHelperOperations[] = {
    {"room.flags.add", "Room.addFlag(name: RoomFlagName): MutationResult",
     RoomFlagHelperAllowedFlags, RoomFlagHelperExcludedFlags,
     RoomFlagHelperBuilderZoneFlags, RoomFlagHelperAdminOnlyFlags, RoomFlagHelperBlockedFlags,
     "Requires an opaque room target token resolved to a loaded room in the authenticated "
     "builder's authorized zone. Builder-zone authority may change ordinary presentation, "
     "movement-cost, entry, magic, riding, drinking, peace, and vnum-visibility flags only; "
     "death, private, godRoom, securityRoom, and noTeleport require explicit immortal/admin "
     "override evidence; BFS_MARK, PERMAFFECT/permanentAffect, and unnamed bits are always "
     "blocked before audit.",
     "dark/shadowy affect lighting and look output; noMob/noRide/tunnel affect movement and "
     "entry limits; noMagic affects spell use; drinkWater/drinkPoison affect drinking results; "
     "peaceRoom affects combat start; hideVnum affects builder visibility; death/private/"
     "godRoom/securityRoom/noTeleport are high-impact safety/security/transport flags.",
     "Audit request is recorded after target/flag validation and before any room_data.room_flags "
     "bit is changed; it must include operation, canonical flag name, authority class, builder "
     "account id, eligible immortal character id, target zone, room vnum, package id, and "
     "request id. Admin-only flag changes must also include verified override scope, override "
     "decision evidence, previous flag membership, and intended new flag membership.",
     "Builder diagnostics name only the rejected flag and stable reason category "
     "(unsupported-envelope, unknown-operation, invalid-target, invalid-arguments, "
     "authority-rejected, blocked-flag, admin-only-flag, stale-room, wrong-zone, invalid-token, "
     "audit-rejected, apply-rejected) without exposing token secrets or owner evidence.",
     "Apply records each previous room_data.room_flags value and restores it in reverse order "
     "if any later helper in the same mixed mutation batch fails before scalar setters commit.",
     "Offline fixtures use the same flag vocabulary, token/stale-handle model, sanitized "
     "MutationResult shape, policy classifications, side-effect notes, and no-partial-write "
     "behavior without requiring authentication.",
     "Cover allowed add, duplicate add idempotence, forbidden BFS_MARK/PERMAFFECT, wrong-zone "
     "room, stale room token, admin-only denial without override, audit rejection, rollback "
     "after mixed failure, and absence of raw Room.setFlags."},
    {"room.flags.remove", "Room.removeFlag(name: RoomFlagName): MutationResult",
     RoomFlagHelperAllowedFlags, RoomFlagHelperExcludedFlags,
     RoomFlagHelperBuilderZoneFlags, RoomFlagHelperAdminOnlyFlags, RoomFlagHelperBlockedFlags,
     "Requires an opaque room target token resolved to a loaded room in the authenticated "
     "builder's authorized zone. Builder-zone authority may change ordinary presentation, "
     "movement-cost, entry, magic, riding, drinking, peace, and vnum-visibility flags only; "
     "death, private, godRoom, securityRoom, and noTeleport require explicit immortal/admin "
     "override evidence; BFS_MARK, PERMAFFECT/permanentAffect, and unnamed bits are always "
     "blocked before audit.",
     "dark/shadowy affect lighting and look output; noMob/noRide/tunnel affect movement and "
     "entry limits; noMagic affects spell use; drinkWater/drinkPoison affect drinking results; "
     "peaceRoom affects combat start; hideVnum affects builder visibility; death/private/"
     "godRoom/securityRoom/noTeleport are high-impact safety/security/transport flags.",
     "Audit request is recorded after target/flag validation and before any room_data.room_flags "
     "bit is changed; it must include operation, canonical flag name, authority class, builder "
     "account id, eligible immortal character id, target zone, room vnum, package id, and "
     "request id. Admin-only flag changes must also include verified override scope, override "
     "decision evidence, previous flag membership, and intended new flag membership.",
     "Builder diagnostics name only the rejected flag and stable reason category "
     "(unsupported-envelope, unknown-operation, invalid-target, invalid-arguments, "
     "authority-rejected, blocked-flag, admin-only-flag, stale-room, wrong-zone, invalid-token, "
     "audit-rejected, apply-rejected) without exposing token secrets or owner evidence.",
     "Apply records each previous room_data.room_flags value and restores it in reverse order "
     "if any later helper in the same mixed mutation batch fails before scalar setters commit.",
     "Offline fixtures use the same flag vocabulary, token/stale-handle model, sanitized "
     "MutationResult shape, policy classifications, side-effect notes, and no-partial-write "
     "behavior without requiring authentication.",
     "Cover allowed remove, missing remove idempotence, forbidden BFS_MARK/PERMAFFECT, wrong-zone "
     "room, stale room token, admin-only denial without override, audit rejection, rollback "
     "after mixed failure, and absence of raw Room.setFlags."},
};

static_assert(BFS_MARK != 0, "BFS_MARK must stay an explicit room flag helper exclusion.");
static_assert(PERMAFFECT != 0, "PERMAFFECT must stay an explicit room flag helper exclusion.");

constexpr JsApiRoomExitHelperOperation RoomExitHelperOperations[] = {
    {"room.exit.state",
     "RotS.Script.setExitState(room: Room, direction: DirectionName, state: ExitStateName): "
     "MutationResult",
     "SET_EXIT_STATE",
     "Preflight policy for changing one loaded room exit's door state among open, closed, and "
     "locked without exposing raw dir_option pointers or numeric exit bitvectors.",
     "Requires target-scoped room/zone authority, canonical direction validation, an existing exit "
     "with a destination room, door-only validation matching EX_ISDOOR, state validation limited "
     "to open/closed/locked, reciprocal reverse-exit policy, reset-command impact review, and "
     "audit before mutation. JavaScript V1 deliberately fixes the legacy truthy-direction bug so "
     "north/direction 0 is valid, deliberately accepts open/state 0, bounds direction before any "
     "dir_option or rev_dir access, and rejects invalid states instead of preserving legacy "
     "default-to-open behavior.",
     "Legacy set_exit_state preserves all exit_info bits except EX_CLOSED and EX_LOCKED, maps "
     "state 0 to open, 1 to closed, 2 to closed+locked, clears EX_ISBROKEN while emitting a blur "
     "message, emits close/lock/unlock/open room messages through the first room occupant, and "
     "SCRIPT_SET_EXIT_STATE mirrors only a reciprocal reverse exit whose to_room points back to "
     "the source room.",
     "Audit before door mutation with operation, source room, direction, previous state bits, "
     "requested state, reverse-exit decision, reset-command evidence, builder account id, eligible "
     "immortal character id, package id, and request id.",
     "Stable categories include invalid-target, not-authorized, invalid-direction, missing-exit, "
     "nowhere-exit, not-door, invalid-state, stale-room, stale-destination, reset-conflict, "
     "audit-rejected, and apply-rejected.",
     "Rollback restores source and mirrored reverse exit_info values plus any EX_ISBROKEN bit "
     "cleared by apply, and discards queued room messages if a later helper fails before descriptor "
     "output commits.",
     "Offline fixtures must model canonical directions, open/closed/locked state names, existing "
     "exit records, EX_ISDOOR/EX_ISBROKEN, reciprocal reverse-exit mirroring, room-message "
     "reachability, reset-conflict diagnostics, accepted hidden exit state, and frozen snapshots.",
     "Cover valid open/close/lock, non-door rejection, NOWHERE and missing exits, broken-door clear "
     "with rollback, reciprocal reverse mirroring, non-reciprocal reverse non-mirroring, reset "
     "conflict, stale rooms, audit rejection, and absence of raw Room.setExit."},
    {"room.exit.destination",
     "RotS.Script.changeExitTo(room: Room, direction: DirectionName, destination: Room): "
     "MutationResult",
     "CHANGE_EXIT_TO",
     "Preflight policy for changing one loaded room exit destination to another loaded room handle "
     "without accepting raw room vnums or exposing direct room_data.dir_option writes.",
     "Requires source-room authority, destination-zone authority, canonical direction validation, "
     "existing source exit validation before any dir_option access, non-NOWHERE destination "
     "liveness, bidirectional-link policy, reset-command impact review, topology/audit authority, "
     "and no raw integer room lookup from builder code. JavaScript V1 deliberately rejects invalid "
     "directions and missing source exits instead of preserving legacy chained-comparison and "
     "unchecked dir_option dereference hazards.",
     "Legacy SCRIPT_CHANGE_EXIT_TO resolves the destination vnum with real_room() and assigns only "
     "source.dir_option[direction].to_room; it does not create missing exits, does not update a "
     "reverse exit, does not validate reset command consistency, and does not emit messages. "
     "JavaScript V1 should explicitly choose whether to preserve one-way behavior or require an "
     "optional reciprocal-link helper instead of silently rewriting both rooms.",
     "Audit before topology mutation with operation, source room, direction, previous destination, "
     "new destination, bidirectional-link decision, reset-command evidence, builder account id, "
     "eligible immortal character id, package id, and request id.",
     "Stable categories include invalid-target, not-authorized, invalid-direction, missing-exit, "
     "invalid-destination, stale-room, stale-destination, wrong-zone, reset-conflict, "
     "bidirectional-conflict, audit-rejected, and apply-rejected.",
     "Rollback restores the previous destination and any explicitly chosen reciprocal-link edits if "
     "a later helper fails before descriptor output commits.",
     "Offline fixtures must model source exits, destination rooms, NOWHERE/null destinations, "
     "one-way versus reciprocal-link decisions, reset-conflict diagnostics, accepted hidden exit "
     "destination state, and frozen read-only Room.exits snapshots.",
     "Cover valid one-way destination change, missing source exit, stale destination, wrong-zone "
     "destination, reciprocal-link conflict, reset-command conflict, rollback, and absence of raw "
     "Room.setExit."},
};

constexpr JsApiCharacterMovementHelperOperation CharacterMovementHelperOperations[] = {
    {"character.load_mob", "RotS.Script.loadMob(vnum: number, room: Room): MutationResult",
     "LOAD_MOB",
     "Preflight policy for creating a new mobile from a server-owned prototype vnum and placing it "
     "only into a loaded authorized room handle. Unlike legacy LOAD_MOB, the JavaScript helper must "
     "not expose a mutable temporary character variable or return a live character handle until "
     "local live-character handles have a separate lifetime-token design.",
     "Requires target-scoped zone authority for the room, mobile prototype visibility through a "
     "server-owned catalog or an explicit admin override, per-script load limits, and a fresh "
     "dispatch authority token.",
     "Allocates one NPC, inserts it into the global character list and target room people list, "
     "initializes mobile runtime state, Mudlle/mobile-procedure call stacks, fast-mob affects, and "
     "mob-index live counts, and may affect tracking, followers, and combat targeting once "
     "callable; any zone reset-line accounting policy must be explicit rather than inherited from "
     "legacy script LOAD_MOB.",
     "Audit before allocation with operation, mobile vnum, target room vnum, target zone, builder "
     "account id, eligible immortal character id, package id, request id, authority class, and "
     "prototype decision evidence.",
     "Stable categories include unsupported-envelope, unknown-operation, invalid-target, "
     "invalid-arguments, not-found, authority-rejected, stale-room, wrong-zone, mutation-limit, "
     "audit-rejected, and apply-rejected without exposing token secrets, player emails, or raw room "
     "indexes.",
     "Rollback must either link the new mobile completely or extract the allocated mobile if any "
     "later helper in the same batch fails before descriptor output commits.",
     "Offline fixtures must model a hidden mobilePrototypes catalog, deterministic temporary mobile "
     "ids, room character membership, per-run load limits, not-found branches, fixture-only "
     "prototype visibility, and no visible ctx snapshot mutation.",
     "Cover missing prototype, wrong-zone room, stale room, load limit, audit rejection, later batch "
     "rollback, deterministic generated ids, no returned mutable handle, and absence of raw "
     "character list writes."},
    {"character.teleport", "RotS.Script.teleportChar(character: Character, room: Room): MutationResult",
     "TELEPORT_CHAR",
     "Moves a live character handle to a loaded room handle and carries eligible NPC followers or "
     "mount/rider relationships according to the selected helper variant. Raw room vnums and copied "
     "ids are not accepted.",
     "Requires authority over the moved character and destination zone, live room/character target "
     "tokens, death/private/security/noTeleport policy checks, and explicit override evidence for "
     "admin-only destination bypasses.",
     "Updates room people lists, stops or preserves combat according to the movement policy, moves "
     "eligible followers/mounts for follower-preserving variants, emits no legacy movement text by "
     "default, and can affect room entry triggers, visibility, tracking, and wait state.",
     "Audit before movement with operation, moved character id/vnum/name class, source room, "
     "destination room, target zone, follower mode, builder account id, eligible immortal character "
     "id, package id, request id, and override evidence.",
     "Stable categories include unsupported-envelope, unknown-operation, invalid-target, "
     "invalid-arguments, not-authorized, blocked-room, no-teleport, stale-character, stale-room, "
     "trigger-blocked, audit-rejected, and apply-rejected without exposing raw descriptor or account "
     "data.",
     "Rollback must record each moved character's previous room and relationship state, then restore "
     "in reverse order if a later helper fails before output commits.",
     "Offline fixtures must model room membership, follower/mount links, combat state, blocked room "
     "flags, trigger-blocked outcomes, accepted hidden movement state, and frozen visible snapshots.",
     "Cover teleport without followers, teleport with NPC followers, teleport-to-target-room, "
     "private/security/death/noTeleport rooms, stale handles, trigger block, combat cleanup, and "
     "mixed-batch rollback."},
    {"character.teleport_only",
     "RotS.Script.teleportCharOnly(character: Character, room: Room): MutationResult",
     "TELEPORT_CHAR_X",
     "Moves only the selected live character handle to a loaded room handle; followers, group "
     "members, and mount riders are not implicitly moved.",
     "Requires the same character and destination-zone authority as teleportChar, but additionally "
     "records that follower propagation is deliberately disabled.",
     "Updates only the selected character's room membership and combat/movement bookkeeping while "
     "leaving follower/master lists intact unless live safety policy requires breaking them.",
     "Audit before movement with operation, source room, destination room, target zone, builder "
     "account id, eligible immortal character id, package id, request id, and follower propagation "
     "set to none.",
     "Stable categories include invalid-target, not-authorized, blocked-room, no-teleport, "
     "stale-character, stale-room, trigger-blocked, audit-rejected, and apply-rejected.",
     "Rollback restores the moved character to its previous room before any output commits.",
     "Offline fixtures must preserve follower room membership when only the leader moves and expose "
     "that accepted hidden movement state to later character helpers in the same run.",
     "Cover leader-only movement, follower non-movement, stale leader, wrong-zone destination, and "
     "rollback after a later helper failure."},
    {"character.teleport_to_target_room",
     "RotS.Script.teleportCharToTargetRoom(character: Character, target: Character): "
     "MutationResult",
     "TELEPORT_CHAR_XL",
     "Moves a live character handle to the current room of another live character handle after "
     "resolving the target's room at preflight/apply time. Raw target room ids are not accepted.",
     "Requires authority over the moved character plus destination authority derived from the target "
     "character's loaded room and zone at the time of validation.",
     "Uses the same movement, combat, room-entry, visibility, and no-teleport policy as teleportChar "
     "after destination resolution.",
     "Audit records the source character, target character, resolved destination room/zone, package "
     "id, request id, builder account id, and eligible immortal evidence before movement.",
     "Stable categories include invalid-target, target-not-in-room, not-authorized, blocked-room, "
     "no-teleport, stale-character, stale-room, trigger-blocked, audit-rejected, and apply-rejected.",
     "Rollback restores all moved characters to their prior rooms if target resolution or a later "
     "helper fails before output commits.",
     "Offline fixtures must model target-room resolution, stale target rooms, target movement between "
     "helper calls through hidden state, and frozen script-visible snapshots.",
     "Cover valid target-room movement, target in NOWHERE, stale target, target moved by an earlier "
     "accepted helper, wrong-zone target room, and rollback."},
    {"character.extract", "RotS.Script.extractChar(character: Character): MutationResult",
     "EXTRACT_CHAR",
     "Preflight policy for extracting only an authorized NPC/helper character handle. Player "
     "characters and account-backed characters are not valid targets for builder scripts, matching "
     "legacy EXTRACT_CHAR's NPC-only branch while avoiding descriptor/account lifecycle paths.",
     "Requires target authority, explicit NPC-only validation, source-room authority with a "
     "current room, no protected special-procedure state, no player descriptor/account ownership, "
     "and fresh liveness validation immediately before apply.",
     "Removes the character from room, combat, follower/master, mount, and descriptor-adjacent "
     "runtime structures using the same stale-handle policy as live extraction; carried and worn "
     "objects become room contents or are rejected before promotion if that cannot be rolled back.",
     "Audit before extraction with operation, target character class, mobile vnum when available, "
     "source room, target zone, builder account id, eligible immortal character id, package id, and "
     "request id.",
     "Stable categories include invalid-target, protected-character, not-authorized, "
     "stale-character, in-combat-blocked, mixed-batch-rejected, audit-rejected, and "
     "apply-rejected without exposing raw player data or descriptor state.",
     "Rollback is not assumed because extraction destroys a live character handle, moves carried "
     "and worn objects to room contents only when the target has a current room, conditionally "
     "decrements and clamps mob-index/load-line counts, and touches combat/wait/follower/riding "
     "state; V1 promotion must reject no-current-room targets and mixed batches containing "
     "extractChar unless a later staged-extraction design proves complete compensation before "
     "descriptor output commits.",
     "Offline fixtures must mark extracted NPC handles stale for later helper calls, remove them "
     "from hidden room membership, place carried and worn objects into hidden room contents, "
     "preserve visible snapshots, reject player extraction, and keep failed extraction from "
     "mutating hidden state.",
     "Cover NPC success, player/account-backed rejection, stale handle, combat-protected target, "
     "follower/master cleanup, carried/equipped object room placement, later stale-handle branch, "
     "and no partial extraction in mixed helper batches."},
    {"character.follow", "RotS.Script.doFollow(follower: Character, leader: Character): MutationResult",
     "DO_FOLLOW",
     "Preflight policy for making one live character follow another live character through the "
     "existing reciprocal follow relationship model; raw follower-list replacement remains "
     "unavailable, and JavaScript must not inherit legacy DO_FOLLOW's surprising loop branch that "
     "stops the would-be leader's own master before adding the new follower.",
     "Requires authority over the follower, live target tokens for both characters, explicit "
     "self-follow, already-following, and loop rejection without mutating either relationship, "
     "follower-cap policy, charm/tame/recruit/pet ownership policy when NPCs are involved, "
     "previous-master replacement policy, and same-room/message policy before audit.",
     "Updates follower.master and leader.followers reciprocally, may break the follower's previous "
     "follow link only through explicit stop_follower-compatible semantics, may clear "
     "hunting memory for the previous master, charm/tame/recruit/pet state, and pet/group "
     "membership relationships, emits follow/stop-follow messages unless V1 deliberately "
     "suppresses or defers them, and can affect mount, movement propagation, combat XP sharing, "
     "and room movement.",
     "Audit before relationship mutation with operation, follower identity class, leader identity "
     "class, previous master, leader current master, follower count, protected-state source, "
     "builder account id, eligible immortal character id, package id, request id, and authority "
     "evidence.",
     "Stable categories include invalid-target, not-authorized, already-following, self-follow, "
     "follow-loop, "
     "follower-cap, protected-follower, stale-character, audit-rejected, and apply-rejected.",
     "Rollback restores the previous master/follower-list links in reverse order before output "
     "commits if a later helper fails, including follower-side stop_follower cleanup, hunting "
     "memory policy, pet/group membership restoration, and any message suppression/deferment state "
     "that V1 chooses to allow; loop rejection must leave the would-be leader's master untouched "
     "instead of reproducing legacy SCRIPT_DO_FOLLOW cleanup.",
     "Offline fixtures must model master/follower reciprocal links, loop rejection, follower caps, "
     "previous-master replacement, protected charm/tame/recruit/pet branches, already-following "
     "branches, accepted hidden relationship state, and frozen visible snapshots.",
     "Cover valid follow, replacing the follower's existing master, self-follow, looped graph "
     "rejection without altering the leader's master, stale leader/follower, cap rejection, "
     "protected follower rejection, hunting-memory and pet/group cleanup/restoration, movement "
     "propagation expectations, message/audit ordering, and rollback."},
    {"character.flee", "RotS.Script.doFlee(character: Character): MutationResult", "DO_FLEE",
     "Preflight policy for a bounded flee action on one live character handle without exposing "
     "generic command execution or arbitrary builder-selected directions.",
     "Requires authority over the character, live source-room state, flee-eligible position/tactics, "
     "at least one eligible non-death exit, EX_NOFLEE/EX_NOWALK filtering, MOB_STAY_ZONE and "
     "MOB_STAY_TYPE rules for NPCs, and per-script action-loop limits.",
     "Legacy do_flee tries up to six random directions, calls check_simple_move before do_move with "
     "SCMD_FLEE, may stand the character, may stop riding on mount failures, removes AFF_HUNT on "
     "success, stops combat for the fleeing character and room opponents, charges player flee XP, "
     "moves through do_move side effects, and can propagate AFF_HAZE direction redirection, "
     "followers/mounts, duplicate command/pre-enter checks, entry triggers, movement cost, "
     "stay-zone drops, death-room kills, and descriptor output.",
     "Audit before movement with operation, source room, deterministic selected exit, any explicit "
     "AFF_HAZE redirection policy, destination room, combat state, tactics/position, "
     "follower/mount policy, builder account id, eligible immortal character id, package id, and "
     "request id.",
     "Stable categories include invalid-target, not-authorized, not-in-room, not-eligible, no-exit, "
     "no-flee, blocked-exit, too-exhausted, stale-character, stale-room, trigger-blocked, "
     "audit-rejected, and apply-rejected. V1 must return branchable MutationResult codes instead "
     "of calling the generic flee command path from builder scripts.",
     "Rollback restores room membership, movement points, riding/follower propagation, combat "
     "bookkeeping, AFF_HUNT, stay-zone drops, XP loss, and descriptor output if a later helper in "
     "the same batch fails before descriptor output commits.",
     "Offline fixtures must model an ordered eligible-exit catalog, EX_NOFLEE/EX_NOWALK/death-room "
     "filtering, MOB_STAY_ZONE/MOB_STAY_TYPE, position/tactics eligibility, deterministic exit "
     "selection, AFF_HAZE policy, duplicate command/pre-enter check policy, follower/mount "
     "behavior, combat cleanup, accepted hidden movement state, and frozen snapshots.",
     "Cover berserk and non-standing eligibility, no-exit rooms, all exits blocked by no-flee/no-walk "
     "or death rooms, NPC stay-zone/stay-type rejection, stale handles, deterministic exit choice "
     "replacing legacy randomness, haze redirection suppression or modeling, duplicate "
     "command/pre-enter check collapse or preservation, special/trigger blocks, combat cleanup, "
     "follower/mount behavior, death-room avoidance, and rollback."},
};

constexpr JsApiCombatEffectHelperOperation CombatEffectHelperOperations[] = {
    {"combat.attack", "RotS.Script.doHit(attacker: Character, victim: Character): MutationResult",
     "DO_HIT",
     "Preflight policy for starting or redirecting one live attacker toward one same-room live "
     "victim without exposing generic command execution or text target lookup.",
     "Requires authority over the attacker, same-room victim liveness, visibility or explicit "
     "script-target allowance, peace-room denial, self-hit rejection, charm/master protection, "
     "big-brother target validation, sanctuary checks, combat recursion limits, and action-loop "
     "guards before audit.",
     "Legacy do_hit may delegate mental attackers to do_mental, start combat with hit(), emit "
     "attack messages, set WAIT_STATE, or redirect an existing fight target. Promotion must define "
     "whether builder scripts can start combat, retarget combat, or only request an attack intent.",
     "Audit before combat mutation with operation, attacker, victim, source room, existing fight "
     "targets, peace-room/charm/sanctuary decisions, builder account id, eligible immortal "
     "character id, package id, and request id.",
     "Stable categories include invalid-target, not-authorized, not-in-room, self-target, "
     "peace-room, protected-target, cannot-see, already-fighting, sanctuary-blocked, "
     "audit-rejected, and apply-rejected.",
     "Rollback restores attacker/victim fighting pointers, wait state, emitted descriptor output, "
     "and any hit-side effects if a later helper in the same batch fails before descriptor output "
     "commits; V1 may instead reject mixed batches containing accepted attacks.",
     "Offline fixtures must model same-room checks, visibility/protected target branches, existing "
     "combat state, accepted hidden combat state, wait-state intent, and frozen visible snapshots.",
     "Cover valid attack, self-target, peace-room, charm/master protection, sanctuary block, "
     "already-fighting retarget policy, stale handles, combat recursion guards, and rollback or "
     "mixed-batch rejection."},
    {"combat.damage",
     "RotS.Script.applyDamage(victim: Character, amount: number, options?: DamageOptions): "
     "MutationResult",
     "SET_INT_VALUE ch.hit|ON_DAMAGE",
     "Preflight policy for bounded, audited damage or healing deltas through explicit helpers "
     "instead of direct Character.points/current hit mutation; this deliberately replaces unsafe "
     "legacy SET_INT_VALUE ch.hit writes with a trigger-aware damage/healing path.",
     "Requires target authority, integer bounded amount, target liveness, death-threshold policy, "
     "blocking ON_DAMAGE trigger ordering, weapon/source attribution rules, no recursive "
     "damage-trigger loops, and package-level action limits.",
     "Damage may update hit points, invoke victim and weapon ON_DAMAGE blockers before apply, enter "
     "or alter combat state, update damage-details accounting, trigger death flow, emit descriptor "
     "output, and interact with affects, sanctuary, armor, and proc handlers. Healing must define "
     "max-hit caps, undead/negative branches, output, and audit without bypassing combat state.",
     "Audit before hit-point mutation with operation, target, amount, source kind/id, attacker when "
     "present, weapon when present, trigger-block result, previous/current hit points, builder "
     "account id, eligible immortal character id, package id, and request id.",
     "Stable categories include invalid-target, not-authorized, invalid-amount, blocked-by-trigger, "
     "recursive-damage, would-kill, already-dead, audit-rejected, and apply-rejected.",
     "Rollback restores hit points, damage-details accounting, combat/death bookkeeping, affected "
     "descriptor output, and any queued death side effects if a later helper fails before output "
     "commits; V1 kill-capable damage may be isolated from mixed batches.",
     "Offline fixtures must model hit/max-hit values, blocking trigger outcomes, attacker/weapon "
     "metadata, death threshold policy, accepted hidden hit-point state, and frozen snapshots.",
     "Cover bounded damage, bounded healing, trigger-blocked damage, recursive damage rejection, "
     "would-kill policy, stale victim/attacker/weapon, max-hit caps, and rollback."},
    {"combat.kill", "RotS.Script.rawKill(character: Character): MutationResult", "RAW_KILL",
     "Preflight policy for destructive death processing on one live character handle; raw silent "
     "kill remains unavailable to normal builder scripts.",
     "Requires admin-grade or explicitly delegated zone authority, target liveness, NPC/player "
     "policy, SPECIAL_DEATH terminal-hook policy, killer/source attribution policy, no pending mixed "
     "mutations, and crash-save/account safety review before audit.",
     "Legacy raw_kill completes and aborts delays, stops fighting, runs SPECIAL_DEATH, stops riding, "
     "removes bash and all affects, sends death cries, creates corpses, records big-brother/death "
     "state, saves/crash-saves players, restores player abilities, moves corpses/characters, and "
     "can recurse through normal death processing.",
     "Audit before death processing with operation, target identity class, killer/source when "
     "present, room, corpse policy, player/account policy, builder account id, eligible immortal "
     "character id, package id, and request id.",
     "Stable categories include invalid-target, not-authorized, protected-character, "
     "special-death-blocked, player-target-blocked, already-dead, audit-rejected, and "
     "apply-rejected.",
     "Rollback is not assumed for rawKill because death processing creates corpses, saves player "
     "state, clears affects, and mutates account-adjacent lifecycle state; V1 promotion must reject "
     "mixed batches or prove full compensation before descriptor output commits.",
     "Offline fixtures must model protected/player target branches, SPECIAL_DEATH block outcomes, "
     "stale handles after accepted death, corpse/output diagnostics, and frozen visible snapshots.",
     "Cover NPC kill, player rejection or admin-only path, SPECIAL_DEATH block, stale target, "
     "delay/fighting/riding cleanup policy, corpse side effects, and mixed-batch rejection."},
    {"combat.experience",
     "RotS.Script.gainExperience(character: Character, amount: number): MutationResult",
     "GAIN_EXP",
     "Preflight policy for explicit player progression changes; raw experience mutation and "
     "generic stat writes remain unavailable.",
     "Requires player-only target validation matching legacy SCRIPT_GAIN_EXP, account/admin or "
     "reward-source authority, bounded signed amount, high-level scaling policy, level-up/down "
     "side-effect policy, persistence/crash-save policy, and audit before mutation.",
     "Legacy SCRIPT_GAIN_EXP ignores NPCs, scales positive or negative amounts for characters above "
     "level 30, then calls gain_exp(), which can alter level, practices, points, messages, and "
     "persistence-related progression state.",
     "Audit before progression mutation with operation, target player, raw amount, scaled amount, "
     "level before/after policy, reward/source reason, builder account id, eligible immortal "
     "character id, package id, and request id.",
     "Stable categories include invalid-target, npc-target, not-authorized, invalid-amount, "
     "level-boundary-blocked, audit-rejected, and apply-rejected.",
     "Rollback restores experience, level-derived state, practice/progression counters, descriptor "
     "output, and persistence marks if a later helper fails; V1 may require experience helpers to "
     "be terminal or isolated from mixed batches.",
     "Offline fixtures must model player/NPC distinction, bounded signed amounts, level-scaling "
     "branches, accepted hidden experience state, level-boundary diagnostics, and frozen snapshots.",
     "Cover player gain/loss, NPC no-op rejection, high-level scaling, level-up/down policy, stale "
     "target, audit rejection, and rollback or terminal-helper isolation."},
};

constexpr JsApiEquipmentHelperOperation EquipmentHelperOperations[] = {
    {"equipment.wear",
     "RotS.Script.doWear(character: Character, object: GameObject, slot?: WearSlotName): "
     "MutationResult",
     "DO_WEAR",
     "Preflight policy for wearing one direct carried object on one live character without exposing "
     "generic command text, all-dot wear loops, or direct equipment slot assignment.",
     "Requires authority over the character and direct carried object, live reciprocal "
     "object.carried_by membership before slot resolution, optional canonical wear-slot "
     "validation, item wear-flag compatibility, Beorning/Olog-hai body restrictions, current-room "
     "validation before equip_char zap checks, anti-alignment and item race-flag "
     "zap-to-inventory restrictions, two-handed/shield conflict policy, belt prerequisite policy, "
     "and available destination slot state before audit.",
     "Legacy perform_wear checks ON_WEAR before final alternate finger/neck/wrist/belt fallback, "
     "may perform_remove an occupied slot into inventory, transfers the object from carrying to "
     "equipment, emits wear messages, calls equip_char, updates light counters, carried/worn/"
     "encumbrance weights, dodge/offense/parry, object affects, affect totals, crash-save state, "
     "two-handed and shield interactions, poison damage/death side effects, and descriptor output.",
     "Audit before equipment mutation with operation, character, object, requested slot, resolved "
     "slot, resolved alternate slot, replaced object if any, ON_WEAR requested-slot result, "
     "restriction decisions, builder account id, eligible immortal character id, package id, and "
     "request id.",
     "Stable categories include invalid-target, not-authorized, not-carried, invalid-slot, "
     "cannot-wear-slot, restricted-body, restricted-item, no-current-room, missing-belt, "
     "two-handed-conflict, slot-occupied, inventory-full, blocked-by-trigger, stale-character, "
     "stale-object, audit-rejected, and apply-rejected.",
     "Rollback restores inventory membership, equipment slots, any forced removed item, light "
     "counters, carried/worn/encumbrance weights, combat stats, object affects, poison/death "
     "side effects, crash-save flags, and descriptor output if a later helper fails before output "
     "commits.",
     "Offline fixtures must model canonical wear slots, item wear flags, direct carried ownership, "
     "race/body and item anti-alignment/race-flag zap-to-inventory restrictions, current-room "
     "availability, belt occupancy, two-handed/shield state, ON_WEAR block outcomes, accepted "
     "hidden equipment state, forced remove behavior, light and affect recalculation summaries, "
     "poison/death diagnostics, and frozen visible snapshots.",
     "Cover valid wear, explicit slot validation, alternate slot fallback, trigger-blocked wear, "
     "wrong owner, stale handles, no-current-room zap rejection, race/body rejection, missing belt, "
     "shield/two-handed conflict, forced remove rollback, poison death isolation, and mixed-batch "
     "no-partial behavior."},
    {"equipment.remove",
     "RotS.Script.doRemove(character: Character, slotOrObject: WearSlotName | GameObject): "
     "MutationResult",
     "DO_REMOVE",
     "Preflight policy for removing one equipped object from one live character without exposing "
     "generic remove text, all-dot loops, or raw equipment array writes.",
     "Requires authority over the character and equipped object, live reciprocal equipment-slot "
     "membership, canonical bounded slot or equipped-object resolution before any equipment array "
     "read, carry-count capacity for the removed item, belt cascade policy, room placement "
     "authority when belt items must fall, and audit before mutation.",
     "Legacy perform_remove moves the selected equipment item into inventory, emits remove "
     "messages, may cascade WEAR_BELT_1/2/3 removal when the waist belt is removed, may drop "
     "belt-carried items into the room when inventory is full, decrements light counters, updates "
     "carried/worn/encumbrance weights, dodge/offense/parry, object affects, affect totals, "
     "poison damage/death side effects, crash-save state, room contents, and descriptor output.",
     "Audit before equipment removal with operation, character, requested slot or object, resolved "
     "slot, cascaded belt slots, capacity/drop decisions, builder account id, eligible immortal "
     "character id, package id, and request id.",
     "Stable categories include invalid-target, not-authorized, not-equipped, invalid-slot, "
     "inventory-full, belt-cascade-drop, invalid-legacy-slot, stale-character, stale-object, "
     "stale-room, audit-rejected, and apply-rejected.",
     "Rollback restores equipment slots, inventory membership, room drops, light counters, "
     "carried/worn/encumbrance weights, combat stats, object affects, poison/death side effects, "
     "crash-save flags, and descriptor output if a later helper fails before output commits.",
     "Offline fixtures must model equipped-slot membership, inventory capacity, waist-belt cascade "
     "state, room-drop branches, accepted hidden equipment/inventory/room state, light and affect "
     "recalculation summaries, poison/death diagnostics, and frozen visible snapshots.",
     "Cover valid remove, object and slot target resolution, missing equipment, full inventory, "
     "waist belt cascade to inventory and room, stale handles, light decrement, poison death "
     "isolation, and rollback."},
    {"equipment.equip_character",
     "RotS.Script.equipChar(character: Character, prototypes: readonly number[]): MutationResult",
     "EQUIP_CHAR",
     "Preflight policy for loading a bounded set of object prototypes to a live character and then "
     "requesting wear-all behavior without exposing mutable temporary object slots.",
     "Requires authority over the character, object prototype catalog authority, bounded prototype "
     "list size, deterministic load identity policy, direct inventory placement, wear-all ordering, "
     "pet/tamed-mobile restrictions from do_wear(\"all\"), current-room validation before "
     "equip_char zap checks, item anti-alignment/race-flag zap-to-inventory restrictions, capacity "
     "and weight preflight, ON_WEAR block policy for each loaded object, and audit before any "
     "prototype is consumed.",
     "Legacy SCRIPT_EQUIP_CHAR loads up to five object vnums into the character inventory, then "
     "calls do_wear(\"all\"), which walks the current carrying list and can partially wear, leave in "
     "inventory, or trigger every wear/equip/remove/light/affect/poison side effect from the normal "
     "wear path. JavaScript V1 deliberately tightens this into no-partial preflight/rollback "
     "semantics instead of preserving legacy partial wear-all behavior.",
     "Audit before prototype load with operation, character, prototype vnums, generated object ids, "
     "wear order, per-object result codes, builder account id, eligible immortal character id, "
     "package id, and request id.",
     "Stable categories include invalid-target, not-authorized, invalid-prototype, not-found, "
     "too-many-prototypes, inventory-full, too-heavy, blocked-by-trigger, partial-wear-blocked, "
     "pet-restricted, no-current-room, stale-character, audit-rejected, and apply-rejected.",
     "Rollback extracts or restores every loaded object, restores inventory/equipment slots, light "
     "counters, carried/worn/encumbrance weights, combat stats, object affects, poison/death side "
     "effects, crash-save flags, and descriptor output if any item fails or a later helper fails; "
     "this no-partial behavior is an intentional JavaScript V1 improvement over legacy partial "
     "wear-all.",
     "Offline fixtures must model objectPrototypes, deterministic generated object ids, capacity "
     "and weight preflight, wear-all source order, anti-alignment/race-flag zap-to-inventory "
     "branches, accepted hidden inventory/equipment state, per-object ON_WEAR outcomes, and frozen "
     "visible snapshots.",
     "Cover successful multi-item equip, missing prototype, sixth prototype rejection, capacity/"
     "weight preflight, trigger-blocked item without consuming earlier loads, partial-wear "
     "rollback, deterministic ordering, stale character, and no returned mutable object handles."},
};

constexpr JsApiRawSetterGuardrail RawSetterGuardrails[] = {
    {JsApiStructOwner::CharData, "in_room", "setRoom", "character-movement-relationships",
     "Character movement must use movement/teleport helpers, not raw room assignment."},
    {JsApiStructOwner::CharData, "affected", "setAffects", "affects",
     "Character affects must use add/remove helpers with recomputation."},
    {JsApiStructOwner::CharData, "equipment", "setEquipmentSlot",
     "inventory-equipment-object-movement",
     "Equipment changes must use wear/remove helpers with trigger parity."},
    {JsApiStructOwner::CharData, "carrying", "setInventory",
     "inventory-equipment-object-movement",
     "Inventory list replacement would bypass ownership, weight, containers, and crash-save."},
    {JsApiStructOwner::CharData, "followers", "setFollowers",
     "character-movement-relationships",
     "Follower list replacement would bypass reciprocal links and loop prevention."},
    {JsApiStructOwner::CharData, "master", "setMaster", "character-movement-relationships",
     "Master changes need follow/unfollow helpers with reciprocal validation."},
    {JsApiStructOwner::CharData, "mount_data", "setMount", "character-movement-relationships",
     "Mount changes need mount/dismount helpers with rider propagation."},
    {JsApiStructOwner::CharData, "group", "setGroup", "character-movement-relationships",
     "Group writes need a live group registry or equivalent lifetime token."},
    {JsApiStructOwner::CharData, "player", "setProfile", "profile-admin",
     "Profile updates need split admin helpers and account/player persistence policy."},
    {JsApiStructOwner::CharData, "skills", "setSkill", "profile-admin",
     "Skill updates need training/admin helpers and derived knowledge recalculation."},
    {JsApiStructOwner::ObjData, "obj_flags", "setFlags", "object-value-domains",
     "Object flags and values need item-type-specific helpers, not raw obj_flags replacement."},
    {JsApiStructOwner::ObjData, "affected", "setAffects", "affects",
     "Object affects need slot-specific helpers and equipment recalculation."},
    {JsApiStructOwner::ObjData, "ex_description", "setExtraDescriptions",
     "zone-descriptions-and-visibility",
     "Object extra descriptions need bounded add/update/remove helpers."},
    {JsApiStructOwner::ObjData, "in_room", "setRoom", "inventory-equipment-object-movement",
     "Object room placement needs movement/load/extract helpers."},
    {JsApiStructOwner::ObjData, "carried_by", "setCarriedBy",
     "inventory-equipment-object-movement",
     "Carrier changes need inventory transfer helpers with reciprocal list validation."},
    {JsApiStructOwner::ObjData, "in_obj", "setContainer",
     "inventory-equipment-object-movement",
     "Container changes need put/get helpers with cycle, capacity, and weight checks."},
    {JsApiStructOwner::ObjData, "contains", "setContents",
     "inventory-equipment-object-movement",
     "Contents replacement would bypass nested ownership and linked-list invariants."},
    {JsApiStructOwner::ObjData, "touched", "setTouched", "object-value-domains",
     "Touched is runtime/player-interaction state and remains read-only."},
    {JsApiStructOwner::RoomData, "room_flags", "setFlags", "room-flags",
     "Room flags need a named allowlist and internal/transient bit exclusion."},
    {JsApiStructOwner::RoomData, "dir_option", "setExit", "room-exits",
     "Exit changes need directional helpers with destination, door, reset, and persistence policy."},
    {JsApiStructOwner::RoomData, "ex_description", "setExtraDescriptions",
     "zone-descriptions-and-visibility",
     "Room extra descriptions need bounded add/update/remove helpers."},
    {JsApiStructOwner::RoomData, "contents", "setContents",
     "inventory-equipment-object-movement",
     "Room contents replacement would bypass object placement and light/counting rules."},
    {JsApiStructOwner::RoomData, "people", "setCharacters",
     "character-movement-relationships",
     "Room occupant replacement would bypass movement, combat, mounts, and followers."},
    {JsApiStructOwner::RoomData, "affected", "setAffects", "affects",
     "Room affects need add/remove helpers with room-flag synchronization."},
    {JsApiStructOwner::RoomData, "light", "setLight", "room-flags",
     "Light is a derived live counter owned by object/light mechanics."},
    {JsApiStructOwner::ZoneData, "zone_short_description", "setShortDescriptions",
     "zone-descriptions-and-visibility",
     "Zone description lists need bounded add/update/remove helpers."},
    {JsApiStructOwner::ZoneData, "zone_description", "setExtraDescriptions",
     "zone-descriptions-and-visibility",
     "Zone extra-description lists need bounded add/update/remove helpers."},
    {JsApiStructOwner::ZoneData, "zone_map", "setMapDescriptions",
     "zone-descriptions-and-visibility",
     "Zone map-description lists need bounded add/update/remove helpers."},
    {JsApiStructOwner::ZoneData, "min_level_look", "setMinimumLookLevel",
     "zone-descriptions-and-visibility",
     "Minimum-look writes need a confirmed persisted builder edit path."},
    {JsApiStructOwner::ZoneData, "white_power", "setWhitePower", "zone-faction-power",
     "Faction power is derived live state and needs admin recalculation helpers."},
    {JsApiStructOwner::ZoneData, "dark_power", "setDarkPower", "zone-faction-power",
     "Faction power is derived live state and needs admin recalculation helpers."},
    {JsApiStructOwner::ZoneData, "magi_power", "setMagiPower", "zone-faction-power",
     "Faction power is derived live state and needs admin recalculation helpers."},
    {JsApiStructOwner::ZoneData, "cmdno", "setResetCommandCount", "zone-reset-authoring",
     "Reset command count must be owned by atomic reset-command helpers."},
    {JsApiStructOwner::ZoneData, "cmd", "setResetCommands", "zone-reset-authoring",
     "Reset command tables must be owned by atomic reset-command helpers."},
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

const JsApiDeferredHelperPlan *js_api_deferred_helper_plans() { return DeferredHelperPlans; }

std::size_t js_api_deferred_helper_plan_count() {
    return sizeof(DeferredHelperPlans) / sizeof(DeferredHelperPlans[0]);
}

const JsApiRawSetterGuardrail *js_api_raw_setter_guardrails() { return RawSetterGuardrails; }

std::size_t js_api_raw_setter_guardrail_count() {
    return sizeof(RawSetterGuardrails) / sizeof(RawSetterGuardrails[0]);
}

const JsApiHelperMutationGateRequirement *js_api_helper_mutation_gate_requirements()
{
    return HelperMutationGateRequirements;
}

std::size_t js_api_helper_mutation_gate_requirement_count()
{
    return sizeof(HelperMutationGateRequirements) / sizeof(HelperMutationGateRequirements[0]);
}

const JsApiRoomFlagHelperOperation *js_api_room_flag_helper_operations()
{
    return RoomFlagHelperOperations;
}

std::size_t js_api_room_flag_helper_operation_count()
{
    return sizeof(RoomFlagHelperOperations) / sizeof(RoomFlagHelperOperations[0]);
}

const JsApiRoomExitHelperOperation *js_api_room_exit_helper_operations()
{
    return RoomExitHelperOperations;
}

std::size_t js_api_room_exit_helper_operation_count()
{
    return sizeof(RoomExitHelperOperations) / sizeof(RoomExitHelperOperations[0]);
}

const JsApiCharacterMovementHelperOperation *js_api_character_movement_helper_operations()
{
    return CharacterMovementHelperOperations;
}

std::size_t js_api_character_movement_helper_operation_count()
{
    return sizeof(CharacterMovementHelperOperations) / sizeof(CharacterMovementHelperOperations[0]);
}

const JsApiCombatEffectHelperOperation *js_api_combat_effect_helper_operations()
{
    return CombatEffectHelperOperations;
}

std::size_t js_api_combat_effect_helper_operation_count()
{
    return sizeof(CombatEffectHelperOperations) / sizeof(CombatEffectHelperOperations[0]);
}

const JsApiEquipmentHelperOperation *js_api_equipment_helper_operations()
{
    return EquipmentHelperOperations;
}

std::size_t js_api_equipment_helper_operation_count()
{
    return sizeof(EquipmentHelperOperations) / sizeof(EquipmentHelperOperations[0]);
}
