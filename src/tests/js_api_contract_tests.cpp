#include "../js_api_contract.h"

#include "../js_scripting_manifest.h"

#include <gtest/gtest.h>

#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> split_context_fields(const char *fields) {
    std::vector<std::string> result;
    std::stringstream stream(fields ? fields : "");
    std::string field;
    while (std::getline(stream, field, ',')) {
        while (!field.empty() && field.front() == ' ')
            field.erase(field.begin());
        while (!field.empty() && field.back() == ' ')
            field.pop_back();
        if (!field.empty())
            result.push_back(field);
    }
    return result;
}

bool contains_raw_cpp_name(const std::string &value) {
    const char *blocked[] = {
        "char_data", "obj_data", "room_data", "descriptor_data", "struct ",
        "*",         "&",        "void*",     "player_data",     "affected_type",
    };
    for (const char *token : blocked) {
        if (value.find(token) != std::string::npos)
            return true;
    }
    return false;
}

} // namespace

TEST(JsApiContract, ExposesStableMetadataForGeneratedConsumers) {
    const JsApiContractMetadata &metadata = js_api_contract_metadata();

    EXPECT_EQ(metadata.schema_version, 1);
    EXPECT_EQ(metadata.api_revision, 2);
    EXPECT_STREQ(metadata.api_version, "unpublished");
    EXPECT_STREQ(metadata.contract_checksum, "rots-js-api-contract-v1-revision-2");
    EXPECT_STREQ(metadata.generated_typings_version, "unpublished-2");
    EXPECT_STREQ(metadata.documentation_version, "unpublished-2");
    EXPECT_STREQ(metadata.minimum_trigger_catalog_revision, "1");
    EXPECT_NE(std::string(metadata.notes).find("pure result helpers"), std::string::npos);
    EXPECT_NE(std::string(metadata.notes).find("type-only mutation result contract"),
              std::string::npos);
    EXPECT_NE(std::string(metadata.notes).find("side-effect host bindings remain deferred"),
              std::string::npos);
}

TEST(JsApiContract, ContainsExpectedHandleAndContextTypes) {
    const char *expected_types[] = {
        "Character",
        "AbilityScores",
        "CharacterPoints",
        "CharacterSpecials",
        "CharacterConditions",
        "CharacterSpecials2",
        "Profession",
        "SpecializationData",
        "DamageDetails",
        "DamageEntry",
        "SkillValue",
        "KnowledgeValue",
        "Affect",
        "ObjectAffect",
        "ExtraDescription",
        "EquipmentSlot",
        "EquipmentObjectSnapshot",
        "InventoryObjectSnapshot",
        "CharacterRelationshipSnapshot",
        "Player",
        "Mob",
        "GameObject",
        "Room",
        "Zone",
        "TriggerInfo",
        "ScriptContext",
        "MutationResult",
        "ScriptResult",
        "Script",
    };

    for (const char *name : expected_types)
        EXPECT_NE(find_js_api_contract_type(name), nullptr) << name;

    const JsApiType *player = find_js_api_contract_type("Player");
    ASSERT_NE(player, nullptr);
    EXPECT_STREQ(player->extends, "Character");

    const JsApiType *character_points = find_js_api_contract_type("CharacterPoints");
    ASSERT_NE(character_points, nullptr);
    EXPECT_NE(find_js_api_contract_member(*character_points, "bodypartHits"), nullptr);
    EXPECT_NE(find_js_api_contract_member(*character_points, "spellPower"), nullptr);
}

TEST(JsApiContract, DocumentsEveryTypeAndMember) {
    for (std::size_t type_index = 0; type_index < js_api_contract_type_count(); ++type_index) {
        const JsApiType &type = js_api_contract_types()[type_index];
        EXPECT_NE(type.name, nullptr);
        EXPECT_FALSE(std::string(type.name).empty());
        EXPECT_NE(type.docs, nullptr) << type.name;
        EXPECT_FALSE(std::string(type.docs).empty()) << type.name;
        EXPECT_GT(type.member_count, 0U) << type.name;

        for (std::size_t member_index = 0; member_index < type.member_count; ++member_index) {
            const JsApiMember &member = type.members[member_index];
            EXPECT_NE(member.name, nullptr) << type.name;
            EXPECT_FALSE(std::string(member.name).empty()) << type.name;
            EXPECT_NE(member.type_name, nullptr) << type.name << "." << member.name;
            EXPECT_FALSE(std::string(member.type_name).empty()) << type.name << "." << member.name;
            EXPECT_NE(member.permission, nullptr) << type.name << "." << member.name;
            EXPECT_FALSE(std::string(member.permission).empty()) << type.name << "." << member.name;
            EXPECT_NE(member.docs, nullptr) << type.name << "." << member.name;
            EXPECT_FALSE(std::string(member.docs).empty()) << type.name << "." << member.name;
        }
    }
}

TEST(JsApiContract, HasNoDuplicateTypesOrMembers) {
    std::set<std::string> type_names;
    std::set<std::string> qualified_members;

    for (std::size_t type_index = 0; type_index < js_api_contract_type_count(); ++type_index) {
        const JsApiType &type = js_api_contract_types()[type_index];
        EXPECT_TRUE(type_names.insert(type.name).second) << type.name;

        std::set<std::string> member_names;
        for (std::size_t member_index = 0; member_index < type.member_count; ++member_index) {
            const JsApiMember &member = type.members[member_index];
            EXPECT_TRUE(member_names.insert(member.name).second) << type.name << "." << member.name;
            EXPECT_TRUE(qualified_members.insert(std::string(type.name) + "." + member.name).second)
                << type.name << "." << member.name;
        }
    }
}

TEST(JsApiContract, DoesNotExposeRawCppPointersOrInternalStructs) {
    for (std::size_t type_index = 0; type_index < js_api_contract_type_count(); ++type_index) {
        const JsApiType &type = js_api_contract_types()[type_index];
        EXPECT_FALSE(contains_raw_cpp_name(type.name)) << type.name;
        EXPECT_FALSE(contains_raw_cpp_name(type.docs)) << type.name;

        for (std::size_t member_index = 0; member_index < type.member_count; ++member_index) {
            const JsApiMember &member = type.members[member_index];
            EXPECT_FALSE(contains_raw_cpp_name(member.type_name))
                << type.name << "." << member.name;
            EXPECT_FALSE(contains_raw_cpp_name(member.return_type))
                << type.name << "." << member.name;
            EXPECT_FALSE(contains_raw_cpp_name(member.docs)) << type.name << "." << member.name;
        }
    }
}

TEST(JsApiContract, KeepsSideEffectApisDeferredOrUnsupported) {
    for (std::size_t type_index = 0; type_index < js_api_contract_type_count(); ++type_index) {
        const JsApiType &type = js_api_contract_types()[type_index];
        for (std::size_t member_index = 0; member_index < type.member_count; ++member_index) {
            const JsApiMember &member = type.members[member_index];
            if (member.side_effect == JsApiSideEffect::None)
                continue;

            EXPECT_TRUE(member.status == JsApiMemberStatus::Deferred ||
                        member.status == JsApiMemberStatus::Unsupported)
                << type.name << "." << member.name;
            EXPECT_STRNE(member.permission, "read-only") << type.name << "." << member.name;
        }
    }
}

TEST(JsApiContract, ModelsNullableAndLiveHandleFields) {
    for (std::size_t type_index = 0; type_index < js_api_contract_type_count(); ++type_index) {
        const JsApiType &type = js_api_contract_types()[type_index];
        for (std::size_t member_index = 0; member_index < type.member_count; ++member_index) {
            const JsApiMember &member = type.members[member_index];
            const std::string type_name = member.type_name;
            if (member.nullable) {
                EXPECT_NE(type_name.find("null"), std::string::npos)
                    << type.name << "." << member.name;
            }
            if (member.requires_live_handle) {
                EXPECT_TRUE(type.kind == JsApiTypeKind::Interface ||
                            type.kind == JsApiTypeKind::Namespace)
                    << type.name << "." << member.name;
            }
        }
    }
}

TEST(JsApiContract, CoversManifestContextFields) {
    const JsApiType *context = find_js_api_contract_type("ScriptContext");
    ASSERT_NE(context, nullptr);

    for (std::size_t index = 0; index < js_scripting_manifest_entry_count(); ++index) {
        const JsScriptingManifestEntry &entry = js_scripting_manifest_entries()[index];
        for (const std::string &field : split_context_fields(entry.context_fields)) {
            EXPECT_NE(find_js_api_contract_member(*context, field.c_str()), nullptr)
                << entry.legacy_name << " missing " << field;
        }
    }
}

TEST(JsApiContract, MarksActorControlledTextAsSanitized) {
    const JsApiType *context = find_js_api_contract_type("ScriptContext");
    ASSERT_NE(context, nullptr);

    const JsApiMember *text = find_js_api_contract_member(*context, "text");
    ASSERT_NE(text, nullptr);
    EXPECT_TRUE(text->nullable);
    EXPECT_NE(std::string(text->docs).find("Sanitized"), std::string::npos);

    const JsApiMember *args = find_js_api_contract_member(*context, "args");
    ASSERT_NE(args, nullptr);
    EXPECT_TRUE(args->nullable);
    EXPECT_NE(std::string(args->docs).find("Sanitized"), std::string::npos);
}

TEST(JsApiContract, FindsTypesAndMembersByName) {
    const JsApiType *character = find_js_api_contract_type("Character");
    ASSERT_NE(character, nullptr);

    const JsApiMember *name = find_js_api_contract_member(*character, "name");
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(name->kind, JsApiMemberKind::Property);
    EXPECT_STREQ(name->type_name, "string");

    const JsApiMember *experience = find_js_api_contract_member(*character, "experience");
    ASSERT_NE(experience, nullptr);
    EXPECT_EQ(experience->kind, JsApiMemberKind::Property);
    EXPECT_STREQ(experience->type_name, "number");
    EXPECT_EQ(experience->status, JsApiMemberStatus::PlannedReadOnly);
    EXPECT_NE(std::string(experience->docs).find("CHx_EXP"), std::string::npos);

    const JsApiMember *rank = find_js_api_contract_member(*character, "rank");
    ASSERT_NE(rank, nullptr);
    EXPECT_EQ(rank->kind, JsApiMemberKind::Property);
    EXPECT_STREQ(rank->type_name, "number");
    EXPECT_EQ(rank->status, JsApiMemberStatus::PlannedReadOnly);
    EXPECT_NE(std::string(rank->docs).find("CHx_RANK"), std::string::npos);

    for (const char *member_name :
         {"classPoints",      "interruptCount",  "interruptTime", "specialBusy", "baseAbilities",
          "currentAbilities", "rolledAbilities", "points",        "specials",    "specials2",
          "professions",      "specializations", "damageDetails", "skills",      "knowledge",
          "affects",          "equipment",       "inventory",     "followers",   "master"}) {
        const JsApiMember *member = find_js_api_contract_member(*character, member_name);
        ASSERT_NE(member, nullptr) << member_name;
        EXPECT_EQ(member->status, JsApiMemberStatus::PlannedReadOnly) << member_name;
    }
    EXPECT_STREQ(find_js_api_contract_member(*character, "classPoints")->type_name, "number");
    EXPECT_STREQ(find_js_api_contract_member(*character, "interruptCount")->type_name, "number");
    EXPECT_STREQ(find_js_api_contract_member(*character, "interruptTime")->type_name, "number");
    EXPECT_STREQ(find_js_api_contract_member(*character, "specialBusy")->type_name, "boolean");
    EXPECT_STREQ(find_js_api_contract_member(*character, "baseAbilities")->type_name,
                 "AbilityScores");
    EXPECT_STREQ(find_js_api_contract_member(*character, "currentAbilities")->type_name,
                 "AbilityScores");
    EXPECT_STREQ(find_js_api_contract_member(*character, "rolledAbilities")->type_name,
                 "AbilityScores");
    EXPECT_STREQ(find_js_api_contract_member(*character, "points")->type_name, "CharacterPoints");
    EXPECT_STREQ(find_js_api_contract_member(*character, "specials")->type_name,
                 "CharacterSpecials");
    EXPECT_STREQ(find_js_api_contract_member(*character, "specials2")->type_name,
                 "CharacterSpecials2");
    EXPECT_STREQ(find_js_api_contract_member(*character, "professions")->type_name,
                 "readonly Profession[]");
    EXPECT_STREQ(find_js_api_contract_member(*character, "specializations")->type_name,
                 "SpecializationData");
    EXPECT_STREQ(find_js_api_contract_member(*character, "damageDetails")->type_name,
                 "DamageDetails");
    EXPECT_STREQ(find_js_api_contract_member(*character, "skills")->type_name,
                 "readonly SkillValue[]");
    EXPECT_STREQ(find_js_api_contract_member(*character, "knowledge")->type_name,
                 "readonly KnowledgeValue[]");
    EXPECT_STREQ(find_js_api_contract_member(*character, "affects")->type_name,
                 "readonly Affect[]");
    EXPECT_STREQ(find_js_api_contract_member(*character, "equipment")->type_name,
                 "readonly EquipmentSlot[]");

    const JsApiType *ability_scores = find_js_api_contract_type("AbilityScores");
    ASSERT_NE(ability_scores, nullptr);
    for (const char *member_name :
         {"strength", "intelligence", "willpower", "dexterity", "constitution", "leadership"}) {
        const JsApiMember *member = find_js_api_contract_member(*ability_scores, member_name);
        ASSERT_NE(member, nullptr) << member_name;
        EXPECT_STREQ(member->type_name, "number") << member_name;
        EXPECT_EQ(member->status, JsApiMemberStatus::PlannedReadOnly) << member_name;
    }
    const JsApiType *profession = find_js_api_contract_type("Profession");
    ASSERT_NE(profession, nullptr);
    for (const char *member_name :
         {"key", "name", "level", "points", "coefficient", "experience"}) {
        const JsApiMember *member = find_js_api_contract_member(*profession, member_name);
        ASSERT_NE(member, nullptr) << member_name;
        EXPECT_EQ(member->status, JsApiMemberStatus::PlannedReadOnly) << member_name;
    }
    EXPECT_NE(std::string(find_js_api_contract_member(*profession, "key")->type_name).find("mage"),
              std::string::npos);
    const JsApiType *specialization_data = find_js_api_contract_type("SpecializationData");
    ASSERT_NE(specialization_data, nullptr);
    for (const char *member_name :
         {"selectedId", "selectedKey", "selectedName", "currentId", "currentKey", "currentName",
          "isMageSpecialization", "hasRuntimeState"}) {
        const JsApiMember *member = find_js_api_contract_member(*specialization_data, member_name);
        ASSERT_NE(member, nullptr) << member_name;
        EXPECT_EQ(member->status, JsApiMemberStatus::PlannedReadOnly) << member_name;
    }
    const JsApiType *damage_details = find_js_api_contract_type("DamageDetails");
    ASSERT_NE(damage_details, nullptr);
    for (const char *member_name :
         {"elapsedCombatSeconds", "totalDamage", "damagePerSecond", "entries"}) {
        const JsApiMember *member = find_js_api_contract_member(*damage_details, member_name);
        ASSERT_NE(member, nullptr) << member_name;
        EXPECT_EQ(member->status, JsApiMemberStatus::PlannedReadOnly) << member_name;
    }
    EXPECT_STREQ(find_js_api_contract_member(*damage_details, "elapsedCombatSeconds")->type_name,
                 "number");
    EXPECT_STREQ(find_js_api_contract_member(*damage_details, "totalDamage")->type_name, "number");
    EXPECT_STREQ(find_js_api_contract_member(*damage_details, "damagePerSecond")->type_name,
                 "number");
    EXPECT_STREQ(find_js_api_contract_member(*damage_details, "entries")->type_name,
                 "readonly DamageEntry[]");
    const JsApiType *damage_entry = find_js_api_contract_type("DamageEntry");
    ASSERT_NE(damage_entry, nullptr);
    for (const char *member_name :
         {"sourceId", "sourceKind", "sourceName", "instanceCount", "totalDamage", "largestDamage",
          "averageDamage", "percentOfTotal"}) {
        const JsApiMember *member = find_js_api_contract_member(*damage_entry, member_name);
        ASSERT_NE(member, nullptr) << member_name;
        EXPECT_EQ(member->status, JsApiMemberStatus::PlannedReadOnly) << member_name;
    }
    EXPECT_STREQ(find_js_api_contract_member(*damage_entry, "sourceKind")->type_name,
                 "'skill' | 'attack' | 'unknown'");
    EXPECT_STREQ(find_js_api_contract_member(*damage_entry, "sourceId")->type_name, "number");
    EXPECT_STREQ(find_js_api_contract_member(*damage_entry, "sourceName")->type_name, "string");
    EXPECT_STREQ(find_js_api_contract_member(*damage_entry, "instanceCount")->type_name, "number");
    EXPECT_STREQ(find_js_api_contract_member(*damage_entry, "totalDamage")->type_name, "number");
    EXPECT_STREQ(find_js_api_contract_member(*damage_entry, "largestDamage")->type_name, "number");
    EXPECT_STREQ(find_js_api_contract_member(*damage_entry, "averageDamage")->type_name, "number");
    EXPECT_STREQ(find_js_api_contract_member(*damage_entry, "percentOfTotal")->type_name, "number");

    const JsApiType *skill_value = find_js_api_contract_type("SkillValue");
    ASSERT_NE(skill_value, nullptr);
    for (const char *member_name :
         {"id", "name", "profession", "level", "practice", "minimumPosition", "manaCost", "beats",
          "targets", "learnDifficulty", "learnType", "isFast", "specialization"}) {
        const JsApiMember *member = find_js_api_contract_member(*skill_value, member_name);
        ASSERT_NE(member, nullptr) << member_name;
        EXPECT_EQ(member->status, JsApiMemberStatus::PlannedReadOnly) << member_name;
    }
    EXPECT_STREQ(find_js_api_contract_member(*skill_value, "profession")->type_name,
                 "'general' | 'mage' | 'mystic' | 'ranger' | 'warrior' | 'unknown'");
    EXPECT_STREQ(find_js_api_contract_member(*skill_value, "isFast")->type_name, "boolean");
    const JsApiType *knowledge_value = find_js_api_contract_type("KnowledgeValue");
    ASSERT_NE(knowledge_value, nullptr);
    for (const char *member_name :
         {"id", "name", "profession", "level", "knowledge", "minimumPosition", "manaCost", "beats",
          "targets", "learnDifficulty", "learnType", "isFast", "specialization"}) {
        const JsApiMember *member = find_js_api_contract_member(*knowledge_value, member_name);
        ASSERT_NE(member, nullptr) << member_name;
        EXPECT_EQ(member->status, JsApiMemberStatus::PlannedReadOnly) << member_name;
    }
    EXPECT_STREQ(find_js_api_contract_member(*knowledge_value, "profession")->type_name,
                 "'general' | 'mage' | 'mystic' | 'ranger' | 'warrior' | 'unknown'");
    EXPECT_STREQ(find_js_api_contract_member(*knowledge_value, "knowledge")->type_name, "number");
    const JsApiType *affect = find_js_api_contract_type("Affect");
    ASSERT_NE(affect, nullptr);
    for (const char *member_name : {"type", "name", "duration", "timePhase", "modifier", "location",
                                    "locationName", "bitvector", "bitvectorNames", "counter"}) {
        const JsApiMember *member = find_js_api_contract_member(*affect, member_name);
        ASSERT_NE(member, nullptr) << member_name;
        EXPECT_EQ(member->status, JsApiMemberStatus::PlannedReadOnly) << member_name;
    }
    EXPECT_STREQ(find_js_api_contract_member(*affect, "bitvectorNames")->type_name,
                 "readonly string[]");
    const JsApiType *object_affect = find_js_api_contract_type("ObjectAffect");
    ASSERT_NE(object_affect, nullptr);
    for (const char *member_name : {"slotIndex", "location", "locationName", "modifier"}) {
        const JsApiMember *member = find_js_api_contract_member(*object_affect, member_name);
        ASSERT_NE(member, nullptr) << member_name;
        EXPECT_EQ(member->status, JsApiMemberStatus::PlannedReadOnly) << member_name;
    }
    EXPECT_STREQ(find_js_api_contract_member(*object_affect, "slotIndex")->type_name, "number");
    EXPECT_STREQ(find_js_api_contract_member(*object_affect, "location")->type_name, "number");
    EXPECT_STREQ(find_js_api_contract_member(*object_affect, "locationName")->type_name, "string");
    EXPECT_STREQ(find_js_api_contract_member(*object_affect, "modifier")->type_name, "number");
    const JsApiType *extra_description = find_js_api_contract_type("ExtraDescription");
    ASSERT_NE(extra_description, nullptr);
    for (const char *member_name : {"keyword", "description"}) {
        const JsApiMember *member = find_js_api_contract_member(*extra_description, member_name);
        ASSERT_NE(member, nullptr) << member_name;
        EXPECT_EQ(member->status, JsApiMemberStatus::PlannedReadOnly) << member_name;
        EXPECT_STREQ(member->type_name, "string") << member_name;
    }

    const JsApiMember *character_equipment = find_js_api_contract_member(*character, "equipment");
    ASSERT_NE(character_equipment, nullptr);
    EXPECT_STREQ(character_equipment->type_name, "readonly EquipmentSlot[]");
    EXPECT_EQ(character_equipment->status, JsApiMemberStatus::PlannedReadOnly);
    const JsApiType *equipment_slot = find_js_api_contract_type("EquipmentSlot");
    ASSERT_NE(equipment_slot, nullptr);
    for (const char *member_name : {"slotIndex", "slotName", "object"}) {
        const JsApiMember *member = find_js_api_contract_member(*equipment_slot, member_name);
        ASSERT_NE(member, nullptr) << member_name;
        EXPECT_EQ(member->status, JsApiMemberStatus::PlannedReadOnly) << member_name;
    }
    EXPECT_STREQ(find_js_api_contract_member(*equipment_slot, "slotIndex")->type_name, "number");
    EXPECT_STREQ(find_js_api_contract_member(*equipment_slot, "slotName")->type_name, "string");
    EXPECT_STREQ(find_js_api_contract_member(*equipment_slot, "object")->type_name,
                 "EquipmentObjectSnapshot | null");
    EXPECT_TRUE(find_js_api_contract_member(*equipment_slot, "object")->nullable);
    const JsApiType *equipment_object = find_js_api_contract_type("EquipmentObjectSnapshot");
    ASSERT_NE(equipment_object, nullptr);
    for (const char *member_name :
         {"id", "name", "description", "shortDescription", "actionDescription", "vnum", "flags",
          "affects", "extraDescriptions", "room", "carriedBy", "wornBy", "isValid"}) {
        const JsApiMember *member = find_js_api_contract_member(*equipment_object, member_name);
        ASSERT_NE(member, nullptr) << member_name;
        EXPECT_NE(member->name, std::string("setName"));
        EXPECT_EQ(member->status == JsApiMemberStatus::PlannedReadOnly ||
                      member->status == JsApiMemberStatus::PlannedPureHelper,
                  true)
            << member_name;
    }
    EXPECT_STREQ(find_js_api_contract_member(*equipment_object, "affects")->type_name,
                 "readonly ObjectAffect[]");
    EXPECT_STREQ(find_js_api_contract_member(*equipment_object, "extraDescriptions")->type_name,
                 "readonly ExtraDescription[]");
    EXPECT_STREQ(find_js_api_contract_member(*equipment_object, "room")->type_name, "null");
    EXPECT_STREQ(find_js_api_contract_member(*equipment_object, "carriedBy")->type_name, "null");
    EXPECT_STREQ(find_js_api_contract_member(*equipment_object, "wornBy")->type_name, "null");
    const JsApiMember *character_inventory = find_js_api_contract_member(*character, "inventory");
    ASSERT_NE(character_inventory, nullptr);
    EXPECT_STREQ(character_inventory->type_name, "readonly InventoryObjectSnapshot[]");
    EXPECT_EQ(character_inventory->status, JsApiMemberStatus::PlannedReadOnly);
    const JsApiType *inventory_object = find_js_api_contract_type("InventoryObjectSnapshot");
    ASSERT_NE(inventory_object, nullptr);
    EXPECT_NE(find_js_api_contract_member(*inventory_object, "flags"), nullptr);
    EXPECT_STREQ(find_js_api_contract_member(*inventory_object, "affects")->type_name,
                 "readonly ObjectAffect[]");
    EXPECT_STREQ(find_js_api_contract_member(*inventory_object, "extraDescriptions")->type_name,
                 "readonly ExtraDescription[]");
    EXPECT_STREQ(find_js_api_contract_member(*inventory_object, "room")->type_name, "null");
    EXPECT_STREQ(find_js_api_contract_member(*inventory_object, "carriedBy")->type_name, "null");
    EXPECT_STREQ(find_js_api_contract_member(*inventory_object, "wornBy")->type_name, "null");
    const JsApiMember *character_followers = find_js_api_contract_member(*character, "followers");
    ASSERT_NE(character_followers, nullptr);
    EXPECT_STREQ(character_followers->type_name, "readonly CharacterRelationshipSnapshot[]");
    EXPECT_FALSE(character_followers->nullable);
    EXPECT_EQ(character_followers->status, JsApiMemberStatus::PlannedReadOnly);
    const JsApiMember *character_master = find_js_api_contract_member(*character, "master");
    ASSERT_NE(character_master, nullptr);
    EXPECT_STREQ(character_master->type_name, "CharacterRelationshipSnapshot | null");
    EXPECT_TRUE(character_master->nullable);
    EXPECT_EQ(character_master->status, JsApiMemberStatus::PlannedReadOnly);
    const JsApiMember *character_mount = find_js_api_contract_member(*character, "mount");
    ASSERT_NE(character_mount, nullptr);
    EXPECT_STREQ(character_mount->type_name, "MountData");
    EXPECT_FALSE(character_mount->nullable);
    EXPECT_EQ(character_mount->status, JsApiMemberStatus::PlannedReadOnly);
    const JsApiMember *character_profile = find_js_api_contract_member(*character, "profile");
    ASSERT_NE(character_profile, nullptr);
    EXPECT_STREQ(character_profile->type_name, "CharacterProfile");
    EXPECT_FALSE(character_profile->nullable);
    EXPECT_EQ(character_profile->status, JsApiMemberStatus::PlannedReadOnly);
    EXPECT_EQ(find_js_api_contract_member(*character, "group"), nullptr);
    const JsApiType *profile = find_js_api_contract_type("CharacterProfile");
    ASSERT_NE(profile, nullptr);
    const auto expect_profile_member_type = [&](const char *member_name, const char *type_name) {
        const JsApiMember *member = find_js_api_contract_member(*profile, member_name);
        ASSERT_NE(member, nullptr) << member_name;
        EXPECT_STREQ(member->type_name, type_name) << member_name;
    };
    expect_profile_member_type("name", "string");
    expect_profile_member_type("shortDescription", "string");
    expect_profile_member_type("longDescription", "string | null");
    expect_profile_member_type("description", "string | null");
    expect_profile_member_type("title", "string | null");
    expect_profile_member_type("deathCry", "string | null");
    expect_profile_member_type("deathCry2", "string | null");
    expect_profile_member_type("corpseNumber", "number");
    expect_profile_member_type("raceId", "number");
    expect_profile_member_type("sex", "number");
    expect_profile_member_type("bodyType", "number");
    expect_profile_member_type("profession", "number");
    expect_profile_member_type("level", "number");
    expect_profile_member_type("language", "number");
    expect_profile_member_type("hometown", "number");
    expect_profile_member_type("birthEpochSeconds", "number");
    expect_profile_member_type("logonEpochSeconds", "number");
    expect_profile_member_type("playedSeconds", "number");
    expect_profile_member_type("weight", "number");
    expect_profile_member_type("height", "number");
    expect_profile_member_type("ranking", "number");
    expect_profile_member_type("talks", "readonly number[]");
    const JsApiType *relationship = find_js_api_contract_type("CharacterRelationshipSnapshot");
    ASSERT_NE(relationship, nullptr);
    const JsApiMember *relationship_id = find_js_api_contract_member(*relationship, "id");
    ASSERT_NE(relationship_id, nullptr);
    EXPECT_STREQ(relationship_id->type_name, "string");
    EXPECT_EQ(relationship_id->status, JsApiMemberStatus::PlannedReadOnly);
    EXPECT_EQ(relationship_id->kind, JsApiMemberKind::Property);
    const JsApiMember *relationship_name = find_js_api_contract_member(*relationship, "name");
    ASSERT_NE(relationship_name, nullptr);
    EXPECT_STREQ(relationship_name->type_name, "string");
    const JsApiMember *relationship_race = find_js_api_contract_member(*relationship, "race");
    ASSERT_NE(relationship_race, nullptr);
    EXPECT_STREQ(relationship_race->type_name, "string");
    const JsApiMember *relationship_vnum = find_js_api_contract_member(*relationship, "vnum");
    ASSERT_NE(relationship_vnum, nullptr);
    EXPECT_STREQ(relationship_vnum->type_name, "number | null");
    EXPECT_TRUE(relationship_vnum->nullable);
    const JsApiMember *relationship_prototype_vnum =
        find_js_api_contract_member(*relationship, "prototypeVnum");
    ASSERT_NE(relationship_prototype_vnum, nullptr);
    EXPECT_STREQ(relationship_prototype_vnum->type_name, "number | null");
    EXPECT_TRUE(relationship_prototype_vnum->nullable);
    const JsApiMember *relationship_level = find_js_api_contract_member(*relationship, "level");
    ASSERT_NE(relationship_level, nullptr);
    EXPECT_STREQ(relationship_level->type_name, "number");
    const JsApiMember *relationship_is_npc = find_js_api_contract_member(*relationship, "isNpc");
    ASSERT_NE(relationship_is_npc, nullptr);
    EXPECT_STREQ(relationship_is_npc->type_name, "boolean");
    const JsApiMember *relationship_is_player =
        find_js_api_contract_member(*relationship, "isPlayer");
    ASSERT_NE(relationship_is_player, nullptr);
    EXPECT_STREQ(relationship_is_player->type_name, "boolean");
    const JsApiMember *relationship_is_valid =
        find_js_api_contract_member(*relationship, "isValid");
    ASSERT_NE(relationship_is_valid, nullptr);
    EXPECT_STREQ(relationship_is_valid->type_name, "() => boolean");
    EXPECT_EQ(relationship_is_valid->kind, JsApiMemberKind::Method);
    EXPECT_EQ(relationship_is_valid->status, JsApiMemberStatus::PlannedPureHelper);
    const JsApiType *mount_data = find_js_api_contract_type("MountData");
    ASSERT_NE(mount_data, nullptr);
    EXPECT_STREQ(find_js_api_contract_member(*mount_data, "mount")->type_name,
                 "CharacterRelationshipSnapshot | null");
    EXPECT_STREQ(find_js_api_contract_member(*mount_data, "rider")->type_name,
                 "CharacterRelationshipSnapshot | null");
    EXPECT_STREQ(find_js_api_contract_member(*mount_data, "nextRider")->type_name,
                 "CharacterRelationshipSnapshot | null");
    EXPECT_STREQ(find_js_api_contract_member(*mount_data, "isRiding")->type_name, "boolean");
    EXPECT_STREQ(find_js_api_contract_member(*mount_data, "isMounted")->type_name, "boolean");
    const JsApiType *mob = find_js_api_contract_type("Mob");
    ASSERT_NE(mob, nullptr);
    const JsApiMember *prototype_vnum = find_js_api_contract_member(*mob, "prototypeVnum");
    ASSERT_NE(prototype_vnum, nullptr);
    EXPECT_STREQ(prototype_vnum->type_name, "number | null");
    EXPECT_TRUE(prototype_vnum->nullable);
    EXPECT_EQ(prototype_vnum->status, JsApiMemberStatus::PlannedReadOnly);

    const JsApiType *room = find_js_api_contract_type("Room");
    ASSERT_NE(room, nullptr);
    const JsApiMember *room_description = find_js_api_contract_member(*room, "description");
    ASSERT_NE(room_description, nullptr);
    EXPECT_STREQ(room_description->type_name, "string");
    EXPECT_EQ(room_description->status, JsApiMemberStatus::PlannedReadOnly);

    const JsApiMember *room_level = find_js_api_contract_member(*room, "level");
    ASSERT_NE(room_level, nullptr);
    EXPECT_STREQ(room_level->type_name, "number");
    EXPECT_EQ(room_level->status, JsApiMemberStatus::PlannedReadOnly);

    const JsApiMember *room_sector_type = find_js_api_contract_member(*room, "sectorType");
    ASSERT_NE(room_sector_type, nullptr);
    EXPECT_STREQ(room_sector_type->type_name, "string");
    EXPECT_EQ(room_sector_type->status, JsApiMemberStatus::PlannedReadOnly);

    const JsApiMember *room_flags = find_js_api_contract_member(*room, "flags");
    ASSERT_NE(room_flags, nullptr);
    EXPECT_STREQ(room_flags->type_name, "readonly string[]");
    EXPECT_EQ(room_flags->status, JsApiMemberStatus::PlannedReadOnly);
    EXPECT_NE(std::string(room_flags->docs).find("BFS_MARK"), std::string::npos);
    const JsApiMember *room_extra_descriptions =
        find_js_api_contract_member(*room, "extraDescriptions");
    ASSERT_NE(room_extra_descriptions, nullptr);
    EXPECT_STREQ(room_extra_descriptions->type_name, "readonly ExtraDescription[]");
    EXPECT_EQ(room_extra_descriptions->status, JsApiMemberStatus::PlannedReadOnly);

    const JsApiMember *room_exits = find_js_api_contract_member(*room, "exits");
    ASSERT_NE(room_exits, nullptr);
    EXPECT_STREQ(room_exits->type_name, "readonly RoomExit[]");
    EXPECT_EQ(room_exits->status, JsApiMemberStatus::PlannedReadOnly);

    const JsApiType *room_exit = find_js_api_contract_type("RoomExit");
    ASSERT_NE(room_exit, nullptr);
    EXPECT_STREQ(find_js_api_contract_member(*room_exit, "directionIndex")->type_name, "number");
    EXPECT_STREQ(find_js_api_contract_member(*room_exit, "direction")->type_name, "string");
    EXPECT_STREQ(find_js_api_contract_member(*room_exit, "toRoomVnum")->type_name, "number | null");
    EXPECT_STREQ(find_js_api_contract_member(*room_exit, "keyword")->type_name, "string");
    EXPECT_STREQ(find_js_api_contract_member(*room_exit, "description")->type_name, "string");
    EXPECT_STREQ(find_js_api_contract_member(*room_exit, "keyVnum")->type_name, "number");
    EXPECT_STREQ(find_js_api_contract_member(*room_exit, "width")->type_name, "number");
    EXPECT_STREQ(find_js_api_contract_member(*room_exit, "flags")->type_name, "readonly string[]");

    const JsApiMember *room_alignment = find_js_api_contract_member(*room, "alignment");
    ASSERT_NE(room_alignment, nullptr);
    EXPECT_STREQ(room_alignment->type_name, "number");
    EXPECT_EQ(room_alignment->status, JsApiMemberStatus::PlannedReadOnly);

    const JsApiMember *room_light = find_js_api_contract_member(*room, "light");
    ASSERT_NE(room_light, nullptr);
    EXPECT_STREQ(room_light->type_name, "number");
    EXPECT_EQ(room_light->status, JsApiMemberStatus::PlannedReadOnly);

    const JsApiMember *is_sunlit = find_js_api_contract_member(*room, "isSunlit");
    ASSERT_NE(is_sunlit, nullptr);
    EXPECT_STREQ(is_sunlit->type_name, "boolean");
    EXPECT_FALSE(is_sunlit->nullable);
    EXPECT_EQ(is_sunlit->status, JsApiMemberStatus::PlannedReadOnly);
    EXPECT_NE(std::string(is_sunlit->docs).find("SCRIPT_IF_ROOM_SUNLIT"), std::string::npos);

    const JsApiType *object = find_js_api_contract_type("GameObject");
    ASSERT_NE(object, nullptr);
    const JsApiMember *object_description = find_js_api_contract_member(*object, "description");
    ASSERT_NE(object_description, nullptr);
    EXPECT_STREQ(object_description->type_name, "string");

    const JsApiMember *object_short_description =
        find_js_api_contract_member(*object, "shortDescription");
    ASSERT_NE(object_short_description, nullptr);
    EXPECT_STREQ(object_short_description->type_name, "string");

    const JsApiMember *object_action_description =
        find_js_api_contract_member(*object, "actionDescription");
    ASSERT_NE(object_action_description, nullptr);
    EXPECT_STREQ(object_action_description->type_name, "string | null");
    EXPECT_TRUE(object_action_description->nullable);

    const JsApiMember *object_flags = find_js_api_contract_member(*object, "flags");
    ASSERT_NE(object_flags, nullptr);
    EXPECT_STREQ(object_flags->type_name, "ObjectFlags");
    EXPECT_EQ(object_flags->status, JsApiMemberStatus::PlannedReadOnly);

    const JsApiType *object_flags_type = find_js_api_contract_type("ObjectFlags");
    ASSERT_NE(object_flags_type, nullptr);
    const JsApiMember *object_type = find_js_api_contract_member(*object_flags_type, "itemType");
    ASSERT_NE(object_type, nullptr);
    EXPECT_STREQ(object_type->type_name, "string");
    for (const char *member_name :
         {"values",        "rawValues",    "value",       "value0",      "value1",
          "value2",        "value3",       "value4",      "typeFlag",    "type_flag",
          "wearBits",      "wear_flags",   "extraBits",   "extra_flags", "bitvector",
          "butcherItem",   "butcher_item", "progNumber",  "prog_number", "scriptNumber",
          "script_number", "scriptInfo",   "script_info", "poisoned",    "poisonData",
          "poisondata",    "poison_data",  "rawMaterial"}) {
        EXPECT_EQ(find_js_api_contract_member(*object_flags_type, member_name), nullptr)
            << member_name;
    }
    const JsApiMember *object_material =
        find_js_api_contract_member(*object_flags_type, "material");
    ASSERT_NE(object_material, nullptr);
    EXPECT_STREQ(object_material->type_name, "string");

    const JsApiMember *object_extra_descriptions =
        find_js_api_contract_member(*object, "extraDescriptions");
    ASSERT_NE(object_extra_descriptions, nullptr);
    EXPECT_STREQ(object_extra_descriptions->type_name, "readonly ExtraDescription[]");
    EXPECT_EQ(object_extra_descriptions->status, JsApiMemberStatus::PlannedReadOnly);
    const JsApiMember *object_container = find_js_api_contract_member(*object, "container");
    ASSERT_NE(object_container, nullptr);
    EXPECT_STREQ(object_container->type_name, "EquipmentObjectSnapshot | null");
    EXPECT_TRUE(object_container->nullable);
    EXPECT_EQ(object_container->status, JsApiMemberStatus::PlannedReadOnly);
    const JsApiMember *object_contents = find_js_api_contract_member(*object, "contents");
    ASSERT_NE(object_contents, nullptr);
    EXPECT_STREQ(object_contents->type_name, "readonly EquipmentObjectSnapshot[]");
    EXPECT_FALSE(object_contents->nullable);
    EXPECT_EQ(object_contents->status, JsApiMemberStatus::PlannedReadOnly);
    const JsApiMember *object_touched = find_js_api_contract_member(*object, "touched");
    ASSERT_NE(object_touched, nullptr);
    EXPECT_STREQ(object_touched->type_name, "boolean");
    EXPECT_FALSE(object_touched->nullable);
    EXPECT_EQ(object_touched->status, JsApiMemberStatus::PlannedReadOnly);
    const JsApiType *equipment_snapshot = find_js_api_contract_type("EquipmentObjectSnapshot");
    ASSERT_NE(equipment_snapshot, nullptr);
    const JsApiMember *equipment_object_touched =
        find_js_api_contract_member(*equipment_snapshot, "touched");
    ASSERT_NE(equipment_object_touched, nullptr);
    EXPECT_STREQ(equipment_object_touched->type_name, "boolean");
    EXPECT_EQ(equipment_object_touched->status, JsApiMemberStatus::PlannedReadOnly);
    const JsApiType *inventory_snapshot = find_js_api_contract_type("InventoryObjectSnapshot");
    ASSERT_NE(inventory_snapshot, nullptr);
    const JsApiMember *inventory_object_touched =
        find_js_api_contract_member(*inventory_snapshot, "touched");
    ASSERT_NE(inventory_object_touched, nullptr);
    EXPECT_STREQ(inventory_object_touched->type_name, "boolean");
    EXPECT_EQ(inventory_object_touched->status, JsApiMemberStatus::PlannedReadOnly);

    const char *classification_only_object_members[] = {
        "nextContent", "next", "ownerId", "loadedBy", "values", "value0", "rawValues",
    };
    for (const char *member_name : classification_only_object_members) {
        EXPECT_EQ(find_js_api_contract_member(*object, member_name), nullptr) << member_name;
    }

    const JsApiType *zone = find_js_api_contract_type("Zone");
    ASSERT_NE(zone, nullptr);
    const JsApiMember *zone_level = find_js_api_contract_member(*zone, "level");
    ASSERT_NE(zone_level, nullptr);
    EXPECT_STREQ(zone_level->type_name, "number");

    const char *zone_text_members[] = {"description", "map"};
    for (const char *member_name : zone_text_members) {
        const JsApiMember *member = find_js_api_contract_member(*zone, member_name);
        ASSERT_NE(member, nullptr) << member_name;
        EXPECT_STREQ(member->type_name, "string | null") << member_name;
        EXPECT_TRUE(member->nullable) << member_name;
    }

    const char *zone_number_members[] = {
        "lifespan",  "age",       "topRoomVnum",      "x",         "y", "whitePower",
        "darkPower", "magiPower", "minimumLookLevel", "resetMode",
    };
    for (const char *member_name : zone_number_members) {
        const JsApiMember *member = find_js_api_contract_member(*zone, member_name);
        ASSERT_NE(member, nullptr) << member_name;
        EXPECT_STREQ(member->type_name, "number") << member_name;
        EXPECT_EQ(member->status, JsApiMemberStatus::PlannedReadOnly) << member_name;
    }

    const JsApiMember *zone_symbol = find_js_api_contract_member(*zone, "symbol");
    ASSERT_NE(zone_symbol, nullptr);
    EXPECT_STREQ(zone_symbol->type_name, "string");
    EXPECT_EQ(zone_symbol->status, JsApiMemberStatus::PlannedReadOnly);

    EXPECT_EQ(find_js_api_contract_type("Missing"), nullptr);
    EXPECT_EQ(find_js_api_contract_member(*character, "missing"), nullptr);
}

TEST(JsApiContract, DefinesSetterMutationResultContract) {
    const JsApiType *mutation_result = find_js_api_contract_type("MutationResult");
    ASSERT_NE(mutation_result, nullptr);
    EXPECT_EQ(mutation_result->kind, JsApiTypeKind::Interface);
    EXPECT_NE(std::string(mutation_result->docs).find("does not make any setter callable"),
              std::string::npos);

    struct ExpectedMember {
        const char *name;
        const char *type_name;
        bool nullable;
    };
    const ExpectedMember expected[] = {
        {"ok", "boolean", false},
        {"code",
         "'ok' | 'invalid-value' | 'out-of-range' | 'not-authorized' | 'stale-handle' | "
         "'unsupported' | 'deferred'",
         false},
        {"message", "string | null", true},
        {"field", "string | null", true},
    };
    for (const ExpectedMember &item : expected) {
        const JsApiMember *member = find_js_api_contract_member(*mutation_result, item.name);
        ASSERT_NE(member, nullptr) << item.name;
        EXPECT_STREQ(member->type_name, item.type_name) << item.name;
        EXPECT_EQ(member->nullable, item.nullable) << item.name;
        EXPECT_EQ(member->status, JsApiMemberStatus::PlannedReadOnly) << item.name;
        EXPECT_EQ(member->side_effect, JsApiSideEffect::None) << item.name;
    }
}

TEST(JsApiContract, ExposesStableEnumNames) {
    EXPECT_STREQ(js_api_type_kind_name(JsApiTypeKind::Class), "class");
    EXPECT_STREQ(js_api_type_kind_name(JsApiTypeKind::Interface), "interface");
    EXPECT_STREQ(js_api_type_kind_name(JsApiTypeKind::Namespace), "namespace");
    EXPECT_STREQ(js_api_type_kind_name(static_cast<JsApiTypeKind>(999)), "unknown");

    EXPECT_STREQ(js_api_member_kind_name(JsApiMemberKind::Property), "property");
    EXPECT_STREQ(js_api_member_kind_name(JsApiMemberKind::Method), "method");
    EXPECT_STREQ(js_api_member_kind_name(static_cast<JsApiMemberKind>(999)), "unknown");

    EXPECT_STREQ(js_api_member_status_name(JsApiMemberStatus::PlannedReadOnly),
                 "planned-read-only");
    EXPECT_STREQ(js_api_member_status_name(JsApiMemberStatus::PlannedPureHelper),
                 "planned-pure-helper");
    EXPECT_STREQ(js_api_member_status_name(JsApiMemberStatus::Deferred), "deferred");
    EXPECT_STREQ(js_api_member_status_name(JsApiMemberStatus::Unsupported), "unsupported");
    EXPECT_STREQ(js_api_member_status_name(static_cast<JsApiMemberStatus>(999)), "unknown");

    EXPECT_STREQ(js_api_side_effect_name(JsApiSideEffect::None), "none");
    EXPECT_STREQ(js_api_side_effect_name(JsApiSideEffect::Output), "output");
    EXPECT_STREQ(js_api_side_effect_name(JsApiSideEffect::Mutation), "mutation");
    EXPECT_STREQ(js_api_side_effect_name(JsApiSideEffect::WorldMutation), "world-mutation");
    EXPECT_STREQ(js_api_side_effect_name(static_cast<JsApiSideEffect>(999)), "unknown");
}
