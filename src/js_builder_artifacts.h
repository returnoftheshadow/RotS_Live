#ifndef JS_BUILDER_ARTIFACTS_H
#define JS_BUILDER_ARTIFACTS_H

#include <string>

std::string js_generate_typescript_declarations();
std::string js_generate_api_markdown_reference();
std::string js_generate_editor_lsp_config_json();

#endif
