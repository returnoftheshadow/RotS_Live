#include "js_live_registry_admin.h"
#include "js_server_identity.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <vector>

namespace {

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::vector<std::string> split_words(const std::string &argument) {
    std::vector<std::string> words;
    std::istringstream stream(argument);
    std::string word;
    while (stream >> word)
        words.push_back(word);
    return words;
}

bool is_js_reload_alias(const std::string &word) {
    const std::string lowered = lower_copy(word);
    return lowered == "js" || lowered == "javascript" || lowered == "javascripts";
}

bool is_numeric_lookup_token(const std::string &word) {
    if (word.empty())
        return false;
    std::size_t index = 0;
    if (word[0] == '+' || word[0] == '-')
        index = 1;
    for (; index < word.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(word[index])))
            return false;
    }
    return true;
}

bool parse_positive_vnum(const std::string &word, int *vnum) {
    if (!is_numeric_lookup_token(word) || word.empty() || word == "+" || word == "-")
        return false;

    errno = 0;
    char *end = nullptr;
    const long parsed = std::strtol(word.c_str(), &end, 10);
    if (errno == ERANGE || !end || *end != '\0' || parsed <= 0 ||
        parsed > std::numeric_limits<int>::max())
        return false;

    if (vnum)
        *vnum = static_cast<int>(parsed);
    return true;
}

std::string usage() {
    return "Usage: reload js [refresh|status [package-id|vnum]]\n\r";
}

void append_diagnostics(std::ostringstream &output,
                        const std::vector<JsLiveRegistryReloadDiagnostic> &diagnostics) {
    for (const JsLiveRegistryReloadDiagnostic &diagnostic : diagnostics) {
        output << "  diagnostic "
               << js_live_registry_reload_diagnostic_code_name(diagnostic.code) << ": "
               << diagnostic.message << "\n\r";
    }
}

void append_status_diagnostics(std::ostringstream &output,
                               const std::vector<JsLiveRegistryStatusDiagnostic> &diagnostics) {
    for (const JsLiveRegistryStatusDiagnostic &diagnostic : diagnostics) {
        output << "  diagnostic "
               << js_live_registry_status_diagnostic_code_name(diagnostic.code) << ": "
               << diagnostic.message << "\n\r";
    }
}

} // namespace

JsLiveRegistryAdminService::JsLiveRegistryAdminService()
    : m_publish_service(m_live_store, js_publish_endpoint_server_options()) {}

JsLiveRegistryAdminService::JsLiveRegistryAdminService(
    const JsLiveRegistryReloadOptions &reload_options)
    : m_publish_service(m_live_store, js_publish_endpoint_server_options()),
      m_reload_service(reload_options) {}

JsLiveRegistryAdminService::JsLiveRegistryAdminService(
    const JsLivePackageStoreOptions &live_store_options,
    const JsLiveRegistryReloadOptions &reload_options)
    : m_live_store(live_store_options),
      m_publish_service(m_live_store, js_publish_endpoint_server_options()),
      m_reload_service(reload_options) {}

JsLiveRegistryReloadResult JsLiveRegistryAdminService::refresh() {
    JsLiveRegistryReloadResult result;
    m_reload_service.refresh_from_live_store(m_live_store, &result);
    return result;
}

JsLiveRegistryStartupLoadResult
JsLiveRegistryAdminService::hydrate_from_file(const std::string &path) {
    JsLiveRegistryStartupLoadResult result;
    result.file_load = js_live_package_store_snapshot_load_file(path);
    if (!result.file_load.ok)
        return result;

    JsLivePackageStoreSnapshot loaded_snapshot = result.file_load.snapshot;
    result.file_load.snapshot = {};
    if (!js_live_registry_snapshot_matches_server_instance(loaded_snapshot,
            m_reload_service.expected_server_instance_id(), &result.reload))
        return result;
    const JsLivePackageStoreSnapshot previous_snapshot = m_live_store.export_snapshot();
    result.store_hydration = m_live_store.hydrate_from_snapshot(loaded_snapshot);
    if (!result.store_hydration.ok)
        return result;

    result.reload = refresh();
    if (!result.reload.ok) {
        result.rollback_hydration = m_live_store.hydrate_from_snapshot(previous_snapshot);
        return result;
    }

    result.ok = true;
    return result;
}

JsLiveRegistryStatusResult JsLiveRegistryAdminService::status_snapshot() const {
    return js_live_registry_status_snapshot(m_reload_service);
}

JsLiveRegistryStatusResult
JsLiveRegistryAdminService::status_for_package_id(const std::string &package_id) const {
    return js_live_registry_status_for_package_id(m_reload_service, package_id);
}

JsLiveRegistryStatusResult JsLiveRegistryAdminService::status_for_vnum(int vnum) const {
    return js_live_registry_status_for_vnum(m_reload_service, vnum);
}

JsLivePackageStore &JsLiveRegistryAdminService::live_store() { return m_live_store; }

JsPublishEndpointService &JsLiveRegistryAdminService::publish_service() {
    return m_publish_service;
}

const JsLiveRegistryReloadService &JsLiveRegistryAdminService::reload_service() const {
    return m_reload_service;
}

JsPublishEndpointServiceOptions js_publish_endpoint_server_options() {
    JsPublishEndpointServiceOptions options;
    options.package_validation_options.mode =
        JsScriptPackageValidationMode::InternalValidationOnly;
    options.server_instance_id = js_server_instance_id();
    return options;
}

JsLiveRegistryReloadOptions js_live_registry_server_reload_options() {
    JsLiveRegistryReloadOptions options;
    options.replace_options.validation_options.mode =
        JsScriptPackageValidationMode::InternalValidationOnly;
    options.expected_server_instance_id = js_server_instance_id();
    return options;
}

JsLiveRegistryAdminService &js_live_registry_admin_service() {
    static JsLiveRegistryAdminService service(js_live_registry_server_reload_options());
    return service;
}

JsLiveRegistryReloadResult js_live_registry_startup_refresh() {
    return js_live_registry_admin_service().refresh();
}

JsLiveRegistryStartupLoadResult js_live_registry_startup_load_file(const std::string &path) {
    return js_live_registry_admin_service().hydrate_from_file(path);
}

std::string js_live_registry_format_reload_result(const JsLiveRegistryReloadResult &reload) {
    std::ostringstream output;
    output << "JavaScript live registry refresh: "
           << js_live_registry_reload_status_name(reload.status) << " (packages="
           << reload.package_count << ").\n\r";
    append_diagnostics(output, reload.diagnostics);
    return output.str();
}

std::string js_live_registry_format_status_result(const JsLiveRegistryStatusResult &status) {
    std::ostringstream output;
    output << "JavaScript live registry status: packages=" << status.summary.package_count
           << ", successful-reloads=" << status.summary.successful_reload_count
           << ", last-successful-packages=" << status.summary.last_successful_package_count
           << ".\n\r";
    append_status_diagnostics(output, status.diagnostics);
    for (const JsLiveRegistryPackageInspection &package : status.packages) {
        output << "  package " << package.package_id << " vnum=" << package.vnum
               << " zone=" << package.zone << " host=" << package.host << " version="
               << package.package_version_id << "\n\r";
        output << "    staged=" << package.staged_digest << " current-live="
               << package.current_live_checksum << "\n\r";
        output << "    manifest=" << package.manifest_checksum << " runtime="
               << package.runtime_name << " " << package.runtime_version << " typings="
               << package.generated_typings_version << "\n\r";
        output << "    compiled-js=" << package.compiled_javascript_checksum << " loaded-at="
               << package.loaded_at_epoch_seconds << "\n\r";
        for (const JsLiveRegistryTriggerBindingStatus &binding : package.trigger_bindings) {
            output << "    trigger " << binding.kind << ":" << binding.legacy_value << " -> "
                   << binding.handler_name << "\n\r";
        }
    }
    return output.str();
}

JsLiveRegistryAdminCommandResult
js_live_registry_handle_reload_command(JsLiveRegistryAdminService &service,
                                       const std::string &argument) {
    const std::vector<std::string> words = split_words(argument);
    if (words.empty() || !is_js_reload_alias(words[0]))
        return {};

    JsLiveRegistryAdminCommandResult command;
    command.handled = true;
    if (words.size() > 3) {
        command.output = usage();
        return command;
    }

    const std::string action = words.size() > 1 ? lower_copy(words[1]) : "refresh";
    if (action == "help") {
        command.ok = true;
        command.output = usage();
        return command;
    }

    if (action == "refresh" || action == "reload") {
        if (words.size() > 2) {
            command.output = usage();
            return command;
        }
        const JsLiveRegistryReloadResult reload = service.refresh();
        const JsLiveRegistryStatusResult status = service.status_snapshot();
        command.ok = reload.ok && status.ok;
        command.output = js_live_registry_format_reload_result(reload) +
                         js_live_registry_format_status_result(status);
        return command;
    }

    if (action == "status") {
        JsLiveRegistryStatusResult status;
        if (words.size() == 2)
            status = service.status_snapshot();
        else if (is_numeric_lookup_token(words[2])) {
            int vnum = 0;
            status = service.status_for_vnum(parse_positive_vnum(words[2], &vnum) ? vnum : 0);
        } else
            status = service.status_for_package_id(words[2]);
        command.ok = status.ok;
        command.output = js_live_registry_format_status_result(status);
        return command;
    }

    command.output = usage();
    return command;
}
