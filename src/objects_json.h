#ifndef OBJECTS_JSON_H
#define OBJECTS_JSON_H

#include "structs.h"

#include <array>
#include <string>
#include <vector>

namespace objects_json {

static constexpr int OBJECTS_SCHEMA_VERSION = 1;

struct ObjectAffectData {
    int location = 0;
    int modifier = 0;
};

struct ObjectRecord {
    int item_number = 0;
    std::array<int, 5> values {};
    int extra_flags = 0;
    int weight = 0;
    int timer = 0;
    long bitvector = 0;
    std::array<ObjectAffectData, MAX_OBJ_AFFECT> affects {};
    int wear_pos = 0;
    int loaded_by = 0;
};

struct RentData {
    int time = 0;
    int rentcode = 0;
    int net_cost_per_hour = 0;
    int gold = 0;
    int nitems = 0;
    int spare0 = 0;
    int spare1 = 0;
    int spare2 = 0;
    int spare3 = 0;
    int spare4 = 0;
    int spare5 = 0;
    int spare6 = 0;
    int spare7 = 0;
};

struct AliasData {
    std::string keyword;
    std::string command;
};

struct FollowerData {
    int fol_vnum = 0;
    int mount_vnum = 0;
    int wimpy = 0;
    int exp = 0;
    int flag_config = 0;
    int spare1 = 0;
    int spare2 = 0;
    std::vector<ObjectRecord> objects;
};

struct ObjectSaveData {
    int version = OBJECTS_SCHEMA_VERSION;
    RentData rent;
    std::vector<ObjectRecord> objects;
    std::array<int, MAX_MAXBOARD> board_points {};
    std::vector<AliasData> aliases;
    std::vector<FollowerData> followers;
};

bool object_save_data_from_binary(const std::string& bytes, ObjectSaveData* data, std::string* error_message = nullptr);
bool object_save_data_to_binary(const ObjectSaveData& data, std::string* bytes, std::string* error_message = nullptr);
std::string serialize_objects_to_json(const ObjectSaveData& data);
bool deserialize_objects_from_json(const std::string& json, ObjectSaveData* data, std::string* error_message = nullptr);

} // namespace objects_json

#endif
