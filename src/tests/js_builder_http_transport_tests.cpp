#include "../js_builder_http_transport.h"

#include "../json_utils.h"

#include <gtest/gtest.h>

#include <cstdlib>

namespace {

void expect_parse_error(const JsBuilderHttpTransportParseResult &result, int status,
    const std::string &reason_code)
{
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(status, result.http_status);
    EXPECT_EQ(reason_code, result.reason_code);
}

std::string request_with_header_bytes(std::size_t target_header_bytes)
{
    std::string request = "GET /api/builder/js/manifest HTTP/1.1\r\n";
    const std::string header_prefix = "x-fill: ";
    if (request.size() + header_prefix.size() > target_header_bytes) {
        return request + "\r\n\r\n";
    }
    request += header_prefix;
    request.append(target_header_bytes - request.size(), 'a');
    request += "\r\n\r\n";
    return request;
}

std::size_t rendered_content_length(const std::string &response)
{
    const std::string needle = "\r\ncontent-length: ";
    const std::size_t start = response.find(needle);
    if (start == std::string::npos)
        return std::string::npos;
    const std::size_t value_start = start + needle.size();
    const std::size_t value_end = response.find("\r\n", value_start);
    if (value_end == std::string::npos)
        return std::string::npos;
    return static_cast<std::size_t>(
        std::strtoul(response.substr(value_start, value_end - value_start).c_str(),
            nullptr, 10));
}

} // namespace

TEST(JsBuilderHttpTransport, ParsesForwardedRequestForIngress)
{
    const std::string raw =
        "POST /api/js-scripts/stage HTTP/1.1\r\n"
        "host: 127.0.0.1:4802\r\n"
        "content-type: application/json\r\n"
        "authorization: Bearer session-token\r\n"
        "x-rots-builder-proxy-secret: local-secret\r\n"
        "content-length: 11\r\n"
        "\r\n"
        "{\"ok\":true}";

    JsBuilderHttpTransportParseResult result =
        js_builder_http_transport_parse_request(raw);

    EXPECT_TRUE(result.ok);
    EXPECT_EQ("POST", result.request.method);
    EXPECT_EQ("/api/js-scripts/stage", result.request.path);
    EXPECT_EQ("{\"ok\":true}", result.request.body);
    ASSERT_EQ(5u, result.request.headers.size());
    EXPECT_EQ("content-type", result.request.headers[1].first);
    EXPECT_EQ("application/json", result.request.headers[1].second);
}

TEST(JsBuilderHttpTransport, ParsesBodylessManifestRequest)
{
    JsBuilderHttpTransportParseResult result =
        js_builder_http_transport_parse_request(
            "GET /api/builder/js/manifest HTTP/1.1\r\n"
            "x-rots-builder-proxy-secret: local-secret\r\n"
            "\r\n");

    EXPECT_TRUE(result.ok);
    EXPECT_EQ("GET", result.request.method);
    EXPECT_EQ("/api/builder/js/manifest", result.request.path);
    EXPECT_TRUE(result.request.body.empty());
}

TEST(JsBuilderHttpTransport, RejectsMalformedStartLineAndPath)
{
    expect_parse_error(js_builder_http_transport_parse_request(
                           "POST /api/builder/login\r\n\r\n"),
        400, "builder.http.invalid-start-line");
    expect_parse_error(js_builder_http_transport_parse_request(
                           "POST http://127.0.0.1/api/builder/login HTTP/1.1\r\n\r\n"),
        400, "builder.http.invalid-start-line");
    expect_parse_error(js_builder_http_transport_parse_request(
                           "POST /api/builder/login?debug=true HTTP/1.1\r\n\r\n"),
        400, "builder.http.invalid-start-line");
}

TEST(JsBuilderHttpTransport, RejectsMalformedHeaders)
{
    expect_parse_error(js_builder_http_transport_parse_request(
                           "POST /api/builder/login HTTP/1.1\r\nbad-header\r\n\r\n"),
        400, "builder.http.invalid-header");
    expect_parse_error(js_builder_http_transport_parse_request(
                           "POST /api/builder/login HTTP/1.1\r\n: value\r\n\r\n"),
        400, "builder.http.invalid-header");
    expect_parse_error(js_builder_http_transport_parse_request(
                           "POST /api/builder/login HTTP/1.1\r\n bad: value\r\n\r\n"),
        400, "builder.http.invalid-header");
    expect_parse_error(js_builder_http_transport_parse_request(
                           "POST /api/builder/login HTTP/1.1\r\nbad name: value\r\n\r\n"),
        400, "builder.http.invalid-header");
    expect_parse_error(js_builder_http_transport_parse_request(
                           "POST /api/builder/login HTTP/1.1\r\nbad\tname: value\r\n\r\n"),
        400, "builder.http.invalid-header");
    expect_parse_error(js_builder_http_transport_parse_request(
                           "POST /api/builder/login HTTP/1.1\r\nbad : value\r\n\r\n"),
        400, "builder.http.invalid-header");
    expect_parse_error(js_builder_http_transport_parse_request(
                           std::string("POST /api/builder/login HTTP/1.1\r\nbad: va")
                               + '\x01' + "lue\r\n\r\n"),
        400, "builder.http.invalid-header");
    expect_parse_error(js_builder_http_transport_parse_request(
                           "POST /api/builder/login HTTP/1.1\r\n"
                           "authorization: Bearer token\nsmuggled\r\n\r\n"),
        400, "builder.http.invalid-header");
    expect_parse_error(js_builder_http_transport_parse_request(
                           "POST /api/builder/login HTTP/1.1\r\n"
                           "content-type: application/json\nsmuggled\r\n\r\n"),
        400, "builder.http.invalid-header");
    expect_parse_error(js_builder_http_transport_parse_request(
                           "POST /api/builder/login HTTP/1.1\r\n"
                           "x-rots-builder-proxy-secret: secret\nsmuggled\r\n\r\n"),
        400, "builder.http.invalid-header");
    expect_parse_error(js_builder_http_transport_parse_request(
                           "POST /api/builder/login HTTP/1.1\r\n"
                           "content-type: application/json\r\n charset=utf-8\r\n\r\n"),
        400, "builder.http.invalid-header");
    expect_parse_error(js_builder_http_transport_parse_request(
                           "POST /api/builder/login HTTP/1.1\r\n"
                           "content-type: application/json\r\n\tcharset=utf-8\r\n\r\n"),
        400, "builder.http.invalid-header");
    expect_parse_error(js_builder_http_transport_parse_request(
                           "POST /api/builder/login HTTP/1.1\r\n"
                           "transfer-encoding: chunked\r\n\r\n"),
        400, "builder.http.unsupported-transfer");
}

TEST(JsBuilderHttpTransport, EnforcesContentLengthAndBodyLimits)
{
    expect_parse_error(js_builder_http_transport_parse_request(
                           "POST /api/builder/login HTTP/1.1\r\n"
                           "content-length: 1\r\ncontent-length: 1\r\n\r\nx"),
        400, "builder.http.duplicate-content-length");
    expect_parse_error(js_builder_http_transport_parse_request(
                           "POST /api/builder/login HTTP/1.1\r\n"
                           "content-length: nope\r\n\r\nx"),
        400, "builder.http.invalid-content-length");
    expect_parse_error(js_builder_http_transport_parse_request(
                           "POST /api/builder/login HTTP/1.1\r\n"
                           "content-length: +1\r\n\r\nx"),
        400, "builder.http.invalid-content-length");
    expect_parse_error(js_builder_http_transport_parse_request(
                           "POST /api/builder/login HTTP/1.1\r\n"
                           "content-length: -1\r\n\r\nx"),
        400, "builder.http.invalid-content-length");
    expect_parse_error(js_builder_http_transport_parse_request(
                           "POST /api/builder/login HTTP/1.1\r\n"
                           "content-length:\r\n\r\n"),
        400, "builder.http.invalid-content-length");
    expect_parse_error(js_builder_http_transport_parse_request(
                           "POST /api/builder/login HTTP/1.1\r\n"
                           "content-length: 999999999999999999999999999999\r\n\r\n"),
        400, "builder.http.invalid-content-length");
    expect_parse_error(js_builder_http_transport_parse_request(
                           "POST /api/builder/login HTTP/1.1\r\n"
                           "content-length: 5\r\n\r\nx"),
        400, "builder.http.body-length-mismatch");
    expect_parse_error(js_builder_http_transport_parse_request(
                           "POST /api/builder/login HTTP/1.1\r\n"
                           "content-length: 1\r\n\r\nxy"),
        400, "builder.http.body-length-mismatch");
    expect_parse_error(js_builder_http_transport_parse_request(
                           "POST /api/builder/login HTTP/1.1\r\n"
                           "content-length: 0\r\n\r\nGET /api/builder/js/manifest HTTP/1.1\r\n\r\n"),
        400, "builder.http.body-length-mismatch");
    expect_parse_error(js_builder_http_transport_parse_request(
                           "POST /api/builder/login HTTP/1.1\r\n\r\nx"),
        400, "builder.http.missing-content-length");

    JsBuilderHttpTransportOptions options;
    options.maximum_body_bytes = 1;
    EXPECT_TRUE(js_builder_http_transport_parse_request(
                    "POST /api/builder/login HTTP/1.1\r\n"
                    "content-length: 1\r\n\r\nx",
                    options)
                    .ok);
    expect_parse_error(js_builder_http_transport_parse_request(
                           "POST /api/builder/login HTTP/1.1\r\n"
                           "content-length: 2\r\n\r\nxx",
                           options),
        413, "builder.http.body-too-large");
}

TEST(JsBuilderHttpTransport, EnforcesHeaderLimitAndRequiresTerminator)
{
    JsBuilderHttpTransportOptions options;
    options.maximum_header_bytes = 8;
    expect_parse_error(js_builder_http_transport_parse_request(
                           "POST /api/builder/login HTTP/1.1\r\n\r\n", options),
        413, "builder.http.headers-too-large");
    expect_parse_error(js_builder_http_transport_parse_request(
                           "POST /api/builder/login HTTP/1.1\r\n", options),
        413, "builder.http.headers-too-large");
    expect_parse_error(js_builder_http_transport_parse_request(
                           "GET /api/builder/js/manifest HTTP/1.1\n\n"),
        400, "builder.http.incomplete-headers");

    JsBuilderHttpTransportOptions boundary_options;
    boundary_options.maximum_header_bytes = 80;
    EXPECT_TRUE(js_builder_http_transport_parse_request(
                    request_with_header_bytes(boundary_options.maximum_header_bytes),
                    boundary_options)
                    .ok);
    expect_parse_error(js_builder_http_transport_parse_request(
                           request_with_header_bytes(boundary_options.maximum_header_bytes + 1),
                           boundary_options),
        413, "builder.http.headers-too-large");
    expect_parse_error(js_builder_http_transport_parse_request(
                           "GET /api/builder/js/manifest HTTP/1.1\r\nx-fill: a",
                           boundary_options),
        400, "builder.http.incomplete-headers");
}

TEST(JsBuilderHttpTransport, RendersJsonHttpResponse)
{
    JsBuilderHttpIngressResult ingress;
    ingress.ok = true;
    ingress.http_status = 200;
    ingress.reason_code = "builder.manifest.current";
    ingress.json = "{\"ok\":true}";

    std::string response = js_builder_http_transport_render_response(ingress);

    EXPECT_NE(std::string::npos, response.find("HTTP/1.1 200 OK\r\n"));
    EXPECT_NE(std::string::npos, response.find("content-type: application/json\r\n"));
    EXPECT_NE(std::string::npos, response.find("content-length: 11\r\n"));
    EXPECT_NE(std::string::npos, response.find("connection: close\r\n\r\n"));
    EXPECT_TRUE(response.size() >= ingress.json.size());
    EXPECT_EQ(ingress.json,
        response.substr(response.size() - ingress.json.size()));
    EXPECT_EQ(ingress.json.size(), rendered_content_length(response));

    ingress.json = "{\"ok\":true,\"text\":\"escaped \\u00e9\"}";
    response = js_builder_http_transport_render_response(ingress);
    const std::size_t body_start = response.find("\r\n\r\n");
    ASSERT_NE(std::string::npos, body_start);
    const std::string body = response.substr(body_start + 4);
    EXPECT_EQ(body.size(), rendered_content_length(response));
}

TEST(JsBuilderHttpTransport, RendersFallbackJsonForEmptyBody)
{
    JsBuilderHttpIngressResult ingress;
    ingress.http_status = 500;
    std::string response = js_builder_http_transport_render_response(ingress);

    EXPECT_NE(std::string::npos, response.find("HTTP/1.1 500 Internal Server Error\r\n"));
    EXPECT_NE(std::string::npos, response.find("builder.http.empty-response"));
    const std::size_t body_start = response.find("\r\n\r\n");
    ASSERT_NE(std::string::npos, body_start);
    const std::string body = response.substr(body_start + 4);
    json_utils::JsonReader reader(body);
    std::string error_message;
    EXPECT_TRUE(reader.parse_root_object(
        [](const std::string &, json_utils::JsonReader *nested_reader,
            std::string *nested_error_message) {
            return nested_reader->skip_value(nested_error_message);
        },
        &error_message))
        << error_message;
}
