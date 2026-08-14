#include "../js_api_contract.h"
#include "../js_api_struct_mapping.h"
#include "../structs.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    return value;
}

std::vector<std::string> split_pipe_list(const char *value) {
    std::vector<std::string> parts;
    std::stringstream stream(value ? value : "");
    std::string part;
    while (std::getline(stream, part, '|')) {
        part = trim(part);
        if (!part.empty())
            parts.push_back(part);
    }
    return parts;
}

JsApiStructOwner owner_from_source_struct(const std::string &source_struct) {
    if (source_struct == "char_data")
        return JsApiStructOwner::CharData;
    if (source_struct == "obj_data")
        return JsApiStructOwner::ObjData;
    if (source_struct == "room_data")
        return JsApiStructOwner::RoomData;
    if (source_struct == "zone_data")
        return JsApiStructOwner::ZoneData;
    ADD_FAILURE() << "Unknown source struct in helper plan: " << source_struct;
    return JsApiStructOwner::CharData;
}

const JsApiDeferredHelperPlan *find_deferred_helper_plan(const std::string &id) {
    for (std::size_t index = 0; index < js_api_deferred_helper_plan_count(); ++index) {
        if (js_api_deferred_helper_plans()[index].id == id)
            return &js_api_deferred_helper_plans()[index];
    }
    return nullptr;
}

std::string strip_line_comment(std::string line) {
    const std::size_t comment = line.find("//");
    if (comment != std::string::npos)
        line.erase(comment);
    return line;
}

std::string strip_inline_block_comments(std::string line) {
    std::size_t start = line.find("/*");
    while (start != std::string::npos) {
        const std::size_t end = line.find("*/", start + 2);
        if (end == std::string::npos) {
            line.erase(start);
            break;
        }
        line.erase(start, end - start + 2);
        start = line.find("/*", start);
    }
    return line;
}

std::vector<std::string> split_commas(const std::string &value) {
    std::vector<std::string> parts;
    std::stringstream stream(value);
    std::string part;
    while (std::getline(stream, part, ','))
        parts.push_back(part);
    return parts;
}

std::string field_name_from_declarator(std::string declarator) {
    const std::size_t equals = declarator.find('=');
    if (equals != std::string::npos)
        declarator.erase(equals);
    const std::size_t bracket = declarator.find('[');
    if (bracket != std::string::npos)
        declarator.erase(bracket);
    declarator = trim(declarator);
    std::size_t end = declarator.size();
    while (end > 0 && !std::isalnum(static_cast<unsigned char>(declarator[end - 1])) &&
           declarator[end - 1] != '_')
        --end;
    std::size_t start = end;
    while (start > 0 && (std::isalnum(static_cast<unsigned char>(declarator[start - 1])) ||
                         declarator[start - 1] == '_'))
        --start;
    return declarator.substr(start, end - start);
}

std::set<std::string> source_public_fields(const char *path, const char *struct_name) {
    std::ifstream input(path);
    EXPECT_TRUE(input.is_open()) << path;

    std::set<std::string> fields;
    bool in_struct = false;
    std::string line;
    const std::string marker = std::string("struct ") + struct_name + " {";
    while (std::getline(input, line)) {
        if (!in_struct) {
            if (trim(line) == marker)
                in_struct = true;
            continue;
        }
        if (line.find("};") != std::string::npos)
            break;

        line = trim(strip_line_comment(strip_inline_block_comments(line)));
        if (line.empty() || line == "public:" || line.find(';') == std::string::npos)
            continue;
        if (line.find("(*funct)") != std::string::npos) {
            fields.insert("funct");
            continue;
        }
        if (line.find('(') != std::string::npos || line.find("static ") == 0)
            continue;

        line.erase(line.find(';'));
        for (const std::string &part : split_commas(line)) {
            const std::string name = field_name_from_declarator(part);
            if (!name.empty())
                fields.insert(name);
        }
    }
    return fields;
}

std::set<std::string> catalog_fields(JsApiStructOwner owner) {
    std::set<std::string> fields;
    for (std::size_t index = 0; index < js_api_struct_field_mapping_count(); ++index) {
        const JsApiStructFieldMapping &mapping = js_api_struct_field_mappings()[index];
        if (mapping.owner == owner)
            fields.insert(mapping.source_field);
    }
    return fields;
}

bool is_identifier(const char *value) {
    const std::string text(value ? value : "");
    if (text.empty() || !(std::isalpha(static_cast<unsigned char>(text[0])) || text[0] == '_'))
        return false;
    for (char ch : text) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'))
            return false;
    }
    return true;
}

bool is_allowed_status(const char *value) {
    const std::set<std::string> allowed = {
        "implemented-read-only-getter",
        "planned-read-only-getter",
        "planned-validated-setter",
        "implemented-validated-setter",
        "deferred",
        "internal-only",
        "unsupported",
    };
    return allowed.count(value ? value : "") != 0;
}

bool is_allowed_side_effect(const char *value) {
    const std::set<std::string> allowed = {
        "none",
        "mutation",
        "world-mutation",
    };
    return allowed.count(value ? value : "") != 0;
}

void expect_field(JsApiStructOwner owner, const char *field) {
    const JsApiStructFieldMapping *mapping = find_js_api_struct_field_mapping(owner, field);
    ASSERT_NE(mapping, nullptr) << js_api_struct_owner_name(owner) << "." << field;
    EXPECT_STREQ(mapping->source_struct, js_api_struct_owner_name(owner));
    EXPECT_TRUE(is_identifier(mapping->js_property)) << mapping->js_property;
    EXPECT_TRUE(is_identifier(mapping->getter_name)) << mapping->getter_name;
    EXPECT_TRUE(is_identifier(mapping->setter_name)) << mapping->setter_name;
    EXPECT_STRNE(mapping->type_name, "");
    EXPECT_TRUE(is_allowed_status(mapping->getter_status)) << mapping->getter_status;
    EXPECT_TRUE(is_allowed_status(mapping->setter_status)) << mapping->setter_status;
    EXPECT_GT(std::string(mapping->getter_docs).size(), 24U);
    EXPECT_GT(std::string(mapping->setter_docs).size(), 24U);
    EXPECT_TRUE(is_allowed_side_effect(mapping->side_effect)) << mapping->side_effect;
}

} // namespace

TEST(JsApiStructMapping, CoversEveryPublicTopLevelStructField) {
    const char *char_fields[] = {
        "abs_number",
        "player_index",
        "nr",
        "in_room",
        "player",
        "abilities",
        "tmpabilities",
        "constabilities",
        "points",
        "specials",
        "specials2",
        "profs",
        "extra_specialization_data",
        "damage_details",
        "skills",
        "knowledge",
        "affected",
        "equipment",
        "carrying",
        "desc",
        "next_in_room",
        "next",
        "next_fighting",
        "next_fast_update",
        "followers",
        "master",
        "master_number",
        "mount_data",
        "group",
        "temp",
        "delay",
        "next_die",
        "classpoints",
        "interrupt_count",
        "interrupt_time",
        "spec_busy",
    };
    const char *object_fields[] = {
        "item_number",
        "in_room",
        "obj_flags",
        "affected",
        "name",
        "description",
        "short_description",
        "action_description",
        "ex_description",
        "carried_by",
        "owner",
        "in_obj",
        "contains",
        "next_content",
        "next",
        "touched",
        "loaded_by",
    };
    const char *room_fields[] = {
        "number",      "zone",           "level",      "sector_type", "name",
        "description", "ex_description", "dir_option", "room_track",  "room_flags",
        "alignment",   "light",          "bfs_dir",    "bfs_next",    "funct",
        "contents",    "people",         "affected",   "bleed_track",
    };
    const char *zone_fields[] = {
        "name",
        "description",
        "map",
        "lifespan",
        "age",
        "top",
        "x",
        "y",
        "symbol",
        "level",
        "white_power",
        "dark_power",
        "magi_power",
        "zone_short_description",
        "zone_description",
        "zone_map",
        "min_level_look",
        "owners",
        "reset_mode",
        "number",
        "cmdno",
        "cmd",
    };

    EXPECT_EQ(js_api_struct_field_mapping_count_for_owner(JsApiStructOwner::CharData),
              sizeof(char_fields) / sizeof(char_fields[0]));
    EXPECT_EQ(js_api_struct_field_mapping_count_for_owner(JsApiStructOwner::ObjData),
              sizeof(object_fields) / sizeof(object_fields[0]));
    EXPECT_EQ(js_api_struct_field_mapping_count_for_owner(JsApiStructOwner::RoomData),
              sizeof(room_fields) / sizeof(room_fields[0]));
    EXPECT_EQ(js_api_struct_field_mapping_count_for_owner(JsApiStructOwner::ZoneData),
              sizeof(zone_fields) / sizeof(zone_fields[0]));

    for (const char *field : char_fields)
        expect_field(JsApiStructOwner::CharData, field);
    for (const char *field : object_fields)
        expect_field(JsApiStructOwner::ObjData, field);
    for (const char *field : room_fields)
        expect_field(JsApiStructOwner::RoomData, field);
    for (const char *field : zone_fields)
        expect_field(JsApiStructOwner::ZoneData, field);
}

TEST(JsApiStructMapping, PublicMappingsHaveFinalExplicitAccessorPolicy) {
    std::size_t public_count = 0;
    std::size_t implemented_getter_count = 0;
    std::size_t callable_setter_count = 0;
    std::size_t documented_non_callable_setter_count = 0;

    for (std::size_t index = 0; index < js_api_struct_field_mapping_count(); ++index) {
        const JsApiStructFieldMapping &mapping = js_api_struct_field_mappings()[index];
        const std::string field_id =
            std::string(js_api_struct_owner_name(mapping.owner)) + "." + mapping.source_field;
        const std::string getter_status = mapping.getter_status;
        const std::string setter_status = mapping.setter_status;
        const std::string getter_docs = mapping.getter_docs;
        const std::string setter_docs = mapping.setter_docs;
        const std::string notes = mapping.notes;
        const bool public_row = getter_status != "internal-only";

        EXPECT_NE(getter_status, "planned-read-only-getter") << field_id;
        EXPECT_NE(setter_status, "planned-validated-setter") << field_id;

        if (!public_row) {
            EXPECT_EQ(setter_status, "unsupported") << field_id;
            EXPECT_STRNE(mapping.getter_docs, "") << field_id;
            EXPECT_STRNE(mapping.setter_docs, "") << field_id;
            continue;
        }

        ++public_count;
        EXPECT_STRNE(mapping.js_property, "") << field_id;
        EXPECT_STRNE(mapping.getter_name, "") << field_id;
        EXPECT_STRNE(mapping.setter_name, "") << field_id;
        EXPECT_STRNE(mapping.type_name, "") << field_id;
        EXPECT_STRNE(mapping.getter_docs, "") << field_id;
        EXPECT_STRNE(mapping.setter_docs, "") << field_id;

        if (getter_status == "implemented-read-only-getter") {
            ++implemented_getter_count;
            EXPECT_NE(getter_docs.find("Returns"), std::string::npos) << field_id;
        } else {
            EXPECT_EQ(getter_status, "deferred") << field_id;
            const std::string getter_policy = getter_docs + " " + notes;
            EXPECT_TRUE(getter_policy.find("Planned") != std::string::npos ||
                        getter_policy.find("Deferred") != std::string::npos)
                << field_id;
        }

        if (setter_status == "implemented-validated-setter") {
            ++callable_setter_count;
            EXPECT_EQ(getter_status, "implemented-read-only-getter") << field_id;
            EXPECT_NE(std::string(mapping.side_effect), "none") << field_id;
            EXPECT_NE(setter_docs.find("target-scoped persistent setter authority"),
                      std::string::npos)
                << field_id;
        } else {
            ++documented_non_callable_setter_count;
            EXPECT_TRUE(setter_status == "deferred" || setter_status == "unsupported") << field_id;
            const std::string setter_policy = setter_docs + " " + notes;
            EXPECT_TRUE(setter_policy.find("helper") != std::string::npos ||
                        setter_policy.find("authority") != std::string::npos ||
                        setter_policy.find("persistence") != std::string::npos ||
                        setter_policy.find("internal") != std::string::npos ||
                        setter_policy.find("linked-list") != std::string::npos ||
                        setter_policy.find("recalculation") != std::string::npos ||
                        setter_policy.find("side effect") != std::string::npos ||
                        setter_policy.find("requires") != std::string::npos ||
                        setter_policy.find("require") != std::string::npos ||
                        setter_policy.find("need") != std::string::npos ||
                        setter_policy.find("owned") != std::string::npos ||
                        setter_policy.find("because") != std::string::npos ||
                        setter_policy.find("until") != std::string::npos ||
                        setter_policy.find("runtime") != std::string::npos ||
                        setter_policy.find("visibility") != std::string::npos ||
                        setter_policy.find("persist") != std::string::npos ||
                        setter_policy.find("Changing") != std::string::npos ||
                        setter_policy.find("Moving") != std::string::npos ||
                        setter_policy.find("Replacing") != std::string::npos ||
                        setter_policy.find("Direct") != std::string::npos ||
                        setter_policy.find("bypass") != std::string::npos)
                << field_id;
            if (setter_status == "deferred") {
                EXPECT_GT(setter_docs.size(), 24U) << field_id;
            } else {
                EXPECT_NE(setter_docs.find("unsupported"), std::string::npos) << field_id;
            }
        }
    }

    EXPECT_GT(public_count, 0U);
    EXPECT_GT(implemented_getter_count, 0U);
    EXPECT_GT(callable_setter_count, 0U);
    EXPECT_GT(documented_non_callable_setter_count, 0U);
}

TEST(JsApiStructMapping, DeferredHelperPlansArePrioritizedAndReferenceMappedFields) {
    EXPECT_EQ(js_api_deferred_helper_plan_count(), 11U);

    std::set<std::string> ids;
    std::set<int> priorities;
    int previous_priority = 0;

    for (std::size_t index = 0; index < js_api_deferred_helper_plan_count(); ++index) {
        const JsApiDeferredHelperPlan &plan = js_api_deferred_helper_plans()[index];
        SCOPED_TRACE(plan.id);

        EXPECT_TRUE(ids.insert(plan.id).second);
        EXPECT_TRUE(priorities.insert(plan.priority).second);
        EXPECT_GT(plan.priority, previous_priority);
        previous_priority = plan.priority;

        EXPECT_STRNE(plan.title, "");
        EXPECT_STRNE(plan.helper_shape, "");
        EXPECT_STRNE(plan.authority_policy, "");
        EXPECT_STRNE(plan.offline_parity, "");
        EXPECT_STRNE(plan.test_focus, "");
        EXPECT_STRNE(plan.notes, "");

        EXPECT_EQ(std::string(plan.helper_shape).find("raw setter"), std::string::npos);
        EXPECT_EQ(std::string(plan.helper_shape).find("direct setter"), std::string::npos);
        const std::string combined_plan_text = std::string(plan.helper_shape) + " " +
                                               plan.authority_policy + " " + plan.offline_parity +
                                               " " + plan.test_focus + " " + plan.notes;
        for (const char *raw_setter : {"Character.setRoom", "GameObject.setRoom",
                 "GameObject.setCarriedBy", "GameObject.setContainer", "GameObject.setContents",
                 "GameObject.setFlags", "GameObject.setExtraDescriptions", "Room.setContents",
                 "Room.setCharacters", "Room.setExtraDescriptions", "Zone.setResetCommands"}) {
            EXPECT_EQ(combined_plan_text.find(raw_setter), std::string::npos) << raw_setter;
        }
        EXPECT_NE(std::string(plan.authority_policy).find("authority"), std::string::npos);
        EXPECT_NE(std::string(plan.offline_parity).find("Offline fixtures"), std::string::npos);
        EXPECT_NE(std::string(plan.test_focus).find("Cover"), std::string::npos);

        const std::vector<std::string> source_fields = split_pipe_list(plan.source_fields);
        EXPECT_FALSE(source_fields.empty());
        for (const std::string &source_field : source_fields) {
            const std::size_t separator = source_field.find('.');
            ASSERT_NE(separator, std::string::npos) << source_field;
            ASSERT_EQ(source_field.find('.', separator + 1), std::string::npos) << source_field;

            const std::string source_struct = source_field.substr(0, separator);
            const std::string field = source_field.substr(separator + 1);
            const JsApiStructFieldMapping *mapping =
                find_js_api_struct_field_mapping(owner_from_source_struct(source_struct),
                                                 field.c_str());
            ASSERT_NE(mapping, nullptr) << source_field;

            const std::string getter_status = mapping->getter_status;
            const std::string setter_status = mapping->setter_status;
            EXPECT_NE(getter_status, "planned-read-only-getter") << source_field;
            EXPECT_NE(setter_status, "planned-validated-setter") << source_field;
            EXPECT_STRNE(setter_status.c_str(), "implemented-validated-setter") << source_field;
        }
    }
}

TEST(JsApiStructMapping, DeferredHelperPlanPrioritizesHighImpactBuilderAuthoring) {
    struct ExpectedPlan {
        const char *id;
        int priority;
        const char *required_field;
        const char *required_authority_text;
        const char *required_test_text;
    };

    const ExpectedPlan expected[] = {
        {"helper-guardrail-foundation", 10, "char_data.in_room", "stale-handle denial",
         "raw setInventory/setContents/setFlags/setExtraDescriptions absence"},
        {"room-flags", 20, "room_data.room_flags", "internal/transient bit exclusion",
         "BFS_MARK/PERMAFFECT/unnamed-bit rejection"},
        {"room-exits", 30, "room_data.dir_option", "destination-zone policy",
         "reset-command conflicts"},
        {"object-value-domains", 40, "obj_data.obj_flags", "item-type compatibility",
         "wrong item-type helpers"},
        {"affects", 50, "char_data.affected", "stat/flag recomputation",
         "room flag synchronization"},
        {"inventory-equipment-object-movement", 60, "obj_data.contains",
         "batch preflight", "multi-reward no-partial rollback"},
        {"character-movement-relationships", 70, "char_data.in_room",
         "loop prevention", "trigger-blocked movement"},
        {"zone-reset-authoring", 80, "zone_data.cmd", "stale base checksum checks",
         "mixed valid/invalid batch atomicity"},
        {"zone-descriptions-and-visibility", 90, "zone_data.min_level_look",
         "text bounds/sanitization", "missing persistence support"},
        {"zone-faction-power", 100, "zone_data.white_power", "admin",
         "non-admin rejection"},
        {"profile-admin", 110, "char_data.player", "account/admin authority",
         "audit-before-mutation ordering"},
    };

    ASSERT_EQ(js_api_deferred_helper_plan_count(), sizeof(expected) / sizeof(expected[0]));
    for (std::size_t index = 0; index < js_api_deferred_helper_plan_count(); ++index) {
        const JsApiDeferredHelperPlan &plan = js_api_deferred_helper_plans()[index];
        const ExpectedPlan &expected_plan = expected[index];
        SCOPED_TRACE(expected_plan.id);

        EXPECT_STREQ(plan.id, expected_plan.id);
        EXPECT_EQ(plan.priority, expected_plan.priority);
        EXPECT_NE(std::string(plan.source_fields).find(expected_plan.required_field),
                  std::string::npos);
        EXPECT_NE(std::string(plan.authority_policy).find(expected_plan.required_authority_text),
                  std::string::npos);
        EXPECT_NE(std::string(plan.test_focus).find(expected_plan.required_test_text),
                  std::string::npos);
    }
}

TEST(JsApiStructMapping, RewardCustodyHelperPlanUsesModernTypedHelpers) {
    const JsApiDeferredHelperPlan *plan =
        find_deferred_helper_plan("inventory-equipment-object-movement");
    ASSERT_NE(plan, nullptr);

    const std::string plan_text = std::string(plan->title) + " " + plan->helper_shape + " " +
                                  plan->authority_policy + " " + plan->offline_parity + " " +
                                  plan->test_focus + " " + plan->notes;

    for (const char *required : {"giveReward", "exchangeReceivedObject", "findInventoryObject",
             "findEquippedObject", "findRoomObject", "cloneObjectFrom", "stashObject",
             "moveObjectToRoomCustody", "extractObject", "wearObject", "removeObject",
             "container helpers", "actor/target authority", "zone ownership",
             "reciprocal list validation", "crash-save policy",
             "prototype-backed source-object liveness",
             "explicit prototype versus live-instance copy policy",
             "explicit inventory/equipment/room lookup domains",
             "bounded equipment-slot validation before equipment array reads",
             "direct-room-content ownership",
             "rejected unknown vnums",
             "absent and multiple-match result codes",
             "nested-container cycle prevention",
             "direct room custody authority",
             "no-room-membership rejection before OBJ_FROM_ROOM-style removal",
             "container capacity/counting",
             "nested weight propagation",
             "room light-counter and crash-save side effects",
             "recursive contents extraction",
             "stale-handle marking after accepted extraction",
             "room/container/carrier/equipment cleanup",
             "object index count compensation",
             "ON_WEAR/receive trigger ordering",
             "default item-return branch policy", "received-item preservation until exchange acceptance",
             "not-found reward prototype result codes", "audit-before-mutation",
             "default item return", "typed inventory/equipment/room lookup",
             "hidden lookup catalogs", "hidden accepted custody, exchange, clone, room, container, and extraction state",
             "default-return outcomes", "batch preflight",
             "multi-reward no-partial rollback",
             "LOAD_OBJ_X explicit-source clone rejection",
             "ASSIGN_INV direct versus recursive inventory lookup policy",
             "ASSIGN_EQ slot bounds before equipment reads",
             "ASSIGN_ROOM direct-content lookup",
             "received-item preservation on failed exchange",
             "no-room-membership rejection",
             "container cycle/capacity failures",
             "recursive extraction of contained objects",
             "stale-handle branches after accepted direct-location extraction",
             "object index count compensation",
             "room light counter rollback",
             "mixed extraction batch rejection unless staged rollback or compensation is implemented",
             "legacy temp object slots should become local TypeScript variables"}) {
        EXPECT_NE(plan_text.find(required), std::string::npos) << required;
    }

    for (const char *forbidden : {"ob1", "ob2", "ch1", "ch2", "temporary pointer",
             "pointer slot", "direct setter"}) {
        EXPECT_EQ(plan_text.find(forbidden), std::string::npos) << forbidden;
    }
}

TEST(JsApiStructMapping, HelperMutationGateRequirementsAreCompleteAndBuilderSafe) {
    struct ExpectedRequirement {
        const char *id;
        const char *server_text;
        const char *offline_text;
        const char *test_text;
    };

    const ExpectedRequirement expected[] = {
        {"opaque-target-tokens", "package id/checksum", "fixture-local handles", "forged ids"},
        {"target-scoped-authority", "host type", "authority failure shape", "wrong-zone"},
        {"stale-handle-denial", "generation/version mismatches", "mark handles stale",
         "extracted objects"},
        {"per-helper-mutation-limit", "before enqueue and before audit", "same caps",
         "excessive helper calls"},
        {"sanitized-mutation-result", "not-authorized and stale-handle", "frozen",
         "hidden evidence"},
        {"audit-before-apply", "audit failure must leave live state unchanged",
         "readiness diagnostics",
         "audit write failure"},
        {"atomic-no-partial-write", "across room, object, zone, and affect helper families",
         "fixture state unchanged",
         "valid-then-invalid batches"},
    };

    ASSERT_EQ(js_api_helper_mutation_gate_requirement_count(),
              sizeof(expected) / sizeof(expected[0]));

    std::set<std::string> ids;
    for (std::size_t index = 0; index < js_api_helper_mutation_gate_requirement_count();
         ++index) {
        const JsApiHelperMutationGateRequirement &requirement =
            js_api_helper_mutation_gate_requirements()[index];
        const ExpectedRequirement &expected_requirement = expected[index];
        SCOPED_TRACE(requirement.id);

        EXPECT_TRUE(ids.insert(requirement.id).second);
        EXPECT_STREQ(requirement.id, expected_requirement.id);
        EXPECT_STRNE(requirement.title, "");
        EXPECT_NE(std::string(requirement.server_policy).find(expected_requirement.server_text),
                  std::string::npos);
        EXPECT_NE(std::string(requirement.offline_policy).find(expected_requirement.offline_text),
                  std::string::npos);
        EXPECT_NE(std::string(requirement.test_policy).find(expected_requirement.test_text),
                  std::string::npos);

        const std::string combined_text = std::string(requirement.server_policy) + " " +
                                          requirement.offline_policy + " " +
                                          requirement.test_policy;
        EXPECT_EQ(combined_text.find("char_data *"), std::string::npos);
        EXPECT_EQ(combined_text.find("obj_data *"), std::string::npos);
        EXPECT_EQ(combined_text.find("room_data *"), std::string::npos);
        EXPECT_EQ(combined_text.find("zone_data *"), std::string::npos);
        EXPECT_EQ(combined_text.find("/home/"), std::string::npos);
        EXPECT_EQ(combined_text.find("Bearer"), std::string::npos);
    }
}

TEST(JsApiStructMapping, HelperGuardrailFoundationCoversEveryMutationGateRequirement) {
    const JsApiDeferredHelperPlan *foundation =
        find_deferred_helper_plan("helper-guardrail-foundation");
    ASSERT_NE(foundation, nullptr);

    const std::string foundation_policy = std::string(foundation->helper_shape) + " " +
                                          foundation->authority_policy + " " +
                                          foundation->offline_parity + " " +
                                          foundation->test_focus + " " +
                                          foundation->notes;

    for (std::size_t index = 0; index < js_api_helper_mutation_gate_requirement_count();
         ++index) {
        const JsApiHelperMutationGateRequirement &requirement =
            js_api_helper_mutation_gate_requirements()[index];
        SCOPED_TRACE(requirement.id);

        if (std::string(requirement.id) == "opaque-target-tokens") {
            EXPECT_NE(foundation_policy.find("live target tokens"), std::string::npos);
        } else if (std::string(requirement.id) == "target-scoped-authority") {
            EXPECT_NE(foundation_policy.find("target-scoped authority"), std::string::npos);
        } else if (std::string(requirement.id) == "stale-handle-denial") {
            EXPECT_NE(foundation_policy.find("stale-handle"), std::string::npos);
        } else if (std::string(requirement.id) == "per-helper-mutation-limit") {
            EXPECT_NE(foundation_policy.find("mutation-count limits"), std::string::npos);
        } else if (std::string(requirement.id) == "sanitized-mutation-result") {
            EXPECT_NE(foundation_policy.find("sanitized MutationResult"), std::string::npos);
        } else if (std::string(requirement.id) == "audit-before-apply") {
            EXPECT_NE(foundation_policy.find("audit-before-mutation"), std::string::npos);
        } else if (std::string(requirement.id) == "atomic-no-partial-write") {
            EXPECT_NE(foundation_policy.find("no-partial-write rollback"), std::string::npos);
        } else {
            ADD_FAILURE() << "Unhandled helper gate requirement id: " << requirement.id;
        }
    }
}

TEST(JsApiStructMapping, RoomFlagHelperOperationsDefineFilteredInternalCatalog) {
    ASSERT_EQ(js_api_room_flag_helper_operation_count(), 2U);

    const JsApiDeferredHelperPlan *plan = find_deferred_helper_plan("room-flags");
    ASSERT_NE(plan, nullptr);
    const std::string plan_text = std::string(plan->helper_shape) + " " + plan->authority_policy +
                                  " " + plan->offline_parity + " " + plan->test_focus;

    const std::vector<std::string> expected_allowed_flags = {"dark", "death", "noMob",
        "indoors", "noRide", "shadowy", "noMagic", "tunnel", "private", "godRoom",
        "drinkWater", "drinkPoison", "securityRoom", "peaceRoom", "noTeleport", "hideVnum"};
    const std::vector<std::string> expected_excluded_flags = {
        "BFS_MARK", "PERMAFFECT", "permanentAffect", "unnamed-room-flag-bits"};
    const std::vector<std::string> expected_builder_zone_flags = {"dark", "noMob",
        "indoors", "noRide", "shadowy", "noMagic", "tunnel", "drinkWater", "drinkPoison",
        "peaceRoom", "hideVnum"};
    const std::vector<std::string> expected_admin_only_flags = {
        "death", "private", "godRoom", "securityRoom", "noTeleport"};
    const char *expected_operations[] = {"room.flags.add", "room.flags.remove"};
    const char *expected_helpers[] = {"Room.addFlag(name: MutableRoomFlagName): MutationResult",
        "Room.removeFlag(name: MutableRoomFlagName): MutationResult"};

    std::set<std::string> operation_names;
    for (std::size_t index = 0; index < js_api_room_flag_helper_operation_count(); ++index) {
        const JsApiRoomFlagHelperOperation &operation =
            js_api_room_flag_helper_operations()[index];
        SCOPED_TRACE(operation.operation_name);

        EXPECT_TRUE(operation_names.insert(operation.operation_name).second);
        EXPECT_STREQ(operation.operation_name, expected_operations[index]);
        EXPECT_STREQ(operation.helper_name, expected_helpers[index]);
        for (const char *forbidden : {"setFlags", "setRaw", "bitvector", "value"}) {
            EXPECT_EQ(std::string(operation.operation_name).find(forbidden), std::string::npos)
                << forbidden;
            EXPECT_EQ(std::string(operation.helper_name).find(forbidden), std::string::npos)
                << forbidden;
        }
        EXPECT_NE(std::string(operation.authority_policy).find("opaque room target token"),
                  std::string::npos);
        EXPECT_NE(std::string(operation.authority_policy).find("authorized zone"),
                  std::string::npos);
        EXPECT_NE(std::string(operation.authority_policy).find("immortal/admin override"),
                  std::string::npos);
        EXPECT_NE(std::string(operation.side_effect_policy).find("lighting"), std::string::npos);
        EXPECT_NE(std::string(operation.side_effect_policy).find("movement"), std::string::npos);
        EXPECT_NE(std::string(operation.side_effect_policy).find("combat"), std::string::npos);
        EXPECT_NE(std::string(operation.side_effect_policy).find("security"), std::string::npos);
        for (const char *side_effect : {"drinkWater/drinkPoison", "noMagic", "noRide",
                 "noTeleport", "hideVnum", "entry limits"}) {
            EXPECT_NE(std::string(operation.side_effect_policy).find(side_effect),
                      std::string::npos)
                << side_effect;
        }
        EXPECT_NE(std::string(operation.audit_policy).find("before any room_data.room_flags"),
                  std::string::npos);
        for (const char *audit_field : {"operation", "canonical flag name", "authority class",
                 "builder account id", "eligible immortal character id", "target zone",
                 "room vnum", "package id", "request id", "verified override scope",
                 "override decision evidence", "previous flag membership",
                 "intended new flag membership"}) {
            EXPECT_NE(std::string(operation.audit_policy).find(audit_field), std::string::npos)
                << audit_field;
        }
        EXPECT_NE(std::string(operation.diagnostic_policy).find("stable reason category"),
                  std::string::npos);
        for (const char *diagnostic_category : {"unsupported-envelope", "unknown-operation",
                 "invalid-target", "invalid-arguments", "authority-rejected", "blocked-flag",
                 "admin-only-flag", "stale-room", "wrong-zone", "invalid-token",
                 "audit-rejected", "apply-rejected"}) {
            EXPECT_NE(std::string(operation.diagnostic_policy).find(diagnostic_category),
                      std::string::npos)
                << diagnostic_category;
        }
        EXPECT_NE(std::string(operation.diagnostic_policy).find("without exposing token secrets"),
                  std::string::npos);
        EXPECT_NE(std::string(operation.rollback_policy).find("previous room_data.room_flags"),
                  std::string::npos);
        EXPECT_NE(std::string(operation.rollback_policy).find("reverse order"), std::string::npos);
        EXPECT_NE(std::string(operation.rollback_policy).find("before scalar setters commit"),
                  std::string::npos);
        EXPECT_NE(std::string(operation.offline_policy).find("same flag vocabulary"),
                  std::string::npos);
        EXPECT_NE(std::string(operation.test_focus).find("Room.setFlags"), std::string::npos);

        const std::vector<std::string> allowed_flags =
            split_pipe_list(operation.allowed_flags);
        EXPECT_EQ(allowed_flags, expected_allowed_flags);
        EXPECT_EQ(std::set<std::string>(allowed_flags.begin(), allowed_flags.end()).size(),
                  allowed_flags.size());
        EXPECT_EQ(std::find(allowed_flags.begin(), allowed_flags.end(), "BFS_MARK"),
                  allowed_flags.end());
        EXPECT_EQ(std::find(allowed_flags.begin(), allowed_flags.end(), "PERMAFFECT"),
                  allowed_flags.end());
        EXPECT_EQ(std::find(allowed_flags.begin(), allowed_flags.end(), "permanentAffect"),
                  allowed_flags.end());
        EXPECT_EQ(std::find(allowed_flags.begin(), allowed_flags.end(), "bfsMark"),
                  allowed_flags.end());
        EXPECT_EQ(std::find(allowed_flags.begin(), allowed_flags.end(), "permaffect"),
                  allowed_flags.end());

        const std::vector<std::string> builder_zone_flags =
            split_pipe_list(operation.builder_zone_flags);
        const std::vector<std::string> admin_only_flags =
            split_pipe_list(operation.admin_only_flags);
        const std::vector<std::string> blocked_flags =
            split_pipe_list(operation.blocked_flags);
        EXPECT_EQ(builder_zone_flags, expected_builder_zone_flags);
        EXPECT_EQ(admin_only_flags, expected_admin_only_flags);
        EXPECT_EQ(blocked_flags, expected_excluded_flags);

        std::set<std::string> authority_classified_flags;
        for (const std::string &flag : builder_zone_flags) {
            EXPECT_TRUE(std::find(allowed_flags.begin(), allowed_flags.end(), flag) !=
                        allowed_flags.end())
                << flag;
            EXPECT_TRUE(authority_classified_flags.insert(flag).second) << flag;
        }
        for (const std::string &flag : admin_only_flags) {
            EXPECT_TRUE(std::find(allowed_flags.begin(), allowed_flags.end(), flag) !=
                        allowed_flags.end())
                << flag;
            EXPECT_TRUE(authority_classified_flags.insert(flag).second) << flag;
        }
        EXPECT_EQ(authority_classified_flags,
                  std::set<std::string>(allowed_flags.begin(), allowed_flags.end()));

        const std::vector<std::string> excluded_flags =
            split_pipe_list(operation.excluded_flags);
        EXPECT_EQ(excluded_flags, expected_excluded_flags);
        EXPECT_EQ(std::set<std::string>(excluded_flags.begin(), excluded_flags.end()).size(),
                  excluded_flags.size());
        for (const std::string &flag : excluded_flags)
            EXPECT_EQ(std::find(allowed_flags.begin(), allowed_flags.end(), flag),
                      allowed_flags.end())
                << flag;
        for (const std::string &flag : blocked_flags)
            EXPECT_EQ(std::find(allowed_flags.begin(), allowed_flags.end(), flag),
                      allowed_flags.end())
                << flag;
    }

    EXPECT_TRUE(operation_names.count("room.flags.add") > 0);
    EXPECT_TRUE(operation_names.count("room.flags.remove") > 0);
    EXPECT_NE(plan_text.find("BFS_MARK/PERMAFFECT"), std::string::npos);
    EXPECT_NE(plan_text.find("atomic mixed batches"), std::string::npos);
}

TEST(JsApiStructMapping, RoomExitHelperOperationsDefineInternalCatalog) {
    ASSERT_EQ(js_api_room_exit_helper_operation_count(), 2U);

    const JsApiDeferredHelperPlan *plan = find_deferred_helper_plan("room-exits");
    ASSERT_NE(plan, nullptr);
    const std::string plan_text = std::string(plan->helper_shape) + " " + plan->authority_policy +
                                  " " + plan->offline_parity + " " + plan->test_focus;
    EXPECT_NE(plan_text.find("destination-zone policy"), std::string::npos);
    EXPECT_NE(plan_text.find("bidirectional-link diagnostics"), std::string::npos);
    EXPECT_NE(plan_text.find("reset-command conflicts"), std::string::npos);

    struct ExpectedOperation {
        const char *operation_name;
        const char *helper_name;
        const char *legacy_commands;
        const char *target_text;
        const char *side_effect_text;
        const char *diagnostic_text;
        const char *offline_text;
        const char *test_text;
    };

    const ExpectedOperation expected[] = {
        {"room.exit.state",
         "RotS.Script.setExitState(room: Room, direction: DirectionName, state: "
         "ExitStateName): MutationResult",
         "SET_EXIT_STATE", "bounds direction before any dir_option or rev_dir access",
         "preserves all exit_info bits", "not-door", "accepted hidden exit state",
         "non-reciprocal reverse non-mirroring"},
        {"room.exit.destination",
         "RotS.Script.changeExitTo(room: Room, direction: DirectionName, destination: Room): "
         "MutationResult",
         "CHANGE_EXIT_TO", "non-NOWHERE destination liveness",
         "does not update a reverse exit", "bidirectional-conflict",
         "one-way versus reciprocal-link decisions",
         "valid one-way destination change"},
    };

    std::set<std::string> operation_names;
    std::set<std::string> helper_names;
    for (std::size_t index = 0; index < js_api_room_exit_helper_operation_count(); ++index) {
        const JsApiRoomExitHelperOperation &operation = js_api_room_exit_helper_operations()[index];
        const ExpectedOperation &expected_operation = expected[index];
        SCOPED_TRACE(operation.operation_name);

        EXPECT_TRUE(operation_names.insert(operation.operation_name).second);
        EXPECT_TRUE(helper_names.insert(operation.helper_name).second);
        EXPECT_STREQ(operation.operation_name, expected_operation.operation_name);
        EXPECT_STREQ(operation.helper_name, expected_operation.helper_name);
        EXPECT_STREQ(operation.legacy_commands, expected_operation.legacy_commands);
        EXPECT_NE(std::string(operation.target_policy).find(expected_operation.target_text),
                  std::string::npos);
        EXPECT_NE(std::string(operation.side_effect_policy).find(expected_operation.side_effect_text),
                  std::string::npos);
        EXPECT_NE(std::string(operation.diagnostic_policy).find(expected_operation.diagnostic_text),
                  std::string::npos);
        EXPECT_NE(std::string(operation.offline_policy).find(expected_operation.offline_text),
                  std::string::npos);
        EXPECT_NE(std::string(operation.test_focus).find(expected_operation.test_text),
                  std::string::npos);

        EXPECT_NE(std::string(operation.authority_policy).find("authority"), std::string::npos);
        EXPECT_NE(std::string(operation.audit_policy).find("Audit"), std::string::npos);
        EXPECT_NE(std::string(operation.audit_policy).find("builder account id"),
                  std::string::npos);
        EXPECT_NE(std::string(operation.audit_policy).find("eligible immortal"),
                  std::string::npos);
        EXPECT_NE(std::string(operation.audit_policy).find("package id"), std::string::npos);
        EXPECT_NE(std::string(operation.rollback_policy).find("Rollback"), std::string::npos);
        EXPECT_NE(std::string(operation.offline_policy).find("Offline fixtures"),
                  std::string::npos);

        for (const char *forbidden : {"setExit(", "raw pointer", "descriptor"}) {
            EXPECT_EQ(std::string(operation.helper_name).find(forbidden), std::string::npos)
                << forbidden;
        }
    }

    EXPECT_TRUE(operation_names.count("room.exit.state") > 0);
    EXPECT_TRUE(operation_names.count("room.exit.destination") > 0);
}

TEST(JsApiStructMapping, CharacterMovementHelperOperationsDefineInternalCatalog) {
    ASSERT_EQ(js_api_character_movement_helper_operation_count(), 7U);

    const JsApiDeferredHelperPlan *plan =
        find_deferred_helper_plan("character-movement-relationships");
    ASSERT_NE(plan, nullptr);
    const std::string plan_text = std::string(plan->helper_shape) + " " + plan->authority_policy +
                                  " " + plan->offline_parity + " " + plan->test_focus;
    EXPECT_NE(plan_text.find("Move/teleport/follow"), std::string::npos);
    EXPECT_NE(plan_text.find("loop prevention"), std::string::npos);
    EXPECT_NE(plan_text.find("trigger-blocked movement"), std::string::npos);

    struct ExpectedOperation {
        const char *operation_name;
        const char *helper_name;
        const char *legacy_commands;
        const char *target_text;
        const char *side_effect_text;
        const char *diagnostic_text;
        const char *offline_text;
        const char *test_text;
    };

    const ExpectedOperation expected[] = {
        {"character.load_mob",
         "RotS.Script.loadMob(vnum: number, room: Room): MutationResult", "LOAD_MOB",
         "mutable temporary character variable", "Mudlle/mobile-procedure call stacks", "not-found",
         "hidden mobilePrototypes catalog", "deterministic generated ids"},
        {"character.teleport",
         "RotS.Script.teleportChar(character: Character, room: Room): MutationResult",
         "TELEPORT_CHAR", "Raw room vnums", "room people lists", "trigger-blocked",
         "follower/mount links", "teleport with NPC followers"},
        {"character.teleport_only",
         "RotS.Script.teleportCharOnly(character: Character, room: Room): MutationResult",
         "TELEPORT_CHAR_X", "followers, group", "only the selected character",
         "no-teleport", "follower room membership", "follower non-movement"},
        {"character.teleport_to_target_room",
         "RotS.Script.teleportCharToTargetRoom(character: Character, target: Character): "
         "MutationResult",
         "TELEPORT_CHAR_XL", "current room of another live character", "destination resolution",
         "target-not-in-room", "target-room resolution", "target moved by an earlier"},
        {"character.extract", "RotS.Script.extractChar(character: Character): MutationResult",
         "EXTRACT_CHAR", "authorized NPC/helper character", "carried and worn objects",
         "mixed-batch-rejected", "place carried and worn objects into hidden room contents",
         "player/account-backed rejection"},
        {"character.follow",
         "RotS.Script.doFollow(follower: Character, leader: Character): MutationResult",
         "DO_FOLLOW", "surprising loop branch", "stop_follower semantics",
         "protected-follower", "previous-master replacement",
         "looped graph rejection without altering the leader's master"},
        {"character.flee", "RotS.Script.doFlee(character: Character): MutationResult", "DO_FLEE",
         "without exposing generic command", "tries up to six random directions", "not-eligible",
         "ordered eligible-exit catalog", "duplicate command/pre-enter check"},
    };

    std::set<std::string> operation_names;
    std::set<std::string> helper_names;
    for (std::size_t index = 0; index < js_api_character_movement_helper_operation_count();
         ++index) {
        const JsApiCharacterMovementHelperOperation &operation =
            js_api_character_movement_helper_operations()[index];
        const ExpectedOperation &expected_operation = expected[index];
        SCOPED_TRACE(operation.operation_name);

        EXPECT_TRUE(operation_names.insert(operation.operation_name).second);
        EXPECT_TRUE(helper_names.insert(operation.helper_name).second);
        EXPECT_STREQ(operation.operation_name, expected_operation.operation_name);
        EXPECT_STREQ(operation.helper_name, expected_operation.helper_name);
        EXPECT_STREQ(operation.legacy_commands, expected_operation.legacy_commands);
        EXPECT_NE(std::string(operation.target_policy).find(expected_operation.target_text),
                  std::string::npos);
        EXPECT_NE(std::string(operation.side_effect_policy).find(expected_operation.side_effect_text),
                  std::string::npos);
        EXPECT_NE(std::string(operation.diagnostic_policy).find(expected_operation.diagnostic_text),
                  std::string::npos);
        EXPECT_NE(std::string(operation.offline_policy).find(expected_operation.offline_text),
                  std::string::npos);
        EXPECT_NE(std::string(operation.test_focus).find(expected_operation.test_text),
                  std::string::npos);

        EXPECT_NE(std::string(operation.authority_policy).find("authority"), std::string::npos);
        EXPECT_NE(std::string(operation.audit_policy).find("Audit"), std::string::npos);
        EXPECT_NE(std::string(operation.audit_policy).find("builder account id"),
                  std::string::npos);
        EXPECT_NE(std::string(operation.audit_policy).find("eligible immortal"),
                  std::string::npos);
        EXPECT_NE(std::string(operation.audit_policy).find("package id"), std::string::npos);
        EXPECT_NE(std::string(operation.rollback_policy).find("Rollback"), std::string::npos);
        EXPECT_NE(std::string(operation.offline_policy).find("Offline fixtures"),
                  std::string::npos);

        if (operation.operation_name == std::string("character.follow")) {
            EXPECT_NE(std::string(operation.authority_policy).find("already-following"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.authority_policy).find("previous-master"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.side_effect_policy).find("hunting memory"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.side_effect_policy).find("pet/group"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.side_effect_policy).find("follow/stop-follow messages"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.audit_policy).find("leader current master"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.diagnostic_policy).find("already-following"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.rollback_policy).find("leader's master untouched"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.offline_policy).find("accepted hidden relationship state"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.rollback_policy).find("hunting memory policy"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.test_focus).find(
                          "hunting-memory and pet/group cleanup/restoration"),
                      std::string::npos);
        }

        if (operation.operation_name == std::string("character.flee")) {
            EXPECT_NE(std::string(operation.authority_policy).find("EX_NOFLEE"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.authority_policy).find("EX_NOWALK"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.authority_policy).find("MOB_STAY_ZONE"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.authority_policy).find("MOB_STAY_TYPE"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.audit_policy).find("source room"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.audit_policy).find("destination room"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.side_effect_policy).find("AFF_HAZE"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.side_effect_policy).find("followers/mounts"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.diagnostic_policy).find("MutationResult codes"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.diagnostic_policy).find("generic flee command"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.rollback_policy).find("AFF_HUNT"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.rollback_policy).find("XP loss"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.offline_policy).find("ordered eligible-exit catalog"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.test_focus).find("death rooms"), std::string::npos);
            EXPECT_NE(std::string(operation.test_focus).find("NPC stay-zone/stay-type"),
                      std::string::npos);
        }

        if (operation.operation_name == std::string("character.extract")) {
            EXPECT_NE(std::string(operation.target_policy).find("Player characters"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.authority_policy).find("source-room authority"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.authority_policy).find("current room"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.side_effect_policy).find("descriptor-adjacent"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.side_effect_policy).find("carried and worn"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.diagnostic_policy).find("mixed-batch-rejected"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.rollback_policy).find("no-current-room"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.rollback_policy).find("conditionally"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.rollback_policy).find("clamps mob-index/load-line counts"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.rollback_policy).find("reject mixed batches"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.offline_policy).find("stale"), std::string::npos);
            EXPECT_NE(std::string(operation.offline_policy).find("hidden room contents"),
                      std::string::npos);
        }

        for (const char *forbidden : {"setRoom", "setFollowers", "setMaster", "setMount",
                 "setGroup", "raw pointer", "descriptor"}) {
            EXPECT_EQ(std::string(operation.helper_name).find(forbidden), std::string::npos)
                << forbidden;
        }
    }

    EXPECT_TRUE(operation_names.count("character.load_mob") > 0);
    EXPECT_TRUE(operation_names.count("character.teleport") > 0);
    EXPECT_TRUE(operation_names.count("character.teleport_only") > 0);
    EXPECT_TRUE(operation_names.count("character.teleport_to_target_room") > 0);
    EXPECT_TRUE(operation_names.count("character.extract") > 0);
    EXPECT_TRUE(operation_names.count("character.follow") > 0);
    EXPECT_TRUE(operation_names.count("character.flee") > 0);
}

TEST(JsApiStructMapping, CombatEffectHelperOperationsDefineInternalCatalog) {
    ASSERT_EQ(js_api_combat_effect_helper_operation_count(), 4U);

    struct ExpectedOperation {
        const char *operation_name;
        const char *helper_name;
        const char *legacy_commands;
        const char *target_text;
        const char *side_effect_text;
        const char *diagnostic_text;
        const char *offline_text;
        const char *test_text;
    };

    const ExpectedOperation expected[] = {
        {"combat.attack",
         "RotS.Script.doHit(attacker: Character, victim: Character): MutationResult", "DO_HIT",
         "without exposing generic command", "Legacy do_hit", "peace-room",
         "accepted hidden combat state", "charm/master protection"},
        {"combat.damage",
         "RotS.Script.applyDamage(victim: Character, amount: number, options?: DamageOptions): "
         "MutationResult",
         "SET_INT_VALUE ch.hit|ON_DAMAGE", "bounded, audited damage or healing",
         "victim and weapon ON_DAMAGE", "recursive-damage", "accepted hidden hit-point state",
         "trigger-blocked damage"},
        {"combat.kill", "RotS.Script.rawKill(character: Character): MutationResult", "RAW_KILL",
         "destructive death processing", "Legacy raw_kill", "special-death-blocked",
         "SPECIAL_DEATH block outcomes", "mixed-batch rejection"},
        {"combat.experience",
         "RotS.Script.gainExperience(character: Character, amount: number): MutationResult",
         "GAIN_EXP", "player progression", "Legacy SCRIPT_GAIN_EXP", "npc-target",
         "level-scaling branches", "high-level scaling"},
    };

    std::set<std::string> operation_names;
    std::set<std::string> helper_names;
    for (std::size_t index = 0; index < js_api_combat_effect_helper_operation_count(); ++index) {
        const JsApiCombatEffectHelperOperation &operation =
            js_api_combat_effect_helper_operations()[index];
        const ExpectedOperation &expected_operation = expected[index];
        SCOPED_TRACE(operation.operation_name);

        EXPECT_TRUE(operation_names.insert(operation.operation_name).second);
        EXPECT_TRUE(helper_names.insert(operation.helper_name).second);
        EXPECT_STREQ(operation.operation_name, expected_operation.operation_name);
        EXPECT_STREQ(operation.helper_name, expected_operation.helper_name);
        EXPECT_STREQ(operation.legacy_commands, expected_operation.legacy_commands);
        EXPECT_NE(std::string(operation.target_policy).find(expected_operation.target_text),
                  std::string::npos);
        EXPECT_NE(std::string(operation.side_effect_policy).find(expected_operation.side_effect_text),
                  std::string::npos);
        EXPECT_NE(std::string(operation.diagnostic_policy).find(expected_operation.diagnostic_text),
                  std::string::npos);
        EXPECT_NE(std::string(operation.offline_policy).find(expected_operation.offline_text),
                  std::string::npos);
        EXPECT_NE(std::string(operation.test_focus).find(expected_operation.test_text),
                  std::string::npos);

        EXPECT_NE(std::string(operation.authority_policy).find("authority"), std::string::npos);
        EXPECT_NE(std::string(operation.audit_policy).find("Audit"), std::string::npos);
        EXPECT_NE(std::string(operation.audit_policy).find("builder account id"),
                  std::string::npos);
        EXPECT_NE(std::string(operation.audit_policy).find("eligible immortal"),
                  std::string::npos);
        EXPECT_NE(std::string(operation.audit_policy).find("package id"), std::string::npos);
        EXPECT_NE(std::string(operation.rollback_policy).find("Rollback"), std::string::npos);
        EXPECT_NE(std::string(operation.offline_policy).find("Offline fixtures"),
                  std::string::npos);

        for (const char *forbidden : {"doCommand", "Character.setPoints", "setDamageDetails",
                 "raw pointer", "generic command"}) {
            EXPECT_EQ(std::string(operation.helper_name).find(forbidden), std::string::npos)
                << forbidden;
        }
    }

    EXPECT_TRUE(operation_names.count("combat.attack") > 0);
    EXPECT_TRUE(operation_names.count("combat.damage") > 0);
    EXPECT_TRUE(operation_names.count("combat.kill") > 0);
    EXPECT_TRUE(operation_names.count("combat.experience") > 0);
}

TEST(JsApiStructMapping, EquipmentHelperOperationsDefineInternalCatalog) {
    ASSERT_EQ(js_api_equipment_helper_operation_count(), 3U);

    const JsApiDeferredHelperPlan *plan =
        find_deferred_helper_plan("inventory-equipment-object-movement");
    ASSERT_NE(plan, nullptr);
    const std::string plan_text = std::string(plan->helper_shape) + " " + plan->authority_policy +
                                  " " + plan->offline_parity + " " + plan->test_focus;
    EXPECT_NE(plan_text.find("wearObject"), std::string::npos);
    EXPECT_NE(plan_text.find("ON_WEAR/receive trigger ordering"), std::string::npos);
    EXPECT_NE(plan_text.find("wear restriction failures"), std::string::npos);

    struct ExpectedOperation {
        const char *operation_name;
        const char *helper_name;
        const char *legacy_commands;
        const char *target_text;
        const char *side_effect_text;
        const char *diagnostic_text;
        const char *offline_text;
        const char *test_text;
    };

    const ExpectedOperation expected[] = {
        {"equipment.wear",
         "RotS.Script.doWear(character: Character, object: GameObject, slot?: WearSlotName): "
         "MutationResult",
         "DO_WEAR", "direct carried object", "ON_WEAR before final alternate",
         "restricted-item", "item anti-alignment/race-flag restrictions",
         "alternate slot fallback"},
        {"equipment.remove",
         "RotS.Script.doRemove(character: Character, slotOrObject: WearSlotName | GameObject): "
         "MutationResult",
         "DO_REMOVE", "equipped object", "cascade WEAR_BELT_1/2/3", "belt-cascade-drop",
         "waist-belt cascade state", "waist belt cascade"},
        {"equipment.equip_character",
         "RotS.Script.equipChar(character: Character, prototypes: readonly number[]): "
         "MutationResult",
         "EQUIP_CHAR", "bounded set of object prototypes", "no-partial preflight/rollback",
         "too-many-prototypes", "anti-alignment/race-flag zap-to-inventory branches",
         "no returned mutable object handles"},
    };

    std::set<std::string> operation_names;
    std::set<std::string> helper_names;
    for (std::size_t index = 0; index < js_api_equipment_helper_operation_count(); ++index) {
        const JsApiEquipmentHelperOperation &operation = js_api_equipment_helper_operations()[index];
        const ExpectedOperation &expected_operation = expected[index];
        SCOPED_TRACE(operation.operation_name);

        EXPECT_TRUE(operation_names.insert(operation.operation_name).second);
        EXPECT_TRUE(helper_names.insert(operation.helper_name).second);
        EXPECT_STREQ(operation.operation_name, expected_operation.operation_name);
        EXPECT_STREQ(operation.helper_name, expected_operation.helper_name);
        EXPECT_STREQ(operation.legacy_commands, expected_operation.legacy_commands);
        EXPECT_NE(std::string(operation.target_policy).find(expected_operation.target_text),
                  std::string::npos);
        EXPECT_NE(std::string(operation.side_effect_policy).find(expected_operation.side_effect_text),
                  std::string::npos);
        EXPECT_NE(std::string(operation.diagnostic_policy).find(expected_operation.diagnostic_text),
                  std::string::npos);
        EXPECT_NE(std::string(operation.offline_policy).find(expected_operation.offline_text),
                  std::string::npos);
        EXPECT_NE(std::string(operation.test_focus).find(expected_operation.test_text),
                  std::string::npos);

        EXPECT_NE(std::string(operation.authority_policy).find("authority"), std::string::npos);
        EXPECT_NE(std::string(operation.audit_policy).find("Audit"), std::string::npos);
        EXPECT_NE(std::string(operation.audit_policy).find("builder account id"),
                  std::string::npos);
        EXPECT_NE(std::string(operation.audit_policy).find("eligible immortal"),
                  std::string::npos);
        EXPECT_NE(std::string(operation.audit_policy).find("package id"), std::string::npos);
        EXPECT_NE(std::string(operation.rollback_policy).find("Rollback"), std::string::npos);
        EXPECT_NE(std::string(operation.offline_policy).find("Offline fixtures"),
                  std::string::npos);

        if (operation.operation_name == std::string("equipment.wear")) {
            EXPECT_NE(std::string(operation.authority_policy).find(
                          "object.carried_by membership before slot resolution"),
                      std::string::npos);
            EXPECT_EQ(std::string(operation.authority_policy).find("pet/tamed-mobile"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.authority_policy).find("current-room validation"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.authority_policy).find("zap-to-inventory"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.diagnostic_policy).find("no-current-room"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.audit_policy).find("resolved alternate slot"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.audit_policy).find("ON_WEAR requested-slot result"),
                      std::string::npos);
        }

        if (operation.operation_name == std::string("equipment.remove")) {
            EXPECT_NE(std::string(operation.authority_policy).find(
                          "before any equipment array read"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.diagnostic_policy).find("invalid-legacy-slot"),
                      std::string::npos);
        }

        if (operation.operation_name == std::string("equipment.equip_character")) {
            EXPECT_NE(std::string(operation.authority_policy).find("pet/tamed-mobile"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.authority_policy).find("current-room validation"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.authority_policy).find("zap-to-inventory"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.diagnostic_policy).find("pet-restricted"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.diagnostic_policy).find("no-current-room"),
                      std::string::npos);
            EXPECT_NE(std::string(operation.offline_policy).find("zap-to-inventory branches"),
                      std::string::npos);
        }

        for (const char *forbidden : {"setEquipmentSlot", "setInventory", "raw pointer",
                 "descriptor"}) {
            EXPECT_EQ(std::string(operation.helper_name).find(forbidden), std::string::npos)
                << forbidden;
        }
    }

    EXPECT_TRUE(operation_names.count("equipment.wear") > 0);
    EXPECT_TRUE(operation_names.count("equipment.remove") > 0);
    EXPECT_TRUE(operation_names.count("equipment.equip_character") > 0);
}

TEST(JsApiStructMapping, RawSetterGuardrailsReferenceNonCallableMappedFields) {
    EXPECT_GT(js_api_raw_setter_guardrail_count(), 30U);

    std::set<std::string> helper_plan_ids;
    for (std::size_t index = 0; index < js_api_deferred_helper_plan_count(); ++index)
        helper_plan_ids.insert(js_api_deferred_helper_plans()[index].id);

    std::set<std::string> guardrail_keys;
    for (std::size_t index = 0; index < js_api_raw_setter_guardrail_count(); ++index) {
        const JsApiRawSetterGuardrail &guardrail = js_api_raw_setter_guardrails()[index];
        SCOPED_TRACE(std::string(js_api_struct_owner_name(guardrail.owner)) + "." +
                     guardrail.source_field + "." + guardrail.setter_name);

        EXPECT_TRUE(helper_plan_ids.count(guardrail.helper_plan_id) > 0)
            << guardrail.helper_plan_id;
        EXPECT_STRNE(guardrail.reason, "");
        EXPECT_TRUE(guardrail_keys
                        .insert(std::string(js_api_struct_owner_name(guardrail.owner)) + "." +
                                guardrail.source_field + "." + guardrail.setter_name)
                        .second);

        const JsApiStructFieldMapping *mapping =
            find_js_api_struct_field_mapping(guardrail.owner, guardrail.source_field);
        ASSERT_NE(mapping, nullptr);
        EXPECT_STREQ(mapping->setter_name, guardrail.setter_name);
        EXPECT_STRNE(mapping->setter_status, "implemented-validated-setter");
        EXPECT_TRUE(std::string(mapping->setter_status) == "deferred" ||
                    std::string(mapping->setter_status) == "unsupported")
            << mapping->setter_status;
    }
}

TEST(JsApiStructMapping, FinalAccessorPolicyMatrixStaysIntentional) {
    struct ExpectedFieldPolicy {
        JsApiStructOwner owner;
        const char *source_field;
        const char *getter_status;
        const char *setter_status;
    };

    const ExpectedFieldPolicy expected[] = {
        {JsApiStructOwner::CharData, "abs_number", "internal-only", "unsupported"},
        {JsApiStructOwner::CharData, "player_index", "internal-only", "unsupported"},
        {JsApiStructOwner::CharData, "nr", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::CharData, "in_room", "implemented-read-only-getter", "deferred"},
        {JsApiStructOwner::CharData, "player", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::CharData, "abilities", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::CharData, "tmpabilities", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::CharData, "constabilities", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::CharData, "points", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::CharData, "specials", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::CharData, "specials2", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::CharData, "profs", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::CharData, "extra_specialization_data", "implemented-read-only-getter",
         "unsupported"},
        {JsApiStructOwner::CharData, "damage_details", "implemented-read-only-getter",
         "unsupported"},
        {JsApiStructOwner::CharData, "skills", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::CharData, "knowledge", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::CharData, "affected", "implemented-read-only-getter", "deferred"},
        {JsApiStructOwner::CharData, "equipment", "implemented-read-only-getter", "deferred"},
        {JsApiStructOwner::CharData, "carrying", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::CharData, "desc", "internal-only", "unsupported"},
        {JsApiStructOwner::CharData, "next_in_room", "internal-only", "unsupported"},
        {JsApiStructOwner::CharData, "next", "internal-only", "unsupported"},
        {JsApiStructOwner::CharData, "next_fighting", "internal-only", "unsupported"},
        {JsApiStructOwner::CharData, "next_fast_update", "internal-only", "unsupported"},
        {JsApiStructOwner::CharData, "followers", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::CharData, "master", "implemented-read-only-getter", "deferred"},
        {JsApiStructOwner::CharData, "master_number", "internal-only", "unsupported"},
        {JsApiStructOwner::CharData, "mount_data", "implemented-read-only-getter", "deferred"},
        {JsApiStructOwner::CharData, "group", "deferred", "unsupported"},
        {JsApiStructOwner::CharData, "temp", "internal-only", "unsupported"},
        {JsApiStructOwner::CharData, "delay", "internal-only", "unsupported"},
        {JsApiStructOwner::CharData, "next_die", "internal-only", "unsupported"},
        {JsApiStructOwner::CharData, "classpoints", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::CharData, "interrupt_count", "implemented-read-only-getter",
         "unsupported"},
        {JsApiStructOwner::CharData, "interrupt_time", "implemented-read-only-getter",
         "unsupported"},
        {JsApiStructOwner::CharData, "spec_busy", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::ObjData, "item_number", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::ObjData, "in_room", "implemented-read-only-getter", "deferred"},
        {JsApiStructOwner::ObjData, "obj_flags", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::ObjData, "affected", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::ObjData, "name", "implemented-read-only-getter",
         "implemented-validated-setter"},
        {JsApiStructOwner::ObjData, "description", "implemented-read-only-getter",
         "implemented-validated-setter"},
        {JsApiStructOwner::ObjData, "short_description", "implemented-read-only-getter",
         "implemented-validated-setter"},
        {JsApiStructOwner::ObjData, "action_description", "implemented-read-only-getter",
         "implemented-validated-setter"},
        {JsApiStructOwner::ObjData, "ex_description", "implemented-read-only-getter",
         "unsupported"},
        {JsApiStructOwner::ObjData, "carried_by", "implemented-read-only-getter", "deferred"},
        {JsApiStructOwner::ObjData, "owner", "internal-only", "unsupported"},
        {JsApiStructOwner::ObjData, "in_obj", "implemented-read-only-getter", "deferred"},
        {JsApiStructOwner::ObjData, "contains", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::ObjData, "next_content", "internal-only", "unsupported"},
        {JsApiStructOwner::ObjData, "next", "internal-only", "unsupported"},
        {JsApiStructOwner::ObjData, "touched", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::ObjData, "loaded_by", "internal-only", "unsupported"},
        {JsApiStructOwner::RoomData, "number", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::RoomData, "zone", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::RoomData, "level", "implemented-read-only-getter",
         "implemented-validated-setter"},
        {JsApiStructOwner::RoomData, "sector_type", "implemented-read-only-getter",
         "implemented-validated-setter"},
        {JsApiStructOwner::RoomData, "name", "implemented-read-only-getter",
         "implemented-validated-setter"},
        {JsApiStructOwner::RoomData, "description", "implemented-read-only-getter",
         "implemented-validated-setter"},
        {JsApiStructOwner::RoomData, "ex_description", "implemented-read-only-getter",
         "unsupported"},
        {JsApiStructOwner::RoomData, "dir_option", "implemented-read-only-getter", "deferred"},
        {JsApiStructOwner::RoomData, "room_track", "internal-only", "unsupported"},
        {JsApiStructOwner::RoomData, "room_flags", "implemented-read-only-getter", "deferred"},
        {JsApiStructOwner::RoomData, "alignment", "implemented-read-only-getter", "deferred"},
        {JsApiStructOwner::RoomData, "light", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::RoomData, "bfs_dir", "internal-only", "unsupported"},
        {JsApiStructOwner::RoomData, "bfs_next", "internal-only", "unsupported"},
        {JsApiStructOwner::RoomData, "funct", "internal-only", "unsupported"},
        {JsApiStructOwner::RoomData, "contents", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::RoomData, "people", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::RoomData, "affected", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::RoomData, "bleed_track", "internal-only", "unsupported"},
        {JsApiStructOwner::ZoneData, "name", "implemented-read-only-getter",
         "implemented-validated-setter"},
        {JsApiStructOwner::ZoneData, "description", "implemented-read-only-getter",
         "implemented-validated-setter"},
        {JsApiStructOwner::ZoneData, "map", "implemented-read-only-getter",
         "implemented-validated-setter"},
        {JsApiStructOwner::ZoneData, "lifespan", "implemented-read-only-getter",
         "implemented-validated-setter"},
        {JsApiStructOwner::ZoneData, "age", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::ZoneData, "top", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::ZoneData, "x", "implemented-read-only-getter",
         "implemented-validated-setter"},
        {JsApiStructOwner::ZoneData, "y", "implemented-read-only-getter",
         "implemented-validated-setter"},
        {JsApiStructOwner::ZoneData, "symbol", "implemented-read-only-getter",
         "implemented-validated-setter"},
        {JsApiStructOwner::ZoneData, "level", "implemented-read-only-getter",
         "implemented-validated-setter"},
        {JsApiStructOwner::ZoneData, "white_power", "implemented-read-only-getter",
         "unsupported"},
        {JsApiStructOwner::ZoneData, "dark_power", "implemented-read-only-getter",
         "unsupported"},
        {JsApiStructOwner::ZoneData, "magi_power", "implemented-read-only-getter",
         "unsupported"},
        {JsApiStructOwner::ZoneData, "zone_short_description", "deferred", "unsupported"},
        {JsApiStructOwner::ZoneData, "zone_description", "deferred", "unsupported"},
        {JsApiStructOwner::ZoneData, "zone_map", "deferred", "unsupported"},
        {JsApiStructOwner::ZoneData, "min_level_look", "implemented-read-only-getter",
         "deferred"},
        {JsApiStructOwner::ZoneData, "owners", "internal-only", "unsupported"},
        {JsApiStructOwner::ZoneData, "reset_mode", "implemented-read-only-getter",
         "implemented-validated-setter"},
        {JsApiStructOwner::ZoneData, "number", "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::ZoneData, "cmdno", "internal-only", "unsupported"},
        {JsApiStructOwner::ZoneData, "cmd", "internal-only", "unsupported"},
    };

    ASSERT_EQ(js_api_struct_field_mapping_count(), sizeof(expected) / sizeof(expected[0]));
    for (std::size_t index = 0; index < js_api_struct_field_mapping_count(); ++index) {
        const JsApiStructFieldMapping &mapping = js_api_struct_field_mappings()[index];
        const ExpectedFieldPolicy &expected_policy = expected[index];
        const std::string field_id =
            std::string(js_api_struct_owner_name(expected_policy.owner)) + "." +
            expected_policy.source_field;

        EXPECT_EQ(mapping.owner, expected_policy.owner) << field_id;
        EXPECT_STREQ(mapping.source_field, expected_policy.source_field) << field_id;
        EXPECT_STREQ(mapping.getter_status, expected_policy.getter_status) << field_id;
        EXPECT_STREQ(mapping.setter_status, expected_policy.setter_status) << field_id;
    }
}

TEST(JsApiStructMapping, FinalAccessorPolicySummaryStaysIntentional) {
    struct ExpectedOwnerPolicy {
        JsApiStructOwner owner;
        std::size_t total;
        std::size_t implemented_getters;
        std::size_t deferred_getters;
        std::size_t internal_getters;
        std::size_t implemented_setters;
        std::size_t deferred_setters;
        std::size_t unsupported_setters;
    };

    const ExpectedOwnerPolicy expected[] = {
        {JsApiStructOwner::CharData, 36, 24, 1, 11, 0, 5, 31},
        {JsApiStructOwner::ObjData, 17, 13, 0, 4, 4, 3, 10},
        {JsApiStructOwner::RoomData, 19, 14, 0, 5, 4, 3, 12},
        {JsApiStructOwner::ZoneData, 22, 16, 3, 3, 9, 1, 12},
    };

    for (const ExpectedOwnerPolicy &owner_policy : expected) {
        std::size_t total = 0;
        std::size_t implemented_getters = 0;
        std::size_t deferred_getters = 0;
        std::size_t internal_getters = 0;
        std::size_t implemented_setters = 0;
        std::size_t deferred_setters = 0;
        std::size_t unsupported_setters = 0;

        for (std::size_t index = 0; index < js_api_struct_field_mapping_count(); ++index) {
            const JsApiStructFieldMapping &mapping = js_api_struct_field_mappings()[index];
            if (mapping.owner != owner_policy.owner)
                continue;

            ++total;
            const std::string getter_status = mapping.getter_status;
            const std::string setter_status = mapping.setter_status;
            if (getter_status == "implemented-read-only-getter")
                ++implemented_getters;
            else if (getter_status == "deferred")
                ++deferred_getters;
            else if (getter_status == "internal-only")
                ++internal_getters;

            if (setter_status == "implemented-validated-setter")
                ++implemented_setters;
            else if (setter_status == "deferred")
                ++deferred_setters;
            else if (setter_status == "unsupported")
                ++unsupported_setters;
        }

        const char *owner_name = js_api_struct_owner_name(owner_policy.owner);
        EXPECT_EQ(total, owner_policy.total) << owner_name;
        EXPECT_EQ(implemented_getters + deferred_getters + internal_getters, total) << owner_name;
        EXPECT_EQ(implemented_setters + deferred_setters + unsupported_setters, total)
            << owner_name;
        EXPECT_EQ(implemented_getters, owner_policy.implemented_getters) << owner_name;
        EXPECT_EQ(deferred_getters, owner_policy.deferred_getters) << owner_name;
        EXPECT_EQ(internal_getters, owner_policy.internal_getters) << owner_name;
        EXPECT_EQ(implemented_setters, owner_policy.implemented_setters) << owner_name;
        EXPECT_EQ(deferred_setters, owner_policy.deferred_setters) << owner_name;
        EXPECT_EQ(unsupported_setters, owner_policy.unsupported_setters) << owner_name;
    }
}

TEST(JsApiStructMapping, MatchesCurrentStructFieldDeclarations) {
    EXPECT_EQ(catalog_fields(JsApiStructOwner::CharData),
              source_public_fields("src/structs.h", "char_data"));
    EXPECT_EQ(catalog_fields(JsApiStructOwner::ObjData),
              source_public_fields("src/structs.h", "obj_data"));
    EXPECT_EQ(catalog_fields(JsApiStructOwner::RoomData),
              source_public_fields("src/structs.h", "room_data"));
    EXPECT_EQ(catalog_fields(JsApiStructOwner::ZoneData),
              source_public_fields("src/zone.h", "zone_data"));
}

TEST(JsApiStructMapping, HasUniqueOwnerFieldPairs) {
    std::set<std::string> seen;
    for (std::size_t index = 0; index < js_api_struct_field_mapping_count(); ++index) {
        const JsApiStructFieldMapping &mapping = js_api_struct_field_mappings()[index];
        std::string key =
            std::string(js_api_struct_owner_name(mapping.owner)) + "." + mapping.source_field;
        EXPECT_TRUE(seen.insert(key).second) << key;
    }
}

TEST(JsApiStructMapping, HasUniqueJavaScriptPropertiesAndSettersPerOwner) {
    std::set<std::string> properties;
    std::set<std::string> setters;
    for (std::size_t index = 0; index < js_api_struct_field_mapping_count(); ++index) {
        const JsApiStructFieldMapping &mapping = js_api_struct_field_mappings()[index];
        const std::string owner = js_api_struct_owner_name(mapping.owner);
        EXPECT_TRUE(properties.insert(owner + "." + mapping.js_property).second)
            << owner << "." << mapping.js_property;
        EXPECT_TRUE(setters.insert(owner + "." + mapping.setter_name).second)
            << owner << "." << mapping.setter_name;
    }
}

TEST(JsApiStructMapping, ImplementedReadOnlyFieldsExistInApiContract) {
    for (std::size_t index = 0; index < js_api_struct_field_mapping_count(); ++index) {
        const JsApiStructFieldMapping &mapping = js_api_struct_field_mappings()[index];
        if (std::string(mapping.getter_status) != "implemented-read-only-getter")
            continue;

        const char *type_name = nullptr;
        switch (mapping.owner) {
        case JsApiStructOwner::CharData:
            type_name = "Character";
            break;
        case JsApiStructOwner::ObjData:
            type_name = "GameObject";
            break;
        case JsApiStructOwner::RoomData:
            type_name = "Room";
            break;
        case JsApiStructOwner::ZoneData:
            type_name = "Zone";
            break;
        }

        const JsApiType *type = find_js_api_contract_type(type_name);
        ASSERT_NE(type, nullptr) << type_name;
        EXPECT_NE(find_js_api_contract_member(*type, mapping.js_property), nullptr)
            << js_api_struct_owner_name(mapping.owner) << "." << mapping.source_field << " -> "
            << type_name << "." << mapping.js_property;
    }
}

TEST(JsApiStructMapping, PinsPromotedGetterGroupStatus) {
    struct ExpectedPromotedGetter {
        JsApiStructOwner owner;
        const char *field;
        const char *property;
    };

    const ExpectedPromotedGetter expected[] = {
        {JsApiStructOwner::ObjData, "description", "description"},
        {JsApiStructOwner::ObjData, "short_description", "shortDescription"},
        {JsApiStructOwner::RoomData, "description", "description"},
        {JsApiStructOwner::RoomData, "level", "level"},
        {JsApiStructOwner::RoomData, "alignment", "alignment"},
        {JsApiStructOwner::ZoneData, "level", "level"},
    };

    for (const ExpectedPromotedGetter &entry : expected) {
        const JsApiStructFieldMapping *mapping =
            find_js_api_struct_field_mapping(entry.owner, entry.field);
        ASSERT_NE(mapping, nullptr) << entry.field;
        EXPECT_STREQ(mapping->js_property, entry.property);
        EXPECT_STREQ(mapping->getter_status, "implemented-read-only-getter");
        if (std::string(entry.field) == "description" ||
            std::string(entry.field) == "short_description" ||
            ((entry.owner == JsApiStructOwner::ZoneData ||
              entry.owner == JsApiStructOwner::RoomData) &&
             std::string(entry.field) == "level")) {
            EXPECT_STREQ(mapping->setter_status, "implemented-validated-setter");
        } else {
            EXPECT_STRNE(mapping->setter_status, "implemented-validated-setter");
        }
    }
}

TEST(JsApiStructMapping, PinsSecondPromotedGetterGroupStatus) {
    struct ExpectedPromotedGetter {
        JsApiStructOwner owner;
        const char *field;
        const char *property;
    };

    const ExpectedPromotedGetter expected[] = {
        {JsApiStructOwner::ObjData, "action_description", "actionDescription"},
        {JsApiStructOwner::ZoneData, "description", "description"},
        {JsApiStructOwner::ZoneData, "map", "map"},
        {JsApiStructOwner::ZoneData, "lifespan", "lifespan"},
        {JsApiStructOwner::ZoneData, "age", "age"},
        {JsApiStructOwner::ZoneData, "top", "topRoomVnum"},
        {JsApiStructOwner::ZoneData, "x", "x"},
        {JsApiStructOwner::ZoneData, "y", "y"},
        {JsApiStructOwner::ZoneData, "symbol", "symbol"},
        {JsApiStructOwner::ZoneData, "white_power", "whitePower"},
        {JsApiStructOwner::ZoneData, "dark_power", "darkPower"},
        {JsApiStructOwner::ZoneData, "magi_power", "magiPower"},
        {JsApiStructOwner::ZoneData, "min_level_look", "minimumLookLevel"},
        {JsApiStructOwner::ZoneData, "reset_mode", "resetMode"},
    };

    for (const ExpectedPromotedGetter &entry : expected) {
        const JsApiStructFieldMapping *mapping =
            find_js_api_struct_field_mapping(entry.owner, entry.field);
        ASSERT_NE(mapping, nullptr) << entry.field;
        EXPECT_STREQ(mapping->js_property, entry.property);
        EXPECT_STREQ(mapping->getter_status, "implemented-read-only-getter");
        if (std::string(entry.field) == "action_description" ||
            std::string(entry.field) == "description" || std::string(entry.field) == "map" ||
            std::string(entry.field) == "symbol" || std::string(entry.field) == "x" ||
            std::string(entry.field) == "y" || std::string(entry.field) == "reset_mode" ||
            std::string(entry.field) == "lifespan" ||
            (entry.owner == JsApiStructOwner::ZoneData && std::string(entry.field) == "level") ||
            (entry.owner == JsApiStructOwner::RoomData &&
             std::string(entry.field) == "sector_type")) {
            EXPECT_STREQ(mapping->setter_status, "implemented-validated-setter");
        } else {
            EXPECT_STRNE(mapping->setter_status, "implemented-validated-setter");
        }
    }
}

TEST(JsApiStructMapping, ImplementedSetterDocsReferencePersistentAuthority) {
    for (std::size_t index = 0; index < js_api_struct_field_mapping_count(); ++index) {
        const JsApiStructFieldMapping &mapping = js_api_struct_field_mappings()[index];
        if (std::string(mapping.setter_status) != "implemented-validated-setter")
            continue;

        EXPECT_NE(std::string(mapping.setter_docs).find("persistent setter authority"),
                  std::string::npos)
            << js_api_struct_owner_name(mapping.owner) << "." << mapping.source_field;
        EXPECT_NE(std::string(mapping.setter_docs).find("target-scoped"), std::string::npos)
            << js_api_struct_owner_name(mapping.owner) << "." << mapping.source_field;
        EXPECT_NE(std::string(mapping.notes).find("dispatch mutation authority"), std::string::npos)
            << js_api_struct_owner_name(mapping.owner) << "." << mapping.source_field;
        EXPECT_EQ(std::string(mapping.setter_docs).find("deferred"), std::string::npos)
            << js_api_struct_owner_name(mapping.owner) << "." << mapping.source_field;
        EXPECT_EQ(std::string(mapping.notes).find("Snapshot-only"), std::string::npos)
            << js_api_struct_owner_name(mapping.owner) << "." << mapping.source_field;
    }

    const JsApiStructFieldMapping *zone_lifespan =
        find_js_api_struct_field_mapping(JsApiStructOwner::ZoneData, "lifespan");
    ASSERT_NE(zone_lifespan, nullptr);
    EXPECT_STREQ(zone_lifespan->setter_status, "implemented-validated-setter");
    const JsApiStructFieldMapping *room_level =
        find_js_api_struct_field_mapping(JsApiStructOwner::RoomData, "level");
    ASSERT_NE(room_level, nullptr);
    EXPECT_STREQ(room_level->setter_status, "implemented-validated-setter");
}

TEST(JsApiStructMapping, ClassifiesZoneScalarSetterCandidates) {
    struct ExpectedZoneScalar {
        const char *field;
        const char *status;
        const char *setter_doc_text;
        const char *note_text;
    };

    const ExpectedZoneScalar expected[] = {
        {"x", "implemented-validated-setter", "0 through 25",
         "target-scoped dispatch mutation authority"},
        {"y", "implemented-validated-setter", "0 through 25",
         "target-scoped dispatch mutation authority"},
        {"symbol", "implemented-validated-setter", "target-scoped persistent setter authority",
         "target-scoped dispatch mutation authority"},
        {"min_level_look", "deferred", "do not currently persist or edit this value",
         "persisted builder edit path"},
        {"lifespan", "implemented-validated-setter", "1 through 10080",
         "target-scoped dispatch mutation authority"},
        {"reset_mode", "implemented-validated-setter", "0 through 3",
         "target-scoped dispatch mutation authority"},
        {"age", "unsupported", "reset scheduling should own this value", ""},
        {"top", "unsupported", "Changing zone room bounds", "World topology field"},
        {"white_power", "unsupported", "Direct White-side power writes are unsupported",
         "Derived gameplay state"},
        {"dark_power", "unsupported", "Direct Dark-side power writes are unsupported",
         "Derived gameplay state"},
        {"magi_power", "unsupported", "Direct Magi-side power writes are unsupported",
         "Derived gameplay state"},
        {"level", "implemented-validated-setter", "0 through 100",
         "target-scoped dispatch mutation authority"},
    };

    for (const ExpectedZoneScalar &entry : expected) {
        const JsApiStructFieldMapping *mapping =
            find_js_api_struct_field_mapping(JsApiStructOwner::ZoneData, entry.field);
        ASSERT_NE(mapping, nullptr) << entry.field;
        EXPECT_STREQ(mapping->getter_status, "implemented-read-only-getter") << entry.field;
        EXPECT_STREQ(mapping->setter_status, entry.status) << entry.field;
        EXPECT_NE(std::string(mapping->setter_docs).find(entry.setter_doc_text), std::string::npos)
            << entry.field;
        if (entry.note_text[0] != '\0') {
            EXPECT_NE(std::string(mapping->notes).find(entry.note_text), std::string::npos)
                << entry.field;
        }
    }

    const JsApiStructFieldMapping *zone_x =
        find_js_api_struct_field_mapping(JsApiStructOwner::ZoneData, "x");
    ASSERT_NE(zone_x, nullptr);
    EXPECT_EQ(WORLD_SIZE_X / 2, 25);
    EXPECT_NE(std::string(zone_x->setter_docs).find("0 through 25"), std::string::npos);
    EXPECT_NE(std::string(zone_x->setter_docs).find("integer"), std::string::npos);
    EXPECT_NE(std::string(zone_x->setter_docs).find("rejects negative values"), std::string::npos);
    EXPECT_NE(std::string(zone_x->setter_docs).find("values above 25 such as 26"),
              std::string::npos);
    EXPECT_NE(std::string(zone_x->setter_docs).find("fractional or other non-integer values"),
              std::string::npos);
    EXPECT_NE(std::string(zone_x->setter_docs).find("legacy map clamping"), std::string::npos);
    EXPECT_NE(std::string(zone_x->setter_docs).find("redraw the cached world map"),
              std::string::npos);
    EXPECT_NE(std::string(zone_x->setter_docs).find("target-scoped persistent setter authority"),
              std::string::npos);

    const JsApiStructFieldMapping *zone_y =
        find_js_api_struct_field_mapping(JsApiStructOwner::ZoneData, "y");
    ASSERT_NE(zone_y, nullptr);
    EXPECT_EQ(WORLD_SIZE_Y - 1, 25);
    EXPECT_NE(std::string(zone_y->setter_docs).find("0 through 25"), std::string::npos);
    EXPECT_NE(std::string(zone_y->setter_docs).find("accept boundary values 0 and 25"),
              std::string::npos);
    EXPECT_NE(std::string(zone_y->setter_docs).find("reject negative values"), std::string::npos);
    EXPECT_NE(std::string(zone_y->setter_docs).find("reject values above 25 such as 26"),
              std::string::npos);
    EXPECT_NE(
        std::string(zone_y->setter_docs).find("reject fractional or other non-integer values"),
        std::string::npos);
    EXPECT_NE(std::string(zone_y->setter_docs).find("outside the map buffer"), std::string::npos);
    EXPECT_NE(std::string(zone_y->setter_docs).find("redraw the map"), std::string::npos);
    EXPECT_NE(std::string(zone_y->setter_docs).find("target-scoped persistent setter authority"),
              std::string::npos);

    const JsApiStructFieldMapping *zone_symbol =
        find_js_api_struct_field_mapping(JsApiStructOwner::ZoneData, "symbol");
    ASSERT_NE(zone_symbol, nullptr);
    for (const char *fragment : {"single-character", "empty", "multi-character", "control",
                                 "whitespace-only", "target-scoped persistent setter authority"}) {
        EXPECT_NE(std::string(zone_symbol->setter_docs).find(fragment), std::string::npos)
            << fragment;
    }

    struct ExpectedGuardedZoneScalar {
        const char *field;
        const char *status;
        const char *required_text;
    };

    const ExpectedGuardedZoneScalar guarded[] = {
        {"white_power", "unsupported", "future faction-power APIs must own recalculation"},
        {"dark_power", "unsupported", "future faction-power APIs must own recalculation"},
        {"magi_power", "unsupported", "future faction-power APIs must own recalculation"},
        {"owners", "unsupported", "Linked list pointer must never be exposed"},
        {"number", "unsupported", "Changing a loaded zone vnum"},
        {"zone_short_description", "unsupported", "add/update/remove helper APIs"},
        {"zone_description", "unsupported", "add/update/remove helper APIs"},
        {"zone_map", "unsupported", "add/update/remove helper APIs"},
        {"cmdno", "unsupported", "explicit reset-command helpers"},
        {"cmd", "unsupported", "explicit reset-command helpers"},
    };
    for (const ExpectedGuardedZoneScalar &entry : guarded) {
        const JsApiStructFieldMapping *mapping =
            find_js_api_struct_field_mapping(JsApiStructOwner::ZoneData, entry.field);
        ASSERT_NE(mapping, nullptr) << entry.field;
        EXPECT_STREQ(mapping->setter_status, entry.status) << entry.field;
        EXPECT_NE(
            (std::string(mapping->setter_docs) + " " + mapping->notes).find(entry.required_text),
            std::string::npos)
            << entry.field;
    }

    struct ExpectedZoneRemainder {
        const char *field;
        const char *property;
        const char *setter_name;
        const char *type_name;
        bool nullable;
        const char *getter_status;
        const char *setter_status;
        const char *side_effect;
    };

    const ExpectedZoneRemainder remaining[] = {
        {"white_power", "whitePower", "setWhitePower", "number", false,
         "implemented-read-only-getter", "unsupported", "mutation"},
        {"dark_power", "darkPower", "setDarkPower", "number", false, "implemented-read-only-getter",
         "unsupported", "mutation"},
        {"magi_power", "magiPower", "setMagiPower", "number", false, "implemented-read-only-getter",
         "unsupported", "mutation"},
        {"min_level_look", "minimumLookLevel", "setMinimumLookLevel", "number", false,
         "implemented-read-only-getter", "deferred", "mutation"},
        {"age", "age", "setAge", "number", false, "implemented-read-only-getter", "unsupported",
         "mutation"},
        {"top", "topRoomVnum", "setTopRoomVnum", "number", false, "implemented-read-only-getter",
         "unsupported", "none"},
        {"number", "vnum", "setVnum", "number", false, "implemented-read-only-getter",
         "unsupported", "none"},
        {"zone_short_description", "shortDescriptions", "setShortDescriptions",
         "readonly ExtraDescription[]", true, "deferred", "unsupported", "mutation"},
        {"zone_description", "extraDescriptions", "setExtraDescriptions",
         "readonly ExtraDescription[]", true, "deferred", "unsupported", "mutation"},
        {"zone_map", "mapDescriptions", "setMapDescriptions", "readonly ExtraDescription[]", true,
         "deferred", "unsupported", "mutation"},
        {"owners", "owners", "setOwners", "never", true, "internal-only", "unsupported", "none"},
        {"cmdno", "resetCommandCount", "setResetCommandCount", "number", false, "internal-only",
         "unsupported", "none"},
        {"cmd", "resetCommands", "setResetCommands", "never", true, "internal-only", "unsupported",
         "none"},
    };
    for (const ExpectedZoneRemainder &entry : remaining) {
        const JsApiStructFieldMapping *mapping =
            find_js_api_struct_field_mapping(JsApiStructOwner::ZoneData, entry.field);
        ASSERT_NE(mapping, nullptr) << entry.field;
        EXPECT_STREQ(mapping->js_property, entry.property) << entry.field;
        EXPECT_STREQ(mapping->setter_name, entry.setter_name) << entry.field;
        EXPECT_STREQ(mapping->type_name, entry.type_name) << entry.field;
        EXPECT_EQ(mapping->nullable, entry.nullable) << entry.field;
        EXPECT_STREQ(mapping->getter_status, entry.getter_status) << entry.field;
        EXPECT_STREQ(mapping->setter_status, entry.setter_status) << entry.field;
        EXPECT_STREQ(mapping->side_effect, entry.side_effect) << entry.field;
    }
}

TEST(JsApiStructMapping, PinsRoomValueDomainGetterGroupStatus) {
    struct ExpectedPromotedGetter {
        const char *field;
        const char *property;
    };

    const ExpectedPromotedGetter expected[] = {
        {"sector_type", "sectorType"},
        {"room_flags", "flags"},
        {"light", "light"},
    };

    for (const ExpectedPromotedGetter &entry : expected) {
        const JsApiStructFieldMapping *mapping =
            find_js_api_struct_field_mapping(JsApiStructOwner::RoomData, entry.field);
        ASSERT_NE(mapping, nullptr) << entry.field;
        EXPECT_STREQ(mapping->js_property, entry.property);
        EXPECT_STREQ(mapping->getter_status, "implemented-read-only-getter");
        if (std::string(entry.field) == "sector_type") {
            EXPECT_STREQ(mapping->setter_status, "implemented-validated-setter");
            EXPECT_NE(std::string(mapping->setter_docs).find("canonical live sector-name"),
                      std::string::npos);
        } else if (std::string(entry.field) == "light") {
            EXPECT_STREQ(mapping->setter_status, "unsupported");
            EXPECT_NE(std::string(mapping->getter_docs).find("light-source count"),
                      std::string::npos);
            EXPECT_NE(std::string(mapping->setter_docs).find("Direct light counter writes"),
                      std::string::npos);
            EXPECT_NE(std::string(mapping->notes).find("Derived counter"), std::string::npos);
        } else {
            EXPECT_STRNE(mapping->setter_status, "implemented-validated-setter");
        }
    }

    const JsApiStructFieldMapping *room_flags =
        find_js_api_struct_field_mapping(JsApiStructOwner::RoomData, "room_flags");
    ASSERT_NE(room_flags, nullptr);
    EXPECT_NE(std::string(room_flags->getter_docs).find("BFS_MARK"), std::string::npos);
    EXPECT_STREQ(room_flags->setter_status, "deferred");
    EXPECT_NE(std::string(room_flags->setter_docs).find("builder-facing flag vocabulary"),
              std::string::npos);
    EXPECT_NE(std::string(room_flags->setter_docs).find("room-affect synchronization"),
              std::string::npos);
    EXPECT_NE(std::string(room_flags->setter_docs).find("teleport"), std::string::npos);
    EXPECT_NE(std::string(room_flags->notes).find("Raw bitvector"), std::string::npos);
    EXPECT_NE(std::string(room_flags->notes).find("read-only permanentAffect"), std::string::npos);
    EXPECT_NE(std::string(room_flags->notes).find("PERMAFFECT"), std::string::npos);

    const JsApiStructFieldMapping *alignment =
        find_js_api_struct_field_mapping(JsApiStructOwner::RoomData, "alignment");
    ASSERT_NE(alignment, nullptr);
    EXPECT_STREQ(alignment->getter_status, "implemented-read-only-getter");
    EXPECT_STREQ(alignment->setter_status, "deferred");
    EXPECT_NE(std::string(alignment->setter_docs).find("room file writer"), std::string::npos);
    EXPECT_NE(std::string(alignment->setter_docs).find("does not copy alignment"),
              std::string::npos);
    EXPECT_NE(std::string(alignment->notes).find("persistence/editing semantics"),
              std::string::npos);
}

TEST(JsApiStructMapping, PinsCriticalBuilderFacingMappings) {
    struct ExpectedMapping {
        JsApiStructOwner owner;
        const char *field;
        const char *property;
        const char *type_name;
        bool nullable;
        const char *getter_status;
        const char *setter_status;
    };
    const ExpectedMapping expected[] = {
        {JsApiStructOwner::CharData, "nr", "prototypeVnum", "number | null", true,
         "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::CharData, "in_room", "room", "Room | null", true,
         "implemented-read-only-getter", "deferred"},
        {JsApiStructOwner::ObjData, "item_number", "vnum", "number | null", true,
         "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::ObjData, "carried_by", "carriedBy", "Character | null", true,
         "implemented-read-only-getter", "deferred"},
        {JsApiStructOwner::ObjData, "obj_flags", "flags", "ObjectFlags", false,
         "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::RoomData, "number", "vnum", "number", false,
         "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::RoomData, "zone", "zone", "Zone | null", true,
         "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::ZoneData, "number", "vnum", "number", false,
         "implemented-read-only-getter", "unsupported"},
        {JsApiStructOwner::ZoneData, "name", "name", "string", false,
         "implemented-read-only-getter", "implemented-validated-setter"},
    };

    for (const ExpectedMapping &item : expected) {
        const JsApiStructFieldMapping *mapping =
            find_js_api_struct_field_mapping(item.owner, item.field);
        ASSERT_NE(mapping, nullptr) << item.field;
        EXPECT_STREQ(mapping->js_property, item.property) << item.field;
        EXPECT_STREQ(mapping->type_name, item.type_name) << item.field;
        EXPECT_EQ(mapping->nullable, item.nullable) << item.field;
        EXPECT_STREQ(mapping->getter_status, item.getter_status) << item.field;
        EXPECT_STREQ(mapping->setter_status, item.setter_status) << item.field;
    }
}

TEST(JsApiStructMapping, PinsObjectDeferredClassificationMappings) {
    struct ExpectedMapping {
        const char *field;
        const char *property;
        const char *type_name;
        const char *getter_status;
        const char *setter_status;
        const char *side_effect;
        const char *getter_docs_fragment;
        const char *notes_fragment;
    };
    const ExpectedMapping expected[] = {
        {"affected", "affects", "readonly ObjectAffect[]", "implemented-read-only-getter",
         "unsupported", "mutation", "canonical slot ordering", "read-only diagnostic snapshot"},
        {"ex_description", "extraDescriptions", "readonly ExtraDescription[]",
         "implemented-read-only-getter", "unsupported", "mutation", "keyword and description text",
         "bounded frozen text snapshot"},
        {"owner", "ownerId", "never", "internal-only", "unsupported", "none",
         "sensitive authorization data", "identity policy"},
        {"in_obj", "container", "EquipmentObjectSnapshot | null", "implemented-read-only-getter",
         "deferred", "world-mutation", "reciprocally contains",
         "shallow frozen container snapshot"},
        {"contains", "contents", "readonly EquipmentObjectSnapshot[]",
         "implemented-read-only-getter", "unsupported", "world-mutation",
         "directly contained live objects", "shallow frozen contents snapshots"},
        {"next_content", "nextContent", "never", "internal-only", "unsupported", "none",
         "traversal state is internal", "Internal traversal link"},
        {"next", "next", "never", "internal-only", "unsupported", "none",
         "traversal state is internal", "Internal traversal link"},
        {"touched", "touched", "boolean", "implemented-read-only-getter", "unsupported", "mutation",
         "Any nonzero stored value is exposed as true", "normalized read-only boolean"},
        {"loaded_by", "loadedBy", "number", "internal-only", "unsupported", "none",
         "administrative audit data", "Administrative audit data"},
    };

    for (const ExpectedMapping &item : expected) {
        const JsApiStructFieldMapping *mapping =
            find_js_api_struct_field_mapping(JsApiStructOwner::ObjData, item.field);
        ASSERT_NE(mapping, nullptr) << item.field;
        EXPECT_STREQ(mapping->js_property, item.property) << item.field;
        EXPECT_STREQ(mapping->type_name, item.type_name) << item.field;
        EXPECT_STREQ(mapping->getter_status, item.getter_status) << item.field;
        EXPECT_STREQ(mapping->setter_status, item.setter_status) << item.field;
        EXPECT_STREQ(mapping->side_effect, item.side_effect) << item.field;
        EXPECT_NE(std::string(mapping->getter_docs).find(item.getter_docs_fragment),
                  std::string::npos)
            << item.field;
        EXPECT_NE(std::string(mapping->notes).find(item.notes_fragment), std::string::npos)
            << item.field;
    }
}

TEST(JsApiStructMapping, PinsObjectRelationshipAndLifecycleSetterDeferrals) {
    struct ExpectedDeferredSetter {
        const char *field;
        const char *getter_status;
        const char *setter_status;
        const char *required_text;
    };

    const ExpectedDeferredSetter deferred[] = {
        {"in_room", "implemented-read-only-getter", "deferred", "handler-maintained carrier"},
        {"carried_by", "implemented-read-only-getter", "deferred", "player crash-save"},
        {"in_obj", "implemented-read-only-getter", "deferred", "cycle prevention"},
        {"contains", "implemented-read-only-getter", "unsupported", "cycle guards"},
        {"touched", "implemented-read-only-getter", "unsupported",
         "runtime/player-interaction state"},
    };

    for (const ExpectedDeferredSetter &entry : deferred) {
        const JsApiStructFieldMapping *mapping =
            find_js_api_struct_field_mapping(JsApiStructOwner::ObjData, entry.field);
        ASSERT_NE(mapping, nullptr) << entry.field;
        EXPECT_STREQ(mapping->getter_status, entry.getter_status) << entry.field;
        EXPECT_STREQ(mapping->setter_status, entry.setter_status) << entry.field;
        EXPECT_NE(
            (std::string(mapping->setter_docs) + " " + mapping->notes).find(entry.required_text),
            std::string::npos)
            << entry.field;
    }
}

TEST(JsApiStructMapping, PinsCharacterRelationshipAndStateSetterDeferrals) {
    struct ExpectedCharacterDeferral {
        const char *field;
        const char *property;
        const char *setter_name;
        const char *type_name;
        bool nullable;
        const char *getter_status;
        const char *setter_status;
        const char *side_effect;
        const char *required_text;
    };

    const ExpectedCharacterDeferral deferred[] = {
        {"in_room", "room", "setRoom", "Room | null", true, "implemented-read-only-getter",
         "deferred", "world-mutation", "movement triggers"},
        {"affected", "affects", "setAffects", "readonly Affect[]", true,
         "implemented-read-only-getter", "deferred", "world-mutation", "duration accounting"},
        {"equipment", "equipment", "setEquipmentSlot", "readonly EquipmentSlot[]", false,
         "implemented-read-only-getter", "deferred", "world-mutation", "ON_WEAR"},
        {"carrying", "inventory", "setInventory", "readonly InventoryObjectSnapshot[]", true,
         "implemented-read-only-getter", "unsupported", "world-mutation", "carried weight"},
        {"followers", "followers", "setFollowers", "readonly CharacterRelationshipSnapshot[]", true,
         "implemented-read-only-getter", "unsupported", "world-mutation", "follower caps"},
        {"master", "master", "setMaster", "CharacterRelationshipSnapshot | null", true,
         "implemented-read-only-getter", "deferred", "world-mutation", "loop prevention"},
        {"mount_data", "mount", "setMount", "MountData", false, "implemented-read-only-getter",
         "deferred", "world-mutation", "rider back-pointers"},
        {"group", "group", "setGroup", "Group | null", true, "deferred", "unsupported",
         "world-mutation", "leader/member list integrity"},
        {"classpoints", "classPoints", "setClassPoints", "number", false,
         "implemented-read-only-getter", "unsupported", "mutation", "account/admin audit"},
        {"interrupt_count", "interruptCount", "setInterruptCount", "number", false,
         "implemented-read-only-getter", "unsupported", "mutation", "wait-state interactions"},
        {"interrupt_time", "interruptTime", "setInterruptTime", "number", false,
         "implemented-read-only-getter", "unsupported", "mutation", "wait-state interactions"},
        {"spec_busy", "specialBusy", "setSpecialBusy", "boolean", false,
         "implemented-read-only-getter", "unsupported", "mutation", "special-procedure reentrancy"},
    };

    for (const ExpectedCharacterDeferral &entry : deferred) {
        const JsApiStructFieldMapping *mapping =
            find_js_api_struct_field_mapping(JsApiStructOwner::CharData, entry.field);
        ASSERT_NE(mapping, nullptr) << entry.field;
        EXPECT_STREQ(mapping->js_property, entry.property) << entry.field;
        EXPECT_STREQ(mapping->setter_name, entry.setter_name) << entry.field;
        EXPECT_STREQ(mapping->type_name, entry.type_name) << entry.field;
        EXPECT_EQ(mapping->nullable, entry.nullable) << entry.field;
        EXPECT_STREQ(mapping->getter_status, entry.getter_status) << entry.field;
        EXPECT_STREQ(mapping->setter_status, entry.setter_status) << entry.field;
        EXPECT_STREQ(mapping->side_effect, entry.side_effect) << entry.field;
        EXPECT_NE(
            (std::string(mapping->setter_docs) + " " + mapping->notes).find(entry.required_text),
            std::string::npos)
            << entry.field;
    }
}

TEST(JsApiStructMapping, PinsObjectAndRoomNestedListSetterDeferrals) {
    struct ExpectedNestedListDeferral {
        JsApiStructOwner owner;
        const char *field;
        const char *property;
        const char *setter_name;
        const char *type_name;
        bool nullable;
        const char *getter_status;
        const char *setter_status;
        const char *side_effect;
        const char *required_text;
    };

    const ExpectedNestedListDeferral deferred[] = {
        {JsApiStructOwner::ObjData, "obj_flags", "flags", "setFlags", "ObjectFlags", false,
         "implemented-read-only-getter", "unsupported", "mutation", "separate named helper APIs"},
        {JsApiStructOwner::ObjData, "affected", "affects", "setAffects", "readonly ObjectAffect[]",
         false, "implemented-read-only-getter", "unsupported", "mutation", "slot-specific helper"},
        {JsApiStructOwner::ObjData, "ex_description", "extraDescriptions", "setExtraDescriptions",
         "readonly ExtraDescription[]", false, "implemented-read-only-getter", "unsupported",
         "mutation", "add/update/remove helper APIs"},
        {JsApiStructOwner::RoomData, "ex_description", "extraDescriptions", "setExtraDescriptions",
         "readonly ExtraDescription[]", false, "implemented-read-only-getter", "unsupported",
         "mutation", "add/update/remove helper APIs"},
        {JsApiStructOwner::RoomData, "dir_option", "exits", "setExit", "readonly RoomExit[]", false,
         "implemented-read-only-getter", "deferred", "world-mutation", "destination-room"},
        {JsApiStructOwner::RoomData, "contents", "contents", "setContents",
         "readonly EquipmentObjectSnapshot[]", false, "implemented-read-only-getter", "unsupported",
         "world-mutation", "shallow frozen contents snapshots"},
        {JsApiStructOwner::RoomData, "people", "characters", "setCharacters",
         "readonly CharacterRelationshipSnapshot[]", false, "implemented-read-only-getter",
         "unsupported", "world-mutation", "shallow frozen occupant snapshots"},
        {JsApiStructOwner::RoomData, "affected", "affects", "setAffects", "readonly Affect[]",
         false,
         "implemented-read-only-getter", "unsupported", "world-mutation",
         "shallow frozen affect snapshots"},
    };

    for (const ExpectedNestedListDeferral &entry : deferred) {
        const JsApiStructFieldMapping *mapping =
            find_js_api_struct_field_mapping(entry.owner, entry.field);
        ASSERT_NE(mapping, nullptr) << entry.field;
        EXPECT_STREQ(mapping->js_property, entry.property) << entry.field;
        EXPECT_STREQ(mapping->setter_name, entry.setter_name) << entry.field;
        EXPECT_STREQ(mapping->type_name, entry.type_name) << entry.field;
        EXPECT_EQ(mapping->nullable, entry.nullable) << entry.field;
        EXPECT_STREQ(mapping->getter_status, entry.getter_status) << entry.field;
        EXPECT_STREQ(mapping->setter_status, entry.setter_status) << entry.field;
        EXPECT_STREQ(mapping->side_effect, entry.side_effect) << entry.field;
        EXPECT_NE(
            (std::string(mapping->setter_docs) + " " + mapping->notes).find(entry.required_text),
            std::string::npos)
            << entry.field;
    }
}

TEST(JsApiStructMapping, PinsCharacterNestedProfileAndStatSetterDeferrals) {
    struct ExpectedCharacterDeferral {
        const char *field;
        const char *property;
        const char *setter_name;
        const char *type_name;
        bool nullable;
        const char *getter_status;
        const char *setter_status;
        const char *side_effect;
        const char *required_text;
    };

    const ExpectedCharacterDeferral deferred[] = {
        {"player", "profile", "setProfile", "CharacterProfile", false,
         "implemented-read-only-getter", "unsupported", "mutation", "account-backed fields"},
        {"abilities", "baseAbilities", "setBaseAbilities", "AbilityScores", false,
         "implemented-read-only-getter", "unsupported", "mutation", "derived stat recalculation"},
        {"tmpabilities", "currentAbilities", "setCurrentAbilities", "AbilityScores", false,
         "implemented-read-only-getter", "unsupported", "mutation", "active affects"},
        {"constabilities", "rolledAbilities", "setRolledAbilities", "AbilityScores", false,
         "implemented-read-only-getter", "unsupported", "mutation", "character-creation history"},
        {"points", "points", "setPoints", "CharacterPoints", false, "implemented-read-only-getter",
         "unsupported", "mutation", "death handling"},
        {"specials", "specials", "setSpecials", "CharacterSpecials", false,
         "implemented-read-only-getter", "unsupported", "mutation", "combat targets"},
        {"specials2", "specials2", "setSpecials2", "CharacterSpecials2", false,
         "implemented-read-only-getter", "unsupported", "mutation", "player/NPC flags"},
        {"profs", "professions", "setProfessions", "readonly Profession[]", true,
         "implemented-read-only-getter", "unsupported", "mutation", "skill recalculation"},
        {"extra_specialization_data", "specializations", "setSpecializations", "SpecializationData",
         false, "implemented-read-only-getter", "unsupported", "mutation",
         "class-specific invariants"},
        {"damage_details", "damageDetails", "setDamageDetails", "DamageDetails", false,
         "implemented-read-only-getter", "unsupported", "mutation", "combat participation"},
        {"skills", "skills", "setSkill", "readonly SkillValue[]", true,
         "implemented-read-only-getter", "unsupported", "mutation", "practice sessions"},
        {"knowledge", "knowledge", "setKnowledge", "readonly KnowledgeValue[]", true,
         "implemented-read-only-getter", "unsupported", "mutation", "recalculation helpers"},
    };

    for (const ExpectedCharacterDeferral &entry : deferred) {
        const JsApiStructFieldMapping *mapping =
            find_js_api_struct_field_mapping(JsApiStructOwner::CharData, entry.field);
        ASSERT_NE(mapping, nullptr) << entry.field;
        EXPECT_STREQ(mapping->js_property, entry.property) << entry.field;
        EXPECT_STREQ(mapping->setter_name, entry.setter_name) << entry.field;
        EXPECT_STREQ(mapping->type_name, entry.type_name) << entry.field;
        EXPECT_EQ(mapping->nullable, entry.nullable) << entry.field;
        EXPECT_STREQ(mapping->getter_status, entry.getter_status) << entry.field;
        EXPECT_STREQ(mapping->setter_status, entry.setter_status) << entry.field;
        EXPECT_STREQ(mapping->side_effect, entry.side_effect) << entry.field;
        EXPECT_NE(
            (std::string(mapping->setter_docs) + " " + mapping->notes).find(entry.required_text),
            std::string::npos)
            << entry.field;
    }
}

TEST(JsApiStructMapping, UnsafeImplementationFieldsStayUnavailable) {
    const char *internal_character_fields[] = {
        "abs_number",       "player_index",  "desc", "next_in_room", "next",     "next_fighting",
        "next_fast_update", "master_number", "temp", "delay",        "next_die",
    };
    for (const char *field : internal_character_fields) {
        const JsApiStructFieldMapping *mapping =
            find_js_api_struct_field_mapping(JsApiStructOwner::CharData, field);
        ASSERT_NE(mapping, nullptr) << field;
        EXPECT_STREQ(mapping->getter_status, "internal-only");
        EXPECT_STREQ(mapping->setter_status, "unsupported");
    }

    const char *internal_object_fields[] = {
        "owner",
        "next_content",
        "next",
        "loaded_by",
    };
    for (const char *field : internal_object_fields) {
        const JsApiStructFieldMapping *mapping =
            find_js_api_struct_field_mapping(JsApiStructOwner::ObjData, field);
        ASSERT_NE(mapping, nullptr) << field;
        EXPECT_STREQ(mapping->getter_status, "internal-only");
        EXPECT_STREQ(mapping->setter_status, "unsupported");
    }

    const char *internal_room_fields[] = {
        "room_track", "bfs_dir", "bfs_next", "funct", "bleed_track",
    };
    for (const char *field : internal_room_fields) {
        const JsApiStructFieldMapping *mapping =
            find_js_api_struct_field_mapping(JsApiStructOwner::RoomData, field);
        ASSERT_NE(mapping, nullptr) << field;
        EXPECT_STREQ(mapping->getter_status, "internal-only");
        EXPECT_STREQ(mapping->setter_status, "unsupported");
    }

    const char *internal_zone_fields[] = {
        "owners",
        "cmdno",
        "cmd",
    };
    for (const char *field : internal_zone_fields) {
        const JsApiStructFieldMapping *mapping =
            find_js_api_struct_field_mapping(JsApiStructOwner::ZoneData, field);
        ASSERT_NE(mapping, nullptr) << field;
        EXPECT_STREQ(mapping->getter_status, "internal-only");
        EXPECT_STREQ(mapping->setter_status, "unsupported");
    }
}

TEST(JsApiStructMapping, RejectsMalformedLookups) {
    EXPECT_EQ(find_js_api_struct_field_mapping(JsApiStructOwner::CharData, nullptr), nullptr);
    EXPECT_EQ(find_js_api_struct_field_mapping(JsApiStructOwner::CharData, "missing"), nullptr);
    EXPECT_STREQ(js_api_struct_owner_name(static_cast<JsApiStructOwner>(999)), "unknown");
}
