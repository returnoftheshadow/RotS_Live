#ifndef JS_API_CONTRACT_H
#define JS_API_CONTRACT_H

#include <cstddef>

enum class JsApiTypeKind {
    Class,
    Interface,
    Namespace,
};

enum class JsApiMemberKind {
    Property,
    Method,
};

enum class JsApiMemberStatus {
    PlannedReadOnly,
    PlannedPureHelper,
    ImplementedSideEffectHelper,
    Deferred,
    Unsupported,
};

enum class JsApiSideEffect {
    None,
    Output,
    Mutation,
    WorldMutation,
};

struct JsApiContractMetadata {
    int schema_version;
    int api_revision;
    const char* api_version;
    const char* contract_checksum;
    const char* generated_typings_version;
    const char* documentation_version;
    const char* minimum_trigger_catalog_revision;
    const char* notes;
};

struct JsApiMember {
    const char* name;
    JsApiMemberKind kind;
    const char* type_name;
    const char* return_type;
    bool nullable;
    bool requires_live_handle;
    JsApiSideEffect side_effect;
    JsApiMemberStatus status;
    const char* permission;
    const char* docs;
};

struct JsApiType {
    const char* name;
    JsApiTypeKind kind;
    const char* extends;
    const char* docs;
    const JsApiMember* members;
    std::size_t member_count;
};

const JsApiContractMetadata& js_api_contract_metadata();

const JsApiType* js_api_contract_types();
std::size_t js_api_contract_type_count();
const JsApiType* find_js_api_contract_type(const char* name);
const JsApiMember* find_js_api_contract_member(const JsApiType& type, const char* name);

const char* js_api_type_kind_name(JsApiTypeKind kind);
const char* js_api_member_kind_name(JsApiMemberKind kind);
const char* js_api_member_status_name(JsApiMemberStatus status);
const char* js_api_side_effect_name(JsApiSideEffect side_effect);

#endif
