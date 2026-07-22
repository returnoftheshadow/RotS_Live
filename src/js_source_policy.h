#ifndef JS_SOURCE_POLICY_H
#define JS_SOURCE_POLICY_H

#include <string>
#include <vector>

struct JsSourcePolicyViolation {
    std::string message;
    std::string token;
};

std::vector<JsSourcePolicyViolation> js_source_policy_validate(const std::string& source);

#endif
