#include "../js_builder_manifest_endpoint.h"

#include "../js_api_contract.h"
#include "../js_scripting_manifest.h"
#include "../json_utils.h"

#include <gtest/gtest.h>

namespace {

JsBuilderManifestEndpointRequest request()
{
    JsBuilderManifestEndpointRequest request;
    request.method = "GET";
    request.path = "/api/builder/js/manifest";
    request.trusted_proxy = true;
    return request;
}

void expect_valid_json_object(const std::string &json)
{
    json_utils::JsonReader reader(json);
    std::string error_message;
    EXPECT_TRUE(reader.parse_root_object(
        [](const std::string &, json_utils::JsonReader *nested_reader,
            std::string *nested_error_message) {
            return nested_reader->skip_value(nested_error_message);
        },
        &error_message))
        << error_message;
}

void expect_contains(const std::string &text, const std::string &needle)
{
    EXPECT_NE(std::string::npos, text.find(needle)) << needle;
}

void expect_error(const JsBuilderManifestEndpointResult &result, int status,
    const std::string &reason_code)
{
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(status, result.http_status);
    EXPECT_EQ(reason_code, result.reason_code);
    expect_valid_json_object(result.json);
    expect_contains(result.json, "\"ok\":false");
    expect_contains(result.json, "\"reasonCode\":\"" + reason_code + "\"");
    EXPECT_EQ(std::string::npos, result.json.find("/api/builder/js/manifest"));
    EXPECT_EQ(std::string::npos, result.json.find("secret request body"));
}

} // namespace

TEST(JsBuilderManifestEndpoint, ReturnsAuthoritativeBuilderManifestForTrustedProxy)
{
    JsBuilderManifestEndpointResult result =
        js_builder_manifest_endpoint_dispatch(request());

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(200, result.http_status);
    EXPECT_EQ("builder.manifest.current", result.reason_code);
    expect_valid_json_object(result.json);
    expect_contains(result.json, "\"exportKind\":\"builderManifest\"");
    expect_contains(result.json, js_scripting_manifest_metadata().manifest_checksum);
    expect_contains(result.json, js_api_contract_metadata().contract_checksum);
    expect_contains(result.json, "\"triggerManifest\":{");
    expect_contains(result.json, "\"apiContract\":{");
}

TEST(JsBuilderManifestEndpoint, CanReturnCompactManifestWithoutDocumentation)
{
    JsBuilderManifestEndpointOptions options;
    options.export_options.include_documentation = false;

    JsBuilderManifestEndpointResult result =
        js_builder_manifest_endpoint_dispatch(request(), options);

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(200, result.http_status);
    EXPECT_EQ(std::string::npos, result.json.find("\"docs\":"));
    EXPECT_EQ(std::string::npos, result.json.find("\"notes\":"));
    EXPECT_EQ(std::string::npos, result.json.find("\"reason\":"));
}

TEST(JsBuilderManifestEndpoint, RejectsUntrustedRequests)
{
    JsBuilderManifestEndpointRequest untrusted = request();
    untrusted.trusted_proxy = false;

    JsBuilderManifestEndpointResult result =
        js_builder_manifest_endpoint_dispatch(untrusted);

    expect_error(result, 403, "builder.manifest.untrusted");
}

TEST(JsBuilderManifestEndpoint, RejectsUnsupportedMethodPathQueryAndBody)
{
    JsBuilderManifestEndpointRequest bad_method = request();
    bad_method.method = "POST";
    expect_error(js_builder_manifest_endpoint_dispatch(bad_method), 405,
        "builder.manifest.method-not-allowed");

    JsBuilderManifestEndpointRequest wrong_path_and_method = request();
    wrong_path_and_method.method = "POST";
    wrong_path_and_method.path = "/api/js-scripts/manifest";
    expect_error(js_builder_manifest_endpoint_dispatch(wrong_path_and_method), 404,
        "builder.manifest.not-found");

    JsBuilderManifestEndpointRequest bad_path = request();
    bad_path.path = "/api/js-scripts/manifest";
    expect_error(js_builder_manifest_endpoint_dispatch(bad_path), 404,
        "builder.manifest.not-found");

    JsBuilderManifestEndpointRequest query = request();
    query.path = "/api/builder/js/manifest?docs=false";
    expect_error(js_builder_manifest_endpoint_dispatch(query), 404,
        "builder.manifest.not-found");

    JsBuilderManifestEndpointRequest body = request();
    body.body = "secret request body";
    expect_error(js_builder_manifest_endpoint_dispatch(body), 400,
        "builder.manifest.invalid-request");
}

TEST(JsBuilderManifestEndpoint, RejectsPathConfusionCases)
{
    const char *paths[] = {
        "http://server/api/builder/js/manifest",
        "/api/builder/js/manifest/",
        "/api/builder/js//manifest",
        "/api/builder/js/manifest%3fdocs=false",
        "/api/builder/js/manifest/extra",
        "/prefix/api/builder/js/manifest",
    };

    for (const char *path : paths) {
        JsBuilderManifestEndpointRequest candidate = request();
        candidate.path = path;
        expect_error(js_builder_manifest_endpoint_dispatch(candidate), 404,
            "builder.manifest.not-found");
    }

}

TEST(JsBuilderManifestEndpoint, RejectsInvalidConfiguredRoutePaths)
{
    const char *route_paths[] = {
        "",
        "manifest",
        "/api/builder/js/manifest?bad=true",
    };

    for (const char *route_path : route_paths) {
        JsBuilderManifestEndpointOptions options;
        options.route_path = route_path;
        JsBuilderManifestEndpointRequest candidate = request();
        candidate.path = route_path;
        expect_error(js_builder_manifest_endpoint_dispatch(candidate, options), 500,
            "builder.manifest.invalid-route");
    }
}

TEST(JsBuilderManifestEndpoint, RejectsUntrustedRequestsBeforeParsingShape)
{
    JsBuilderManifestEndpointRequest bad = request();
    bad.trusted_proxy = false;
    bad.method = "POST";
    bad.path = "/wrong/path";
    bad.body = "secret request body";

    expect_error(js_builder_manifest_endpoint_dispatch(bad), 403,
        "builder.manifest.untrusted");
}

TEST(JsBuilderManifestEndpoint, RejectsOversizedManifestResponse)
{
    JsBuilderManifestEndpointOptions options;
    options.maximum_response_bytes = 1;

    JsBuilderManifestEndpointResult result =
        js_builder_manifest_endpoint_dispatch(request(), options);

    expect_error(result, 413, "builder.manifest.response-too-large");
    EXPECT_EQ(std::string::npos, result.json.find("triggerManifest"));
}

TEST(JsBuilderManifestEndpoint, AllowsExactResponseSizeLimit)
{
    const std::size_t manifest_size =
        js_builder_manifest_endpoint_dispatch(request()).json.size();

    JsBuilderManifestEndpointOptions exact;
    exact.maximum_response_bytes = manifest_size;
    EXPECT_TRUE(js_builder_manifest_endpoint_dispatch(request(), exact).ok);

    JsBuilderManifestEndpointOptions too_small;
    too_small.maximum_response_bytes = manifest_size - 1;
    expect_error(js_builder_manifest_endpoint_dispatch(request(), too_small), 413,
        "builder.manifest.response-too-large");
}
