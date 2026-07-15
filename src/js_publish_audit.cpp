#include "js_publish_audit.h"

#include "json_utils.h"

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr std::size_t MaxAuditFieldBytes = 180;

void add_diag(JsPublishAuditAppendResult &result, const std::string &message)
{
    result.diagnostics.push_back({ message });
}

bool is_safe_relative_path(const std::string &path, std::string *error)
{
    if (path.empty()) {
        *error = "Publish audit path is required.";
        return false;
    }
    if (path[0] == '/' || path.find("..") != std::string::npos
        || path.find('\\') != std::string::npos || path.find('\0') != std::string::npos
        || path.back() == '/') {
        *error = "Publish audit path must be a safe relative file path.";
        return false;
    }
    return true;
}

bool validate_parent_directories(const std::string &path, std::string *error)
{
    std::string current;
    std::string::size_type offset = 0;
    for (;;) {
        const std::string::size_type slash = path.find('/', offset);
        if (slash == std::string::npos)
            return true;
        const std::string component = path.substr(offset, slash - offset);
        if (component.empty() || component == "." || component == "..") {
            *error = "Publish audit path contains an unsafe directory component.";
            return false;
        }
        if (!current.empty())
            current += "/";
        current += component;

        struct stat st;
        if (lstat(current.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)
            || S_ISLNK(st.st_mode)) {
            *error = "Publish audit parent directory is unavailable.";
            return false;
        }
        if ((st.st_mode & S_IWOTH) != 0) {
            *error = "Publish audit parent directory is world-writable.";
            return false;
        }
        offset = slash + 1;
    }
}

std::string parent_directory(const std::string &path)
{
    const std::string::size_type slash = path.find_last_of('/');
    if (slash == std::string::npos)
        return ".";
    if (slash == 0)
        return "/";
    return path.substr(0, slash);
}

bool fsync_directory(const std::string &path)
{
    const int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return false;
    const bool ok = fsync(fd) == 0;
    close(fd);
    return ok;
}

bool write_all(int fd, const char *data, std::size_t size)
{
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

bool is_bounded_audit_value(const std::string &value)
{
    return !value.empty() && value.size() <= MaxAuditFieldBytes
        && value.find('\0') == std::string::npos
        && value.find('\n') == std::string::npos
        && value.find('\r') == std::string::npos;
}

bool validate_event(const JsPublishAuditEvent &event, JsPublishAuditAppendResult *result)
{
    if (event.operation != "activate" && event.operation != "rollback") {
        add_diag(*result, "Publish audit operation is unsupported.");
        return false;
    }
    if (event.occurred_at_epoch_seconds <= 0) {
        add_diag(*result, "Publish audit timestamp is required.");
        return false;
    }
    const std::string *required_fields[] = {
        &event.audit_id,
        &event.request_id,
        &event.actor_id,
        &event.builder_account_id,
        &event.package_id,
        &event.package_version_id,
        &event.staged_digest,
        &event.expected_previous_live_checksum,
        &event.current_live_checksum,
    };
    for (const std::string *field : required_fields) {
        if (!is_bounded_audit_value(*field)) {
            add_diag(*result, "Publish audit event contains invalid metadata.");
            return false;
        }
    }
    return true;
}

void append_field(std::string &out, const char *name, const std::string &value)
{
    out += "\"";
    out += name;
    out += "\":\"";
    json_utils::append_escaped_json_string(out, value);
    out += "\",";
}

void append_field(std::string &out, const char *name, long long value, bool comma = true)
{
    out += "\"";
    out += name;
    out += "\":";
    out += std::to_string(value);
    if (comma)
        out += ",";
}

std::string event_to_json_line(const JsPublishAuditEvent &event)
{
    std::string out = "{";
    append_field(out, "schemaVersion", JS_PUBLISH_AUDIT_JSONL_SCHEMA_VERSION);
    append_field(out, "operation", event.operation);
    append_field(out, "auditId", event.audit_id);
    append_field(out, "requestId", event.request_id);
    append_field(out, "actorId", event.actor_id);
    append_field(out, "builderAccountId", event.builder_account_id);
    append_field(out, "packageId", event.package_id);
    append_field(out, "packageVersionId", event.package_version_id);
    append_field(out, "stagedDigest", event.staged_digest);
    append_field(out, "expectedPreviousLiveChecksum", event.expected_previous_live_checksum);
    append_field(out, "currentLiveChecksum", event.current_live_checksum);
    append_field(out, "occurredAtEpochSeconds", event.occurred_at_epoch_seconds, false);
    out += "}\n";
    return out;
}

} // namespace

JsPublishAuditAppendResult
js_publish_audit_append_event(const JsPublishAuditEvent &event,
                              const JsPublishAuditAppendOptions &options)
{
    JsPublishAuditAppendResult result;
    std::string path_error;
    if (!is_safe_relative_path(options.path, &path_error)) {
        add_diag(result, path_error);
        return result;
    }
    if (!validate_parent_directories(options.path, &path_error)) {
        add_diag(result, path_error);
        return result;
    }
    if (!validate_event(event, &result))
        return result;

    const std::string line = event_to_json_line(event);
    if (line.size() > options.maximum_event_bytes) {
        add_diag(result, "Publish audit event exceeds configured size limit.");
        return result;
    }

    const int fd = open(options.path.c_str(),
        O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
    if (fd < 0) {
        add_diag(result, "Publish audit file could not be opened.");
        return result;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        add_diag(result, "Publish audit file must be a regular file.");
        return result;
    }
    if (!write_all(fd, line.data(), line.size()) || fsync(fd) != 0) {
        close(fd);
        add_diag(result, "Publish audit file could not be written.");
        return result;
    }
    if (close(fd) != 0) {
        add_diag(result, "Publish audit file could not be closed.");
        return result;
    }
    if (!fsync_directory(parent_directory(options.path))) {
        add_diag(result, "Publish audit directory could not be synchronized.");
        return result;
    }

    result.ok = true;
    return result;
}
