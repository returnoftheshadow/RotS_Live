#include "js_builder_session_endpoint.h"

#include "json_utils.h"

#include <algorithm>
#include <cctype>

namespace {

constexpr std::size_t MaxSessionEndpointDiagnosticBytes = 160;

std::string bounded_single_line(std::string message)
{
    for (char &ch : message) {
        if (ch == '\n' || ch == '\r' || static_cast<unsigned char>(ch) < 0x20)
            ch = ' ';
    }
    if (message.size() > MaxSessionEndpointDiagnosticBytes)
        message.resize(MaxSessionEndpointDiagnosticBytes);
    return message;
}

bool starts_with(const std::string &value, const std::string &prefix)
{
    return value.size() >= prefix.size()
        && std::equal(prefix.begin(), prefix.end(), value.begin());
}

bool is_json_content_type(const std::string &content_type)
{
    std::size_t start = 0;
    while (start < content_type.size()
        && std::isspace(static_cast<unsigned char>(content_type[start])))
        ++start;
    std::size_t end = content_type.size();
    while (end > start
        && std::isspace(static_cast<unsigned char>(content_type[end - 1])))
        --end;
    if (start == end)
        return true;

    const std::string trimmed = content_type.substr(start, end - start);
    std::size_t semicolon = trimmed.find(';');
    const std::string media_type = trimmed.substr(0, semicolon);
    std::string normalized;
    normalized.reserve(media_type.size());
    for (unsigned char ch : media_type)
        normalized.push_back(static_cast<char>(std::tolower(ch)));
    if (normalized != "application/json")
        return false;
    if (semicolon == std::string::npos)
        return true;
    std::size_t position = semicolon + 1;
    while (position < trimmed.size()) {
        std::size_t next = trimmed.find(';', position);
        std::string parameter = trimmed.substr(position,
            next == std::string::npos ? std::string::npos : next - position);
        std::size_t parameter_start = 0;
        while (parameter_start < parameter.size()
            && std::isspace(static_cast<unsigned char>(parameter[parameter_start])))
            ++parameter_start;
        if (parameter_start < parameter.size()) {
            const std::size_t equals = parameter.find('=', parameter_start);
            if (equals == std::string::npos)
                return false;
            bool has_name = false;
            for (std::size_t i = parameter_start; i < equals; ++i) {
                if (!std::isspace(static_cast<unsigned char>(parameter[i]))) {
                    has_name = true;
                    break;
                }
            }
            if (!has_name)
                return false;
        }
        if (next == std::string::npos)
            break;
        position = next + 1;
    }
    return true;
}

bool is_required_json_content_type(const std::string &content_type)
{
    return std::any_of(content_type.begin(), content_type.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }) && is_json_content_type(content_type);
}

bool is_valid_route_path(const std::string &path)
{
    return !path.empty() && path[0] == '/' && path.find('?') == std::string::npos;
}

void append_json_string_field(
    std::string &json, const char *name, const std::string &value)
{
    json += "\"";
    json += name;
    json += "\":\"";
    json += json_utils::escape_json_string(value);
    json += "\"";
}

JsBuilderSessionEndpointResult endpoint_error(
    int http_status, const char *reason_code, const char *message)
{
    JsBuilderSessionEndpointResult result;
    result.ok = false;
    result.http_status = http_status;
    result.reason_code = reason_code;
    result.json = "{\"ok\":false,\"reasonCode\":\"";
    result.json += reason_code;
    result.json += "\",\"message\":\"";
    result.json += json_utils::escape_json_string(message);
    result.json += "\"}";
    return result;
}

std::string login_response_json(const JsBuilderSessionLoginResult &login)
{
    std::string json = "{\"ok\":";
    json += login.ok ? "true" : "false";
    json += ",";
    append_json_string_field(json, "reasonCode", login.reason_code);
    if (login.ok) {
        json += ",";
        append_json_string_field(json, "token", login.session_token);
        json += ",";
        append_json_string_field(json, "accountId", login.account_id);
        json += ",\"expiresAtEpochSeconds\":";
        json += std::to_string(login.expires_at_epoch_seconds);
        json += ",\"immortalCharacterNames\":[";
        for (std::size_t i = 0; i < login.immortal_character_names.size(); ++i) {
            if (i > 0)
                json += ",";
            json += "\"";
            json += json_utils::escape_json_string(login.immortal_character_names[i]);
            json += "\"";
        }
        json += "]";
    } else {
        json += ",\"message\":\"Builder login was rejected.\"";
        if (!login.diagnostics.empty()) {
            json += ",\"diagnostics\":[";
            for (std::size_t i = 0; i < login.diagnostics.size(); ++i) {
                if (i > 0)
                    json += ",";
                json += "\"";
                json += json_utils::escape_json_string(
                    bounded_single_line(login.diagnostics[i]));
                json += "\"";
            }
            json += "]";
        }
    }
    json += "}";
    return json;
}

bool parse_login_body(const std::string &body,
    JsBuilderSessionLoginRequest *login_request, std::string *error_message)
{
    bool saw_account = false;
    bool saw_password = false;
    bool saw_request_id = false;
    json_utils::JsonReader reader(body);
    return reader.parse_root_object(
        [&](const std::string &name, json_utils::JsonReader *nested_reader,
            std::string *nested_error_message) {
            if (name == "account") {
                if (saw_account)
                    return false;
                saw_account = true;
                return nested_reader->parse_string(
                    &login_request->account_identifier, nested_error_message);
            }
            if (name == "password") {
                if (saw_password)
                    return false;
                saw_password = true;
                return nested_reader->parse_string(
                    &login_request->password, nested_error_message);
            }
            if (name == "requestId") {
                if (saw_request_id)
                    return false;
                saw_request_id = true;
                return nested_reader->parse_string(
                    &login_request->request_id, nested_error_message);
            }
            if (nested_error_message)
                *nested_error_message = "unexpected login field";
            return false;
        },
        error_message) && saw_account && saw_password;
}

JsBuilderSessionEndpointResult dispatch_login(
    const JsBuilderSessionEndpointRequest &request,
    const JsBuilderSessionEndpointOptions &options)
{
    if (request.method != "POST")
        return endpoint_error(405, "builder.login.method-not-allowed",
            "Builder login request was rejected.");
    if (!is_required_json_content_type(request.content_type))
        return endpoint_error(415, "builder.login.unsupported-media-type",
            "Builder login request was rejected.");
    if (request.body.empty())
        return endpoint_error(400, "builder.login.invalid-request",
            "Builder login request was rejected.");

    JsBuilderSessionLoginRequest login_request;
    std::string parse_error;
    if (!parse_login_body(request.body, &login_request, &parse_error))
        return endpoint_error(400, "builder.login.invalid-request",
            "Builder login request was rejected.");

    JsBuilderSessionLoginResult login =
        js_builder_session_login(login_request, options.session_options);
    JsBuilderSessionEndpointResult result;
    result.ok = login.ok;
    result.http_status = login.http_status;
    result.reason_code = login.reason_code;
    result.json = login_response_json(login);
    if (login.ok)
        result.login_result = login;
    return result;
}

bool is_safe_bearer_token(const std::string &token)
{
    return !token.empty() && token.size() <= 512
        && std::none_of(token.begin(), token.end(), [](unsigned char ch) {
               return std::iscntrl(ch) || std::isspace(ch);
           });
}

JsBuilderSessionEndpointResult dispatch_logout(
    const JsBuilderSessionEndpointRequest &request,
    const JsBuilderSessionEndpointOptions &options)
{
    if (request.method != "POST")
        return endpoint_error(405, "builder.logout.method-not-allowed",
            "Builder logout request was rejected.");
    if (!request.body.empty())
        return endpoint_error(400, "builder.logout.invalid-request",
            "Builder logout request was rejected.");
    if (!is_safe_bearer_token(request.bearer_token))
        return endpoint_error(401, "builder.logout.missing-session",
            "Builder logout request was rejected.");
    if (!options.logout_handler)
        return endpoint_error(503, "builder.logout.unavailable",
            "Builder logout is not available.");

    std::string logout_error;
    if (!options.logout_handler(request.bearer_token, &logout_error))
        return endpoint_error(401, "builder.logout.rejected",
            "Builder logout request was rejected.");

    JsBuilderSessionEndpointResult result;
    result.ok = true;
    result.http_status = 200;
    result.reason_code = "builder.logout.accepted";
    result.json = "{\"ok\":true,\"reasonCode\":\"builder.logout.accepted\"}";
    return result;
}

} // namespace

JsBuilderSessionEndpointResult js_builder_session_endpoint_dispatch(
    const JsBuilderSessionEndpointRequest &request,
    const JsBuilderSessionEndpointOptions &options)
{
    if (!request.trusted_proxy)
        return endpoint_error(403, "builder.session.untrusted",
            "Builder session request was rejected.");
    if (!is_valid_route_path(options.login_path)
        || !is_valid_route_path(options.logout_path)
        || options.login_path == options.logout_path)
        return endpoint_error(500, "builder.session.invalid-route",
            "Builder session endpoint is misconfigured.");
    if (request.path != options.login_path && request.path != options.logout_path)
        return endpoint_error(404, "builder.session.not-found",
            "Builder session request was rejected.");
    if (request.path.find('?') != std::string::npos)
        return endpoint_error(404, "builder.session.not-found",
            "Builder session request was rejected.");
    if (request.body.size() > options.maximum_request_bytes)
        return endpoint_error(413, "builder.session.request-too-large",
            "Builder session request was rejected.");

    if (request.path == options.login_path)
        return dispatch_login(request, options);
    return dispatch_logout(request, options);
}
