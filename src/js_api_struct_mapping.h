#ifndef JS_API_STRUCT_MAPPING_H
#define JS_API_STRUCT_MAPPING_H

#include <cstddef>

enum class JsApiStructOwner {
    CharData,
    ObjData,
    RoomData,
    ZoneData,
};

struct JsApiStructFieldMapping {
    JsApiStructOwner owner;
    const char *source_struct;
    const char *source_field;
    const char *js_property;
    const char *getter_name;
    const char *setter_name;
    const char *type_name;
    bool nullable;
    const char *getter_status;
    const char *setter_status;
    const char *getter_docs;
    const char *setter_docs;
    const char *side_effect;
    const char *notes;
};

const JsApiStructFieldMapping *js_api_struct_field_mappings();
std::size_t js_api_struct_field_mapping_count();
const char *js_api_struct_owner_name(JsApiStructOwner owner);
std::size_t js_api_struct_field_mapping_count_for_owner(JsApiStructOwner owner);
const JsApiStructFieldMapping *find_js_api_struct_field_mapping(JsApiStructOwner owner,
                                                                const char *source_field);

#endif
