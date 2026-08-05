#ifndef JS_API_ENUM_CATALOG_H
#define JS_API_ENUM_CATALOG_H

#include <cstddef>
#include <string>

enum class JsApiEnumValueKind {
    String,
    Number,
    Bit
};

struct JsApiEnumValue {
    const char* key;
    const char* string_value;
    int number_value;
    const char* docs;
};

struct JsApiEnumCatalog {
    const char* name;
    const char* type_name;
    JsApiEnumValueKind value_kind;
    const char* docs;
    const char* comparable_fields;
    const JsApiEnumValue* values;
    std::size_t value_count;
};

const char* js_api_enum_value_kind_name(JsApiEnumValueKind kind);
const JsApiEnumCatalog* js_api_enum_catalogs();
std::size_t js_api_enum_catalog_count();
std::string js_api_enum_runtime_object_literal();

#endif
