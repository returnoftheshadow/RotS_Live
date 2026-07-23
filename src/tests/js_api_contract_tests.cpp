#include "../js_api_contract.h"

#include "../js_scripting_manifest.h"

#include <gtest/gtest.h>

#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> split_context_fields(const char* fields)
{
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

bool contains_raw_cpp_name(const std::string& value)
{
    const char* blocked[] = {
        "char_data",
        "obj_data",
        "room_data",
        "descriptor_data",
        "struct ",
        "*",
        "&",
        "void*",
        "player_data",
        "affected_type",
    };
    for (const char* token : blocked) {
        if (value.find(token) != std::string::npos)
            return true;
    }
    return false;
}

} // namespace

TEST(JsApiContract, ExposesStableMetadataForGeneratedConsumers)
{
    const JsApiContractMetadata& metadata = js_api_contract_metadata();

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

TEST(JsApiContract, ContainsExpectedHandleAndContextTypes)
{
    const char* expected_types[] = {
        "Character",
        "AbilityScores",
        "CharacterPoints",
        "CharacterSpecials",
        "CharacterConditions",
        "CharacterSpecials2",
        "Profession",
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

    for (const char* name : expected_types)
        EXPECT_NE(find_js_api_contract_type(name), nullptr) << name;

    const JsApiType* player = find_js_api_contract_type("Player");
    ASSERT_NE(player, nullptr);
    EXPECT_STREQ(player->extends, "Character");

    const JsApiType* character_points = find_js_api_contract_type("CharacterPoints");
    ASSERT_NE(character_points, nullptr);
    EXPECT_NE(find_js_api_contract_member(*character_points, "bodypartHits"), nullptr);
    EXPECT_NE(find_js_api_contract_member(*character_points, "spellPower"), nullptr);
}

TEST(JsApiContract, DocumentsEveryTypeAndMember)
{
    for (std::size_t type_index = 0; type_index < js_api_contract_type_count(); ++type_index) {
        const JsApiType& type = js_api_contract_types()[type_index];
        EXPECT_NE(type.name, nullptr);
        EXPECT_FALSE(std::string(type.name).empty());
        EXPECT_NE(type.docs, nullptr) << type.name;
        EXPECT_FALSE(std::string(type.docs).empty()) << type.name;
        EXPECT_GT(type.member_count, 0U) << type.name;

        for (std::size_t member_index = 0; member_index < type.member_count; ++member_index) {
            const JsApiMember& member = type.members[member_index];
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

TEST(JsApiContract, HasNoDuplicateTypesOrMembers)
{
    std::set<std::string> type_names;
    std::set<std::string> qualified_members;

    for (std::size_t type_index = 0; type_index < js_api_contract_type_count(); ++type_index) {
        const JsApiType& type = js_api_contract_types()[type_index];
        EXPECT_TRUE(type_names.insert(type.name).second) << type.name;

        std::set<std::string> member_names;
        for (std::size_t member_index = 0; member_index < type.member_count; ++member_index) {
            const JsApiMember& member = type.members[member_index];
            EXPECT_TRUE(member_names.insert(member.name).second) << type.name << "." << member.name;
            EXPECT_TRUE(qualified_members.insert(std::string(type.name) + "." + member.name).second)
                << type.name << "." << member.name;
        }
    }
}

TEST(JsApiContract, DoesNotExposeRawCppPointersOrInternalStructs)
{
    for (std::size_t type_index = 0; type_index < js_api_contract_type_count(); ++type_index) {
        const JsApiType& type = js_api_contract_types()[type_index];
        EXPECT_FALSE(contains_raw_cpp_name(type.name)) << type.name;
        EXPECT_FALSE(contains_raw_cpp_name(type.docs)) << type.name;

        for (std::size_t member_index = 0; member_index < type.member_count; ++member_index) {
            const JsApiMember& member = type.members[member_index];
            EXPECT_FALSE(contains_raw_cpp_name(member.type_name)) << type.name << "." << member.name;
            EXPECT_FALSE(contains_raw_cpp_name(member.return_type)) << type.name << "." << member.name;
            EXPECT_FALSE(contains_raw_cpp_name(member.docs)) << type.name << "." << member.name;
        }
    }
}

TEST(JsApiContract, KeepsSideEffectApisDeferredOrUnsupported)
{
    for (std::size_t type_index = 0; type_index < js_api_contract_type_count(); ++type_index) {
        const JsApiType& type = js_api_contract_types()[type_index];
        for (std::size_t member_index = 0; member_index < type.member_count; ++member_index) {
            const JsApiMember& member = type.members[member_index];
            if (member.side_effect == JsApiSideEffect::None)
                continue;

            EXPECT_TRUE(member.status == JsApiMemberStatus::Deferred
                || member.status == JsApiMemberStatus::Unsupported)
                << type.name << "." << member.name;
            EXPECT_STRNE(member.permission, "read-only") << type.name << "." << member.name;
        }
    }
}

TEST(JsApiContract, ModelsNullableAndLiveHandleFields)
{
    for (std::size_t type_index = 0; type_index < js_api_contract_type_count(); ++type_index) {
        const JsApiType& type = js_api_contract_types()[type_index];
        for (std::size_t member_index = 0; member_index < type.member_count; ++member_index) {
            const JsApiMember& member = type.members[member_index];
            const std::string type_name = member.type_name;
            if (member.nullable) {
                EXPECT_NE(type_name.find("null"), std::string::npos)
                    << type.name << "." << member.name;
            }
            if (member.requires_live_handle) {
                EXPECT_TRUE(type.kind == JsApiTypeKind::Interface || type.kind == JsApiTypeKind::Namespace)
                    << type.name << "." << member.name;
            }
        }
    }
}

TEST(JsApiContract, CoversManifestContextFields)
{
    const JsApiType* context = find_js_api_contract_type("ScriptContext");
    ASSERT_NE(context, nullptr);

    for (std::size_t index = 0; index < js_scripting_manifest_entry_count(); ++index) {
        const JsScriptingManifestEntry& entry = js_scripting_manifest_entries()[index];
        for (const std::string& field : split_context_fields(entry.context_fields)) {
            EXPECT_NE(find_js_api_contract_member(*context, field.c_str()), nullptr)
                << entry.legacy_name << " missing " << field;
        }
    }
}

TEST(JsApiContract, MarksActorControlledTextAsSanitized)
{
    const JsApiType* context = find_js_api_contract_type("ScriptContext");
    ASSERT_NE(context, nullptr);

    const JsApiMember* text = find_js_api_contract_member(*context, "text");
    ASSERT_NE(text, nullptr);
    EXPECT_TRUE(text->nullable);
    EXPECT_NE(std::string(text->docs).find("Sanitized"), std::string::npos);

    const JsApiMember* args = find_js_api_contract_member(*context, "args");
    ASSERT_NE(args, nullptr);
    EXPECT_TRUE(args->nullable);
    EXPECT_NE(std::string(args->docs).find("Sanitized"), std::string::npos);
}

TEST(JsApiContract, FindsTypesAndMembersByName)
{
    const JsApiType* character = find_js_api_contract_type("Character");
    ASSERT_NE(character, nullptr);

    const JsApiMember* name = find_js_api_contract_member(*character, "name");
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(name->kind, JsApiMemberKind::Property);
    EXPECT_STREQ(name->type_name, "string");

    const JsApiMember* experience = find_js_api_contract_member(*character, "experience");
    ASSERT_NE(experience, nullptr);
    EXPECT_EQ(experience->kind, JsApiMemberKind::Property);
    EXPECT_STREQ(experience->type_name, "number");
    EXPECT_EQ(experience->status, JsApiMemberStatus::PlannedReadOnly);
    EXPECT_NE(std::string(experience->docs).find("CHx_EXP"), std::string::npos);

    const JsApiMember* rank = find_js_api_contract_member(*character, "rank");
    ASSERT_NE(rank, nullptr);
    EXPECT_EQ(rank->kind, JsApiMemberKind::Property);
    EXPECT_STREQ(rank->type_name, "number");
    EXPECT_EQ(rank->status, JsApiMemberStatus::PlannedReadOnly);
    EXPECT_NE(std::string(rank->docs).find("CHx_RANK"), std::string::npos);

    for (const char *member_name :
        {"classPoints", "interruptCount", "interruptTime", "specialBusy", "baseAbilities",
            "currentAbilities", "rolledAbilities", "points", "specials", "specials2",
            "professions"}) {
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
    for (const char *member_name : {"key", "name", "level", "points", "coefficient",
             "experience"}) {
        const JsApiMember *member = find_js_api_contract_member(*profession, member_name);
        ASSERT_NE(member, nullptr) << member_name;
        EXPECT_EQ(member->status, JsApiMemberStatus::PlannedReadOnly) << member_name;
    }
    EXPECT_NE(std::string(find_js_api_contract_member(*profession, "key")->type_name).find("mage"),
        std::string::npos);

    const JsApiType* mob = find_js_api_contract_type("Mob");
    ASSERT_NE(mob, nullptr);
    const JsApiMember* prototype_vnum = find_js_api_contract_member(*mob, "prototypeVnum");
    ASSERT_NE(prototype_vnum, nullptr);
    EXPECT_STREQ(prototype_vnum->type_name, "number | null");
    EXPECT_TRUE(prototype_vnum->nullable);
    EXPECT_EQ(prototype_vnum->status, JsApiMemberStatus::PlannedReadOnly);

    const JsApiType* room = find_js_api_contract_type("Room");
    ASSERT_NE(room, nullptr);
    const JsApiMember* room_description = find_js_api_contract_member(*room, "description");
    ASSERT_NE(room_description, nullptr);
    EXPECT_STREQ(room_description->type_name, "string");
    EXPECT_EQ(room_description->status, JsApiMemberStatus::PlannedReadOnly);

    const JsApiMember* room_level = find_js_api_contract_member(*room, "level");
    ASSERT_NE(room_level, nullptr);
    EXPECT_STREQ(room_level->type_name, "number");
    EXPECT_EQ(room_level->status, JsApiMemberStatus::PlannedReadOnly);

    const JsApiMember* room_sector_type = find_js_api_contract_member(*room, "sectorType");
    ASSERT_NE(room_sector_type, nullptr);
    EXPECT_STREQ(room_sector_type->type_name, "string");
    EXPECT_EQ(room_sector_type->status, JsApiMemberStatus::PlannedReadOnly);

    const JsApiMember* room_flags = find_js_api_contract_member(*room, "flags");
    ASSERT_NE(room_flags, nullptr);
    EXPECT_STREQ(room_flags->type_name, "readonly string[]");
    EXPECT_EQ(room_flags->status, JsApiMemberStatus::PlannedReadOnly);
    EXPECT_NE(std::string(room_flags->docs).find("BFS_MARK"), std::string::npos);

    const JsApiMember* room_alignment = find_js_api_contract_member(*room, "alignment");
    ASSERT_NE(room_alignment, nullptr);
    EXPECT_STREQ(room_alignment->type_name, "number");
    EXPECT_EQ(room_alignment->status, JsApiMemberStatus::PlannedReadOnly);

    const JsApiMember* room_light = find_js_api_contract_member(*room, "light");
    ASSERT_NE(room_light, nullptr);
    EXPECT_STREQ(room_light->type_name, "number");
    EXPECT_EQ(room_light->status, JsApiMemberStatus::PlannedReadOnly);

    const JsApiMember* is_sunlit = find_js_api_contract_member(*room, "isSunlit");
    ASSERT_NE(is_sunlit, nullptr);
    EXPECT_STREQ(is_sunlit->type_name, "boolean");
    EXPECT_FALSE(is_sunlit->nullable);
    EXPECT_EQ(is_sunlit->status, JsApiMemberStatus::PlannedReadOnly);
    EXPECT_NE(std::string(is_sunlit->docs).find("SCRIPT_IF_ROOM_SUNLIT"), std::string::npos);

    const JsApiType* object = find_js_api_contract_type("GameObject");
    ASSERT_NE(object, nullptr);
    const JsApiMember* object_description = find_js_api_contract_member(*object, "description");
    ASSERT_NE(object_description, nullptr);
    EXPECT_STREQ(object_description->type_name, "string");

    const JsApiMember* object_short_description =
        find_js_api_contract_member(*object, "shortDescription");
    ASSERT_NE(object_short_description, nullptr);
    EXPECT_STREQ(object_short_description->type_name, "string");

    const JsApiMember* object_action_description =
        find_js_api_contract_member(*object, "actionDescription");
    ASSERT_NE(object_action_description, nullptr);
    EXPECT_STREQ(object_action_description->type_name, "string | null");
    EXPECT_TRUE(object_action_description->nullable);

    const JsApiMember* object_flags = find_js_api_contract_member(*object, "flags");
    ASSERT_NE(object_flags, nullptr);
    EXPECT_STREQ(object_flags->type_name, "ObjectFlags");
    EXPECT_EQ(object_flags->status, JsApiMemberStatus::PlannedReadOnly);

    const JsApiType* object_flags_type = find_js_api_contract_type("ObjectFlags");
    ASSERT_NE(object_flags_type, nullptr);
    const JsApiMember* object_type = find_js_api_contract_member(*object_flags_type, "itemType");
    ASSERT_NE(object_type, nullptr);
    EXPECT_STREQ(object_type->type_name, "string");
    const JsApiMember* object_values = find_js_api_contract_member(*object_flags_type, "values");
    EXPECT_EQ(object_values, nullptr);
    const JsApiMember* object_material = find_js_api_contract_member(*object_flags_type, "material");
    ASSERT_NE(object_material, nullptr);
    EXPECT_STREQ(object_material->type_name, "string");

    const char* classification_only_object_members[] = {
        "affects",
        "extraDescriptions",
        "container",
        "contents",
        "nextContent",
        "next",
        "touched",
        "ownerId",
        "loadedBy",
        "values",
        "value0",
        "rawValues",
    };
    for (const char* member_name : classification_only_object_members) {
        EXPECT_EQ(find_js_api_contract_member(*object, member_name), nullptr) << member_name;
    }

    const JsApiType* zone = find_js_api_contract_type("Zone");
    ASSERT_NE(zone, nullptr);
    const JsApiMember* zone_level = find_js_api_contract_member(*zone, "level");
    ASSERT_NE(zone_level, nullptr);
    EXPECT_STREQ(zone_level->type_name, "number");

    const char* zone_text_members[] = { "description", "map" };
    for (const char* member_name : zone_text_members) {
        const JsApiMember* member = find_js_api_contract_member(*zone, member_name);
        ASSERT_NE(member, nullptr) << member_name;
        EXPECT_STREQ(member->type_name, "string | null") << member_name;
        EXPECT_TRUE(member->nullable) << member_name;
    }

    const char* zone_number_members[] = {
        "lifespan", "age", "topRoomVnum", "x", "y", "whitePower", "darkPower", "magiPower",
        "minimumLookLevel", "resetMode",
    };
    for (const char* member_name : zone_number_members) {
        const JsApiMember* member = find_js_api_contract_member(*zone, member_name);
        ASSERT_NE(member, nullptr) << member_name;
        EXPECT_STREQ(member->type_name, "number") << member_name;
        EXPECT_EQ(member->status, JsApiMemberStatus::PlannedReadOnly) << member_name;
    }

    const JsApiMember* zone_symbol = find_js_api_contract_member(*zone, "symbol");
    ASSERT_NE(zone_symbol, nullptr);
    EXPECT_STREQ(zone_symbol->type_name, "string");
    EXPECT_EQ(zone_symbol->status, JsApiMemberStatus::PlannedReadOnly);

    EXPECT_EQ(find_js_api_contract_type("Missing"), nullptr);
    EXPECT_EQ(find_js_api_contract_member(*character, "missing"), nullptr);
}

TEST(JsApiContract, DefinesSetterMutationResultContract)
{
    const JsApiType* mutation_result = find_js_api_contract_type("MutationResult");
    ASSERT_NE(mutation_result, nullptr);
    EXPECT_EQ(mutation_result->kind, JsApiTypeKind::Interface);
    EXPECT_NE(std::string(mutation_result->docs).find("does not make any setter callable"),
        std::string::npos);

    struct ExpectedMember {
        const char* name;
        const char* type_name;
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
    for (const ExpectedMember& item : expected) {
        const JsApiMember* member = find_js_api_contract_member(*mutation_result, item.name);
        ASSERT_NE(member, nullptr) << item.name;
        EXPECT_STREQ(member->type_name, item.type_name) << item.name;
        EXPECT_EQ(member->nullable, item.nullable) << item.name;
        EXPECT_EQ(member->status, JsApiMemberStatus::PlannedReadOnly) << item.name;
        EXPECT_EQ(member->side_effect, JsApiSideEffect::None) << item.name;
    }
}

TEST(JsApiContract, ExposesStableEnumNames)
{
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
