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
    EXPECT_EQ(metadata.api_revision, 1);
    EXPECT_STREQ(metadata.api_version, "unpublished");
    EXPECT_STREQ(metadata.contract_checksum, "rots-js-api-contract-v1-revision-1");
    EXPECT_STREQ(metadata.generated_typings_version, "unpublished");
    EXPECT_STREQ(metadata.documentation_version, "unpublished");
    EXPECT_STREQ(metadata.minimum_trigger_catalog_revision, "1");
    EXPECT_NE(std::string(metadata.notes).find("pure result helpers"), std::string::npos);
    EXPECT_NE(std::string(metadata.notes).find("side-effect host bindings remain deferred"),
        std::string::npos);
}

TEST(JsApiContract, ContainsExpectedHandleAndContextTypes)
{
    const char* expected_types[] = {
        "Character",
        "Player",
        "Mob",
        "GameObject",
        "Room",
        "Zone",
        "TriggerInfo",
        "ScriptContext",
        "ScriptResult",
        "Script",
    };

    for (const char* name : expected_types)
        EXPECT_NE(find_js_api_contract_type(name), nullptr) << name;

    const JsApiType* player = find_js_api_contract_type("Player");
    ASSERT_NE(player, nullptr);
    EXPECT_STREQ(player->extends, "Character");
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

    const JsApiType* mob = find_js_api_contract_type("Mob");
    ASSERT_NE(mob, nullptr);
    const JsApiMember* prototype_vnum = find_js_api_contract_member(*mob, "prototypeVnum");
    ASSERT_NE(prototype_vnum, nullptr);
    EXPECT_STREQ(prototype_vnum->type_name, "number | null");
    EXPECT_TRUE(prototype_vnum->nullable);
    EXPECT_EQ(prototype_vnum->status, JsApiMemberStatus::PlannedReadOnly);

    const JsApiType* room = find_js_api_contract_type("Room");
    ASSERT_NE(room, nullptr);
    const JsApiMember* is_sunlit = find_js_api_contract_member(*room, "isSunlit");
    ASSERT_NE(is_sunlit, nullptr);
    EXPECT_STREQ(is_sunlit->type_name, "boolean");
    EXPECT_FALSE(is_sunlit->nullable);
    EXPECT_EQ(is_sunlit->status, JsApiMemberStatus::PlannedReadOnly);
    EXPECT_NE(std::string(is_sunlit->docs).find("SCRIPT_IF_ROOM_SUNLIT"), std::string::npos);

    EXPECT_EQ(find_js_api_contract_type("Missing"), nullptr);
    EXPECT_EQ(find_js_api_contract_member(*character, "missing"), nullptr);
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
