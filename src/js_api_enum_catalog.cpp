#include "js_api_enum_catalog.h"

#include "json_utils.h"

#include <sstream>
#include <string>

namespace {

constexpr JsApiEnumValue RaceValues[] = {
    {"God", "God", 0, "Immortal/admin race name as exposed by character snapshots."},
    {"Human", "Human", 1, "Human character race."},
    {"Dwarf", "Dwarf", 2, "Dwarf character race."},
    {"WoodElf", "Wood Elf", 3, "Wood Elf character race."},
    {"Hobbit", "Hobbit", 4, "Hobbit character race."},
    {"HighElf", "High Elf", 5, "High Elf character race."},
    {"Beorning", "Beorning", 6, "Beorning character race."},
    {"UrukHai", "Uruk-Hai", 11, "Uruk-Hai character race."},
    {"Harad", "Harad", 12, "Harad character race."},
    {"Orc", "Orc", 13, "Orc character race."},
    {"Easterling", "Easterling", 14, "Easterling character race."},
    {"UrukLhuth", "Uruk-Lhuth", 15, "Uruk-Lhuth character race."},
    {"Undead", "Undead", 16, "Undead character race."},
    {"OlogHai", "Olog-Hai", 17, "Olog-Hai character race."},
    {"Haradrim", "Haradrim", 18, "Haradrim character race."},
    {"Troll", "Troll", 20, "Troll character race."},
    {"Unknown", "Unknown", -1, "Fallback name when live data contains an unknown race id."},
};

constexpr JsApiEnumValue DirectionValues[] = {
    {"North", "north", 0, "North exit or movement direction."},
    {"East", "east", 1, "East exit or movement direction."},
    {"South", "south", 2, "South exit or movement direction."},
    {"West", "west", 3, "West exit or movement direction."},
    {"Up", "up", 4, "Up exit or movement direction."},
    {"Down", "down", 5, "Down exit or movement direction."},
};

constexpr JsApiEnumValue RoomSectorValues[] = {
    {"Floor", "Floor", 0, "Indoor floor sector."},
    {"City", "City", 1, "City sector."},
    {"Field", "Field", 2, "Field sector."},
    {"Forest", "Forest", 3, "Forest sector."},
    {"Hills", "Hills", 4, "Hills sector."},
    {"Mountain", "Mountain", 5, "Mountain sector."},
    {"Water", "Water", 6, "Swimmable water sector."},
    {"WaterNoSwim", "Water_noswim", 7, "Water sector that requires special traversal."},
    {"Underwater", "Underwater", 8, "Underwater sector."},
    {"Road", "Road", 9, "Road sector."},
    {"Crack", "Crack", 10, "Crack sector."},
    {"DenseForest", "Dense_forest", 11, "Dense forest sector."},
    {"Swamp", "Swamp", 12, "Swamp sector."},
    {"Unknown", "Unknown", -1, "Fallback name when live data contains an unknown sector id."},
};

constexpr JsApiEnumValue WearSlotValues[] = {
    {"Light", "light", 0, "Equipment light slot."},
    {"FingerRight", "fingerRight", 1, "Right finger equipment slot."},
    {"FingerLeft", "fingerLeft", 2, "Left finger equipment slot."},
    {"Neck1", "neck1", 3, "First neck equipment slot."},
    {"Neck2", "neck2", 4, "Second neck equipment slot."},
    {"Body", "body", 5, "Body equipment slot."},
    {"Head", "head", 6, "Head equipment slot."},
    {"Legs", "legs", 7, "Legs equipment slot."},
    {"Feet", "feet", 8, "Feet equipment slot."},
    {"Hands", "hands", 9, "Hands equipment slot."},
    {"Arms", "arms", 10, "Arms equipment slot."},
    {"Shield", "shield", 11, "Shield equipment slot."},
    {"AboutBody", "aboutBody", 12, "About-body equipment slot."},
    {"Waist", "waist", 13, "Waist equipment slot."},
    {"WristRight", "wristRight", 14, "Right wrist equipment slot."},
    {"WristLeft", "wristLeft", 15, "Left wrist equipment slot."},
    {"Wield", "wield", 16, "Wielded weapon equipment slot."},
    {"Hold", "hold", 17, "Held object equipment slot."},
    {"Back", "back", 18, "Back equipment slot."},
    {"Belt1", "belt1", 19, "First belt equipment slot."},
    {"Belt2", "belt2", 20, "Second belt equipment slot."},
    {"Belt3", "belt3", 21, "Third belt equipment slot."},
};

constexpr JsApiEnumValue TargetTypeValues[] = {
    {"Character", "character", 0, "Mudlle target resolved to a character."},
    {"Object", "object", 1, "Mudlle target resolved to an object."},
    {"Room", "room", 2, "Mudlle target resolved to a room."},
    {"Text", "text", 3, "Mudlle target resolved to text."},
    {"Gold", "gold", 4, "Mudlle target resolved to gold."},
    {"Direction", "direction", 5, "Mudlle target resolved to a direction."},
    {"In", "in", 6, "Mudlle target uses the in preposition."},
    {"All", "all", 7, "Mudlle target means all."},
    {"Value", "value", 8, "Mudlle target resolved to a numeric value."},
    {"Other", "other", 9, "Mudlle target resolved to another supported shape."},
    {"Ignore", "ignore", 10, "Mudlle target intentionally ignored."},
    {"None", "none", 11, "Mudlle target absent."},
    {"Unknown", "unknown", -1, "Fallback target type for unknown data."},
};

constexpr JsApiEnumValue PositionValues[] = {
    {"Dead", "Dead", 0, "Dead character position."},
    {"Shaping", "Shaping", 1, "Builder/editor shaping position."},
    {"Incapacitated", "Incapacitated", 2, "Incapacitated character position."},
    {"Stunned", "Stunned", 3, "Stunned character position."},
    {"Sleeping", "Sleeping", 4, "Sleeping character position."},
    {"Resting", "Resting", 5, "Resting character position."},
    {"Sitting", "Sitting", 6, "Sitting character position."},
    {"Fighting", "Fighting", 7, "Fighting character position."},
    {"Standing", "Standing", 8, "Standing character position."},
    {"Unknown", "Unknown", -1, "Fallback name when live data contains an unknown position."},
};

constexpr JsApiEnumValue TacticValues[] = {
    {"Defensive", "defensive", 1, "Defensive combat tactic."},
    {"Careful", "careful", 2, "Careful combat tactic."},
    {"Normal", "normal", 3, "Normal combat tactic."},
    {"Aggressive", "aggressive", 4, "Aggressive combat tactic."},
    {"Berserk", "berserk", 5, "Berserk combat tactic."},
};

constexpr JsApiEnumValue CombatSpeedValues[] = {
    {"Slow", "slow", 1, "Slow shooting or casting mode."},
    {"Normal", "normal", 2, "Normal shooting or casting mode."},
    {"Fast", "fast", 3, "Fast shooting or casting mode."},
    {"Unknown", "Unknown", -1, "Fallback name when live data contains an unknown mode."},
};

constexpr JsApiEnumValue ProfessionValues[] = {
    {"Mage", "mage", 1, "Mage profession key."},
    {"Mystic", "mystic", 2, "Mystic profession key."},
    {"Ranger", "ranger", 3, "Ranger profession key."},
    {"Warrior", "warrior", 4, "Warrior profession key."},
};

constexpr JsApiEnumValue SpecializationValues[] = {
    {"Nothing", "nothing", 0, "No selected specialization."},
    {"Fire", "fire", 1, "Fire specialization."},
    {"Cold", "cold", 2, "Cold specialization."},
    {"Regeneration", "regeneration", 3, "Regeneration specialization."},
    {"Protection", "protection", 4, "Protection specialization."},
    {"Animals", "animals", 5, "Animals specialization."},
    {"Stealth", "stealth", 6, "Stealth specialization."},
    {"WildFighting", "wildFighting", 7, "Wild fighting specialization."},
    {"Teleportation", "teleportation", 8, "Teleportation specialization."},
    {"Illusion", "illusion", 9, "Illusion specialization."},
    {"Lightning", "lightning", 10, "Lightning specialization."},
    {"Guardian", "guardian", 11, "Guardian specialization."},
    {"HeavyFighting", "heavyFighting", 12, "Heavy fighting specialization."},
    {"LightFighting", "lightFighting", 13, "Light fighting specialization."},
    {"Defending", "defending", 14, "Defending specialization."},
    {"Archery", "archery", 15, "Archery specialization."},
    {"Darkness", "darkness", 16, "Darkness specialization."},
    {"Arcane", "arcane", 17, "Arcane specialization."},
    {"WeaponMastery", "weaponMastery", 18, "Weapon mastery specialization."},
    {"BattleMagic", "battleMagic", 19, "Battle magic specialization."},
    {"Unknown", "Unknown", -1, "Fallback specialization key for unknown data."},
};

constexpr JsApiEnumValue ObjectTypeValues[] = {
    {"Light", "light", 1, "Light object type."},
    {"Scroll", "scroll", 2, "Scroll object type."},
    {"Wand", "wand", 3, "Wand object type."},
    {"Staff", "staff", 4, "Staff object type."},
    {"Weapon", "weapon", 5, "Weapon object type."},
    {"FireWeapon", "fireWeapon", 6, "Fire weapon object type."},
    {"Missile", "missile", 7, "Missile object type."},
    {"Treasure", "treasure", 8, "Treasure object type."},
    {"Armor", "armor", 9, "Armor object type."},
    {"Potion", "potion", 10, "Potion object type."},
    {"Worn", "worn", 11, "Worn object type."},
    {"Other", "other", 12, "Other object type."},
    {"Trash", "trash", 13, "Trash object type."},
    {"Trap", "trap", 14, "Trap object type."},
    {"Container", "container", 15, "Container object type."},
    {"Note", "note", 16, "Note object type."},
    {"DrinkContainer", "drinkContainer", 17, "Drink container object type."},
    {"Key", "key", 18, "Key object type."},
    {"Food", "food", 19, "Food object type."},
    {"Money", "money", 20, "Money object type."},
    {"Pen", "pen", 21, "Pen object type."},
    {"Boat", "boat", 22, "Boat object type."},
    {"Fountain", "fountain", 23, "Fountain object type."},
    {"Shield", "shield", 24, "Shield object type."},
    {"Lever", "lever", 25, "Lever object type."},
    {"Unknown", "Unknown", -1, "Fallback object type for unknown data."},
};

constexpr JsApiEnumValue ObjectMaterialValues[] = {
    {"Usual", "usual", 0, "Usual/default object material."},
    {"Cloth", "cloth", 1, "Cloth object material."},
    {"Leather", "leather", 2, "Leather object material."},
    {"Chain", "chain", 3, "Chain object material."},
    {"Metal", "metal", 4, "Metal object material."},
    {"Wood", "wood", 5, "Wood object material."},
    {"Stone", "stone", 6, "Stone object material."},
    {"Crystal", "crystal", 7, "Crystal object material."},
    {"Gold", "gold", 8, "Gold object material."},
    {"Silver", "silver", 9, "Silver object material."},
    {"Mithril", "mithril", 10, "Mithril object material."},
    {"Fur", "fur", 11, "Fur object material."},
    {"Glass", "glass", 12, "Glass object material."},
    {"Plant", "plant", 13, "Plant object material."},
    {"Unknown", "Unknown", -1, "Fallback material for unknown data."},
};

constexpr JsApiEnumValue ObjectWearFlagValues[] = {
    {"Take", "take", 1, "Object can be taken."},
    {"Finger", "finger", 2, "Object can be worn on a finger slot."},
    {"Neck", "neck", 4, "Object can be worn on a neck slot."},
    {"Body", "body", 8, "Object can be worn on the body slot."},
    {"Head", "head", 16, "Object can be worn on the head slot."},
    {"Legs", "legs", 32, "Object can be worn on the legs slot."},
    {"Feet", "feet", 64, "Object can be worn on the feet slot."},
    {"Hands", "hands", 128, "Object can be worn on the hands slot."},
    {"Arms", "arms", 256, "Object can be worn on the arms slot."},
    {"Shield", "shield", 512, "Object can be worn as a shield."},
    {"AboutBody", "aboutBody", 1024, "Object can be worn about the body."},
    {"Waist", "waist", 2048, "Object can be worn on the waist slot."},
    {"Wrist", "wrist", 4096, "Object can be worn on a wrist slot."},
    {"Wield", "wield", 8192, "Object can be wielded."},
    {"Hold", "hold", 16384, "Object can be held."},
    {"Throw", "throw", 32768, "Object can be thrown."},
    {"Back", "back", 65536, "Object can be worn on the back slot."},
    {"Belt", "belt", 131072, "Object can be worn on a belt slot."},
};

constexpr JsApiEnumValue ObjectExtraFlagValues[] = {
    {"Glow", "glow", 1, "Object has the glow flag."},
    {"Hum", "hum", 2, "Object has the hum flag."},
    {"Dark", "dark", 4, "Object has the dark flag."},
    {"Breakable", "breakable", 8, "Object can be broken."},
    {"Evil", "evil", 16, "Object has the evil flag."},
    {"Invisible", "invisible", 32, "Object is invisible."},
    {"Magic", "magic", 64, "Object has the magic flag."},
    {"NoDrop", "noDrop", 128, "Object cannot be dropped by normal flow."},
    {"Broken", "broken", 256, "Object is broken."},
    {"AntiGood", "antiGood", 512, "Object rejects good-aligned users."},
    {"AntiEvil", "antiEvil", 1024, "Object rejects evil-aligned users."},
    {"AntiNeutral", "antiNeutral", 2048, "Object rejects neutral users."},
    {"NoRent", "noRent", 4096, "Object cannot be rented."},
    {"NoInvis", "noInvis", 16384, "Object cannot be made invisible."},
    {"Willpower", "willpower", 32768, "Object has the willpower flag."},
    {"ImmortalOnly", "immortalOnly", 65536, "Object is immortal-only."},
    {"Human", "human", 131072, "Object is restricted to humans."},
    {"Dwarf", "dwarf", 262144, "Object is restricted to dwarves."},
    {"WoodElf", "woodElf", 524288, "Object is restricted to Wood Elves."},
    {"Hobbit", "hobbit", 1048576, "Object is restricted to hobbits."},
    {"Beorning", "beorning", 2097152, "Object is restricted to Beornings."},
    {"Uruk", "uruk", 4194304, "Object is restricted to Uruks."},
    {"Orc", "orc", 8388608, "Object is restricted to Orcs."},
    {"MobOrc", "mobOrc", 16777216, "Object is restricted to mobile Orcs."},
    {"Magus", "magus", 33554432, "Object is restricted to Magus."},
    {"OlogHai", "ologHai", 67108864, "Object is restricted to Olog-Hai."},
    {"Haradrim", "haradrim", 134217728, "Object is restricted to Haradrim."},
    {"StayZone", "stayZone", 268435456, "Object should stay in zone."},
};

constexpr JsApiEnumValue RoomFlagValues[] = {
    {"Dark", "dark", 1, "Room has the dark flag."},
    {"Death", "death", 2, "Room has the death flag."},
    {"NoMob", "noMob", 4, "Room blocks ordinary mobile movement."},
    {"Indoors", "indoors", 8, "Room is indoors."},
    {"NoRide", "noRide", 16, "Room blocks riding."},
    {"PermanentAffect", "permanentAffect", 32, "Room has permanent affect metadata."},
    {"Shadowy", "shadowy", 64, "Room is shadowy."},
    {"NoMagic", "noMagic", 128, "Room blocks magic."},
    {"Tunnel", "tunnel", 256, "Room is a tunnel."},
    {"Private", "private", 512, "Room is private."},
    {"GodRoom", "godRoom", 1024, "Room is restricted to immortals/admins."},
    {"DrinkWater", "drinkWater", 2048, "Room contains drinkable water."},
    {"DrinkPoison", "drinkPoison", 4096, "Room contains poisonous drinkable water."},
    {"SecurityRoom", "securityRoom", 8192, "Room has security restrictions."},
    {"PeaceRoom", "peaceRoom", 16384, "Room blocks combat."},
    {"NoTeleport", "noTeleport", 32768, "Room blocks teleportation."},
    {"HideVnum", "hideVnum", 65536, "Room hides its number from normal display."},
};

constexpr JsApiEnumValue ExitFlagValues[] = {
    {"Door", "door", 1, "Exit is a door."},
    {"Closed", "closed", 2, "Exit is closed."},
    {"Locked", "locked", 4, "Exit is locked."},
    {"NoFlee", "noFlee", 8, "Exit cannot be used for fleeing."},
    {"ResetLocked", "resetLocked", 16, "Exit resets locked."},
    {"Pickproof", "pickproof", 32, "Exit cannot be picked."},
    {"Heavy", "heavy", 64, "Exit is heavy."},
    {"NoBreak", "noBreak", 128, "Exit cannot be broken."},
    {"NoLook", "noLook", 256, "Exit blocks looking through it."},
    {"Hidden", "hidden", 512, "Exit is hidden."},
    {"Broken", "broken", 1024, "Exit is broken."},
    {"NoRide", "noRide", 2048, "Exit blocks riding."},
    {"NoBlink", "noBlink", 4096, "Exit blocks blink-style travel."},
    {"Lever", "lever", 8192, "Exit is controlled by a lever."},
    {"NoWalk", "noWalk", 16384, "Exit blocks walking."},
};

constexpr JsApiEnumValue ApplyLocationValues[] = {
    {"None", "NONE", 0, "No apply location."},
    {"Str", "STR", 1, "Strength apply location."},
    {"Dex", "DEX", 2, "Dexterity apply location."},
    {"Int", "INT", 3, "Intelligence apply location."},
    {"Wis", "WIS", 4, "Wisdom/will apply location."},
    {"Con", "CON", 5, "Constitution apply location."},
    {"Sex", "SEX", 6, "Sex apply location."},
    {"Class", "CLASS", 7, "Class/profession apply location."},
    {"Level", "LEVEL", 8, "Level apply location."},
    {"Age", "AGE", 9, "Age apply location."},
    {"CharWeight", "CHAR_WEIGHT", 10, "Character weight apply location."},
    {"CharHeight", "CHAR_HEIGHT", 11, "Character height apply location."},
    {"Mana", "MANA", 12, "Mana apply location."},
    {"Hit", "HIT", 13, "Hit point apply location."},
    {"Move", "MOVE", 14, "Move point apply location."},
    {"Gold", "GOLD", 15, "Gold apply location."},
    {"Exp", "EXP", 16, "Experience apply location."},
    {"Dodge", "DODGE", 17, "Dodge apply location."},
    {"Ob", "OB", 18, "Offensive bonus apply location."},
    {"Damroll", "DAMROLL", 19, "Damage roll apply location."},
    {"SavingSpell", "SAVING_SPELL", 20, "Saving spell apply location."},
    {"Willpower", "WILLPOWER", 21, "Willpower apply location."},
    {"Regen", "REGEN", 22, "Regeneration apply location."},
    {"Vision", "VISION", 23, "Vision apply location."},
    {"Speed", "SPEED", 24, "Speed apply location."},
    {"Perception", "PERCEPTION", 25, "Perception apply location."},
    {"Armor", "ARMOR", 26, "Armor apply location."},
    {"Spell", "SPELL", 27, "Spell apply location."},
    {"Bitvector", "BITVECTOR", 28, "Affect bitvector apply location."},
    {"ManaRegen", "MANA_REGEN", 29, "Mana regeneration apply location."},
    {"Resistance", "RESISTANCE", 30, "Resistance apply location."},
    {"Vulnerab", "VULNERAB", 31, "Vulnerability apply location."},
    {"Maul", "MAUL", 32, "Maul apply location."},
    {"Bend", "BEND", 33, "Bend apply location."},
    {"PkMage", "PKMAGE", 34, "Mage PK apply location."},
    {"PkMystic", "PKMYSTIC", 35, "Mystic PK apply location."},
    {"PkRanger", "PKRANGER", 36, "Ranger PK apply location."},
    {"PkWarrior", "PKWARRIOR", 37, "Warrior PK apply location."},
    {"SpellPen", "SPELLPEN", 38, "Spell penetration apply location."},
    {"SpellPower", "SPELLPOWER", 39, "Spell power apply location."},
};

#define JS_ENUM_COUNT(values) sizeof(values) / sizeof(values[0])

constexpr JsApiEnumCatalog Catalogs[] = {
    {"Race", "RaceName", JsApiEnumValueKind::String,
     "Character race names for comparisons against Character.race and relationship snapshots.",
     "Character.race, CharacterRelationshipSnapshot.race", RaceValues,
     JS_ENUM_COUNT(RaceValues)},
    {"RaceIds", "RaceId", JsApiEnumValueKind::Number,
     "Numeric race ids for comparisons against Character.profile.raceId.", "CharacterProfile.raceId",
     RaceValues, JS_ENUM_COUNT(RaceValues)},
    {"Direction", "DirectionName", JsApiEnumValueKind::String,
     "Movement and exit direction names.", "ScriptContext.direction, RoomExit.direction",
     DirectionValues, JS_ENUM_COUNT(DirectionValues)},
    {"DirectionIds", "DirectionId", JsApiEnumValueKind::Number,
     "Numeric direction indexes matching the live exit array order.", "RoomExit.directionIndex",
     DirectionValues, JS_ENUM_COUNT(DirectionValues)},
    {"RoomSector", "RoomSectorName", JsApiEnumValueKind::String,
     "Room sector names returned by Room.sectorType.", "Room.sectorType", RoomSectorValues,
     JS_ENUM_COUNT(RoomSectorValues)},
    {"RoomSectorIds", "RoomSectorId", JsApiEnumValueKind::Number,
     "Numeric room sector ids matching live room storage; no script-visible numeric sector field yet.",
     "Prefer RotS.RoomSector with Room.sectorType", RoomSectorValues,
     JS_ENUM_COUNT(RoomSectorValues)},
    {"WearSlot", "WearSlotName", JsApiEnumValueKind::String,
     "Equipment slot names used by wear triggers and equipment snapshots.",
     "ScriptContext.wearSlot, EquipmentSlot.slotName", WearSlotValues,
     JS_ENUM_COUNT(WearSlotValues)},
    {"WearSlotIds", "WearSlotId", JsApiEnumValueKind::Number,
     "Numeric equipment slot ids matching live wear slot storage.", "EquipmentSlot.slotIndex",
     WearSlotValues, JS_ENUM_COUNT(WearSlotValues)},
    {"TargetType", "TargetTypeName", JsApiEnumValueKind::String,
     "Mudlle target type names used by targetTypes fixture/context data.", "ScriptContext.targetTypes",
     TargetTypeValues, JS_ENUM_COUNT(TargetTypeValues)},
    {"CharacterPosition", "CharacterPositionName", JsApiEnumValueKind::String,
     "Character position names returned by Character.specials and skill metadata.",
     "CharacterSpecials.position, CharacterSpecials.defaultPosition, SkillValue.minimumPosition",
     PositionValues, JS_ENUM_COUNT(PositionValues)},
    {"CharacterPositionIds", "CharacterPositionId", JsApiEnumValueKind::Number,
     "Numeric character position ids matching live character state.",
     "CharacterSpecials.position, SkillValue.minimumPosition", PositionValues,
     JS_ENUM_COUNT(PositionValues)},
    {"CharacterTactic", "CharacterTacticName", JsApiEnumValueKind::String,
     "Character combat tactic names.", "CharacterSpecials2.tactics", TacticValues,
     JS_ENUM_COUNT(TacticValues)},
    {"CharacterTacticIds", "CharacterTacticId", JsApiEnumValueKind::Number,
     "Numeric tactic ids matching live character state; no script-visible numeric tactic field yet.",
     "Prefer RotS.CharacterTactic with CharacterSpecials2.tactics", TacticValues,
     JS_ENUM_COUNT(TacticValues)},
    {"CombatSpeed", "CombatSpeedName", JsApiEnumValueKind::String,
     "Shared shooting and casting speed names.", "CharacterSpecials2.shooting, CharacterSpecials2.casting",
     CombatSpeedValues, JS_ENUM_COUNT(CombatSpeedValues)},
    {"CombatSpeedIds", "CombatSpeedId", JsApiEnumValueKind::Number,
     "Numeric shooting and casting speed ids; no script-visible numeric speed field yet.",
     "Prefer RotS.CombatSpeed with CharacterSpecials2.shooting or CharacterSpecials2.casting",
     CombatSpeedValues, JS_ENUM_COUNT(CombatSpeedValues)},
    {"Profession", "ProfessionKey", JsApiEnumValueKind::String,
     "Profession keys used by profession snapshots and skill metadata.",
     "Profession.key, SkillValue.profession, KnowledgeValue.profession", ProfessionValues,
     JS_ENUM_COUNT(ProfessionValues)},
    {"ProfessionIds", "ProfessionId", JsApiEnumValueKind::Number,
     "Numeric profession ids matching live storage. Mystic maps to the old Cleric slot.",
     "CharacterProfile.profession", ProfessionValues, JS_ENUM_COUNT(ProfessionValues)},
    {"Specialization", "SpecializationKey", JsApiEnumValueKind::String,
     "Specialization keys used by specialization snapshots and skill metadata.",
     "SpecializationData.currentKey, SkillValue.specializationId", SpecializationValues,
     JS_ENUM_COUNT(SpecializationValues)},
    {"SpecializationIds", "SpecializationId", JsApiEnumValueKind::Number,
     "Numeric specialization ids matching live storage.", "SpecializationData.currentId",
     SpecializationValues, JS_ENUM_COUNT(SpecializationValues)},
    {"ObjectType", "ObjectTypeName", JsApiEnumValueKind::String,
     "Object type names returned by GameObject.flags.itemType.", "ObjectFlags.itemType",
     ObjectTypeValues, JS_ENUM_COUNT(ObjectTypeValues)},
    {"ObjectTypeIds", "ObjectTypeId", JsApiEnumValueKind::Number,
     "Numeric object type ids matching live object storage; no script-visible numeric object type field yet.",
     "Prefer RotS.ObjectType with ObjectFlags.itemType",
     ObjectTypeValues, JS_ENUM_COUNT(ObjectTypeValues)},
    {"ObjectMaterial", "ObjectMaterialName", JsApiEnumValueKind::String,
     "Object material names returned by GameObject.flags.material.", "ObjectFlags.material",
     ObjectMaterialValues, JS_ENUM_COUNT(ObjectMaterialValues)},
    {"ObjectMaterialIds", "ObjectMaterialId", JsApiEnumValueKind::Number,
     "Numeric object material ids matching live object storage; no script-visible numeric object material field yet.",
     "Prefer RotS.ObjectMaterial with ObjectFlags.material",
     ObjectMaterialValues, JS_ENUM_COUNT(ObjectMaterialValues)},
    {"ObjectWearFlag", "ObjectWearFlagName", JsApiEnumValueKind::String,
     "Object wear flag names returned by ObjectFlags.wearFlags.", "ObjectFlags.wearFlags",
     ObjectWearFlagValues, JS_ENUM_COUNT(ObjectWearFlagValues)},
    {"ObjectWearFlagBits", "ObjectWearFlagBit", JsApiEnumValueKind::Bit,
     "Object wear flag bit values matching live object storage; no script-visible numeric wear-flag field yet.",
     "Prefer RotS.ObjectWearFlag with ObjectFlags.wearFlags",
     ObjectWearFlagValues, JS_ENUM_COUNT(ObjectWearFlagValues)},
    {"ObjectExtraFlag", "ObjectExtraFlagName", JsApiEnumValueKind::String,
     "Object extra flag names returned by ObjectFlags.extraFlags.", "ObjectFlags.extraFlags",
     ObjectExtraFlagValues, JS_ENUM_COUNT(ObjectExtraFlagValues)},
    {"ObjectExtraFlagBits", "ObjectExtraFlagBit", JsApiEnumValueKind::Bit,
     "Object extra flag bit values matching live object storage; no script-visible numeric extra-flag field yet.",
     "Prefer RotS.ObjectExtraFlag with ObjectFlags.extraFlags",
     ObjectExtraFlagValues, JS_ENUM_COUNT(ObjectExtraFlagValues)},
    {"RoomFlag", "RoomFlagName", JsApiEnumValueKind::String,
     "Room flag names returned by Room.flags.", "Room.flags", RoomFlagValues,
     JS_ENUM_COUNT(RoomFlagValues)},
    {"RoomFlagBits", "RoomFlagBit", JsApiEnumValueKind::Bit,
     "Room flag bit values matching live room storage; no script-visible numeric room-flag field yet.",
     "Prefer RotS.RoomFlag with Room.flags", RoomFlagValues,
     JS_ENUM_COUNT(RoomFlagValues)},
    {"ExitFlag", "ExitFlagName", JsApiEnumValueKind::String,
     "Exit flag names returned by RoomExit.flags.", "RoomExit.flags", ExitFlagValues,
     JS_ENUM_COUNT(ExitFlagValues)},
    {"ExitFlagBits", "ExitFlagBit", JsApiEnumValueKind::Bit,
     "Exit flag bit values matching live exit storage; no script-visible numeric exit-flag field yet.",
     "Prefer RotS.ExitFlag with RoomExit.flags", ExitFlagValues,
     JS_ENUM_COUNT(ExitFlagValues)},
    {"ApplyLocation", "ApplyLocationName", JsApiEnumValueKind::String,
     "Apply location names returned by affect and object-affect snapshots.",
     "Affect.locationName, ObjectAffect.locationName", ApplyLocationValues,
     JS_ENUM_COUNT(ApplyLocationValues)},
    {"ApplyLocationIds", "ApplyLocationId", JsApiEnumValueKind::Number,
     "Numeric apply-location ids matching live affect storage.",
     "Affect.locationId, ObjectAffect.locationId", ApplyLocationValues,
     JS_ENUM_COUNT(ApplyLocationValues)},
};

std::string js_string_literal(const char* value) {
    std::string escaped;
    json_utils::append_escaped_json_string(escaped, value ? value : "");
    return "\"" + escaped + "\"";
}

} // namespace

const char* js_api_enum_value_kind_name(JsApiEnumValueKind kind) {
    switch (kind) {
    case JsApiEnumValueKind::String:
        return "string";
    case JsApiEnumValueKind::Number:
        return "number";
    case JsApiEnumValueKind::Bit:
        return "bit";
    }
    return "unknown";
}

const JsApiEnumCatalog* js_api_enum_catalogs() { return Catalogs; }

std::size_t js_api_enum_catalog_count() { return JS_ENUM_COUNT(Catalogs); }

std::string js_api_enum_runtime_object_literal() {
    std::ostringstream out;
    for (std::size_t catalog_index = 0; catalog_index < js_api_enum_catalog_count();
         ++catalog_index) {
        const JsApiEnumCatalog& catalog = Catalogs[catalog_index];
        out << "  " << catalog.name << ": {\n";
        for (std::size_t value_index = 0; value_index < catalog.value_count; ++value_index) {
            const JsApiEnumValue& value = catalog.values[value_index];
            out << "    " << value.key << ": ";
            if (catalog.value_kind == JsApiEnumValueKind::String)
                out << js_string_literal(value.string_value);
            else
                out << value.number_value;
            out << (value_index + 1 == catalog.value_count ? "\n" : ",\n");
        }
        out << "  }";
        if (catalog_index + 1 < js_api_enum_catalog_count())
            out << ",";
        out << "\n";
    }
    return out.str();
}
