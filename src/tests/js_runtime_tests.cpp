#include "../js_runtime.h"

#include <gtest/gtest.h>

namespace {

void expect_allows(JsRuntime &runtime, const char *source) {
    JsRuntimeEvalResult result = runtime.evaluate(source);
    EXPECT_EQ(result.status, JsRuntimeStatus::Ok) << result.diagnostic;
    EXPECT_EQ(result.value, JsRuntimeValue::Allow) << source;
}

} // namespace

TEST(JsRuntime, AllowsTruthyAndUndefinedResults) {
    JsRuntime runtime;

    JsRuntimeEvalResult true_result = runtime.evaluate("true");
    EXPECT_EQ(true_result.status, JsRuntimeStatus::Ok);
    EXPECT_EQ(true_result.value, JsRuntimeValue::Allow);

    JsRuntimeEvalResult undefined_result = runtime.evaluate("let value = 42;");
    EXPECT_EQ(undefined_result.status, JsRuntimeStatus::Ok);
    EXPECT_EQ(undefined_result.value, JsRuntimeValue::Allow);
}

TEST(JsRuntime, BlocksFalseResult) {
    JsRuntime runtime;

    JsRuntimeEvalResult result = runtime.evaluate("false");

    EXPECT_EQ(result.status, JsRuntimeStatus::Ok);
    EXPECT_EQ(result.value, JsRuntimeValue::Block);
}

TEST(JsRuntime, ReportsSyntaxAndRuntimeErrorsWithoutMultilineDiagnostics) {
    JsRuntime runtime;

    JsRuntimeEvalResult syntax_result = runtime.evaluate("function {");
    EXPECT_EQ(syntax_result.status, JsRuntimeStatus::Error);
    EXPECT_FALSE(syntax_result.diagnostic.empty());
    EXPECT_EQ(syntax_result.diagnostic.find('\n'), std::string::npos);
    EXPECT_EQ(syntax_result.diagnostic.find('\r'), std::string::npos);

    JsRuntimeEvalResult throw_result =
        runtime.evaluate("throw new Error('builder failure\\r\\n' + 'x'.repeat(300));");
    EXPECT_EQ(throw_result.status, JsRuntimeStatus::Error);
    EXPECT_NE(throw_result.diagnostic.find("builder failure"), std::string::npos);
    EXPECT_EQ(throw_result.diagnostic.find('\n'), std::string::npos);
    EXPECT_EQ(throw_result.diagnostic.find('\r'), std::string::npos);
    EXPECT_LE(throw_result.diagnostic.size(), 240);
}

TEST(JsRuntime, InterruptsInfiniteLoops) {
    JsRuntimeLimits limits;
    limits.instruction_budget = 64;
    JsRuntime runtime(limits);

    JsRuntimeEvalResult result = runtime.evaluate("while (true) {}");

    EXPECT_EQ(result.status, JsRuntimeStatus::Interrupted);
}

TEST(JsRuntime, ResetsInstructionBudgetForEachEvaluation) {
    JsRuntimeLimits limits;
    limits.instruction_budget = 64;
    JsRuntime runtime(limits);

    EXPECT_EQ(runtime.evaluate("while (true) {}").status, JsRuntimeStatus::Interrupted);
    expect_allows(runtime, "let total = 0; for (let i = 0; i < 1000; ++i) total += i; true;");
}

TEST(JsRuntime, EnforcesMemoryLimit) {
    JsRuntimeLimits limits;
    limits.memory_limit_bytes = 64 * 1024;
    JsRuntime runtime(limits);

    JsRuntimeEvalResult result = runtime.evaluate(
        "let values = []; for (let i = 0; i < 100000; ++i) values.push('xxxxxxxxxxxxxxxx'); true;");

    EXPECT_NE(result.status, JsRuntimeStatus::Ok);
}

TEST(JsRuntime, DoesNotExposeHostOsOrModuleGlobals) {
    JsRuntime runtime;

    expect_allows(runtime, "typeof std === 'undefined'");
    expect_allows(runtime, "typeof os === 'undefined'");
    expect_allows(runtime, "typeof process === 'undefined'");
    expect_allows(runtime, "typeof require === 'undefined'");

    JsRuntimeEvalResult import_result = runtime.evaluate("import('fs')");
    EXPECT_EQ(import_result.status, JsRuntimeStatus::Error);

    JsRuntimeEvalResult static_import_result = runtime.evaluate("import value from 'fs';");
    EXPECT_EQ(static_import_result.status, JsRuntimeStatus::Error);
}

TEST(JsRuntime, DoesNotPersistMutableStateAcrossEvaluations) {
    JsRuntime runtime;

    JsRuntimeEvalResult mutation_result = runtime.evaluate(
        "globalThis.process = { env: {} }; Object.prototype.polluted = true; let value = 1; true;");
    EXPECT_EQ(mutation_result.status, JsRuntimeStatus::Ok) << mutation_result.diagnostic;

    expect_allows(runtime, "typeof process === 'undefined'");
    expect_allows(runtime, "({}).polluted === undefined");
    expect_allows(runtime, "let value = 2; value === 2");
}

TEST(JsRuntime, RejectsRuntimeCompilationAndPromiseResults) {
    JsRuntime runtime;

    JsRuntimeEvalResult eval_result = runtime.evaluate("typeof eval === 'undefined'");
    EXPECT_EQ(eval_result.status, JsRuntimeStatus::Ok) << eval_result.diagnostic;
    EXPECT_EQ(eval_result.value, JsRuntimeValue::Allow);

    JsRuntimeEvalResult function_result = runtime.evaluate("typeof Function === 'undefined'");
    EXPECT_EQ(function_result.status, JsRuntimeStatus::Ok) << function_result.diagnostic;
    EXPECT_EQ(function_result.value, JsRuntimeValue::Allow);

    JsRuntimeEvalResult promise_result = runtime.evaluate("Promise.resolve(false)");
    EXPECT_EQ(promise_result.status, JsRuntimeStatus::Error);
    EXPECT_NE(promise_result.diagnostic.find("promise"), std::string::npos);

    JsRuntimeEvalResult async_result =
        runtime.evaluate("async function run() { return false; } run();");
    EXPECT_EQ(async_result.status, JsRuntimeStatus::Error);
    EXPECT_NE(async_result.diagnostic.find("promise"), std::string::npos);
}

TEST(JsRuntime, RemainsUsableAfterFailedEvaluations) {
    JsRuntimeLimits limits;
    limits.instruction_budget = 64;
    JsRuntime runtime(limits);

    EXPECT_EQ(runtime.evaluate("function {").status, JsRuntimeStatus::Error);
    expect_allows(runtime, "true");

    EXPECT_EQ(runtime.evaluate("throw new Error('builder failure');").status,
              JsRuntimeStatus::Error);
    expect_allows(runtime, "true");

    EXPECT_EQ(runtime.evaluate("while (true) {}").status, JsRuntimeStatus::Interrupted);
    expect_allows(runtime, "true");
}

TEST(JsRuntime, ExposesStablePublicStringNames) {
    EXPECT_STREQ(js_runtime_status_name(JsRuntimeStatus::Ok), "ok");
    EXPECT_STREQ(js_runtime_status_name(JsRuntimeStatus::Error), "error");
    EXPECT_STREQ(js_runtime_status_name(JsRuntimeStatus::Interrupted), "interrupted");
    EXPECT_STREQ(js_runtime_status_name(JsRuntimeStatus::OutOfMemory), "out-of-memory");
    EXPECT_STREQ(js_runtime_status_name(static_cast<JsRuntimeStatus>(999)), "unknown");
    EXPECT_STREQ(js_runtime_value_name(JsRuntimeValue::Allow), "allow");
    EXPECT_STREQ(js_runtime_value_name(JsRuntimeValue::Block), "block");
    EXPECT_STREQ(js_runtime_value_name(static_cast<JsRuntimeValue>(999)), "unknown");
}
