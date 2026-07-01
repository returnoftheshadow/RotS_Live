#ifndef SAVE_BENCHMARK_H
#define SAVE_BENCHMARK_H

#include <string>
#include <vector>

struct char_file_u;

namespace savebench {

// One pipeline stage's timing over N iterations, plus its share of the operation total.
struct StageTiming {
    // Human-readable label for the stage (e.g. "S2 read_account_file"); printed in the report.
    std::string name;
    // Fastest observed single-iteration time, in microseconds.
    long min_us = 0;
    // Mean per-iteration time (sum / iterations), in microseconds; the basis for share.
    long avg_us = 0;
    // Slowest observed single-iteration time, in microseconds.
    long max_us = 0;
    // Percent of the share denominator (sum of named stages + other) this stage's avg_us
    // represents; all per-stage shares and other.share together sum to ~100%.
    double share = 0.0;
};

// Per-direction breakdown: each named stage, the minor-middle remainder, and the total.
struct PipelineReport {
    // The individually-timed stages, in pipeline order.
    std::vector<StageTiming> stages;
    // total - sum(named stages): validate/mkdir/path/owner-resolve/index overhead.
    StageTiming other;
    // End-to-end operation timing; share is fixed at 100%.
    StageTiming total;
};

// Profile the SAVE pipeline for an already-serialized character (chd). Times S2-S5 plus the
// end-to-end total; writes ONLY to scratch_path (a throwaway). Never touches live files.
// Returns false (and sets *error) if the data-transform/IO stage (S5) fails; an S2
// account-read miss is non-fatal (still timed) and does NOT cause a false return.
// When `compare` is non-null, a SEPARATE A/B report is also populated (cache + serialize v1-vs-v2
// variants), leaving the canonical `out` breakdown untouched; default nullptr skips it.
bool profile_save(const char_file_u& chd, const std::string& root,
                  const std::string& account_name, const std::string& character_name,
                  const std::string& scratch_path, int iterations,
                  PipelineReport* out, std::string* error, PipelineReport* compare = nullptr);

// Profile the LOAD pipeline for an account-owned character. Times L1-L4 (+ L5 store_to_char
// when include_store_to_char is true; offline-only, since it allocates into a scratch char).
// Returns false (and sets *error) if a data-transform/IO stage (L2, L3, or L4) fails; an
// L1 account-read miss is non-fatal (still timed) and does NOT cause a false return.
bool profile_load(const std::string& root, const std::string& account_name,
                  const std::string& character_name, int iterations,
                  bool include_store_to_char, PipelineReport* out, std::string* error,
                  PipelineReport* compare = nullptr);

// Render a report as a fixed-width table (stage | min | avg | max | share%).
std::string format_report(const std::string& title, const PipelineReport& report);

} // namespace savebench

#endif // SAVE_BENCHMARK_H
