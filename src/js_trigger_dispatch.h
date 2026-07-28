#ifndef JS_TRIGGER_DISPATCH_H
#define JS_TRIGGER_DISPATCH_H

#include "js_game_adapter.h"
#include "js_live_registry_reload_service.h"
#include "js_runtime.h"
#include "js_script_package.h"
#include "js_script_registry.h"
#include "js_scripting_manifest.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

enum class JsTriggerDispatchStatus {
    NoMatch,
    Allow,
    Block,
    Error,
    BudgetExceeded,
    DepthExceeded,
};

struct JsTriggerDispatchBudgetLimits {
    std::size_t max_invocations_per_pulse = 0;
    std::size_t max_invocations_per_package_per_pulse = 0;
};

class JsTriggerDispatchBudget {
  public:
    bool try_consume(int pulse, int package_vnum, const JsTriggerDispatchBudgetLimits &limits);
    void reset();

  private:
    int m_current_pulse = 0;
    bool m_has_current_pulse = false;
    std::size_t m_pulse_invocations = 0;
    std::unordered_map<int, std::size_t> m_package_invocations;
};

struct JsTriggerDispatchDepthLimits {
    std::size_t max_dispatch_depth = 0;
};

struct JsTriggerMutationAuthorityContext {
    bool allow_persistent_setter_mutations = false;
    std::string builder_account_id;
    int eligible_character_id = 0;
    int target_zone = -1;
    bool allow_room_flag_admin_override = false;
    // Per-dispatch helper target token validation material. Keep server-owned and never expose it
    // through scripts, HTTP responses, BuilderClient artifacts, audit summaries, or diagnostics.
    std::string target_token_secret;
    std::string room_flag_admin_override_evidence;
    std::string decision_evidence;
};

class JsTriggerDispatchDepthGuard {
  public:
    bool would_exceed(const JsTriggerDispatchDepthLimits &limits) const;
    bool try_enter(const JsTriggerDispatchDepthLimits &limits);
    void leave();
    void reset();
    std::size_t current_depth() const;

  private:
    std::size_t m_current_depth = 0;
};

enum class JsTriggerHelperMutationTransactionStatus {
    NotEvaluated,
    Ok,
    UnsupportedEnvelope,
    UnknownOperation,
    InvalidTarget,
    InvalidArguments,
    AuthorityRejected,
    AuditRejected,
    ApplyRejected,
};

enum class JsTriggerCommandResultCode {
    Ok,
    InvalidTarget,
    NotCarried,
    NoDrop,
    InventoryFull,
    TooHeavy,
    NotFound,
    AlreadyWaiting,
    NoRecipient,
    BlockedRoom,
    NoTeleport,
};

struct JsTriggerHelperMutationOperationRegistry {
    const char *const *operation_names = nullptr;
    std::size_t operation_count = 0;
};

struct JsTriggerHelperMutationAuditRequest {
    std::size_t mutation_count = 0;
    std::string operations_summary;
    bool requires_room_flag_admin_override = false;
    std::string room_flag_admin_override_evidence;
    std::string room_flag_authority_summary;
    std::string room_flag_audit_summary;
};

struct JsTriggerCommandMutationAuditRequest {
    std::size_t mutation_count = 0;
    std::string operations_summary;
    JsScriptPackageHost host = JsScriptPackageHost::Character;
    JsScriptingManifestKind kind = JsScriptingManifestKind::LegacyScriptTrigger;
    int legacy_value = 0;
    int package_vnum = 0;
    std::string package_id;
    std::string handler_name;
    int authority_target_zone = -1;
};

struct JsTriggerDispatchRequest;

struct JsTriggerHelperMutationValidationContext {
    const JsTriggerDispatchRequest *request = nullptr;
    const JsGameAdapterOptions *adapter_options = nullptr;
    const JsTriggerMutationAuthorityContext *authority = nullptr;
};

using JsTriggerHelperMutationAuditCallback = bool (*)(
    const JsTriggerHelperMutationAuditRequest &request, std::string *diagnostic, void *user_data);
using JsTriggerCommandMutationAuditCallback = bool (*)(
    const JsTriggerCommandMutationAuditRequest &request, std::string *diagnostic, void *user_data);
using JsTriggerHelperMutationApplyPreconditionCallback = bool (*)(std::size_t mutation_index,
                                                                  void *user_data);

struct JsTriggerHelperMutationTransactionOptions {
    JsTriggerHelperMutationOperationRegistry registry;
    const JsTriggerHelperMutationValidationContext *validation_context = nullptr;
    JsTriggerHelperMutationAuditCallback audit_callback = nullptr;
    void *audit_user_data = nullptr;
    JsTriggerCommandMutationAuditCallback command_audit_callback = nullptr;
    void *command_audit_user_data = nullptr;
    JsTriggerHelperMutationApplyPreconditionCallback apply_precondition_callback = nullptr;
    void *apply_precondition_user_data = nullptr;
};

struct JsTriggerHelperMutationTransactionResult {
    JsTriggerHelperMutationTransactionStatus status = JsTriggerHelperMutationTransactionStatus::Ok;
    std::string diagnostic;
    std::size_t mutation_count = 0;
};

struct JsTriggerRuntimeMutationTransactionProbeResult {
    bool ok = false;
    std::string diagnostic;
    std::size_t prepared_setter_count = 0;
    std::size_t prepared_helper_count = 0;
    JsTriggerHelperMutationTransactionStatus helper_status =
        JsTriggerHelperMutationTransactionStatus::NotEvaluated;
};

struct JsTriggerRuntimeMutationTransactionApplyResult {
    bool ok = false;
    std::string diagnostic;
    std::size_t applied_setter_count = 0;
    std::size_t applied_helper_count = 0;
    JsTriggerHelperMutationTransactionStatus helper_status =
        JsTriggerHelperMutationTransactionStatus::NotEvaluated;
};

struct JsTriggerDispatchOptions {
    JsRuntimeLimits runtime_limits;
    JsTriggerDispatchBudget *budget = nullptr;
    JsTriggerDispatchBudgetLimits budget_limits;
    JsTriggerDispatchDepthGuard *depth_guard = nullptr;
    JsTriggerDispatchDepthLimits depth_limits;
    int current_pulse = 0;
    JsTriggerMutationAuthorityContext mutation_authority;
    JsTriggerHelperMutationTransactionOptions helper_mutation_options;
};

struct JsTriggerDispatchRequest {
    JsScriptPackageHost host = JsScriptPackageHost::Character;
    JsScriptingManifestKind kind = JsScriptingManifestKind::LegacyScriptTrigger;
    int legacy_value = 0;
    int package_vnum = 0;
    std::string package_id;
    std::string handler_name;
    JsGameAdapterContextInput context_input;
};

struct JsTriggerDispatchResult {
    JsTriggerDispatchStatus status = JsTriggerDispatchStatus::NoMatch;
    JsRuntimeStatus runtime_status = JsRuntimeStatus::Ok;
    int package_vnum = 0;
    std::string package_id;
    std::string handler_name;
    std::string diagnostic;
    std::size_t matched_package_count = 0;
};

const char *js_trigger_dispatch_status_name(JsTriggerDispatchStatus status);
const char *js_trigger_command_result_code_name(JsTriggerCommandResultCode code);
JsTriggerCommandResultCode js_trigger_classify_do_give_result(const obj_data *object,
                                                              const char_data *giver,
                                                              const char_data *recipient);
const char *
js_trigger_helper_mutation_transaction_status_name(JsTriggerHelperMutationTransactionStatus status);
bool js_trigger_dispatch_supports_runtime_mutation(const JsRuntimeMutation &mutation);
JsTriggerHelperMutationTransactionResult js_trigger_dispatch_prepare_helper_mutation_transaction(
    const std::vector<JsRuntimeMutation> &mutations,
    const JsTriggerHelperMutationTransactionOptions &options = {});
JsTriggerHelperMutationOperationRegistry js_trigger_dispatch_room_flag_helper_operation_registry();

// Non-mutating transaction verifier for C++ tooling/tests. Do not expose this result through
// builder scripts, HTTP publish responses, or script-visible diagnostics.
JsTriggerRuntimeMutationTransactionProbeResult
js_trigger_dispatch_probe_runtime_mutation_transaction(
    const std::vector<JsRuntimeMutation> &mutations, const JsTriggerDispatchRequest &request,
    const JsGameAdapterOptions &adapter_options, const JsTriggerMutationAuthorityContext &authority,
    const JsTriggerHelperMutationTransactionOptions &helper_options = {});

// Internal transaction applicator for game dispatch and C++ tooling/tests. Do not expose this
// function or detailed result through builder scripts, HTTP publish responses, or script-visible
// diagnostics.
JsTriggerRuntimeMutationTransactionApplyResult
js_trigger_dispatch_apply_runtime_mutation_transaction(
    const std::vector<JsRuntimeMutation> &mutations, const JsTriggerDispatchRequest &request,
    const JsGameAdapterOptions &adapter_options, const JsTriggerMutationAuthorityContext &authority,
    const JsTriggerHelperMutationTransactionOptions &helper_options = {});

JsTriggerDispatchResult js_trigger_dispatch_first_match(const JsScriptPackageRegistry &registry,
                                                        const JsTriggerDispatchRequest &request,
                                                        const JsGameAdapterOptions &adapter_options,
                                                        const JsRuntimeLimits &limits = {});
JsTriggerDispatchResult js_trigger_dispatch_first_match(const JsScriptPackageRegistry &registry,
                                                        const JsTriggerDispatchRequest &request,
                                                        const JsGameAdapterOptions &adapter_options,
                                                        const JsTriggerDispatchOptions &options);

JsTriggerDispatchResult js_trigger_dispatch_live_first_match(
    const JsLiveRegistryReloadService &service, const JsTriggerDispatchRequest &request,
    const JsGameAdapterOptions &adapter_options, const JsRuntimeLimits &limits = {});
JsTriggerDispatchResult js_trigger_dispatch_live_first_match(
    const JsLiveRegistryReloadService &service, const JsTriggerDispatchRequest &request,
    const JsGameAdapterOptions &adapter_options, const JsTriggerDispatchOptions &options);

#endif
