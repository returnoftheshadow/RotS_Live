#include "../js_api_contract.h"
#include "../js_api_struct_mapping.h"
#include "../structs.h"

#include <gtest/gtest.h>

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

TEST(JsApiStructMapping, PublicMappingsHaveFinalExplicitAccessorPolicy)
{
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
            EXPECT_TRUE(setter_status == "deferred" || setter_status == "unsupported")
                << field_id;
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
        EXPECT_NE(std::string(mapping.setter_docs).find("target-scoped"),
            std::string::npos)
            << js_api_struct_owner_name(mapping.owner) << "." << mapping.source_field;
        EXPECT_NE(std::string(mapping.notes).find("dispatch mutation authority"),
            std::string::npos)
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

TEST(JsApiStructMapping, ClassifiesZoneScalarSetterCandidates)
{
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
        EXPECT_NE(std::string(mapping->setter_docs).find(entry.setter_doc_text),
            std::string::npos)
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
    EXPECT_NE(std::string(zone_y->setter_docs).find("reject fractional or other non-integer values"),
        std::string::npos);
    EXPECT_NE(std::string(zone_y->setter_docs).find("outside the map buffer"), std::string::npos);
    EXPECT_NE(std::string(zone_y->setter_docs).find("redraw the map"), std::string::npos);
    EXPECT_NE(std::string(zone_y->setter_docs).find("target-scoped persistent setter authority"),
        std::string::npos);

    const JsApiStructFieldMapping *zone_symbol =
        find_js_api_struct_field_mapping(JsApiStructOwner::ZoneData, "symbol");
    ASSERT_NE(zone_symbol, nullptr);
    for (const char *fragment :
        {"single-character", "empty", "multi-character", "control", "whitespace-only",
            "target-scoped persistent setter authority"}) {
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
        EXPECT_NE((std::string(mapping->setter_docs) + " " + mapping->notes)
                      .find(entry.required_text),
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
            "implemented-read-only-getter",
            "unsupported", "mutation"},
        {"dark_power", "darkPower", "setDarkPower", "number", false,
            "implemented-read-only-getter",
            "unsupported", "mutation"},
        {"magi_power", "magiPower", "setMagiPower", "number", false,
            "implemented-read-only-getter",
            "unsupported", "mutation"},
        {"min_level_look", "minimumLookLevel", "setMinimumLookLevel", "number", false,
            "implemented-read-only-getter", "deferred", "mutation"},
        {"age", "age", "setAge", "number", false, "implemented-read-only-getter",
            "unsupported", "mutation"},
        {"top", "topRoomVnum", "setTopRoomVnum", "number", false,
            "implemented-read-only-getter", "unsupported", "none"},
        {"number", "vnum", "setVnum", "number", false, "implemented-read-only-getter",
            "unsupported", "none"},
        {"zone_short_description", "shortDescriptions", "setShortDescriptions",
            "readonly ExtraDescription[]", true, "deferred", "unsupported", "mutation"},
        {"zone_description", "extraDescriptions", "setExtraDescriptions",
            "readonly ExtraDescription[]", true, "deferred", "unsupported", "mutation"},
        {"zone_map", "mapDescriptions", "setMapDescriptions",
            "readonly ExtraDescription[]", true, "deferred", "unsupported", "mutation"},
        {"owners", "owners", "setOwners", "never", true, "internal-only", "unsupported", "none"},
        {"cmdno", "resetCommandCount", "setResetCommandCount", "number", false,
            "internal-only", "unsupported", "none"},
        {"cmd", "resetCommands", "setResetCommands", "never", true, "internal-only",
            "unsupported", "none"},
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
    EXPECT_NE(std::string(room_flags->notes).find("read-only permanentAffect"),
        std::string::npos);
    EXPECT_NE(std::string(room_flags->notes).find("PERMAFFECT"), std::string::npos);

    const JsApiStructFieldMapping *alignment =
        find_js_api_struct_field_mapping(JsApiStructOwner::RoomData, "alignment");
    ASSERT_NE(alignment, nullptr);
    EXPECT_STREQ(alignment->getter_status, "implemented-read-only-getter");
    EXPECT_STREQ(alignment->setter_status, "deferred");
    EXPECT_NE(std::string(alignment->setter_docs).find("room file writer"),
        std::string::npos);
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
        {"affected", "affects", "readonly ObjectAffect[]", "deferred", "unsupported", "mutation",
         "Future entries must use named apply locations",
         "no builder getter is emitted yet"},
        {"ex_description", "extraDescriptions", "readonly ExtraDescription[]", "deferred",
         "unsupported", "mutation", "bounded list size", "not exposed to builders"},
        {"owner", "ownerId", "never", "internal-only", "unsupported", "none",
         "sensitive authorization data", "identity policy"},
        {"in_obj", "container", "GameObject | null", "deferred", "deferred", "world-mutation",
         "liveness checks, cycle guards", "not exposed to builders"},
        {"contains", "contents", "readonly GameObject[]", "deferred", "unsupported",
         "world-mutation", "bounded traversal", "not exposed to builders"},
        {"next_content", "nextContent", "never", "internal-only", "unsupported", "none",
         "traversal state is internal", "Internal traversal link"},
        {"next", "next", "never", "internal-only", "unsupported", "none",
         "traversal state is internal", "Internal traversal link"},
        {"touched", "touched", "boolean", "deferred", "unsupported", "mutation",
         "gameplay meaning are confirmed", "normalized to boolean"},
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
        {"in_obj", "deferred", "deferred", "cycle prevention"},
        {"contains", "deferred", "unsupported", "cycle guards"},
        {"touched", "deferred", "unsupported", "runtime/player-interaction state"},
    };

    for (const ExpectedDeferredSetter &entry : deferred) {
        const JsApiStructFieldMapping *mapping =
            find_js_api_struct_field_mapping(JsApiStructOwner::ObjData, entry.field);
        ASSERT_NE(mapping, nullptr) << entry.field;
        EXPECT_STREQ(mapping->getter_status, entry.getter_status) << entry.field;
        EXPECT_STREQ(mapping->setter_status, entry.setter_status) << entry.field;
        EXPECT_NE((std::string(mapping->setter_docs) + " " + mapping->notes)
                      .find(entry.required_text),
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
        {"affected", "affects", "setAffects", "readonly Affect[]", true, "deferred", "deferred",
            "world-mutation", "duration accounting"},
        {"equipment", "equipment", "setEquipmentSlot", "readonly (GameObject | null)[]", false,
            "deferred", "deferred", "world-mutation", "ON_WEAR"},
        {"carrying", "inventory", "setInventory", "readonly GameObject[]", true, "deferred",
            "unsupported", "world-mutation", "carried weight"},
        {"followers", "followers", "setFollowers", "readonly Character[]", true, "deferred",
            "unsupported", "world-mutation", "follower caps"},
        {"master", "master", "setMaster", "Character | null", true, "deferred", "deferred",
            "world-mutation", "loop prevention"},
        {"mount_data", "mount", "setMount", "MountData", false, "deferred", "deferred",
            "world-mutation", "rider back-pointers"},
        {"group", "group", "setGroup", "Group | null", true, "deferred", "unsupported",
            "world-mutation", "leader/member list integrity"},
        {"classpoints", "classPoints", "setClassPoints", "number", false,
            "implemented-read-only-getter",
            "unsupported", "mutation", "account/admin audit"},
        {"interrupt_count", "interruptCount", "setInterruptCount", "number", false,
            "implemented-read-only-getter",
            "unsupported", "mutation", "wait-state interactions"},
        {"interrupt_time", "interruptTime", "setInterruptTime", "number", false,
            "implemented-read-only-getter",
            "unsupported", "mutation", "wait-state interactions"},
        {"spec_busy", "specialBusy", "setSpecialBusy", "boolean", false,
            "implemented-read-only-getter",
            "unsupported", "mutation", "special-procedure reentrancy"},
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
        EXPECT_NE((std::string(mapping->setter_docs) + " " + mapping->notes)
                      .find(entry.required_text),
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
        {JsApiStructOwner::ObjData, "affected", "affects", "setAffects",
            "readonly ObjectAffect[]", false, "deferred", "unsupported", "mutation",
            "slot-specific helper"},
        {JsApiStructOwner::ObjData, "ex_description", "extraDescriptions",
            "setExtraDescriptions", "readonly ExtraDescription[]", true, "deferred", "unsupported",
            "mutation", "add/update/remove helper APIs"},
        {JsApiStructOwner::RoomData, "ex_description", "extraDescriptions",
            "setExtraDescriptions", "readonly ExtraDescription[]", true, "deferred", "unsupported",
            "mutation", "add/update/remove helper APIs"},
        {JsApiStructOwner::RoomData, "dir_option", "exits", "setExit", "readonly RoomExit[]",
            false, "deferred", "deferred", "world-mutation", "destination-room"},
        {JsApiStructOwner::RoomData, "contents", "contents", "setContents",
            "readonly GameObject[]", true, "deferred", "unsupported", "world-mutation",
            "object movement/load/extract helpers"},
        {JsApiStructOwner::RoomData, "people", "characters", "setCharacters",
            "readonly Character[]", true, "deferred", "unsupported", "world-mutation",
            "movement/teleport helpers"},
        {JsApiStructOwner::RoomData, "affected", "affects", "setAffects", "readonly Affect[]",
            true, "deferred", "unsupported", "world-mutation", "room flag synchronization"},
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
        EXPECT_NE((std::string(mapping->setter_docs) + " " + mapping->notes)
                      .find(entry.required_text),
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
        {"player", "profile", "setProfile", "CharacterProfile", false, "deferred",
            "unsupported", "mutation", "account-backed fields"},
        {"abilities", "baseAbilities", "setBaseAbilities", "AbilityScores", false,
            "implemented-read-only-getter", "unsupported", "mutation", "derived stat recalculation"},
        {"tmpabilities", "currentAbilities", "setCurrentAbilities", "AbilityScores", false,
            "implemented-read-only-getter", "unsupported", "mutation", "active affects"},
        {"constabilities", "rolledAbilities", "setRolledAbilities", "AbilityScores", false,
            "implemented-read-only-getter", "unsupported", "mutation",
            "character-creation history"},
        {"points", "points", "setPoints", "CharacterPoints", false,
            "implemented-read-only-getter", "unsupported", "mutation", "death handling"},
        {"specials", "specials", "setSpecials", "CharacterSpecials", false,
            "implemented-read-only-getter", "unsupported", "mutation", "combat targets"},
        {"specials2", "specials2", "setSpecials2", "CharacterSpecials2", false,
            "implemented-read-only-getter", "unsupported", "mutation", "player/NPC flags"},
        {"profs", "professions", "setProfessions", "readonly Profession[]", true,
            "implemented-read-only-getter", "unsupported", "mutation", "skill recalculation"},
        {"extra_specialization_data", "specializations", "setSpecializations",
            "SpecializationData", false, "implemented-read-only-getter", "unsupported", "mutation",
            "class-specific invariants"},
        {"damage_details", "damageDetails", "setDamageDetails", "DamageDetails", false,
            "implemented-read-only-getter", "unsupported", "mutation", "combat participation"},
        {"skills", "skills", "setSkill", "readonly SkillValue[]", true,
            "implemented-read-only-getter",
            "unsupported", "mutation", "practice sessions"},
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
        EXPECT_NE((std::string(mapping->setter_docs) + " " + mapping->notes)
                      .find(entry.required_text),
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
