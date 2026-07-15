#include "js_server_identity.h"

#include <cstdlib>

namespace {

std::string configured_identity_value(const char *name, const char *fallback)
{
    const char *value = std::getenv(name);
    if (value)
        return value;
    return fallback;
}

} // namespace

std::string js_server_audience_id()
{
    return configured_identity_value("ROTS_JS_SERVER_AUDIENCE", "server:main");
}

std::string js_workspace_id()
{
    return configured_identity_value("ROTS_JS_WORKSPACE_ID", "workspace:main");
}

std::string js_server_instance_id()
{
    return configured_identity_value("ROTS_JS_SERVER_INSTANCE_ID", "server:main");
}
