#include "../objects_json.h"

#include <gtest/gtest.h>

#include <limits>

namespace {

objects_json::ObjectRecord make_object_record(int item_number, int wear_pos)
{
    objects_json::ObjectRecord record;
    record.item_number = item_number;
    record.values = { 1, 2, 3, 4, 5 };
    record.extra_flags = 42;
    record.weight = 8;
    record.timer = 9;
    record.bitvector = 1234;
    record.affects[0].location = APPLY_STR;
    record.affects[0].modifier = 2;
    record.affects[1].location = APPLY_OB;
    record.affects[1].modifier = 5;
    record.wear_pos = wear_pos;
    record.loaded_by = 101;
    return record;
}

objects_json::ObjectSaveData make_object_save_data()
{
    objects_json::ObjectSaveData data;
    data.rent.time = 1700000000;
    data.rent.rentcode = RENT_CRASH;
    data.rent.net_cost_per_hour = 25;
    data.rent.gold = 500;
    data.rent.nitems = 2;
    data.rent.spare0 = 1;
    data.rent.spare7 = 7;
    data.objects.push_back(make_object_record(1200, WEAR_HEAD));
    data.objects.push_back(make_object_record(1201, MAX_WEAR));
    data.board_points[0] = 11;
    data.board_points[1] = 22;
    data.aliases.push_back({ "assist", "kill orc" });
    data.aliases.push_back({ "retreat", "flee" });

    objects_json::FollowerData follower;
    follower.fol_vnum = 4001;
    follower.mount_vnum = 0;
    follower.wimpy = 12;
    follower.exp = 3456;
    follower.flag_config = 2;
    follower.objects.push_back(make_object_record(2200, WEAR_BODY));
    data.followers.push_back(std::move(follower));
    return data;
}

void expect_object_save_data_equal(const objects_json::ObjectSaveData& expected, const objects_json::ObjectSaveData& actual)
{
    EXPECT_EQ(actual.version, expected.version);
    EXPECT_EQ(actual.rent.time, expected.rent.time);
    EXPECT_EQ(actual.rent.rentcode, expected.rent.rentcode);
    EXPECT_EQ(actual.rent.net_cost_per_hour, expected.rent.net_cost_per_hour);
    EXPECT_EQ(actual.rent.gold, expected.rent.gold);
    EXPECT_EQ(actual.rent.nitems, expected.rent.nitems);
    EXPECT_EQ(actual.rent.spare0, expected.rent.spare0);
    EXPECT_EQ(actual.rent.spare7, expected.rent.spare7);
    EXPECT_EQ(actual.board_points, expected.board_points);

    ASSERT_EQ(actual.objects.size(), expected.objects.size());
    for (size_t index = 0; index < expected.objects.size(); ++index) {
        const auto& expected_object = expected.objects[index];
        const auto& actual_object = actual.objects[index];
        EXPECT_EQ(actual_object.item_number, expected_object.item_number);
        EXPECT_EQ(actual_object.values, expected_object.values);
        EXPECT_EQ(actual_object.extra_flags, expected_object.extra_flags);
        EXPECT_EQ(actual_object.weight, expected_object.weight);
        EXPECT_EQ(actual_object.timer, expected_object.timer);
        EXPECT_EQ(actual_object.bitvector, expected_object.bitvector);
        EXPECT_EQ(actual_object.wear_pos, expected_object.wear_pos);
        EXPECT_EQ(actual_object.loaded_by, expected_object.loaded_by);
        EXPECT_EQ(actual_object.affects.size(), expected_object.affects.size());
        for (size_t affect_index = 0; affect_index < expected_object.affects.size(); ++affect_index) {
            EXPECT_EQ(actual_object.affects[affect_index].location, expected_object.affects[affect_index].location);
            EXPECT_EQ(actual_object.affects[affect_index].modifier, expected_object.affects[affect_index].modifier);
        }
    }

    ASSERT_EQ(actual.aliases.size(), expected.aliases.size());
    for (size_t index = 0; index < expected.aliases.size(); ++index) {
        EXPECT_EQ(actual.aliases[index].keyword, expected.aliases[index].keyword);
        EXPECT_EQ(actual.aliases[index].command, expected.aliases[index].command);
    }

    ASSERT_EQ(actual.followers.size(), expected.followers.size());
    for (size_t index = 0; index < expected.followers.size(); ++index) {
        const auto& expected_follower = expected.followers[index];
        const auto& actual_follower = actual.followers[index];
        EXPECT_EQ(actual_follower.fol_vnum, expected_follower.fol_vnum);
        EXPECT_EQ(actual_follower.mount_vnum, expected_follower.mount_vnum);
        EXPECT_EQ(actual_follower.wimpy, expected_follower.wimpy);
        EXPECT_EQ(actual_follower.exp, expected_follower.exp);
        EXPECT_EQ(actual_follower.flag_config, expected_follower.flag_config);
        EXPECT_EQ(actual_follower.spare1, expected_follower.spare1);
        EXPECT_EQ(actual_follower.spare2, expected_follower.spare2);
        ASSERT_EQ(actual_follower.objects.size(), expected_follower.objects.size());
        for (size_t object_index = 0; object_index < expected_follower.objects.size(); ++object_index) {
            EXPECT_EQ(actual_follower.objects[object_index].item_number, expected_follower.objects[object_index].item_number);
            EXPECT_EQ(actual_follower.objects[object_index].wear_pos, expected_follower.objects[object_index].wear_pos);
        }
    }
}

} // namespace

TEST(ObjectsJson, SerializesAndDeserializesBinaryRoundTrip)
{
    const objects_json::ObjectSaveData original = make_object_save_data();

    std::string bytes;
    std::string error_message;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(original, &bytes, &error_message)) << error_message;

    objects_json::ObjectSaveData parsed;
    ASSERT_TRUE(objects_json::object_save_data_from_binary(bytes, &parsed, &error_message)) << error_message;

    EXPECT_EQ(parsed.rent.rentcode, RENT_CRASH);
    ASSERT_EQ(parsed.objects.size(), 2u);
    EXPECT_EQ(parsed.objects[0].item_number, 1200);
    EXPECT_EQ(parsed.objects[0].wear_pos, WEAR_HEAD);
    EXPECT_EQ(parsed.board_points[1], 22);
    ASSERT_EQ(parsed.aliases.size(), 2u);
    EXPECT_EQ(parsed.aliases[0].keyword, "assist");
    EXPECT_EQ(parsed.aliases[0].command, "kill orc");
    ASSERT_EQ(parsed.followers.size(), 1u);
    EXPECT_EQ(parsed.followers[0].fol_vnum, 4001);
    ASSERT_EQ(parsed.followers[0].objects.size(), 1u);
    EXPECT_EQ(parsed.followers[0].objects[0].item_number, 2200);
}

TEST(ObjectsJson, SerializesAndDeserializesJsonRoundTrip)
{
    const objects_json::ObjectSaveData original = make_object_save_data();
    const std::string json = objects_json::serialize_objects_to_json(original);

    objects_json::ObjectSaveData parsed;
    std::string error_message;
    ASSERT_TRUE(objects_json::deserialize_objects_from_json(json, &parsed, &error_message)) << error_message;

    EXPECT_EQ(parsed.rent.time, original.rent.time);
    EXPECT_EQ(parsed.rent.net_cost_per_hour, original.rent.net_cost_per_hour);
    ASSERT_EQ(parsed.objects.size(), original.objects.size());
    EXPECT_EQ(parsed.objects[1].item_number, original.objects[1].item_number);
    EXPECT_EQ(parsed.aliases[1].command, original.aliases[1].command);
    EXPECT_EQ(parsed.followers[0].objects[0].wear_pos, original.followers[0].objects[0].wear_pos);
}

TEST(ObjectsJson, PreservesObjectAliasAndFollowerOrderingAcrossJsonRoundTrip)
{
    const objects_json::ObjectSaveData original = make_object_save_data();
    const std::string json = objects_json::serialize_objects_to_json(original);

    objects_json::ObjectSaveData parsed;
    std::string error_message;
    ASSERT_TRUE(objects_json::deserialize_objects_from_json(json, &parsed, &error_message)) << error_message;

    expect_object_save_data_equal(original, parsed);
}

TEST(ObjectsJson, SerializesAndDeserializesEmptyObjectSaveData)
{
    objects_json::ObjectSaveData original;
    std::string error_message;

    std::string bytes;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(original, &bytes, &error_message)) << error_message;

    objects_json::ObjectSaveData parsed_from_binary;
    ASSERT_TRUE(objects_json::object_save_data_from_binary(bytes, &parsed_from_binary, &error_message)) << error_message;
    expect_object_save_data_equal(original, parsed_from_binary);

    const std::string json = objects_json::serialize_objects_to_json(original);
    objects_json::ObjectSaveData parsed_from_json;
    ASSERT_TRUE(objects_json::deserialize_objects_from_json(json, &parsed_from_json, &error_message)) << error_message;
    expect_object_save_data_equal(original, parsed_from_json);
}

TEST(ObjectsJson, RejectsTruncatedBinaryObjectPayload)
{
    const objects_json::ObjectSaveData original = make_object_save_data();

    std::string bytes;
    std::string error_message;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(original, &bytes, &error_message)) << error_message;
    bytes.pop_back();

    objects_json::ObjectSaveData parsed;
    EXPECT_FALSE(objects_json::object_save_data_from_binary(bytes, &parsed, &error_message));
    EXPECT_NE(error_message.find("Truncated objects data"), std::string::npos);
}

TEST(ObjectsJson, RejectsTruncatedBinaryInsideAliasAndFollowerSections)
{
    const objects_json::ObjectSaveData original = make_object_save_data();

    std::string bytes;
    std::string error_message;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(original, &bytes, &error_message)) << error_message;

    const size_t alias_offset = bytes.find("kill orc");
    ASSERT_NE(alias_offset, std::string::npos);
    std::string alias_truncated = bytes.substr(0, alias_offset + 2);

    objects_json::ObjectSaveData parsed;
    EXPECT_FALSE(objects_json::object_save_data_from_binary(alias_truncated, &parsed, &error_message));
    EXPECT_FALSE(error_message.empty());

    const size_t follower_offset = bytes.find(reinterpret_cast<const char*>(&original.followers[0].fol_vnum), sizeof(original.followers[0].fol_vnum));
    ASSERT_NE(follower_offset, std::string::npos);
    std::string follower_truncated = bytes.substr(0, bytes.size() - sizeof(obj_file_elem) / 2);

    EXPECT_FALSE(objects_json::object_save_data_from_binary(follower_truncated, &parsed, &error_message));
    EXPECT_FALSE(error_message.empty());
}

TEST(ObjectsJson, RejectsMissingRequiredSectionsInJson)
{
    std::string error_message;
    objects_json::ObjectSaveData parsed;

    const std::string valid_json = objects_json::serialize_objects_to_json(objects_json::ObjectSaveData {});

    std::string missing_board_points_json = valid_json;
    const size_t board_points_start = missing_board_points_json.find("  \"board_points\": [");
    ASSERT_NE(board_points_start, std::string::npos);
    const size_t board_points_end = missing_board_points_json.find("],\n", board_points_start);
    ASSERT_NE(board_points_end, std::string::npos);
    missing_board_points_json.erase(board_points_start, (board_points_end + 3) - board_points_start);

    EXPECT_FALSE(objects_json::deserialize_objects_from_json(missing_board_points_json, &parsed, &error_message));
    EXPECT_NE(error_message.find("required sections"), std::string::npos);

    std::string missing_rent_json = valid_json;
    const size_t rent_start = missing_rent_json.find("  \"rent\": {\n");
    ASSERT_NE(rent_start, std::string::npos);
    const size_t rent_end = missing_rent_json.find("  },\n", rent_start);
    ASSERT_NE(rent_end, std::string::npos);
    missing_rent_json.erase(rent_start, (rent_end + 5) - rent_start);

    EXPECT_FALSE(objects_json::deserialize_objects_from_json(missing_rent_json, &parsed, &error_message));
    EXPECT_NE(error_message.find("required sections"), std::string::npos);
}

TEST(ObjectsJson, RejectsOverlongAliasKeywords)
{
    objects_json::ObjectSaveData data = make_object_save_data();
    data.aliases[0].keyword = "this_alias_keyword_is_too_long";

    std::string bytes;
    std::string error_message;
    EXPECT_FALSE(objects_json::object_save_data_to_binary(data, &bytes, &error_message));
    EXPECT_NE(error_message.find("Alias keyword"), std::string::npos);
}

TEST(ObjectsJson, RejectsOutOfRangeNarrowedFields)
{
    objects_json::ObjectSaveData data = make_object_save_data();
    data.objects[0].values[0] = std::numeric_limits<sh_int>::max() + 1;

    std::string bytes;
    std::string error_message;
    EXPECT_FALSE(objects_json::object_save_data_to_binary(data, &bytes, &error_message));
    EXPECT_NE(error_message.find("object.value"), std::string::npos);

    data = make_object_save_data();
    data.objects[0].affects[0].location = 999;
    EXPECT_FALSE(objects_json::object_save_data_to_binary(data, &bytes, &error_message));
    EXPECT_NE(error_message.find("object.affects.location"), std::string::npos);

    data = make_object_save_data();
    data.board_points[0] = std::numeric_limits<sh_int>::min() - 1;
    EXPECT_FALSE(objects_json::object_save_data_to_binary(data, &bytes, &error_message));
    EXPECT_NE(error_message.find("board_point"), std::string::npos);

    data = make_object_save_data();
    data.rent.spare1 = std::numeric_limits<sh_int>::max() + 1;
    EXPECT_FALSE(objects_json::object_save_data_to_binary(data, &bytes, &error_message));
    EXPECT_NE(error_message.find("rent.spare1"), std::string::npos);

    data = make_object_save_data();
    data.rent.spare2 = std::numeric_limits<sh_int>::min() - 1;
    EXPECT_FALSE(objects_json::object_save_data_to_binary(data, &bytes, &error_message));
    EXPECT_NE(error_message.find("rent.spare2"), std::string::npos);

    data = make_object_save_data();
    data.objects[0].wear_pos = std::numeric_limits<sh_int>::max() + 1;
    EXPECT_FALSE(objects_json::object_save_data_to_binary(data, &bytes, &error_message));
    EXPECT_NE(error_message.find("object.wear_pos"), std::string::npos);
}

TEST(ObjectsJson, RejectsWrongTypedNestedJsonForAliasFollowerAndObjectFields)
{
    objects_json::ObjectSaveData parsed;
    std::string error_message;

    EXPECT_FALSE(objects_json::deserialize_objects_from_json(
        "{\n"
        "  \"version\": 1,\n"
        "  \"rent\": {},\n"
        "  \"objects\": {},\n"
        "  \"board_points\": [0, 0, 0, 0, 0],\n"
        "  \"aliases\": [],\n"
        "  \"followers\": []\n"
        "}\n",
        &parsed, &error_message));
    EXPECT_FALSE(error_message.empty());

    EXPECT_FALSE(objects_json::deserialize_objects_from_json(
        "{\n"
        "  \"version\": 1,\n"
        "  \"rent\": {},\n"
        "  \"objects\": [],\n"
        "  \"board_points\": [0, 0, 0, 0, 0],\n"
        "  \"aliases\": {\"keyword\": \"assist\"},\n"
        "  \"followers\": []\n"
        "}\n",
        &parsed, &error_message));
    EXPECT_FALSE(error_message.empty());

    EXPECT_FALSE(objects_json::deserialize_objects_from_json(
        "{\n"
        "  \"version\": 1,\n"
        "  \"rent\": {},\n"
        "  \"objects\": [],\n"
        "  \"board_points\": [0, 0, 0, 0, 0],\n"
        "  \"aliases\": [],\n"
        "  \"followers\": {\"fol_vnum\": 1}\n"
        "}\n",
        &parsed, &error_message));
    EXPECT_FALSE(error_message.empty());
}

TEST(ObjectsJson, RejectsMissingRequiredNestedAliasObjectAndFollowerFields)
{
    objects_json::ObjectSaveData parsed;
    std::string error_message;

    EXPECT_FALSE(objects_json::deserialize_objects_from_json(
        "{\n"
        "  \"version\": 1,\n"
        "  \"rent\": {\n"
        "    \"time\": 1,\n"
        "    \"net_cost_per_diem\": 2,\n"
        "    \"gold\": 3,\n"
        "    \"account\": 4,\n"
        "    \"nitems\": 5,\n"
        "    \"spare0\": 6,\n"
        "    \"spare1\": 7,\n"
        "    \"spare2\": 8\n"
        "  },\n"
        "  \"objects\": [],\n"
        "  \"board_points\": [0, 0, 0, 0, 0],\n"
        "  \"aliases\": [{\"keyword\": \"assist\"}],\n"
        "  \"followers\": []\n"
        "}\n",
        &parsed, &error_message));
    EXPECT_FALSE(error_message.empty());

    EXPECT_FALSE(objects_json::deserialize_objects_from_json(
        "{\n"
        "  \"version\": 1,\n"
        "  \"rent\": {\n"
        "    \"time\": 1,\n"
        "    \"net_cost_per_diem\": 2,\n"
        "    \"gold\": 3,\n"
        "    \"account\": 4,\n"
        "    \"nitems\": 5,\n"
        "    \"spare0\": 6,\n"
        "    \"spare1\": 7,\n"
        "    \"spare2\": 8\n"
        "  },\n"
        "  \"objects\": [{\n"
        "    \"values\": [0, 0, 0, 0],\n"
        "    \"extra_flags\": 1,\n"
        "    \"weight\": 2,\n"
        "    \"timer\": 3,\n"
        "    \"bitvector\": 4,\n"
        "    \"wear_pos\": 5,\n"
        "    \"loaded_by\": 6,\n"
        "    \"affects\": [{\"location\": 0, \"modifier\": 0}, {\"location\": 0, \"modifier\": 0}, {\"location\": 0, \"modifier\": 0}]\n"
        "  }],\n"
        "  \"board_points\": [0, 0, 0, 0, 0],\n"
        "  \"aliases\": [],\n"
        "  \"followers\": []\n"
        "}\n",
        &parsed, &error_message));
    EXPECT_FALSE(error_message.empty());

    EXPECT_FALSE(objects_json::deserialize_objects_from_json(
        "{\n"
        "  \"version\": 1,\n"
        "  \"rent\": {\n"
        "    \"time\": 1,\n"
        "    \"net_cost_per_diem\": 2,\n"
        "    \"gold\": 3,\n"
        "    \"account\": 4,\n"
        "    \"nitems\": 5,\n"
        "    \"spare0\": 6,\n"
        "    \"spare1\": 7,\n"
        "    \"spare2\": 8\n"
        "  },\n"
        "  \"objects\": [],\n"
        "  \"board_points\": [0, 0, 0, 0, 0],\n"
        "  \"aliases\": [],\n"
        "  \"followers\": [{\n"
        "    \"mount_vnum\": 2,\n"
        "    \"wimpy\": 3,\n"
        "    \"exp\": 4,\n"
        "    \"flag_config\": 5,\n"
        "    \"spare1\": 6,\n"
        "    \"spare2\": 7\n"
        "  }]\n"
        "}\n",
        &parsed, &error_message));
    EXPECT_FALSE(error_message.empty());
}

TEST(ObjectsJson, RejectsMissingRequiredObjectAffectFields)
{
    objects_json::ObjectSaveData parsed;
    std::string error_message;

    EXPECT_FALSE(objects_json::deserialize_objects_from_json(
        "{\n"
        "  \"version\": 1,\n"
        "  \"rent\": {\n"
        "    \"time\": 1,\n"
        "    \"net_cost_per_diem\": 2,\n"
        "    \"gold\": 3,\n"
        "    \"account\": 4,\n"
        "    \"nitems\": 5,\n"
        "    \"spare0\": 6,\n"
        "    \"spare1\": 7,\n"
        "    \"spare2\": 8\n"
        "  },\n"
        "  \"objects\": [{\n"
        "    \"item_number\": 100,\n"
        "    \"values\": [0, 0, 0, 0],\n"
        "    \"extra_flags\": 1,\n"
        "    \"weight\": 2,\n"
        "    \"timer\": 3,\n"
        "    \"bitvector\": 4,\n"
        "    \"wear_pos\": 5,\n"
        "    \"loaded_by\": 6,\n"
        "    \"affects\": [{\"location\": 0}, {\"location\": 0, \"modifier\": 0}, {\"location\": 0, \"modifier\": 0}]\n"
        "  }],\n"
        "  \"board_points\": [0, 0, 0, 0, 0],\n"
        "  \"aliases\": [],\n"
        "  \"followers\": []\n"
        "}\n",
        &parsed, &error_message));
    EXPECT_FALSE(error_message.empty());

    EXPECT_FALSE(objects_json::deserialize_objects_from_json(
        "{\n"
        "  \"version\": 1,\n"
        "  \"rent\": {\n"
        "    \"time\": 1,\n"
        "    \"net_cost_per_diem\": 2,\n"
        "    \"gold\": 3,\n"
        "    \"account\": 4,\n"
        "    \"nitems\": 5,\n"
        "    \"spare0\": 6,\n"
        "    \"spare1\": 7,\n"
        "    \"spare2\": 8\n"
        "  },\n"
        "  \"objects\": [{\n"
        "    \"item_number\": 100,\n"
        "    \"values\": [0, 0, 0, 0],\n"
        "    \"extra_flags\": 1,\n"
        "    \"weight\": 2,\n"
        "    \"timer\": 3,\n"
        "    \"bitvector\": 4,\n"
        "    \"wear_pos\": 5,\n"
        "    \"loaded_by\": 6,\n"
        "    \"affects\": [{\"modifier\": 1}, {\"location\": 0, \"modifier\": 0}, {\"location\": 0, \"modifier\": 0}]\n"
        "  }],\n"
        "  \"board_points\": [0, 0, 0, 0, 0],\n"
        "  \"aliases\": [],\n"
        "  \"followers\": []\n"
        "}\n",
        &parsed, &error_message));
    EXPECT_FALSE(error_message.empty());
}
