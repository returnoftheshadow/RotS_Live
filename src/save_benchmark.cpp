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

// Fill each stage's share and the "other" remainder = total - sum(named), then totals' share.
void finalize_shares(PipelineReport* r) {
    long named = 0;
    for (const StageTiming& s : r->stages) named += s.avg_us;
    r->other.name = "other (validate/mkdir/path/owner/index)";
    long other_avg = r->total.avg_us - named;
    if (other_avg < 0) other_avg = 0; // the combined loop can measure below the stage sum
    r->other.avg_us = other_avg;
    // Use the measured parts (named stages + remainder) as the share denominator so the
    // per-stage and "other" shares always sum to ~100%, independent of timing noise between
    // the separately-timed stages and the combined end-to-end loop.
    const long measured = named + other_avg;
    const double denom = (measured > 0) ? static_cast<double>(measured) : 1.0;
    for (StageTiming& s : r->stages) s.share = 100.0 * s.avg_us / denom;
    r->other.share = 100.0 * other_avg / denom;
    r->total.share = 100.0;
    r->total.name = "TOTAL";
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
    // Build the scratch char the project's way and reuse one instance across iterations so
    // only the final struct needs releasing; freed once after the loop.
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
    snprintf(line, sizeof(line), "  %-38s %6ld  %6ld  %6ld   %5.1f\n",
             r.total.name.c_str(), r.total.min_us, r.total.avg_us, r.total.max_us, r.total.share);
    out += line;
    return out;
}

} // namespace savebench
