#include "objects_json.h"
#include "json_utils.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <sstream>

namespace objects_json {
namespace {

    void set_error(std::string* error_message, const std::string& message)
    {
        if (error_message)
            *error_message = message;
    }

    template <typename T>
    bool read_pod(const std::string& bytes, size_t* offset, T* value, std::string* error_message, const char* label)
    {
        if (offset == nullptr || value == nullptr) {
            set_error(error_message, "Binary parse output parameter must not be null.");
            return false;
        }

        if (*offset + sizeof(T) > bytes.size()) {
            set_error(error_message, std::string("Truncated objects data while reading ") + label + ".");
            return false;
        }

        std::memcpy(value, bytes.data() + *offset, sizeof(T));
        *offset += sizeof(T);
        return true;
    }

    template <typename T>
    void append_pod(std::string* bytes, const T& value)
    {
        bytes->append(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    template <typename TargetType>
    bool validate_narrowed_range(long long value, const char* field_name, std::string* error_message)
    {
        if (value < static_cast<long long>(std::numeric_limits<TargetType>::min())
            || value > static_cast<long long>(std::numeric_limits<TargetType>::max())) {
            set_error(error_message, std::string(field_name) + " is outside the supported storage range.");
            return false;
        }

        return true;
    }

    bool parse_exact_integer_array(json_utils::JsonReader* reader, size_t expected_size, std::vector<long>* values, std::string* error_message)
    {
        std::vector<long> parsed_values;
        if (!reader->parse_array([&parsed_values](json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
                long value = 0;
                if (!nested_reader->parse_long(&value, nested_error_message))
                    return false;
                parsed_values.push_back(value);
                return true;
            }, error_message)) {
            return false;
        }

        if (parsed_values.size() != expected_size) {
            set_error(error_message, "Objects JSON array length did not match the expected size.");
            return false;
        }

        *values = std::move(parsed_values);
        set_error(error_message, "");
        return true;
    }

    bool parse_object_affect(json_utils::JsonReader* reader, ObjectAffectData* affect, std::string* error_message)
    {
        if (affect == nullptr) {
            set_error(error_message, "Object affect output parameter must not be null.");
            return false;
        }

        ObjectAffectData parsed_affect;
        bool saw_location = false;
        bool saw_modifier = false;
        if (!reader->parse_object([&parsed_affect, &saw_location, &saw_modifier](const std::string& key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
                if (key == "location")
                    return saw_location = true, nested_reader->parse_integer(&parsed_affect.location, nested_error_message);
                if (key == "modifier")
                    return saw_modifier = true, nested_reader->parse_integer(&parsed_affect.modifier, nested_error_message);
                return nested_reader->skip_value(nested_error_message);
            }, error_message)) {
            return false;
        }

        if (!saw_location || !saw_modifier) {
            set_error(error_message, "Object affect record was missing one or more required fields.");
            return false;
        }

        *affect = parsed_affect;
        set_error(error_message, "");
        return true;
    }

    bool parse_object_record(json_utils::JsonReader* reader, ObjectRecord* record, std::string* error_message)
    {
        if (record == nullptr) {
            set_error(error_message, "Object record output parameter must not be null.");
            return false;
        }

        ObjectRecord parsed_record;
        bool saw_item_number = false;
        bool saw_values = false;
        bool saw_extra_flags = false;
        bool saw_weight = false;
        bool saw_timer = false;
        bool saw_bitvector = false;
        bool saw_wear_pos = false;
        bool saw_loaded_by = false;
        bool saw_affects = false;
        if (!reader->parse_object([&parsed_record, &saw_item_number, &saw_values, &saw_extra_flags, &saw_weight, &saw_timer, &saw_bitvector, &saw_wear_pos, &saw_loaded_by, &saw_affects](const std::string& key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
                if (key == "item_number")
                    return saw_item_number = true, nested_reader->parse_integer(&parsed_record.item_number, nested_error_message);
                if (key == "values") {
                    saw_values = true;
                    std::vector<long> values;
                    if (!parse_exact_integer_array(nested_reader, parsed_record.values.size(), &values, nested_error_message))
                        return false;
                    for (size_t index = 0; index < values.size(); ++index)
                        parsed_record.values[index] = static_cast<int>(values[index]);
                    return true;
                }
                if (key == "extra_flags")
                    return saw_extra_flags = true, nested_reader->parse_integer(&parsed_record.extra_flags, nested_error_message);
                if (key == "weight")
                    return saw_weight = true, nested_reader->parse_integer(&parsed_record.weight, nested_error_message);
                if (key == "timer")
                    return saw_timer = true, nested_reader->parse_integer(&parsed_record.timer, nested_error_message);
                if (key == "bitvector")
                    return saw_bitvector = true, nested_reader->parse_long(&parsed_record.bitvector, nested_error_message);
                if (key == "wear_pos")
                    return saw_wear_pos = true, nested_reader->parse_integer(&parsed_record.wear_pos, nested_error_message);
                if (key == "loaded_by")
                    return saw_loaded_by = true, nested_reader->parse_integer(&parsed_record.loaded_by, nested_error_message);
                if (key == "affects") {
                    saw_affects = true;
                    std::vector<ObjectAffectData> affects;
                    if (!nested_reader->parse_array([&affects](json_utils::JsonReader* affect_reader, std::string* affect_error_message) {
                            ObjectAffectData affect;
                            if (!parse_object_affect(affect_reader, &affect, affect_error_message))
                                return false;
                            affects.push_back(affect);
                            return true;
                        }, nested_error_message)) {
                        return false;
                    }

                    if (affects.size() != parsed_record.affects.size()) {
                        set_error(nested_error_message, "Object affect array length did not match MAX_OBJ_AFFECT.");
                        return false;
                    }

                    for (size_t index = 0; index < affects.size(); ++index)
                        parsed_record.affects[index] = affects[index];
                    return true;
                }
                return nested_reader->skip_value(nested_error_message);
            }, error_message)) {
            return false;
        }

        if (!saw_item_number || !saw_values || !saw_extra_flags || !saw_weight || !saw_timer || !saw_bitvector || !saw_wear_pos || !saw_loaded_by || !saw_affects) {
            set_error(error_message, "Object record was missing one or more required fields.");
            return false;
        }

        *record = parsed_record;
        set_error(error_message, "");
        return true;
    }

    bool parse_alias_record(json_utils::JsonReader* reader, AliasData* alias, std::string* error_message)
    {
        AliasData parsed_alias;
        bool saw_keyword = false;
        bool saw_command = false;
        if (!reader->parse_object([&parsed_alias, &saw_keyword, &saw_command](const std::string& key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
                if (key == "keyword")
                    return saw_keyword = true, nested_reader->parse_string(&parsed_alias.keyword, nested_error_message);
                if (key == "command")
                    return saw_command = true, nested_reader->parse_string(&parsed_alias.command, nested_error_message);
                return nested_reader->skip_value(nested_error_message);
            }, error_message)) {
            return false;
        }

        if (!saw_keyword || !saw_command) {
            set_error(error_message, "Alias record was missing one or more required fields.");
            return false;
        }

        if (parsed_alias.keyword.size() >= 20) {
            set_error(error_message, "Alias keyword must fit within 19 characters.");
            return false;
        }

        *alias = std::move(parsed_alias);
        set_error(error_message, "");
        return true;
    }

    bool parse_follower_record(json_utils::JsonReader* reader, FollowerData* follower, std::string* error_message)
    {
        FollowerData parsed_follower;
        bool saw_fol_vnum = false;
        bool saw_mount_vnum = false;
        bool saw_wimpy = false;
        bool saw_exp = false;
        bool saw_flag_config = false;
        bool saw_spare1 = false;
        bool saw_spare2 = false;
        bool saw_objects = false;
        if (!reader->parse_object([&parsed_follower, &saw_fol_vnum, &saw_mount_vnum, &saw_wimpy, &saw_exp, &saw_flag_config, &saw_spare1, &saw_spare2, &saw_objects](const std::string& key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
                if (key == "fol_vnum")
                    return saw_fol_vnum = true, nested_reader->parse_integer(&parsed_follower.fol_vnum, nested_error_message);
                if (key == "mount_vnum")
                    return saw_mount_vnum = true, nested_reader->parse_integer(&parsed_follower.mount_vnum, nested_error_message);
                if (key == "wimpy")
                    return saw_wimpy = true, nested_reader->parse_integer(&parsed_follower.wimpy, nested_error_message);
                if (key == "exp")
                    return saw_exp = true, nested_reader->parse_integer(&parsed_follower.exp, nested_error_message);
                if (key == "flag_config")
                    return saw_flag_config = true, nested_reader->parse_integer(&parsed_follower.flag_config, nested_error_message);
                if (key == "spare1")
                    return saw_spare1 = true, nested_reader->parse_integer(&parsed_follower.spare1, nested_error_message);
                if (key == "spare2")
                    return saw_spare2 = true, nested_reader->parse_integer(&parsed_follower.spare2, nested_error_message);
                if (key == "objects") {
                    saw_objects = true;
                    std::vector<ObjectRecord> objects;
                    if (!nested_reader->parse_array([&objects](json_utils::JsonReader* object_reader, std::string* object_error_message) {
                            ObjectRecord object_record;
                            if (!parse_object_record(object_reader, &object_record, object_error_message))
                                return false;
                            objects.push_back(object_record);
                            return true;
                        }, nested_error_message)) {
                        return false;
                    }
                    parsed_follower.objects = std::move(objects);
                    return true;
                }
                return nested_reader->skip_value(nested_error_message);
            }, error_message)) {
            return false;
        }

        if (!saw_fol_vnum || !saw_mount_vnum || !saw_wimpy || !saw_exp || !saw_flag_config || !saw_spare1 || !saw_spare2 || !saw_objects) {
            set_error(error_message, "Follower record was missing one or more required fields.");
            return false;
        }

        *follower = std::move(parsed_follower);
        set_error(error_message, "");
        return true;
    }

    void write_object_record(std::ostringstream& output, const ObjectRecord& record, const char* indent)
    {
        output << indent << "{\n";
        output << indent << "  \"item_number\": " << record.item_number << ",\n";
        output << indent << "  \"values\": [";
        for (size_t index = 0; index < record.values.size(); ++index) {
            if (index > 0)
                output << ", ";
            output << record.values[index];
        }
        output << "],\n";
        output << indent << "  \"extra_flags\": " << record.extra_flags << ",\n";
        output << indent << "  \"weight\": " << record.weight << ",\n";
        output << indent << "  \"timer\": " << record.timer << ",\n";
        output << indent << "  \"bitvector\": " << record.bitvector << ",\n";
        output << indent << "  \"wear_pos\": " << record.wear_pos << ",\n";
        output << indent << "  \"loaded_by\": " << record.loaded_by << ",\n";
        output << indent << "  \"affects\": [\n";
        for (size_t index = 0; index < record.affects.size(); ++index) {
            const ObjectAffectData& affect = record.affects[index];
            output << indent << "    {\"location\": " << affect.location << ", \"modifier\": " << affect.modifier << "}";
            if (index + 1 < record.affects.size())
                output << ",";
            output << "\n";
        }
        output << indent << "  ]\n";
        output << indent << "}";
    }

} // namespace

bool object_save_data_from_binary(const std::string& bytes, ObjectSaveData* data, std::string* error_message)
{
    if (data == nullptr) {
        set_error(error_message, "Objects data output parameter must not be null.");
        return false;
    }

    ObjectSaveData parsed_data;
    size_t offset = 0;

    rent_info raw_rent {};
    if (!read_pod(bytes, &offset, &raw_rent, error_message, "rent data"))
        return false;

    parsed_data.rent.time = raw_rent.time;
    parsed_data.rent.rentcode = raw_rent.rentcode;
    parsed_data.rent.net_cost_per_hour = raw_rent.net_cost_per_hour;
    parsed_data.rent.gold = raw_rent.gold;
    parsed_data.rent.nitems = raw_rent.nitems;
    parsed_data.rent.spare0 = raw_rent.spare0;
    parsed_data.rent.spare1 = raw_rent.spare1;
    parsed_data.rent.spare2 = raw_rent.spare2;
    parsed_data.rent.spare3 = raw_rent.spare3;
    parsed_data.rent.spare4 = raw_rent.spare4;
    parsed_data.rent.spare5 = raw_rent.spare5;
    parsed_data.rent.spare6 = raw_rent.spare6;
    parsed_data.rent.spare7 = raw_rent.spare7;

    while (true) {
        obj_file_elem raw_object {};
        if (!read_pod(bytes, &offset, &raw_object, error_message, "top-level object record"))
            return false;

        if (raw_object.item_number_deprecated != DEPRECATED_ID_VALUE) {
            raw_object.item_number = raw_object.item_number_deprecated;
            raw_object.item_number_deprecated = DEPRECATED_ID_VALUE;
        }

        if (raw_object.item_number == SENTINEL_ITEM_ID_VALUE)
            break;

        ObjectRecord record;
        record.item_number = raw_object.item_number;
        for (size_t index = 0; index < record.values.size(); ++index)
            record.values[index] = raw_object.value[index];
        record.extra_flags = raw_object.extra_flags;
        record.weight = raw_object.weight;
        record.timer = raw_object.timer;
        record.bitvector = raw_object.bitvector;
        for (size_t index = 0; index < record.affects.size(); ++index) {
            record.affects[index].location = raw_object.affected[index].location;
            record.affects[index].modifier = raw_object.affected[index].modifier;
        }
        record.wear_pos = raw_object.wear_pos;
        record.loaded_by = raw_object.loaded_by;
        parsed_data.objects.push_back(record);
    }

    for (size_t index = 0; index < parsed_data.board_points.size(); ++index) {
        sh_int point = 0;
        if (!read_pod(bytes, &offset, &point, error_message, "board point"))
            return false;
        parsed_data.board_points[index] = point;
    }

    while (true) {
        char keyword_bytes[20] {};
        if (!read_pod(bytes, &offset, &keyword_bytes, error_message, "alias keyword"))
            return false;

        if (keyword_bytes[0] == '\0')
            break;

        int command_length = 0;
        if (!read_pod(bytes, &offset, &command_length, error_message, "alias command length"))
            return false;

        if (command_length < 0 || offset + static_cast<size_t>(command_length) > bytes.size()) {
            set_error(error_message, "Alias command length is malformed.");
            return false;
        }

        AliasData alias;
        alias.keyword.assign(keyword_bytes, std::find(keyword_bytes, keyword_bytes + sizeof(keyword_bytes), '\0'));
        alias.command.assign(bytes.data() + offset, static_cast<size_t>(command_length));
        offset += static_cast<size_t>(command_length);
        parsed_data.aliases.push_back(std::move(alias));
    }

    while (true) {
        follower_file_elem raw_follower {};
        if (!read_pod(bytes, &offset, &raw_follower, error_message, "follower record"))
            return false;

        if (raw_follower.fol_vnum == SENTINEL_ITEM_ID_VALUE)
            break;

        FollowerData follower;
        follower.fol_vnum = raw_follower.fol_vnum;
        follower.mount_vnum = raw_follower.mount_vnum;
        follower.wimpy = raw_follower.wimpy;
        follower.exp = raw_follower.exp;
        follower.flag_config = raw_follower.flag_config;
        follower.spare1 = raw_follower.spare1;
        follower.spare2 = raw_follower.spare2;

        while (true) {
            obj_file_elem raw_object {};
            if (!read_pod(bytes, &offset, &raw_object, error_message, "follower object record"))
                return false;

            if (raw_object.item_number_deprecated != DEPRECATED_ID_VALUE) {
                raw_object.item_number = raw_object.item_number_deprecated;
                raw_object.item_number_deprecated = DEPRECATED_ID_VALUE;
            }

            if (raw_object.item_number == SENTINEL_ITEM_ID_VALUE)
                break;

            ObjectRecord record;
            record.item_number = raw_object.item_number;
            for (size_t index = 0; index < record.values.size(); ++index)
                record.values[index] = raw_object.value[index];
            record.extra_flags = raw_object.extra_flags;
            record.weight = raw_object.weight;
            record.timer = raw_object.timer;
            record.bitvector = raw_object.bitvector;
            for (size_t index = 0; index < record.affects.size(); ++index) {
                record.affects[index].location = raw_object.affected[index].location;
                record.affects[index].modifier = raw_object.affected[index].modifier;
            }
            record.wear_pos = raw_object.wear_pos;
            record.loaded_by = raw_object.loaded_by;
            follower.objects.push_back(record);
        }

        parsed_data.followers.push_back(std::move(follower));
    }

    if (offset != bytes.size()) {
        set_error(error_message, "Objects binary data had unexpected trailing bytes.");
        return false;
    }

    *data = std::move(parsed_data);
    set_error(error_message, "");
    return true;
}

bool object_save_data_to_binary(const ObjectSaveData& data, std::string* bytes, std::string* error_message)
{
    if (bytes == nullptr) {
        set_error(error_message, "Objects binary output parameter must not be null.");
        return false;
    }

    std::string serialized_bytes;

    rent_info raw_rent {};
    raw_rent.time = data.rent.time;
    raw_rent.rentcode = data.rent.rentcode;
    raw_rent.net_cost_per_hour = data.rent.net_cost_per_hour;
    raw_rent.gold = data.rent.gold;
    raw_rent.nitems = data.rent.nitems;
    if (!validate_narrowed_range<sh_int>(data.rent.spare0, "rent.spare0", error_message)
        || !validate_narrowed_range<sh_int>(data.rent.spare1, "rent.spare1", error_message)
        || !validate_narrowed_range<sh_int>(data.rent.spare2, "rent.spare2", error_message)) {
        return false;
    }
    raw_rent.spare0 = static_cast<sh_int>(data.rent.spare0);
    raw_rent.spare1 = static_cast<sh_int>(data.rent.spare1);
    raw_rent.spare2 = static_cast<sh_int>(data.rent.spare2);
    raw_rent.spare3 = data.rent.spare3;
    raw_rent.spare4 = data.rent.spare4;
    raw_rent.spare5 = data.rent.spare5;
    raw_rent.spare6 = data.rent.spare6;
    raw_rent.spare7 = data.rent.spare7;
    append_pod(&serialized_bytes, raw_rent);

    auto append_object_record = [&serialized_bytes, error_message](const ObjectRecord& record) {
        obj_file_elem raw_object {};
        raw_object.item_number_deprecated = DEPRECATED_ID_VALUE;
        raw_object.item_number = record.item_number;
        for (size_t index = 0; index < record.values.size(); ++index) {
            if (!validate_narrowed_range<sh_int>(record.values[index], "object.value", error_message))
                return false;
            raw_object.value[index] = static_cast<sh_int>(record.values[index]);
        }
        raw_object.extra_flags = record.extra_flags;
        raw_object.weight = record.weight;
        raw_object.timer = record.timer;
        raw_object.bitvector = record.bitvector;
        for (size_t index = 0; index < record.affects.size(); ++index) {
            if (!validate_narrowed_range<unsigned char>(record.affects[index].location, "object.affects.location", error_message))
                return false;
            raw_object.affected[index].location = static_cast<byte>(record.affects[index].location);
            raw_object.affected[index].modifier = record.affects[index].modifier;
        }
        if (!validate_narrowed_range<sh_int>(record.wear_pos, "object.wear_pos", error_message))
            return false;
        raw_object.wear_pos = static_cast<sh_int>(record.wear_pos);
        raw_object.loaded_by = record.loaded_by;
        append_pod(&serialized_bytes, raw_object);
        return true;
    };

    for (const ObjectRecord& record : data.objects) {
        if (!append_object_record(record))
            return false;
    }

    obj_file_elem object_sentinel {};
    object_sentinel.item_number_deprecated = DEPRECATED_ID_VALUE;
    object_sentinel.item_number = SENTINEL_ITEM_ID_VALUE;
    append_pod(&serialized_bytes, object_sentinel);

    for (int board_point : data.board_points) {
        if (!validate_narrowed_range<sh_int>(board_point, "board_point", error_message))
            return false;
        const sh_int narrowed_board_point = static_cast<sh_int>(board_point);
        append_pod(&serialized_bytes, narrowed_board_point);
    }

    for (const AliasData& alias : data.aliases) {
        if (alias.keyword.size() >= 20) {
            set_error(error_message, "Alias keyword must fit within 19 characters.");
            return false;
        }

        char keyword_bytes[20] {};
        std::memcpy(keyword_bytes, alias.keyword.data(), alias.keyword.size());
        append_pod(&serialized_bytes, keyword_bytes);

        const int command_length = static_cast<int>(alias.command.size());
        append_pod(&serialized_bytes, command_length);
        serialized_bytes.append(alias.command);
    }

    char alias_terminator[20] {};
    append_pod(&serialized_bytes, alias_terminator);

    for (const FollowerData& follower : data.followers) {
        follower_file_elem raw_follower {};
        raw_follower.fol_vnum = follower.fol_vnum;
        raw_follower.mount_vnum = follower.mount_vnum;
        raw_follower.wimpy = follower.wimpy;
        raw_follower.exp = follower.exp;
        raw_follower.flag_config = follower.flag_config;
        raw_follower.spare1 = follower.spare1;
        raw_follower.spare2 = follower.spare2;
        append_pod(&serialized_bytes, raw_follower);

        for (const ObjectRecord& record : follower.objects) {
            if (!append_object_record(record))
                return false;
        }

        append_pod(&serialized_bytes, object_sentinel);
    }

    follower_file_elem follower_sentinel {};
    follower_sentinel.fol_vnum = SENTINEL_ITEM_ID_VALUE;
    append_pod(&serialized_bytes, follower_sentinel);

    *bytes = std::move(serialized_bytes);
    set_error(error_message, "");
    return true;
}

std::string serialize_objects_to_json(const ObjectSaveData& data)
{
    std::ostringstream output;
    output << "{\n";
    output << "  \"version\": " << data.version << ",\n";
    output << "  \"rent\": {\n";
    output << "    \"time\": " << data.rent.time << ",\n";
    output << "    \"rentcode\": " << data.rent.rentcode << ",\n";
    output << "    \"net_cost_per_hour\": " << data.rent.net_cost_per_hour << ",\n";
    output << "    \"gold\": " << data.rent.gold << ",\n";
    output << "    \"nitems\": " << data.rent.nitems << ",\n";
    output << "    \"spare0\": " << data.rent.spare0 << ",\n";
    output << "    \"spare1\": " << data.rent.spare1 << ",\n";
    output << "    \"spare2\": " << data.rent.spare2 << ",\n";
    output << "    \"spare3\": " << data.rent.spare3 << ",\n";
    output << "    \"spare4\": " << data.rent.spare4 << ",\n";
    output << "    \"spare5\": " << data.rent.spare5 << ",\n";
    output << "    \"spare6\": " << data.rent.spare6 << ",\n";
    output << "    \"spare7\": " << data.rent.spare7 << "\n";
    output << "  },\n";
    output << "  \"objects\": [\n";
    for (size_t index = 0; index < data.objects.size(); ++index) {
        write_object_record(output, data.objects[index], "    ");
        if (index + 1 < data.objects.size())
            output << ",";
        output << "\n";
    }
    output << "  ],\n";
    output << "  \"board_points\": [";
    for (size_t index = 0; index < data.board_points.size(); ++index) {
        if (index > 0)
            output << ", ";
        output << data.board_points[index];
    }
    output << "],\n";
    output << "  \"aliases\": [\n";
    for (size_t index = 0; index < data.aliases.size(); ++index) {
        const AliasData& alias = data.aliases[index];
        output << "    {\"keyword\": \"" << json_utils::escape_json_string(alias.keyword)
               << "\", \"command\": \"" << json_utils::escape_json_string(alias.command) << "\"}";
        if (index + 1 < data.aliases.size())
            output << ",";
        output << "\n";
    }
    output << "  ],\n";
    output << "  \"followers\": [\n";
    for (size_t index = 0; index < data.followers.size(); ++index) {
        const FollowerData& follower = data.followers[index];
        output << "    {\n";
        output << "      \"fol_vnum\": " << follower.fol_vnum << ",\n";
        output << "      \"mount_vnum\": " << follower.mount_vnum << ",\n";
        output << "      \"wimpy\": " << follower.wimpy << ",\n";
        output << "      \"exp\": " << follower.exp << ",\n";
        output << "      \"flag_config\": " << follower.flag_config << ",\n";
        output << "      \"spare1\": " << follower.spare1 << ",\n";
        output << "      \"spare2\": " << follower.spare2 << ",\n";
        output << "      \"objects\": [\n";
        for (size_t object_index = 0; object_index < follower.objects.size(); ++object_index) {
            write_object_record(output, follower.objects[object_index], "        ");
            if (object_index + 1 < follower.objects.size())
                output << ",";
            output << "\n";
        }
        output << "      ]\n";
        output << "    }";
        if (index + 1 < data.followers.size())
            output << ",";
        output << "\n";
    }
    output << "  ]\n";
    output << "}\n";
    return output.str();
}

bool deserialize_objects_from_json(const std::string& json, ObjectSaveData* data, std::string* error_message)
{
    if (data == nullptr) {
        set_error(error_message, "Objects data output parameter must not be null.");
        return false;
    }

    ObjectSaveData parsed_data;
    bool saw_rent = false;
    bool saw_objects = false;
    bool saw_board_points = false;
    bool saw_aliases = false;
    bool saw_followers = false;
    json_utils::JsonReader reader(json);
    if (!reader.parse_root_object([&parsed_data, &saw_rent, &saw_objects, &saw_board_points, &saw_aliases, &saw_followers](const std::string& key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
            if (key == "version")
                return nested_reader->parse_integer(&parsed_data.version, nested_error_message);
            if (key == "rent") {
                saw_rent = true;
                return nested_reader->parse_object([&parsed_data](const std::string& rent_key, json_utils::JsonReader* rent_reader, std::string* rent_error_message) {
                    if (rent_key == "time")
                        return rent_reader->parse_integer(&parsed_data.rent.time, rent_error_message);
                    if (rent_key == "rentcode")
                        return rent_reader->parse_integer(&parsed_data.rent.rentcode, rent_error_message);
                    if (rent_key == "net_cost_per_hour")
                        return rent_reader->parse_integer(&parsed_data.rent.net_cost_per_hour, rent_error_message);
                    if (rent_key == "gold")
                        return rent_reader->parse_integer(&parsed_data.rent.gold, rent_error_message);
                    if (rent_key == "nitems")
                        return rent_reader->parse_integer(&parsed_data.rent.nitems, rent_error_message);
                    if (rent_key == "spare0")
                        return rent_reader->parse_integer(&parsed_data.rent.spare0, rent_error_message);
                    if (rent_key == "spare1")
                        return rent_reader->parse_integer(&parsed_data.rent.spare1, rent_error_message);
                    if (rent_key == "spare2")
                        return rent_reader->parse_integer(&parsed_data.rent.spare2, rent_error_message);
                    if (rent_key == "spare3")
                        return rent_reader->parse_integer(&parsed_data.rent.spare3, rent_error_message);
                    if (rent_key == "spare4")
                        return rent_reader->parse_integer(&parsed_data.rent.spare4, rent_error_message);
                    if (rent_key == "spare5")
                        return rent_reader->parse_integer(&parsed_data.rent.spare5, rent_error_message);
                    if (rent_key == "spare6")
                        return rent_reader->parse_integer(&parsed_data.rent.spare6, rent_error_message);
                    if (rent_key == "spare7")
                        return rent_reader->parse_integer(&parsed_data.rent.spare7, rent_error_message);
                    return rent_reader->skip_value(rent_error_message);
                }, nested_error_message);
            }
            if (key == "objects") {
                saw_objects = true;
                std::vector<ObjectRecord> objects;
                if (!nested_reader->parse_array([&objects](json_utils::JsonReader* object_reader, std::string* object_error_message) {
                        ObjectRecord record;
                        if (!parse_object_record(object_reader, &record, object_error_message))
                            return false;
                        objects.push_back(record);
                        return true;
                    }, nested_error_message)) {
                    return false;
                }
                parsed_data.objects = std::move(objects);
                return true;
            }
            if (key == "board_points") {
                saw_board_points = true;
                std::vector<long> board_points;
                if (!parse_exact_integer_array(nested_reader, parsed_data.board_points.size(), &board_points, nested_error_message))
                    return false;
                for (size_t index = 0; index < board_points.size(); ++index)
                    parsed_data.board_points[index] = static_cast<int>(board_points[index]);
                return true;
            }
            if (key == "aliases") {
                saw_aliases = true;
                std::vector<AliasData> aliases;
                if (!nested_reader->parse_array([&aliases](json_utils::JsonReader* alias_reader, std::string* alias_error_message) {
                        AliasData alias;
                        if (!parse_alias_record(alias_reader, &alias, alias_error_message))
                            return false;
                        aliases.push_back(std::move(alias));
                        return true;
                    }, nested_error_message)) {
                    return false;
                }
                parsed_data.aliases = std::move(aliases);
                return true;
            }
            if (key == "followers") {
                saw_followers = true;
                std::vector<FollowerData> followers;
                if (!nested_reader->parse_array([&followers](json_utils::JsonReader* follower_reader, std::string* follower_error_message) {
                        FollowerData follower;
                        if (!parse_follower_record(follower_reader, &follower, follower_error_message))
                            return false;
                        followers.push_back(std::move(follower));
                        return true;
                    }, nested_error_message)) {
                    return false;
                }
                parsed_data.followers = std::move(followers);
                return true;
            }
            return nested_reader->skip_value(nested_error_message);
        }, error_message)) {
        return false;
    }

    if (parsed_data.version != OBJECTS_SCHEMA_VERSION) {
        set_error(error_message, "Unsupported objects JSON schema version.");
        return false;
    }

    if (!saw_rent || !saw_objects || !saw_board_points || !saw_aliases || !saw_followers) {
        set_error(error_message, "Objects JSON was missing one or more required sections.");
        return false;
    }

    *data = std::move(parsed_data);
    set_error(error_message, "");
    return true;
}

} // namespace objects_json
