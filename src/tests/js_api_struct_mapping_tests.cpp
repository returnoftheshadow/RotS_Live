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
        {"white_power", "unsupported", "Derived gameplay state"},
        {"dark_power", "unsupported", "Derived gameplay state"},
        {"magi_power", "unsupported", "Derived gameplay state"},
        {"owners", "unsupported", "Linked list pointer must never be exposed"},
        {"number", "unsupported", "Changing a loaded zone vnum"},
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
         "implemented-read-only-getter", "deferred"},
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
        {"affected", "affects", "readonly ObjectAffect[]", "deferred", "deferred", "mutation",
         "Future entries must use named apply locations",
         "no builder getter is emitted yet"},
        {"ex_description", "extraDescriptions", "readonly ExtraDescription[]", "deferred",
         "deferred", "mutation", "bounded list size", "not exposed to builders"},
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
        {"touched", "touched", "boolean", "deferred", "deferred", "mutation",
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
