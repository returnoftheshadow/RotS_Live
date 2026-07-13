#include "js_script_package_reload_service.h"

#include <cerrno>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <linux/limits.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t MaxReloadDiagnosticBytes = 240;

std::string bounded_single_line(std::string message)
{
    for (char& ch : message) {
        if (ch == '\n' || ch == '\r' || static_cast<unsigned char>(ch) < 0x20)
            ch = ' ';
    }
    if (message.size() > MaxReloadDiagnosticBytes)
        message.resize(MaxReloadDiagnosticBytes);
    return message;
}

void add_reload_diagnostic(JsScriptPackageReloadResult& result,
    JsScriptPackageReloadStatus status, const std::string& message)
{
    JsScriptPackageDiagnostic diagnostic;
    diagnostic.code = JsScriptPackageDiagnosticCode::InvalidMetadata;
    diagnostic.message = bounded_single_line(message);
    result.status = status;
    result.diagnostics.push_back(std::move(diagnostic));
}

bool is_absolute_path(const std::string& path)
{
    return !path.empty() && path[0] == '/';
}

std::vector<std::string> split_request_path(const std::string& path)
{
    std::vector<std::string> components;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::size_t end = slash == std::string::npos ? path.size() : slash;
        const std::string component = path.substr(start, end - start);
        if (!component.empty())
            components.push_back(component);
        if (slash == std::string::npos)
            break;
        start = slash + 1;
    }
    return components;
}

bool has_traversal_component(const std::vector<std::string>& components)
{
    for (const std::string& component : components) {
        if (component == "." || component == "..")
            return true;
        for (unsigned char ch : component) {
            if (ch < 0x20 || ch == 0x7f)
                return true;
        }
    }
    return false;
}

std::string join_components_under_root(const std::string& root,
    const std::vector<std::string>& components)
{
    std::string path = root;
    for (const std::string& component : components) {
        if (!path.empty() && path.back() != '/')
            path += '/';
        path += component;
    }
    return path;
}

bool canonicalize_existing_path(const std::string& path, std::string* canonical_path)
{
    char resolved[PATH_MAX];
    if (!realpath(path.c_str(), resolved))
        return false;
    *canonical_path = resolved;
    return true;
}

bool is_same_or_child_path(const std::string& root, const std::string& path)
{
    if (path == root)
        return true;
    if (path.size() <= root.size())
        return false;
    if (path.compare(0, root.size(), root) != 0)
        return false;
    return root.back() == '/' || path[root.size()] == '/';
}

bool lstat_path(const std::string& path, struct stat* st)
{
    return lstat(path.c_str(), st) == 0;
}

bool make_directory_if_missing(const std::string& path)
{
    if (mkdir(path.c_str(), 0700) == 0)
        return true;
    if (errno != EEXIST)
        return false;

    struct stat st;
    if (!lstat_path(path, &st))
        return false;
    return S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode);
}

bool create_directories(const std::string& path)
{
    if (path.empty())
        return false;

    std::string current;
    std::size_t start = 0;
    if (path[0] == '/') {
        current = "/";
        start = 1;
    }

    while (start <= path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::size_t end = slash == std::string::npos ? path.size() : slash;
        const std::string component = path.substr(start, end - start);
        if (!component.empty()) {
            if (!current.empty() && current.back() != '/')
                current += '/';
            current += component;
            if (!make_directory_if_missing(current))
                return false;
        }
        if (slash == std::string::npos)
            break;
        start = slash + 1;
    }
    return true;
}

bool request_has_symlink_component(const std::string& root,
    const std::vector<std::string>& components)
{
    std::string current = root;
    for (const std::string& component : components) {
        if (!current.empty() && current.back() != '/')
            current += '/';
        current += component;

        struct stat st;
        if (!lstat_path(current, &st))
            return false;
        if (S_ISLNK(st.st_mode))
            return true;
    }
    return false;
}

std::string errno_name()
{
    return std::strerror(errno);
}

struct FdCloser {
    int fd = -1;
    ~FdCloser()
    {
        if (fd >= 0)
            close(fd);
    }

    FdCloser() = default;
    explicit FdCloser(int value)
        : fd(value)
    {
    }

    FdCloser(const FdCloser&) = delete;
    FdCloser& operator=(const FdCloser&) = delete;

    FdCloser(FdCloser&& other)
        : fd(other.fd)
    {
        other.fd = -1;
    }

    FdCloser& operator=(FdCloser&& other)
    {
        if (this != &other) {
            if (fd >= 0)
                close(fd);
            fd = other.fd;
            other.fd = -1;
        }
        return *this;
    }
};

bool read_regular_file_descriptor(int fd, const JsScriptPackageBundleLoadOptions& options,
    JsScriptPackageReloadResult& result, std::string* contents)
{
    contents->clear();

    struct stat st;
    if (fstat(fd, &st) != 0) {
        add_reload_diagnostic(result, JsScriptPackageReloadStatus::InvalidRequestPath,
            "JavaScript package bundle could not be inspected safely");
        return false;
    }
    if (!S_ISREG(st.st_mode)) {
        add_reload_diagnostic(result, JsScriptPackageReloadStatus::InvalidRequestPath,
            "JavaScript package bundle path is not a regular file");
        return false;
    }
    if (st.st_size < 0 || static_cast<std::size_t>(st.st_size) > options.maximum_file_bytes) {
        add_reload_diagnostic(result, JsScriptPackageReloadStatus::LoadFailed,
            "JavaScript package bundle exceeds maximum size");
        return false;
    }

    contents->resize(static_cast<std::size_t>(st.st_size));
    std::size_t offset = 0;
    while (offset < contents->size()) {
        const ssize_t bytes_read = read(fd, &(*contents)[offset], contents->size() - offset);
        if (bytes_read < 0) {
            add_reload_diagnostic(result, JsScriptPackageReloadStatus::LoadFailed,
                "JavaScript package bundle could not be read safely");
            return false;
        }
        if (bytes_read == 0)
            break;
        offset += static_cast<std::size_t>(bytes_read);
    }
    contents->resize(offset);
    if (contents->size() > options.maximum_file_bytes) {
        contents->clear();
        add_reload_diagnostic(result, JsScriptPackageReloadStatus::LoadFailed,
            "JavaScript package bundle exceeds maximum size");
        return false;
    }
    return true;
}

bool read_request_by_openat(const std::string& canonical_root,
    const std::vector<std::string>& components, const JsScriptPackageBundleLoadOptions& options,
    JsScriptPackageReloadResult& result, std::string* contents)
{
    FdCloser current(
        open(canonical_root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (current.fd < 0) {
        add_reload_diagnostic(result, JsScriptPackageReloadStatus::InvalidRoot,
            "JavaScript package root could not be opened safely");
        return false;
    }

    for (std::size_t index = 0; index + 1 < components.size(); ++index) {
        FdCloser next(openat(current.fd, components[index].c_str(),
            O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (next.fd < 0) {
            add_reload_diagnostic(result, JsScriptPackageReloadStatus::InvalidRequestPath,
                "JavaScript package bundle directory could not be opened safely");
            return false;
        }
        current = std::move(next);
    }

    FdCloser file(openat(current.fd, components.back().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    if (file.fd < 0) {
        add_reload_diagnostic(result, JsScriptPackageReloadStatus::InvalidRequestPath,
            "JavaScript package bundle could not be opened safely");
        return false;
    }
    return read_regular_file_descriptor(file.fd, options, result, contents);
}

bool resolve_request_file(const std::string& canonical_root, const std::string& request_path,
    JsScriptPackageReloadResult& result, std::string* absolute_file_path)
{
    if (canonical_root.empty()) {
        add_reload_diagnostic(result, JsScriptPackageReloadStatus::InvalidRoot,
            "JavaScript package root is not a configured directory");
        return false;
    }
    if (request_path.empty()) {
        add_reload_diagnostic(result, JsScriptPackageReloadStatus::InvalidRequestPath,
            "JavaScript package request path is empty");
        return false;
    }
    if (request_path.size() > MaxReloadDiagnosticBytes) {
        add_reload_diagnostic(result, JsScriptPackageReloadStatus::InvalidRequestPath,
            "JavaScript package request path is too long");
        return false;
    }
    if (is_absolute_path(request_path)) {
        add_reload_diagnostic(result, JsScriptPackageReloadStatus::InvalidRequestPath,
            "JavaScript package request path must be relative");
        return false;
    }

    const std::vector<std::string> components = split_request_path(request_path);
    if (components.empty() || has_traversal_component(components)) {
        add_reload_diagnostic(result, JsScriptPackageReloadStatus::InvalidRequestPath,
            "JavaScript package request path contains traversal");
        return false;
    }

    const std::string candidate_path = join_components_under_root(canonical_root, components);
    struct stat st;
    if (!lstat_path(candidate_path, &st)) {
        add_reload_diagnostic(result, JsScriptPackageReloadStatus::MissingFile,
            "JavaScript package bundle does not exist");
        return false;
    }
    if (request_has_symlink_component(canonical_root, components)) {
        add_reload_diagnostic(result, JsScriptPackageReloadStatus::Symlink,
            "JavaScript package bundle path contains a symlink");
        return false;
    }

    std::string canonical_file;
    if (!canonicalize_existing_path(candidate_path, &canonical_file)) {
        add_reload_diagnostic(result, JsScriptPackageReloadStatus::MissingFile,
            std::string("JavaScript package bundle could not be resolved: ") + errno_name());
        return false;
    }
    if (!is_same_or_child_path(canonical_root, canonical_file)) {
        add_reload_diagnostic(result, JsScriptPackageReloadStatus::OutsideRoot,
            "JavaScript package bundle resolves outside the configured root");
        return false;
    }

    if (!stat(candidate_path.c_str(), &st)) {
        if (S_ISDIR(st.st_mode)) {
            add_reload_diagnostic(result, JsScriptPackageReloadStatus::Directory,
                "JavaScript package bundle path is a directory");
            return false;
        }
        if (!S_ISREG(st.st_mode)) {
            add_reload_diagnostic(result, JsScriptPackageReloadStatus::InvalidRequestPath,
                "JavaScript package bundle path is not a regular file");
            return false;
        }
    } else {
        add_reload_diagnostic(result, JsScriptPackageReloadStatus::MissingFile,
            std::string("JavaScript package bundle could not be inspected: ") + errno_name());
        return false;
    }

    *absolute_file_path = canonical_file;
    return true;
}

} // namespace

JsScriptPackageReloadService::JsScriptPackageReloadService(
    const JsScriptPackageReloadOptions& options)
    : m_options(options)
{
    struct stat st;
    if (!lstat_path(m_options.package_root, &st)) {
        if (!m_options.create_package_root || !create_directories(m_options.package_root))
            return;
        if (!lstat_path(m_options.package_root, &st))
            return;
    }
    if (S_ISLNK(st.st_mode))
        return;
    if (!S_ISDIR(st.st_mode))
        return;
    canonicalize_existing_path(m_options.package_root, &m_canonical_root);
}

bool JsScriptPackageReloadService::reload_bundle(const std::string& request_path,
    JsScriptPackageReloadResult* result)
{
    JsScriptPackageReloadResult candidate_result;
    candidate_result.request_path = bounded_single_line(request_path);

    std::string absolute_file_path;
    if (!resolve_request_file(m_canonical_root, request_path, candidate_result, &absolute_file_path)) {
        if (result)
            *result = candidate_result;
        return false;
    }

    std::string contents;
    const std::vector<std::string> components = split_request_path(request_path);
    if (!read_request_by_openat(
            m_canonical_root, components, m_options.load_options, candidate_result, &contents)) {
        if (result)
            *result = candidate_result;
        return false;
    }

    JsScriptPackageBundleLoadResult load_result;
    JsScriptPackageValidationResult validation_result;
    const bool parsed
        = js_script_package_bundle_parse_json(contents, m_options.load_options, &load_result);
    bool replaced = false;
    if (parsed)
        replaced = m_registry.replace_all(
            load_result.packages, m_options.replace_options, &validation_result);
    if (!replaced) {
        candidate_result.load_result = load_result;
        candidate_result.validation_result = validation_result;
        if (!parsed) {
            candidate_result.status = JsScriptPackageReloadStatus::LoadFailed;
            candidate_result.diagnostics = load_result.diagnostics;
        } else {
            candidate_result.status = JsScriptPackageReloadStatus::ValidationFailed;
            candidate_result.diagnostics = validation_result.diagnostics;
        }
        if (result)
            *result = candidate_result;
        return false;
    }

    m_last_successful_request_path = request_path;
    m_last_successful_package_count = m_registry.package_count();

    candidate_result.ok = true;
    candidate_result.status = JsScriptPackageReloadStatus::Success;
    candidate_result.package_count = m_last_successful_package_count;
    candidate_result.load_result = load_result;
    candidate_result.validation_result = validation_result;
    if (result)
        *result = candidate_result;
    return true;
}

const JsScriptPackageRegistry& JsScriptPackageReloadService::registry() const
{
    return m_registry;
}

std::size_t JsScriptPackageReloadService::package_count() const
{
    return m_registry.package_count();
}

bool JsScriptPackageReloadService::empty() const
{
    return m_registry.empty();
}

const std::string& JsScriptPackageReloadService::last_successful_request_path() const
{
    return m_last_successful_request_path;
}

std::size_t JsScriptPackageReloadService::last_successful_package_count() const
{
    return m_last_successful_package_count;
}

const JsScriptPackage* JsScriptPackageReloadService::find_package_by_vnum(int vnum) const
{
    return m_registry.find_package_by_vnum(vnum);
}

const JsScriptPackage* JsScriptPackageReloadService::find_package_by_id(
    const std::string& package_id) const
{
    return m_registry.find_package_by_id(package_id);
}

const JsScriptTriggerBinding* JsScriptPackageReloadService::find_trigger_binding(int package_vnum,
    JsScriptPackageHost host, JsScriptingManifestKind kind, int legacy_value) const
{
    return m_registry.find_trigger_binding(package_vnum, host, kind, legacy_value);
}

const char* js_script_package_reload_status_name(JsScriptPackageReloadStatus status)
{
    switch (status) {
    case JsScriptPackageReloadStatus::Success:
        return "success";
    case JsScriptPackageReloadStatus::InvalidRoot:
        return "invalid-root";
    case JsScriptPackageReloadStatus::InvalidRequestPath:
        return "invalid-request-path";
    case JsScriptPackageReloadStatus::MissingFile:
        return "missing-file";
    case JsScriptPackageReloadStatus::Directory:
        return "directory";
    case JsScriptPackageReloadStatus::Symlink:
        return "symlink";
    case JsScriptPackageReloadStatus::OutsideRoot:
        return "outside-root";
    case JsScriptPackageReloadStatus::LoadFailed:
        return "load-failed";
    case JsScriptPackageReloadStatus::ValidationFailed:
        return "validation-failed";
    }
    return "unknown";
}
