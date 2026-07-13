#include "js_staged_package_identity.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace {

const char *k_digest_algorithm = "sha256:v1";

std::uint32_t rotate_right(std::uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

std::string sha256_digest(const std::string &text) {
    static const std::uint32_t k[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
        0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
        0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
        0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
        0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
        0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
        0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
        0xc67178f2u};

    std::vector<unsigned char> message(text.begin(), text.end());
    const std::uint64_t bit_length = static_cast<std::uint64_t>(message.size()) * 8u;
    message.push_back(0x80u);
    while ((message.size() % 64u) != 56u)
        message.push_back(0u);
    for (int shift = 56; shift >= 0; shift -= 8)
        message.push_back(static_cast<unsigned char>((bit_length >> shift) & 0xffu));

    std::array<std::uint32_t, 8> h = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

    for (std::size_t offset = 0; offset < message.size(); offset += 64) {
        std::array<std::uint32_t, 64> w{};
        for (int i = 0; i < 16; ++i) {
            const std::size_t index = offset + static_cast<std::size_t>(i) * 4u;
            w[i] = (static_cast<std::uint32_t>(message[index]) << 24u) |
                   (static_cast<std::uint32_t>(message[index + 1]) << 16u) |
                   (static_cast<std::uint32_t>(message[index + 2]) << 8u) |
                   static_cast<std::uint32_t>(message[index + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 =
                rotate_right(w[i - 15], 7) ^ rotate_right(w[i - 15], 18) ^ (w[i - 15] >> 3u);
            const std::uint32_t s1 =
                rotate_right(w[i - 2], 17) ^ rotate_right(w[i - 2], 19) ^ (w[i - 2] >> 10u);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        std::uint32_t a = h[0];
        std::uint32_t b = h[1];
        std::uint32_t c = h[2];
        std::uint32_t d = h[3];
        std::uint32_t e = h[4];
        std::uint32_t f = h[5];
        std::uint32_t g = h[6];
        std::uint32_t hh = h[7];

        for (int i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
            const std::uint32_t ch = (e & f) ^ ((~e) & g);
            const std::uint32_t temp1 = hh + s1 + ch + k[i] + w[i];
            const std::uint32_t s0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = s0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }

    std::ostringstream out;
    out << "sha256:";
    for (std::uint32_t word : h)
        out << std::hex << std::setfill('0') << std::setw(8) << word;
    return out.str();
}

void append_field(std::ostringstream &out, const char *name, const std::string &value) {
    out << name << ":" << value.size() << ":" << value << "\n";
}

void append_field(std::ostringstream &out, const char *name, int value) {
    out << name << ":i:" << value << "\n";
}

bool is_blank(const std::string &value) {
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    });
}

void add_diagnostic(JsStagedPackageIdentityResult &result,
                    JsStagedPackageIdentityDiagnosticCode code, const std::string &message) {
    result.diagnostics.push_back({code, message});
}

std::string digest_body(const std::string &digest) {
    const std::string::size_type colon = digest.find(':');
    return colon == std::string::npos ? digest : digest.substr(colon + 1);
}

bool binding_less(const JsScriptTriggerBinding &left, const JsScriptTriggerBinding &right) {
    if (left.kind != right.kind)
        return static_cast<int>(left.kind) < static_cast<int>(right.kind);
    if (left.legacy_value != right.legacy_value)
        return left.legacy_value < right.legacy_value;
    return left.handler_name < right.handler_name;
}

std::string canonical_digest_for_package(const JsScriptPackage &package,
                                         const JsStagedPackageIdentityOptions &options) {
    std::ostringstream out;
    append_field(out, "identity_format", options.canonical_format_version);
    append_field(out, "digest_algorithm", k_digest_algorithm);
    append_field(out, "server_instance_id", options.server_instance_id);
    append_field(out, "builder_account_id", options.builder_account_id);
    append_field(out, "zone", options.zone);
    append_field(out, "host", js_script_package_host_name(package.host));
    append_field(out, "vnum", package.vnum);
    append_field(out, "logical_package_id",
                 js_staged_package_logical_package_id(options.zone, package.host, package.vnum));
    append_field(out, "base_live_checksum", options.base_live_checksum);
    append_field(out, "package_format_version", package.package_format_version);
    append_field(out, "manifest_schema_version", package.manifest_schema_version);
    append_field(out, "trigger_catalog_revision", package.trigger_catalog_revision);
    append_field(out, "manifest_checksum", package.manifest_checksum);
    append_field(out, "runtime_name", package.runtime_name);
    append_field(out, "runtime_version", package.runtime_version);
    append_field(out, "generated_typings_version", package.generated_typings_version);
    append_field(out, "compiled_javascript", package.compiled_javascript);
    append_field(out, "trigger_binding_count", static_cast<int>(package.trigger_bindings.size()));

    std::vector<JsScriptTriggerBinding> sorted_bindings = package.trigger_bindings;
    std::sort(sorted_bindings.begin(), sorted_bindings.end(), binding_less);

    for (const JsScriptTriggerBinding &binding : sorted_bindings) {
        append_field(out, "trigger_kind", js_scripting_manifest_kind_name(binding.kind));
        append_field(out, "trigger_legacy_value", binding.legacy_value);
        append_field(out, "trigger_handler", binding.handler_name);
    }

    return sha256_digest(out.str());
}

} // namespace

JsStagedPackageIdentityOptions::JsStagedPackageIdentityOptions() {
    package_validation_options.mode = JsScriptPackageValidationMode::InternalValidationOnly;
}

std::string js_staged_package_logical_package_id(int zone, JsScriptPackageHost host, int vnum) {
    std::ostringstream out;
    out << "js:" << zone << ":" << js_script_package_host_name(host) << ":" << vnum;
    return out.str();
}

std::string js_staged_package_version_id(const JsStagedPackageIdentity &identity) {
    std::ostringstream out;
    out << "jsv:" << identity.zone << ":" << js_script_package_host_name(identity.host) << ":"
        << identity.vnum << ":" << digest_body(identity.canonical_digest);
    return out.str();
}

JsStagedPackageIdentityResult
js_staged_package_identity_build(const JsScriptPackage &package,
                                 const JsStagedPackageIdentityOptions &options) {
    JsStagedPackageIdentityResult result;

    if (options.zone <= 0)
        add_diagnostic(result, JsStagedPackageIdentityDiagnosticCode::InvalidMetadata,
                       "Staged package identity requires a positive zone.");
    if (is_blank(options.builder_account_id))
        add_diagnostic(result, JsStagedPackageIdentityDiagnosticCode::InvalidMetadata,
                       "Staged package identity requires a builder account id.");
    if (is_blank(options.base_live_checksum))
        add_diagnostic(
            result, JsStagedPackageIdentityDiagnosticCode::InvalidMetadata,
            "Staged package identity requires the base live checksum it was staged against.");
    if (is_blank(options.server_instance_id))
        add_diagnostic(result, JsStagedPackageIdentityDiagnosticCode::InvalidMetadata,
                       "Staged package identity requires the server instance id.");
    if (options.canonical_format_version <= 0)
        add_diagnostic(result, JsStagedPackageIdentityDiagnosticCode::InvalidMetadata,
                       "Staged package identity requires a positive canonical format version.");

    result.package_validation =
        js_script_package_validate(package, options.package_validation_options);
    if (!result.package_validation.ok)
        add_diagnostic(result, JsStagedPackageIdentityDiagnosticCode::PackageValidationFailed,
                       "Package validation failed before staged identity creation.");

    if (!result.diagnostics.empty()) {
        result.ok = false;
        return result;
    }

    result.identity.zone = options.zone;
    result.identity.vnum = package.vnum;
    result.identity.host = package.host;
    result.identity.package_id =
        js_staged_package_logical_package_id(options.zone, package.host, package.vnum);
    result.identity.canonical_digest = canonical_digest_for_package(package, options);
    result.identity.digest_algorithm = k_digest_algorithm;
    result.identity.canonical_format_version = options.canonical_format_version;
    result.identity.package_format_version = package.package_format_version;
    result.identity.builder_account_id = options.builder_account_id;
    result.identity.server_instance_id = options.server_instance_id;
    result.identity.base_live_checksum = options.base_live_checksum;
    result.identity.manifest_checksum = package.manifest_checksum;
    result.identity.compiled_javascript_checksum = package.compiled_javascript_checksum;
    result.identity.runtime_name = package.runtime_name;
    result.identity.runtime_version = package.runtime_version;
    result.identity.generated_typings_version = package.generated_typings_version;
    result.identity.package_version_id = js_staged_package_version_id(result.identity);
    result.ok = true;
    return result;
}

JsPublishAuthorityContext
js_publish_authority_context_from_staged_identity(const JsStagedPackageIdentity &identity) {
    JsPublishAuthorityContext context;
    context.has_package_authority = true;
    context.zone = identity.zone;
    context.vnum = identity.vnum;
    context.host = identity.host;
    context.package_id = identity.package_id;
    context.package_owner_builder_account_id = identity.builder_account_id;
    context.staged_record_loaded = true;
    context.package_version_id = identity.package_version_id;
    context.staged_digest = identity.canonical_digest;
    context.manifest_checksum = identity.manifest_checksum;
    return context;
}
