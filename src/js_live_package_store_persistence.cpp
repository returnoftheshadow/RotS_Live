#include "js_live_package_store_persistence.h"

#include "json_utils.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <sstream>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

constexpr std::size_t MaxJsonBytes = 2 * 1024 * 1024;
constexpr std::size_t MaxPersistedRecords = 128;
constexpr std::size_t MaxPersistedPointers = 128;
constexpr std::size_t MaxPersistedTriggerBindings = 512;
constexpr std::size_t MaxPersistedSourceBytes = 256 * 1024;

void add_diag(JsLivePackageStorePersistenceLoadResult &result, const std::string &message) {
    result.diagnostics.push_back({message});
}

void add_diag(JsLivePackageStorePersistenceFileResult &result, const std::string &message) {
    result.diagnostics.push_back({message});
}

void append_json_string(std::string &out, const std::string &value) {
    json_utils::append_escaped_json_string(out, value);
}

void append_field(std::string &out, const char *name, const std::string &value, bool comma = true) {
    out += "\"";
    out += name;
    out += "\":\"";
    append_json_string(out, value);
    out += "\"";
    if (comma)
        out += ",";
}

void append_field(std::string &out, const char *name, int value, bool comma = true) {
    out += "\"";
    out += name;
    out += "\":";
    out += std::to_string(value);
    if (comma)
        out += ",";
}

void append_field(std::string &out, const char *name, long long value, bool comma = true) {
    out += "\"";
    out += name;
    out += "\":";
    out += std::to_string(value);
    if (comma)
        out += ",";
}

bool parse_host(const std::string &value, JsScriptPackageHost *host) {
    if (value == "character")
        *host = JsScriptPackageHost::Character;
    else if (value == "object")
        *host = JsScriptPackageHost::Object;
    else if (value == "room")
        *host = JsScriptPackageHost::Room;
    else if (value == "mudlle-mobile")
        *host = JsScriptPackageHost::MudlleMobile;
    else
        return false;
    return true;
}

bool parse_kind(const std::string &value, JsScriptingManifestKind *kind) {
    if (value == "legacy-script-trigger")
        *kind = JsScriptingManifestKind::LegacyScriptTrigger;
    else if (value == "mudlle-call-flag")
        *kind = JsScriptingManifestKind::MudlleCallFlag;
    else
        return false;
    return true;
}

void append_identity(std::string &out, const JsStagedPackageIdentity &identity) {
    out += "{";
    append_field(out, "zone", identity.zone);
    append_field(out, "vnum", identity.vnum);
    append_field(out, "host", js_script_package_host_name(identity.host));
    append_field(out, "package_id", identity.package_id);
    append_field(out, "package_version_id", identity.package_version_id);
    append_field(out, "canonical_digest", identity.canonical_digest);
    append_field(out, "digest_algorithm", identity.digest_algorithm);
    append_field(out, "canonical_format_version", identity.canonical_format_version);
    append_field(out, "package_format_version", identity.package_format_version);
    append_field(out, "builder_account_id", identity.builder_account_id);
    append_field(out, "server_instance_id", identity.server_instance_id);
    append_field(out, "base_live_checksum", identity.base_live_checksum);
    append_field(out, "manifest_checksum", identity.manifest_checksum);
    append_field(out, "compiled_javascript_checksum", identity.compiled_javascript_checksum);
    append_field(out, "runtime_name", identity.runtime_name);
    append_field(out, "runtime_version", identity.runtime_version);
    append_field(out, "generated_typings_version", identity.generated_typings_version, false);
    out += "}";
}

void append_audit(std::string &out, const JsStagedPackageAuditMetadata &audit) {
    out += "{";
    append_field(out, "staged_at_epoch_seconds", audit.staged_at_epoch_seconds);
    append_field(out, "request_id", audit.request_id);
    append_field(out, "actor_id", audit.actor_id);
    append_field(out, "permission_snapshot_id", audit.permission_snapshot_id);
    append_field(out, "audit_id", audit.audit_id);
    append_field(out, "source_policy_decision", audit.source_policy_decision);
    append_field(out, "validation_report_digest", audit.validation_report_digest);
    append_field(out, "transport_source_identifier", audit.transport_source_identifier, false);
    out += "}";
}

void append_binding(std::string &out, const JsScriptTriggerBinding &binding) {
    out += "{";
    append_field(out, "kind", js_scripting_manifest_kind_name(binding.kind));
    append_field(out, "legacy_value", binding.legacy_value);
    append_field(out, "handler_name", binding.handler_name, false);
    out += "}";
}

void append_package(std::string &out, const JsScriptPackage &package) {
    out += "{";
    append_field(out, "vnum", package.vnum);
    append_field(out, "package_id", package.package_id);
    append_field(out, "host", js_script_package_host_name(package.host));
    append_field(out, "package_format_version", package.package_format_version);
    append_field(out, "manifest_schema_version", package.manifest_schema_version);
    append_field(out, "trigger_catalog_revision", package.trigger_catalog_revision);
    append_field(out, "manifest_checksum", package.manifest_checksum);
    append_field(out, "runtime_name", package.runtime_name);
    append_field(out, "runtime_version", package.runtime_version);
    append_field(out, "generated_typings_version", package.generated_typings_version);
    append_field(out, "compiled_javascript_checksum", package.compiled_javascript_checksum);
    append_field(out, "compiled_javascript", package.compiled_javascript);
    out += "\"trigger_bindings\":[";
    for (std::size_t i = 0; i < package.trigger_bindings.size(); ++i) {
        if (i > 0)
            out += ",";
        append_binding(out, package.trigger_bindings[i]);
    }
    out += "]}";
}

void append_record(std::string &out, const JsLivePackageRecord &record) {
    out += "{\"identity\":";
    append_identity(out, record.identity);
    out += ",\"staged_audit\":";
    append_audit(out, record.staged_audit);
    out += ",\"package\":";
    append_package(out, record.package);
    out += "}";
}

void append_pointer(std::string &out, const JsLivePackagePointer &pointer) {
    out += "{";
    append_field(out, "zone", pointer.zone);
    append_field(out, "vnum", pointer.vnum);
    append_field(out, "host", js_script_package_host_name(pointer.host));
    append_field(out, "package_id", pointer.package_id);
    append_field(out, "package_version_id", pointer.package_version_id);
    append_field(out, "staged_digest", pointer.staged_digest);
    append_field(out, "expected_previous_live_checksum", pointer.expected_previous_live_checksum);
    append_field(out, "current_live_checksum", pointer.current_live_checksum);
    append_field(out, "loaded_at_epoch_seconds", pointer.loaded_at_epoch_seconds);
    append_field(out, "load_audit_id", pointer.load_audit_id, false);
    out += "}";
}

bool mark_seen(unsigned long long *seen, unsigned long long bit, const char *object_name,
               const std::string &, std::string *error) {
    if ((*seen & bit) != 0) {
        if (error)
            *error = std::string("Live package ") + object_name +
                " JSON has a duplicate field.";
        return false;
    }
    *seen |= bit;
    return true;
}

bool reject_unknown_field(const char *object_name, const std::string &, std::string *error) {
    if (error)
        *error = std::string("Live package ") + object_name + " JSON has an unknown field.";
    return false;
}

bool require_fields(unsigned long long seen, unsigned long long required, const char *object_name,
                    std::string *error) {
    if ((seen & required) == required)
        return true;
    if (error)
        *error = std::string("Live package ") + object_name +
            " JSON is missing required fields.";
    return false;
}

bool is_path_separator(char ch) {
    return ch == '/';
}

bool validate_safe_relative_path(const std::string &path, std::string *error) {
    if (path.empty()) {
        if (error)
            *error = "Live package store path is empty.";
        return false;
    }
    if (is_path_separator(path.front())) {
        if (error)
            *error = "Live package store path must be relative.";
        return false;
    }

    std::string current;
    std::size_t component_start = 0;
    while (component_start <= path.size()) {
        const std::size_t component_end = path.find('/', component_start);
        const bool last_component = component_end == std::string::npos;
        const std::string component =
            path.substr(component_start, last_component ? std::string::npos
                                                       : component_end - component_start);
        if (component.empty() || component == "." || component == "..") {
            if (error)
                *error = "Live package store path contains an unsafe component.";
            return false;
        }
        if (!current.empty())
            current += "/";
        current += component;

        if (!last_component) {
            struct stat st;
            if (lstat(current.c_str(), &st) != 0 || !S_ISDIR(st.st_mode) ||
                S_ISLNK(st.st_mode) || (st.st_mode & S_IWOTH) != 0) {
                if (error)
                    *error = "Live package store parent path is not a trusted directory.";
                return false;
            }
        }

        if (last_component)
            break;
        component_start = component_end + 1;
    }
    return true;
}

std::string parent_directory(const std::string &path) {
    const std::string::size_type slash = path.find_last_of('/');
    if (slash == std::string::npos)
        return ".";
    return path.substr(0, slash);
}

bool reject_unsafe_existing_file(const std::string &path, std::string *error) {
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) {
        if (errno == ENOENT)
            return true;
        if (error)
            *error = "Live package store file metadata could not be read.";
        return false;
    }
    if (S_ISLNK(st.st_mode) || !S_ISREG(st.st_mode)) {
        if (error)
            *error = "Live package store path is not a regular file.";
        return false;
    }
    return true;
}

bool write_all(int fd, const char *data, std::size_t size) {
    std::size_t written = 0;
    while (written < size) {
        const ssize_t count = write(fd, data + written, size - written);
        if (count < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (count == 0)
            return false;
        written += static_cast<std::size_t>(count);
    }
    return true;
}

bool fsync_directory(const std::string &path) {
    const int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return false;
    const bool ok = fsync(fd) == 0;
    close(fd);
    return ok;
}

bool parse_identity(json_utils::JsonReader *reader, JsStagedPackageIdentity *identity,
                    std::string *error) {
    JsStagedPackageIdentity parsed;
    std::string host;
    unsigned long long seen = 0;
    constexpr unsigned long long Zone = 1ULL << 0;
    constexpr unsigned long long Vnum = 1ULL << 1;
    constexpr unsigned long long Host = 1ULL << 2;
    constexpr unsigned long long PackageId = 1ULL << 3;
    constexpr unsigned long long PackageVersionId = 1ULL << 4;
    constexpr unsigned long long CanonicalDigest = 1ULL << 5;
    constexpr unsigned long long DigestAlgorithm = 1ULL << 6;
    constexpr unsigned long long CanonicalFormatVersion = 1ULL << 7;
    constexpr unsigned long long PackageFormatVersion = 1ULL << 8;
    constexpr unsigned long long BuilderAccountId = 1ULL << 9;
    constexpr unsigned long long ServerInstanceId = 1ULL << 10;
    constexpr unsigned long long BaseLiveChecksum = 1ULL << 11;
    constexpr unsigned long long ManifestChecksum = 1ULL << 12;
    constexpr unsigned long long CompiledJavascriptChecksum = 1ULL << 13;
    constexpr unsigned long long RuntimeName = 1ULL << 14;
    constexpr unsigned long long RuntimeVersion = 1ULL << 15;
    constexpr unsigned long long GeneratedTypingsVersion = 1ULL << 16;
    constexpr unsigned long long Required = Zone | Vnum | Host | PackageId | PackageVersionId |
        CanonicalDigest | DigestAlgorithm | CanonicalFormatVersion | PackageFormatVersion |
        BuilderAccountId | ServerInstanceId | BaseLiveChecksum | ManifestChecksum |
        CompiledJavascriptChecksum | RuntimeName | RuntimeVersion | GeneratedTypingsVersion;
    if (!reader->parse_object([&](const std::string &key, json_utils::JsonReader *nested,
                                  std::string *nested_error) {
            if (key == "zone") {
                if (!mark_seen(&seen, Zone, "identity", key, nested_error))
                    return false;
                return nested->parse_integer(&parsed.zone, nested_error);
            }
            if (key == "vnum") {
                if (!mark_seen(&seen, Vnum, "identity", key, nested_error))
                    return false;
                return nested->parse_integer(&parsed.vnum, nested_error);
            }
            if (key == "host") {
                if (!mark_seen(&seen, Host, "identity", key, nested_error))
                    return false;
                return nested->parse_string(&host, nested_error);
            }
            if (key == "package_id") {
                if (!mark_seen(&seen, PackageId, "identity", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.package_id, nested_error);
            }
            if (key == "package_version_id") {
                if (!mark_seen(&seen, PackageVersionId, "identity", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.package_version_id, nested_error);
            }
            if (key == "canonical_digest") {
                if (!mark_seen(&seen, CanonicalDigest, "identity", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.canonical_digest, nested_error);
            }
            if (key == "digest_algorithm") {
                if (!mark_seen(&seen, DigestAlgorithm, "identity", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.digest_algorithm, nested_error);
            }
            if (key == "canonical_format_version") {
                if (!mark_seen(&seen, CanonicalFormatVersion, "identity", key, nested_error))
                    return false;
                return nested->parse_integer(&parsed.canonical_format_version, nested_error);
            }
            if (key == "package_format_version") {
                if (!mark_seen(&seen, PackageFormatVersion, "identity", key, nested_error))
                    return false;
                return nested->parse_integer(&parsed.package_format_version, nested_error);
            }
            if (key == "builder_account_id") {
                if (!mark_seen(&seen, BuilderAccountId, "identity", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.builder_account_id, nested_error);
            }
            if (key == "server_instance_id") {
                if (!mark_seen(&seen, ServerInstanceId, "identity", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.server_instance_id, nested_error);
            }
            if (key == "base_live_checksum") {
                if (!mark_seen(&seen, BaseLiveChecksum, "identity", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.base_live_checksum, nested_error);
            }
            if (key == "manifest_checksum") {
                if (!mark_seen(&seen, ManifestChecksum, "identity", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.manifest_checksum, nested_error);
            }
            if (key == "compiled_javascript_checksum") {
                if (!mark_seen(&seen, CompiledJavascriptChecksum, "identity", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.compiled_javascript_checksum, nested_error);
            }
            if (key == "runtime_name") {
                if (!mark_seen(&seen, RuntimeName, "identity", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.runtime_name, nested_error);
            }
            if (key == "runtime_version") {
                if (!mark_seen(&seen, RuntimeVersion, "identity", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.runtime_version, nested_error);
            }
            if (key == "generated_typings_version") {
                if (!mark_seen(&seen, GeneratedTypingsVersion, "identity", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.generated_typings_version, nested_error);
            }
            return reject_unknown_field("identity", key, nested_error);
        }, error))
        return false;
    if (!require_fields(seen, Required, "identity", error))
        return false;
    if (!parse_host(host, &parsed.host)) {
        if (error)
            *error = "Live package identity host is invalid.";
        return false;
    }
    *identity = parsed;
    return true;
}

bool parse_audit(json_utils::JsonReader *reader, JsStagedPackageAuditMetadata *audit,
                 std::string *error) {
    JsStagedPackageAuditMetadata parsed;
    long staged_at = 0;
    unsigned long long seen = 0;
    constexpr unsigned long long StagedAt = 1ULL << 0;
    constexpr unsigned long long RequestId = 1ULL << 1;
    constexpr unsigned long long ActorId = 1ULL << 2;
    constexpr unsigned long long PermissionSnapshotId = 1ULL << 3;
    constexpr unsigned long long AuditId = 1ULL << 4;
    constexpr unsigned long long SourcePolicyDecision = 1ULL << 5;
    constexpr unsigned long long ValidationReportDigest = 1ULL << 6;
    constexpr unsigned long long TransportSourceIdentifier = 1ULL << 7;
    constexpr unsigned long long Required = StagedAt | RequestId | ActorId |
        PermissionSnapshotId | AuditId | SourcePolicyDecision | ValidationReportDigest |
        TransportSourceIdentifier;
    if (!reader->parse_object([&](const std::string &key, json_utils::JsonReader *nested,
                                  std::string *nested_error) {
            if (key == "staged_at_epoch_seconds") {
                if (!mark_seen(&seen, StagedAt, "audit", key, nested_error))
                    return false;
                return nested->parse_long(&staged_at, nested_error);
            }
            if (key == "request_id") {
                if (!mark_seen(&seen, RequestId, "audit", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.request_id, nested_error);
            }
            if (key == "actor_id") {
                if (!mark_seen(&seen, ActorId, "audit", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.actor_id, nested_error);
            }
            if (key == "permission_snapshot_id") {
                if (!mark_seen(&seen, PermissionSnapshotId, "audit", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.permission_snapshot_id, nested_error);
            }
            if (key == "audit_id") {
                if (!mark_seen(&seen, AuditId, "audit", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.audit_id, nested_error);
            }
            if (key == "source_policy_decision") {
                if (!mark_seen(&seen, SourcePolicyDecision, "audit", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.source_policy_decision, nested_error);
            }
            if (key == "validation_report_digest") {
                if (!mark_seen(&seen, ValidationReportDigest, "audit", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.validation_report_digest, nested_error);
            }
            if (key == "transport_source_identifier") {
                if (!mark_seen(&seen, TransportSourceIdentifier, "audit", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.transport_source_identifier, nested_error);
            }
            return reject_unknown_field("audit", key, nested_error);
        }, error))
        return false;
    if (!require_fields(seen, Required, "audit", error))
        return false;
    parsed.staged_at_epoch_seconds = staged_at;
    *audit = parsed;
    return true;
}

bool parse_binding(json_utils::JsonReader *reader, JsScriptTriggerBinding *binding,
                   std::string *error) {
    JsScriptTriggerBinding parsed;
    std::string kind;
    unsigned long long seen = 0;
    constexpr unsigned long long Kind = 1ULL << 0;
    constexpr unsigned long long LegacyValue = 1ULL << 1;
    constexpr unsigned long long HandlerName = 1ULL << 2;
    constexpr unsigned long long Required = Kind | LegacyValue | HandlerName;
    if (!reader->parse_object([&](const std::string &key, json_utils::JsonReader *nested,
                                  std::string *nested_error) {
            if (key == "kind") {
                if (!mark_seen(&seen, Kind, "trigger binding", key, nested_error))
                    return false;
                return nested->parse_string(&kind, nested_error);
            }
            if (key == "legacy_value") {
                if (!mark_seen(&seen, LegacyValue, "trigger binding", key, nested_error))
                    return false;
                return nested->parse_integer(&parsed.legacy_value, nested_error);
            }
            if (key == "handler_name") {
                if (!mark_seen(&seen, HandlerName, "trigger binding", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.handler_name, nested_error);
            }
            return reject_unknown_field("trigger binding", key, nested_error);
        }, error))
        return false;
    if (!require_fields(seen, Required, "trigger binding", error))
        return false;
    if (!parse_kind(kind, &parsed.kind)) {
        if (error)
            *error = "Live package trigger binding kind is invalid.";
        return false;
    }
    *binding = parsed;
    return true;
}

bool parse_package(json_utils::JsonReader *reader, JsScriptPackage *package, std::string *error) {
    JsScriptPackage parsed;
    std::string host;
    unsigned long long seen = 0;
    constexpr unsigned long long Vnum = 1ULL << 0;
    constexpr unsigned long long PackageId = 1ULL << 1;
    constexpr unsigned long long Host = 1ULL << 2;
    constexpr unsigned long long PackageFormatVersion = 1ULL << 3;
    constexpr unsigned long long ManifestSchemaVersion = 1ULL << 4;
    constexpr unsigned long long TriggerCatalogRevision = 1ULL << 5;
    constexpr unsigned long long ManifestChecksum = 1ULL << 6;
    constexpr unsigned long long RuntimeName = 1ULL << 7;
    constexpr unsigned long long RuntimeVersion = 1ULL << 8;
    constexpr unsigned long long GeneratedTypingsVersion = 1ULL << 9;
    constexpr unsigned long long CompiledJavascriptChecksum = 1ULL << 10;
    constexpr unsigned long long CompiledJavascript = 1ULL << 11;
    constexpr unsigned long long TriggerBindings = 1ULL << 12;
    constexpr unsigned long long Required = Vnum | PackageId | Host | PackageFormatVersion |
        ManifestSchemaVersion | TriggerCatalogRevision | ManifestChecksum | RuntimeName |
        RuntimeVersion | GeneratedTypingsVersion | CompiledJavascriptChecksum |
        CompiledJavascript | TriggerBindings;
    if (!reader->parse_object([&](const std::string &key, json_utils::JsonReader *nested,
                                  std::string *nested_error) {
            if (key == "vnum") {
                if (!mark_seen(&seen, Vnum, "package", key, nested_error))
                    return false;
                return nested->parse_integer(&parsed.vnum, nested_error);
            }
            if (key == "package_id") {
                if (!mark_seen(&seen, PackageId, "package", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.package_id, nested_error);
            }
            if (key == "host") {
                if (!mark_seen(&seen, Host, "package", key, nested_error))
                    return false;
                return nested->parse_string(&host, nested_error);
            }
            if (key == "package_format_version") {
                if (!mark_seen(&seen, PackageFormatVersion, "package", key, nested_error))
                    return false;
                return nested->parse_integer(&parsed.package_format_version, nested_error);
            }
            if (key == "manifest_schema_version") {
                if (!mark_seen(&seen, ManifestSchemaVersion, "package", key, nested_error))
                    return false;
                return nested->parse_integer(&parsed.manifest_schema_version, nested_error);
            }
            if (key == "trigger_catalog_revision") {
                if (!mark_seen(&seen, TriggerCatalogRevision, "package", key, nested_error))
                    return false;
                return nested->parse_integer(&parsed.trigger_catalog_revision, nested_error);
            }
            if (key == "manifest_checksum") {
                if (!mark_seen(&seen, ManifestChecksum, "package", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.manifest_checksum, nested_error);
            }
            if (key == "runtime_name") {
                if (!mark_seen(&seen, RuntimeName, "package", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.runtime_name, nested_error);
            }
            if (key == "runtime_version") {
                if (!mark_seen(&seen, RuntimeVersion, "package", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.runtime_version, nested_error);
            }
            if (key == "generated_typings_version") {
                if (!mark_seen(&seen, GeneratedTypingsVersion, "package", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.generated_typings_version, nested_error);
            }
            if (key == "compiled_javascript_checksum") {
                if (!mark_seen(&seen, CompiledJavascriptChecksum, "package", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.compiled_javascript_checksum, nested_error);
            }
            if (key == "compiled_javascript") {
                if (!mark_seen(&seen, CompiledJavascript, "package", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.compiled_javascript, nested_error);
            }
            if (key == "trigger_bindings") {
                if (!mark_seen(&seen, TriggerBindings, "package", key, nested_error))
                    return false;
                return nested->parse_array([&](json_utils::JsonReader *binding_reader,
                                               std::string *binding_error) {
                    if (parsed.trigger_bindings.size() >= MaxPersistedTriggerBindings) {
                        if (binding_error)
                            *binding_error =
                                "Live package trigger binding limit exceeded during JSON load.";
                        return false;
                    }
                    JsScriptTriggerBinding binding;
                    if (!parse_binding(binding_reader, &binding, binding_error))
                        return false;
                    parsed.trigger_bindings.push_back(binding);
                    return true;
                }, nested_error);
            }
            return reject_unknown_field("package", key, nested_error);
        }, error))
        return false;
    if (!require_fields(seen, Required, "package", error))
        return false;
    if (!parse_host(host, &parsed.host)) {
        if (error)
            *error = "Live package host is invalid.";
        return false;
    }
    if (parsed.compiled_javascript.size() > MaxPersistedSourceBytes) {
        if (error)
            *error = "Live package compiled JavaScript exceeds persistence limit.";
        return false;
    }
    *package = parsed;
    return true;
}

bool parse_record(json_utils::JsonReader *reader, JsLivePackageRecord *record, std::string *error) {
    JsLivePackageRecord parsed;
    unsigned long long seen = 0;
    constexpr unsigned long long Identity = 1ULL << 0;
    constexpr unsigned long long StagedAudit = 1ULL << 1;
    constexpr unsigned long long Package = 1ULL << 2;
    constexpr unsigned long long Required = Identity | StagedAudit | Package;
    if (!reader->parse_object([&](const std::string &key, json_utils::JsonReader *nested,
                                  std::string *nested_error) {
            if (key == "identity") {
                if (!mark_seen(&seen, Identity, "record", key, nested_error))
                    return false;
                return parse_identity(nested, &parsed.identity, nested_error);
            }
            if (key == "staged_audit") {
                if (!mark_seen(&seen, StagedAudit, "record", key, nested_error))
                    return false;
                return parse_audit(nested, &parsed.staged_audit, nested_error);
            }
            if (key == "package") {
                if (!mark_seen(&seen, Package, "record", key, nested_error))
                    return false;
                return parse_package(nested, &parsed.package, nested_error);
            }
            return reject_unknown_field("record", key, nested_error);
        }, error))
        return false;
    if (!require_fields(seen, Required, "record", error))
        return false;
    *record = parsed;
    return true;
}

bool parse_pointer(json_utils::JsonReader *reader, JsLivePackagePointer *pointer,
                   std::string *error) {
    JsLivePackagePointer parsed;
    std::string host;
    long loaded_at = 0;
    unsigned long long seen = 0;
    constexpr unsigned long long Zone = 1ULL << 0;
    constexpr unsigned long long Vnum = 1ULL << 1;
    constexpr unsigned long long Host = 1ULL << 2;
    constexpr unsigned long long PackageId = 1ULL << 3;
    constexpr unsigned long long PackageVersionId = 1ULL << 4;
    constexpr unsigned long long StagedDigest = 1ULL << 5;
    constexpr unsigned long long ExpectedPreviousLiveChecksum = 1ULL << 6;
    constexpr unsigned long long CurrentLiveChecksum = 1ULL << 7;
    constexpr unsigned long long LoadedAt = 1ULL << 8;
    constexpr unsigned long long LoadAuditId = 1ULL << 9;
    constexpr unsigned long long Required = Zone | Vnum | Host | PackageId | PackageVersionId |
        StagedDigest | ExpectedPreviousLiveChecksum | CurrentLiveChecksum | LoadedAt | LoadAuditId;
    if (!reader->parse_object([&](const std::string &key, json_utils::JsonReader *nested,
                                  std::string *nested_error) {
            if (key == "zone") {
                if (!mark_seen(&seen, Zone, "pointer", key, nested_error))
                    return false;
                return nested->parse_integer(&parsed.zone, nested_error);
            }
            if (key == "vnum") {
                if (!mark_seen(&seen, Vnum, "pointer", key, nested_error))
                    return false;
                return nested->parse_integer(&parsed.vnum, nested_error);
            }
            if (key == "host") {
                if (!mark_seen(&seen, Host, "pointer", key, nested_error))
                    return false;
                return nested->parse_string(&host, nested_error);
            }
            if (key == "package_id") {
                if (!mark_seen(&seen, PackageId, "pointer", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.package_id, nested_error);
            }
            if (key == "package_version_id") {
                if (!mark_seen(&seen, PackageVersionId, "pointer", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.package_version_id, nested_error);
            }
            if (key == "staged_digest") {
                if (!mark_seen(&seen, StagedDigest, "pointer", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.staged_digest, nested_error);
            }
            if (key == "expected_previous_live_checksum") {
                if (!mark_seen(&seen, ExpectedPreviousLiveChecksum, "pointer", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.expected_previous_live_checksum, nested_error);
            }
            if (key == "current_live_checksum") {
                if (!mark_seen(&seen, CurrentLiveChecksum, "pointer", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.current_live_checksum, nested_error);
            }
            if (key == "loaded_at_epoch_seconds") {
                if (!mark_seen(&seen, LoadedAt, "pointer", key, nested_error))
                    return false;
                return nested->parse_long(&loaded_at, nested_error);
            }
            if (key == "load_audit_id") {
                if (!mark_seen(&seen, LoadAuditId, "pointer", key, nested_error))
                    return false;
                return nested->parse_string(&parsed.load_audit_id, nested_error);
            }
            return reject_unknown_field("pointer", key, nested_error);
        }, error))
        return false;
    if (!require_fields(seen, Required, "pointer", error))
        return false;
    if (!parse_host(host, &parsed.host)) {
        if (error)
            *error = "Live package pointer host is invalid.";
        return false;
    }
    parsed.loaded_at_epoch_seconds = loaded_at;
    *pointer = parsed;
    return true;
}

} // namespace

std::string js_live_package_store_snapshot_to_json(const JsLivePackageStoreSnapshot &snapshot) {
    std::string out;
    out += "{\"schema_version\":";
    out += std::to_string(JS_LIVE_PACKAGE_STORE_JSON_SCHEMA_VERSION);
    out += ",\"records\":[";
    for (std::size_t i = 0; i < snapshot.records.size(); ++i) {
        if (i > 0)
            out += ",";
        append_record(out, snapshot.records[i]);
    }
    out += "],\"live_pointers\":[";
    for (std::size_t i = 0; i < snapshot.live_pointers.size(); ++i) {
        if (i > 0)
            out += ",";
        append_pointer(out, snapshot.live_pointers[i]);
    }
    out += "]}";
    return out;
}

JsLivePackageStorePersistenceLoadResult
js_live_package_store_snapshot_from_json(const std::string &json) {
    JsLivePackageStorePersistenceLoadResult result;
    if (json.size() > MaxJsonBytes) {
        add_diag(result, "Live package store JSON exceeds persistence input limit.");
        return result;
    }
    json_utils::JsonReader reader(json);
    JsLivePackageStoreSnapshot candidate;
    int schema_version = 0;
    bool saw_schema_version = false;
    bool saw_records = false;
    bool saw_live_pointers = false;
    std::string error;
    if (!reader.parse_root_object([&](const std::string &key, json_utils::JsonReader *nested,
                                      std::string *nested_error) {
            if (key == "schema_version") {
                if (saw_schema_version) {
                    if (nested_error)
                        *nested_error = "Live package store JSON has duplicate schema_version.";
                    return false;
                }
                saw_schema_version = true;
                return nested->parse_integer(&schema_version, nested_error);
            }
            if (key == "records") {
                if (saw_records) {
                    if (nested_error)
                        *nested_error = "Live package store JSON has duplicate records.";
                    return false;
                }
                saw_records = true;
                return nested->parse_array([&](json_utils::JsonReader *record_reader,
                                               std::string *record_error) {
                    if (candidate.records.size() >= MaxPersistedRecords) {
                        if (record_error)
                            *record_error = "Live package record limit exceeded during JSON load.";
                        return false;
                    }
                    JsLivePackageRecord record;
                    if (!parse_record(record_reader, &record, record_error))
                        return false;
                    candidate.records.push_back(record);
                    return true;
                }, nested_error);
            }
            if (key == "live_pointers") {
                if (saw_live_pointers) {
                    if (nested_error)
                        *nested_error = "Live package store JSON has duplicate live_pointers.";
                    return false;
                }
                saw_live_pointers = true;
                return nested->parse_array([&](json_utils::JsonReader *pointer_reader,
                                               std::string *pointer_error) {
                    if (candidate.live_pointers.size() >= MaxPersistedPointers) {
                        if (pointer_error)
                            *pointer_error =
                                "Live package pointer limit exceeded during JSON load.";
                        return false;
                    }
                    JsLivePackagePointer pointer;
                    if (!parse_pointer(pointer_reader, &pointer, pointer_error))
                        return false;
                    candidate.live_pointers.push_back(pointer);
                    return true;
                }, nested_error);
            }
            if (nested_error)
                *nested_error = "Live package store JSON has an unknown top-level field.";
            return false;
        }, &error)) {
        add_diag(result, error.empty() ? "Live package store JSON could not be parsed." : error);
        return result;
    }
    if (!saw_schema_version || !saw_records || !saw_live_pointers) {
        add_diag(result, "Live package store JSON is missing required top-level fields.");
        return result;
    }
    if (schema_version != JS_LIVE_PACKAGE_STORE_JSON_SCHEMA_VERSION) {
        add_diag(result, "Live package store JSON schema version is unsupported.");
        return result;
    }
    JsLivePackageStore validation_store;
    JsLivePackageStoreHydrationResult hydration =
        validation_store.hydrate_from_snapshot(candidate);
    if (!hydration.ok) {
        add_diag(result, "Live package store JSON failed hydration validation.");
        for (const JsLivePackageStoreDiagnostic &diagnostic : hydration.diagnostics)
            add_diag(result, diagnostic.message);
        return result;
    }
    result.snapshot = candidate;
    result.ok = true;
    return result;
}

JsLivePackageStorePersistenceLoadResult
js_live_package_store_snapshot_load_file(const std::string &path) {
    JsLivePackageStorePersistenceLoadResult result;
    std::string path_error;
    if (!validate_safe_relative_path(path, &path_error)) {
        add_diag(result, path_error);
        return result;
    }
    if (!reject_unsafe_existing_file(path, &path_error)) {
        add_diag(result, path_error);
        return result;
    }

    const int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        add_diag(result, "Live package store file could not be opened.");
        return result;
    }

    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        add_diag(result, "Live package store file size could not be read.");
        return result;
    }
    const off_t size = st.st_size;
    if (static_cast<std::size_t>(size) > MaxJsonBytes) {
        close(fd);
        add_diag(result, "Live package store file exceeds persistence input limit.");
        return result;
    }
    std::string json(static_cast<std::size_t>(size), '\0');
    std::size_t total_read = 0;
    while (total_read < json.size()) {
        const ssize_t count = read(fd, &json[total_read], json.size() - total_read);
        if (count < 0) {
            if (errno == EINTR)
                continue;
            close(fd);
            add_diag(result, "Live package store file could not be read.");
            return result;
        }
        if (count == 0)
            break;
        total_read += static_cast<std::size_t>(count);
    }
    close(fd);
    if (total_read != json.size()) {
        add_diag(result, "Live package store file could not be read completely.");
        return result;
    }
    return js_live_package_store_snapshot_from_json(json);
}

JsLivePackageStorePersistenceFileResult
js_live_package_store_snapshot_save_file(const std::string &path,
                                         const JsLivePackageStoreSnapshot &snapshot) {
    JsLivePackageStorePersistenceFileResult result;
    std::string path_error;
    if (!validate_safe_relative_path(path, &path_error)) {
        add_diag(result, path_error);
        return result;
    }
    if (!reject_unsafe_existing_file(path, &path_error)) {
        add_diag(result, path_error);
        return result;
    }

    const std::string json = js_live_package_store_snapshot_to_json(snapshot);
    JsLivePackageStorePersistenceLoadResult validation =
        js_live_package_store_snapshot_from_json(json);
    if (!validation.ok) {
        add_diag(result, "Live package store snapshot failed validation before save.");
        for (const JsLivePackageStorePersistenceDiagnostic &diagnostic : validation.diagnostics)
            add_diag(result, diagnostic.message);
        return result;
    }

    const std::string temp_path = path + ".tmp";
    if (!reject_unsafe_existing_file(temp_path, &path_error)) {
        add_diag(result, path_error);
        return result;
    }
    const int fd =
        open(temp_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) {
        add_diag(result, "Live package store temporary file could not be opened.");
        return result;
    }
    if (!write_all(fd, json.data(), json.size()) || fsync(fd) != 0) {
        close(fd);
        std::remove(temp_path.c_str());
        add_diag(result, "Live package store temporary file could not be written.");
        return result;
    }
    if (close(fd) != 0) {
        std::remove(temp_path.c_str());
        add_diag(result, "Live package store temporary file could not be closed.");
        return result;
    }

    if (std::rename(temp_path.c_str(), path.c_str()) != 0) {
        add_diag(result, "Live package store file could not be replaced.");
        std::remove(temp_path.c_str());
        return result;
    }
    result.target_replaced = true;
    if (!fsync_directory(parent_directory(path))) {
        add_diag(result, "Live package store directory could not be synchronized.");
        return result;
    }
    result.ok = true;
    return result;
}
