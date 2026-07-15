#include "js_builder_http_transport.h"

#include "json_utils.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace {

std::string lower_ascii(const std::string &value)
{
    std::string lower;
    lower.reserve(value.size());
    for (unsigned char ch : value)
        lower.push_back(static_cast<char>(std::tolower(ch)));
    return lower;
}

std::string trim_copy(const std::string &value)
{
    std::size_t start = 0;
    while (start < value.size()
        && std::isspace(static_cast<unsigned char>(value[start])))
        ++start;
    std::size_t end = value.size();
    while (end > start
        && std::isspace(static_cast<unsigned char>(value[end - 1])))
        --end;
    return value.substr(start, end - start);
}

JsBuilderHttpTransportParseResult parse_error(
    int http_status, const char *reason_code, const char *message)
{
    JsBuilderHttpTransportParseResult result;
    result.ok = false;
    result.http_status = http_status;
    result.reason_code = reason_code;
    result.message = message;
    return result;
}

bool parse_start_line(const std::string &line, JsBuilderHttpIngressRequest *request)
{
    std::istringstream stream(line);
    std::string method;
    std::string path;
    std::string version;
    std::string extra;
    stream >> method >> path >> version;
    if (method.empty() || path.empty() || version.empty() || (stream >> extra))
        return false;
    if (version != "HTTP/1.1" && version != "HTTP/1.0")
        return false;
    if (path.empty() || path[0] != '/' || path.find('?') != std::string::npos)
        return false;
    request->method = method;
    request->path = path;
    return true;
}

bool parse_content_length(const std::string &value, std::size_t *length)
{
    const std::string trimmed = trim_copy(value);
    if (trimmed.empty())
        return false;
    for (unsigned char ch : trimmed) {
        if (!std::isdigit(ch))
            return false;
    }
    char *end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(trimmed.c_str(), &end, 10);
    if (errno == ERANGE || end == nullptr || *end != '\0'
        || parsed > std::numeric_limits<std::size_t>::max())
        return false;
    *length = static_cast<std::size_t>(parsed);
    return true;
}

bool is_valid_header_name(const std::string &name)
{
    if (name.empty())
        return false;
    for (unsigned char ch : name) {
        const bool token_char = std::isalnum(ch) || ch == '!' || ch == '#'
            || ch == '$' || ch == '%' || ch == '&' || ch == '\''
            || ch == '*' || ch == '+' || ch == '-' || ch == '.' || ch == '^'
            || ch == '_' || ch == '`' || ch == '|' || ch == '~';
        if (!token_char)
            return false;
    }
    return true;
}

bool is_valid_header_value(const std::string &value)
{
    for (unsigned char ch : value) {
        if (ch < 0x20 || ch == 0x7f)
            return false;
    }
    return true;
}

const char *status_text(int status)
{
    switch (status) {
    case 200:
        return "OK";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 413:
        return "Payload Too Large";
    case 415:
        return "Unsupported Media Type";
    case 500:
        return "Internal Server Error";
    case 503:
        return "Service Unavailable";
    default:
        return "Builder Response";
    }
}

} // namespace

JsBuilderHttpTransportParseResult js_builder_http_transport_parse_request(
    const std::string &raw_request,
    const JsBuilderHttpTransportOptions &options)
{
    const std::size_t header_end = raw_request.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        if (raw_request.size() > options.maximum_header_bytes)
            return parse_error(413, "builder.http.headers-too-large",
                "Builder request was rejected.");
        return parse_error(400, "builder.http.incomplete-headers",
            "Builder request was rejected.");
    }
    if (header_end > options.maximum_header_bytes)
        return parse_error(413, "builder.http.headers-too-large",
            "Builder request was rejected.");

    JsBuilderHttpTransportParseResult result;
    const std::size_t body_start = header_end + 4;
    const std::size_t actual_body_length = raw_request.size() - body_start;

    const std::string headers = raw_request.substr(0, header_end);
    std::size_t line_start = 0;
    const std::size_t first_line_end = headers.find("\r\n");
    const std::string start_line =
        first_line_end == std::string::npos ? headers : headers.substr(0, first_line_end);
    if (!parse_start_line(start_line, &result.request))
        return parse_error(400, "builder.http.invalid-start-line",
            "Builder request was rejected.");
    line_start = first_line_end == std::string::npos ? headers.size() : first_line_end + 2;

    bool saw_content_length = false;
    std::size_t expected_body_length = 0;
    while (line_start < headers.size()) {
        const std::size_t line_end = headers.find("\r\n", line_start);
        const std::string line = headers.substr(line_start,
            line_end == std::string::npos ? std::string::npos : line_end - line_start);
        if (line.empty())
            return parse_error(400, "builder.http.invalid-header",
                "Builder request was rejected.");
        if (std::isspace(static_cast<unsigned char>(line[0])))
            return parse_error(400, "builder.http.invalid-header",
                "Builder request was rejected.");
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos || colon == 0)
            return parse_error(400, "builder.http.invalid-header",
                "Builder request was rejected.");
        const std::string raw_name = line.substr(0, colon);
        const std::string name = trim_copy(raw_name);
        const std::string value = trim_copy(line.substr(colon + 1));
        if (raw_name != name || !is_valid_header_name(name)
            || !is_valid_header_value(value))
            return parse_error(400, "builder.http.invalid-header",
                "Builder request was rejected.");
        const std::string lower_name = lower_ascii(name);
        if (lower_name == "transfer-encoding")
            return parse_error(400, "builder.http.unsupported-transfer",
                "Builder request was rejected.");
        if (lower_name == "content-length") {
            if (saw_content_length)
                return parse_error(400, "builder.http.duplicate-content-length",
                    "Builder request was rejected.");
            saw_content_length = true;
            if (!parse_content_length(value, &expected_body_length))
                return parse_error(400, "builder.http.invalid-content-length",
                    "Builder request was rejected.");
        }
        result.request.headers.push_back({ name, value });
        if (line_end == std::string::npos)
            break;
        line_start = line_end + 2;
    }

    if (saw_content_length) {
        if (expected_body_length > options.maximum_body_bytes)
            return parse_error(413, "builder.http.body-too-large",
                "Builder request was rejected.");
        if (actual_body_length != expected_body_length)
            return parse_error(400, "builder.http.body-length-mismatch",
                "Builder request was rejected.");
    } else if (actual_body_length != 0) {
        return parse_error(400, "builder.http.missing-content-length",
            "Builder request was rejected.");
    }

    result.request.body = raw_request.substr(body_start, actual_body_length);
    result.ok = true;
    result.http_status = 200;
    result.reason_code = "builder.http.accepted";
    return result;
}

std::string js_builder_http_transport_render_response(
    const JsBuilderHttpIngressResult &result)
{
    std::string body = result.json;
    if (body.empty()) {
        body = "{\"ok\":false,\"reasonCode\":\"builder.http.empty-response\","
               "\"message\":\"Builder request was rejected.\"}";
    }
    std::string response = "HTTP/1.1 ";
    response += std::to_string(result.http_status);
    response += " ";
    response += status_text(result.http_status);
    response += "\r\ncontent-type: application/json\r\ncontent-length: ";
    response += std::to_string(body.size());
    response += "\r\nconnection: close\r\n\r\n";
    response += body;
    return response;
}
