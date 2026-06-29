#include "save_benchmark.h"

#include "account_management.h"
#include "character_json.h"
#include "db.h"
#include "stopwatch.h"
#include "structs.h"

#include <cstdio>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace savebench {
namespace {

// Time `body` `iterations` times; return {min, avg, max} microseconds (share filled later).
StageTiming time_stage(const std::string& name, int iterations,
                       const std::function<void()>& body) {
    StageTiming t;
    t.name = name;
    long sum = 0;
    long lo = std::numeric_limits<long>::max();
    long hi = 0;
    Stopwatch sw;
    for (int i = 0; i < iterations; ++i) {
        sw.start();
        body();
        sw.stop();
        const long us = static_cast<long>(sw.elapsed<std::chrono::microseconds>().count());
        sum += us;
        if (us < lo) lo = us;
        if (us > hi) hi = us;
    }
    t.min_us = (iterations > 0) ? lo : 0;
    t.max_us = hi;
    t.avg_us = (iterations > 0) ? sum / iterations : 0;
    return t;
}

// Fill each stage's share and the "other" remainder = max(0, end-to-end total - sum(named)).
// The share denominator is stage_sum + other_us so per-stage and other shares always sum to
// ~100%, allowing a reader to reconcile every share% against the printed footer denominator.
void finalize_shares(PipelineReport* r) {
    long stage_sum = 0;
    for (const StageTiming& s : r->stages) stage_sum += s.avg_us;
    r->other.name = "other (validate/mkdir/path/owner/index)";
    // other_us: uninstrumented middle steps — the part of the end-to-end pass NOT covered by
    // named stages. Clamped to zero when per-stage instrumentation overhead causes stage_sum to
    // exceed the combined pass (routine under QEMU emulation).
    long other_us = r->total.avg_us - stage_sum;
    if (other_us < 0) other_us = 0;
    r->other.avg_us = other_us;
    const long denom_us = stage_sum + other_us;
    const double denom = (denom_us > 0) ? static_cast<double>(denom_us) : 1.0;
    for (StageTiming& s : r->stages) s.share = 100.0 * s.avg_us / denom;
    r->other.share = 100.0 * other_us / denom;
    // r->total.avg_us retains the end-to-end single-pass average; format_report prints it
    // in a second footer line alongside the share denominator so both figures and their
    // difference (per-stage instrumentation overhead) are visible.
}

} // namespace

bool profile_save(const char_file_u& chd, const std::string& root,
                  const std::string& account_name, const std::string& character_name,
                  const std::string& scratch_path, int iterations,
                  PipelineReport* out, std::string* error) {
    if (iterations < 1) iterations = 1;
    std::string err;
    account::AccountData account;

    // S2: read + parse account.json.
    out->stages.push_back(time_stage("S2 read_account_file", iterations, [&]() {
        account::read_account_file(root, account_name, &account, &err);
    }));
    // S3: char_file_u -> CharacterData.
    character_json::CharacterData cd;
    out->stages.push_back(time_stage("S3 character_data_from_store", iterations, [&]() {
        cd = character_json::character_data_from_store(chd);
    }));
    // S4: CharacterData -> JSON string.
    std::string json;
    out->stages.push_back(time_stage("S4 serialize_character_to_json", iterations, [&]() {
        json = character_json::serialize_character_to_json(cd);
    }));
    // S5: atomic temp-write + rename to a THROWAWAY path (never a live file).
    out->stages.push_back(time_stage("S5 write_text_file_atomically", iterations, [&]() {
        account::write_text_file_atomically(scratch_path, json, &err);
    }));
    // Total: the whole serialize-to-disk operation, end to end, into the throwaway path.
    out->total = time_stage("TOTAL save", iterations, [&]() {
        account::AccountData a;
        account::read_account_file(root, account_name, &a, &err);
        const character_json::CharacterData c = character_json::character_data_from_store(chd);
        const std::string j = character_json::serialize_character_to_json(c);
        account::write_text_file_atomically(scratch_path, j, &err);
    });
    std::remove(scratch_path.c_str()); // clean up the throwaway
    finalize_shares(out);
    (void)character_name;
    (void)error;
    return true;
}

bool profile_load(const std::string& root, const std::string& account_name,
                  const std::string& character_name, int iterations,
                  bool include_store_to_char, PipelineReport* out, std::string* error) {
    if (iterations < 1) iterations = 1;
    std::string err;
    account::AccountData account;
    const std::string path = account::account_character_player_path(root, account_name, character_name);

    // L1: read + parse account.json.
    out->stages.push_back(time_stage("L1 read_account_file", iterations, [&]() {
        account::read_account_file(root, account_name, &account, &err);
    }));
    // L2: read character.json bytes.
    std::string json;
    out->stages.push_back(time_stage("L2 read_text_file", iterations, [&]() {
        account::read_text_file(path, &json, &err);
    }));
    // L3: JSON -> CharacterData.
    character_json::CharacterData cd;
    out->stages.push_back(time_stage("L3 deserialize_character_from_json", iterations, [&]() {
        cd = character_json::CharacterData {};
        character_json::deserialize_character_from_json(json, &cd, &err);
    }));
    // L4: CharacterData -> char_file_u.
    char_file_u chd {};
    out->stages.push_back(time_stage("L4 apply_character_data_to_store", iterations, [&]() {
        character_json::apply_character_data_to_store(cd, &chd, &err);
    }));
    // L5: char_file_u -> live char (OFFLINE only; store_to_char allocates into the char).
    // Build the scratch char the project's way and reuse one struct across iterations.
    // NOTE: store_to_char unconditionally CREATE()s title/description/profs/name on every
    // call with no prior free, so per-iteration inner allocations leak; delete scratch frees
    // only the struct shell. The leak is bounded and acceptable for a short-lived offline
    // benchmark — matches the existing db_loader_tests idiom.
    if (include_store_to_char) {
        char_data* scratch = new char_data {};
        clear_char(scratch, MOB_VOID);
        out->stages.push_back(time_stage("L5 store_to_char", iterations, [&]() {
            store_to_char(&chd, scratch);
        }));
        delete scratch;
    }
    out->total = time_stage("TOTAL load", iterations, [&]() {
        account::AccountData a;
        account::read_account_file(root, account_name, &a, &err);
        std::string j;
        account::read_text_file(path, &j, &err);
        character_json::CharacterData c {};
        character_json::deserialize_character_from_json(j, &c, &err);
        char_file_u s {};
        character_json::apply_character_data_to_store(c, &s, &err);
    });
    finalize_shares(out);
    (void)error;
    return true;
}

std::string format_report(const std::string& title, const PipelineReport& r) {
    char line[160];
    std::string out = "\n=== " + title + " pipeline (microseconds) ===\n";
    out += "  stage                                    min     avg     max   share%\n";
    for (const StageTiming& s : r.stages) {
        snprintf(line, sizeof(line), "  %-38s %6ld  %6ld  %6ld   %5.1f\n",
                 s.name.c_str(), s.min_us, s.avg_us, s.max_us, s.share);
        out += line;
    }
    snprintf(line, sizeof(line), "  %-38s %6ld  %6ld  %6ld   %5.1f\n",
             r.other.name.c_str(), r.other.min_us, r.other.avg_us, r.other.max_us, r.other.share);
    out += line;
    // Footer 1: share denominator (stage_sum + other_us) — every share% reconciles against
    // this value: share% = stage.avg_us / denom * 100, and all shares together sum to ~100%.
    long stage_sum = 0;
    for (const StageTiming& s : r.stages) stage_sum += s.avg_us;
    const long denom_us = stage_sum + r.other.avg_us;
    const long end_to_end_us = r.total.avg_us;
    snprintf(line, sizeof(line),
             "  sum of stages (+other)   = %ld us  (share denominator; sums to 100%%)\n", denom_us);
    out += line;
    // Footer 2: end-to-end single-pass average. Append an overhead note when per-stage
    // instrumentation causes stage_sum to exceed the combined pass (gap is NOT hidden).
    if (stage_sum > end_to_end_us) {
        snprintf(line, sizeof(line),
                 "  end-to-end single pass   = %ld us  (per-stage timing overhead ~%ld us)\n",
                 end_to_end_us, stage_sum - end_to_end_us);
    } else {
        snprintf(line, sizeof(line),
                 "  end-to-end single pass   = %ld us\n", end_to_end_us);
    }
    out += line;
    return out;
}

} // namespace savebench
