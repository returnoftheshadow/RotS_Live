#include "../js_publish_audit.h"

#include <cstdio>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

std::string temp_file_path(const std::string &name)
{
    return "build/js_publish_audit_" + name + ".jsonl";
}

std::string read_file(const std::string &path)
{
    std::ifstream input(path.c_str());
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

JsPublishAuditEvent make_event(const std::string &operation = "activate")
{
    JsPublishAuditEvent event;
    event.operation = operation;
    event.audit_id = "audit:append";
    event.request_id = "request:append";
    event.actor_id = "actor:42";
    event.builder_account_id = "account:builder";
    event.package_id = "js:30:character:3001";
    event.package_version_id = "jsv:30:character:3001:1";
    event.staged_digest = "sha256:abc";
    event.expected_previous_live_checksum = "live:old";
    event.current_live_checksum = "live:new";
    event.occurred_at_epoch_seconds = 123456;
    return event;
}

JsPublishAuditAppendOptions make_options(const std::string &path)
{
    JsPublishAuditAppendOptions options;
    options.path = path;
    return options;
}

} // namespace

TEST(JsPublishAudit, AppendsDurableJsonLine)
{
    const std::string path = temp_file_path("append");
    std::remove(path.c_str());

    JsPublishAuditAppendResult result =
        js_publish_audit_append_event(make_event(), make_options(path));

    ASSERT_TRUE(result.ok);
    const std::string contents = read_file(path);
    EXPECT_NE(std::string::npos, contents.find("\"schemaVersion\":1"));
    EXPECT_NE(std::string::npos, contents.find("\"operation\":\"activate\""));
    EXPECT_NE(std::string::npos, contents.find("\"auditId\":\"audit:append\""));
    EXPECT_NE(std::string::npos, contents.find("\"packageId\":\"js:30:character:3001\""));
    EXPECT_EQ('\n', contents.back());
}

TEST(JsPublishAudit, AppendsMultipleEventsWithoutReplacing)
{
    const std::string path = temp_file_path("multi");
    std::remove(path.c_str());

    ASSERT_TRUE(js_publish_audit_append_event(make_event("activate"), make_options(path)).ok);
    JsPublishAuditEvent rollback = make_event("rollback");
    rollback.audit_id = "audit:rollback";
    ASSERT_TRUE(js_publish_audit_append_event(rollback, make_options(path)).ok);

    const std::string contents = read_file(path);
    EXPECT_NE(std::string::npos, contents.find("\"operation\":\"activate\""));
    EXPECT_NE(std::string::npos, contents.find("\"operation\":\"rollback\""));
    EXPECT_NE(std::string::npos, contents.find("\"auditId\":\"audit:rollback\""));
    EXPECT_NE(contents.find("\"operation\":\"activate\""),
              contents.find("\"operation\":\"rollback\""));
}

TEST(JsPublishAudit, RejectsUnsafePathsWithoutWriting)
{
    JsPublishAuditAppendResult empty =
        js_publish_audit_append_event(make_event(), make_options(""));
    JsPublishAuditAppendResult absolute =
        js_publish_audit_append_event(make_event(), make_options("/tmp/rots-audit.jsonl"));
    JsPublishAuditAppendResult traversal =
        js_publish_audit_append_event(make_event(), make_options("build/../audit.jsonl"));

    EXPECT_FALSE(empty.ok);
    EXPECT_FALSE(absolute.ok);
    EXPECT_FALSE(traversal.ok);
}

TEST(JsPublishAudit, RejectsInvalidEventMetadata)
{
    const std::string path = temp_file_path("invalid-event");
    std::remove(path.c_str());
    JsPublishAuditEvent event = make_event();
    event.audit_id.clear();

    JsPublishAuditAppendResult result =
        js_publish_audit_append_event(event, make_options(path));

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(read_file(path).size() > 0);
}

TEST(JsPublishAudit, RejectsMissingDirectoryWithoutCreatingFile)
{
    const std::string path = "build/missing-js-publish-audit-dir/audit.jsonl";
    std::remove(path.c_str());

    JsPublishAuditAppendResult result =
        js_publish_audit_append_event(make_event(), make_options(path));

    EXPECT_FALSE(result.ok);
}

TEST(JsPublishAudit, EscapesStringFieldsWithoutBreakingJsonLine)
{
    const std::string path = temp_file_path("escaped");
    std::remove(path.c_str());
    JsPublishAuditEvent event = make_event();
    event.actor_id = "actor:\"quoted\"\\slash";

    JsPublishAuditAppendResult result =
        js_publish_audit_append_event(event, make_options(path));

    ASSERT_TRUE(result.ok);
    const std::string contents = read_file(path);
    EXPECT_NE(std::string::npos, contents.find("\\\"quoted\\\""));
    EXPECT_NE(std::string::npos, contents.find("\\\\slash"));
    EXPECT_EQ(contents.size() - 1, contents.find('\n'));
}

TEST(JsPublishAudit, RejectsEventAboveConfiguredSizeWithoutAppending)
{
    const std::string path = temp_file_path("oversized");
    std::remove(path.c_str());
    JsPublishAuditAppendOptions options = make_options(path);
    options.maximum_event_bytes = 16;

    JsPublishAuditAppendResult result =
        js_publish_audit_append_event(make_event(), options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(read_file(path).empty());
}

TEST(JsPublishAudit, RejectsSymlinkAuditFile)
{
    const std::string target_path = temp_file_path("symlink-target");
    const std::string link_path = temp_file_path("symlink-link");
    std::remove(target_path.c_str());
    std::remove(link_path.c_str());
    {
        std::ofstream target(target_path.c_str());
        target << "unchanged\n";
    }
    ASSERT_EQ(0, symlink("js_publish_audit_symlink-target.jsonl", link_path.c_str()));

    JsPublishAuditAppendResult result =
        js_publish_audit_append_event(make_event(), make_options(link_path));

    EXPECT_FALSE(result.ok);
    EXPECT_EQ("unchanged\n", read_file(target_path));
    std::remove(link_path.c_str());
}

TEST(JsPublishAudit, RejectsSymlinkAndWorldWritableParents)
{
    const std::string real_parent = "build/js_publish_audit_real_parent";
    const std::string link_parent = "build/js_publish_audit_link_parent";
    const std::string writable_parent = "build/js_publish_audit_world_writable";
    std::remove((writable_parent + "/audit.jsonl").c_str());
    rmdir(real_parent.c_str());
    rmdir(writable_parent.c_str());
    std::remove(link_parent.c_str());
    ASSERT_EQ(0, mkdir(real_parent.c_str(), 0700));
    ASSERT_EQ(0, symlink("js_publish_audit_real_parent", link_parent.c_str()));
    ASSERT_EQ(0, mkdir(writable_parent.c_str(), 0777));
    ASSERT_EQ(0, chmod(writable_parent.c_str(), 0777));

    JsPublishAuditAppendResult symlink_parent = js_publish_audit_append_event(
        make_event(), make_options(link_parent + "/audit.jsonl"));
    JsPublishAuditAppendResult world_writable_parent = js_publish_audit_append_event(
        make_event(), make_options(writable_parent + "/audit.jsonl"));

    EXPECT_FALSE(symlink_parent.ok);
    EXPECT_FALSE(world_writable_parent.ok);
    std::remove(link_parent.c_str());
    chmod(writable_parent.c_str(), 0700);
    rmdir(real_parent.c_str());
    rmdir(writable_parent.c_str());
}
