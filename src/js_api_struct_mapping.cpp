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
     "readonly GameObject[]", true, Deferred, Unsupported,
     "Planned read-only snapshot of objects directly in the room.",
     "Replacing the room contents linked list from JavaScript is unsupported; use explicit "
     "object movement/load/extract helpers so room contents, object container/carrier links, "
     "light counters, crash-save flags, and stale-list checks stay centralized.",
     "world-mutation", "Use explicit object movement helpers if added later."},
    {JsApiStructOwner::RoomData, "room_data", "people", "characters", "getCharacters",
     "setCharacters", "readonly Character[]", true, Deferred, Unsupported,
     "Planned read-only snapshot of characters currently in the room.",
     "Replacing the room people linked list from JavaScript is unsupported; use explicit "
     "movement/teleport helpers so character room pointers, combat state, followers, mounts, "
     "visibility, and room security checks stay centralized.",
     "world-mutation", "Use explicit movement helpers if added later."},
    {JsApiStructOwner::RoomData, "room_data", "affected", "affects", "getAffects", "setAffects",
     "readonly Affect[]", true, Deferred, Unsupported, "Planned read-only room affect snapshot.",
     "Room affect writes are unsupported for builder scripts until explicit add/remove helpers "
     "own duration accounting, room flag synchronization, movement/combat/light side effects, "
     "and persistence.",
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
