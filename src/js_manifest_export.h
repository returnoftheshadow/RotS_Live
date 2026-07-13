#ifndef JS_MANIFEST_EXPORT_H
#define JS_MANIFEST_EXPORT_H

#include <cstddef>
#include <string>

struct JsManifestExportOptions {
    bool include_documentation = true;
};

constexpr std::size_t JS_MANIFEST_EXPORT_MAX_DOCUMENTATION_BYTES = 512;

std::string js_export_trigger_manifest_json(const JsManifestExportOptions &options = {});
std::string js_export_api_contract_json(const JsManifestExportOptions &options = {});
std::string js_export_builder_manifest_json(const JsManifestExportOptions &options = {});

#endif
