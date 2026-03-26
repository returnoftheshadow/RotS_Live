# Replace `sprintf` TODO

## Goal
- Replace all runtime `sprintf` usage in the C/C++ codebase with safer alternatives.
- Preserve existing output and formatting behavior unless a call site is already relying on undefined behavior.
- Reduce both overflow risk and overlap/append bugs.

## Current Inventory
- Runtime `sprintf` call sites in `src/`: `1460`
- Comment-only examples in `src/protocol.h`: `2`
- Note: these counts are the original audit baseline, not the live remaining count after in-progress work.

## Progress Log

### Completed Chunks
- Added a bounded `skill_timer::report_skill_status()` API in [src/skill_timer.h](/home/seth/repos/RotS_Live/src/skill_timer.h) and [src/skill_timer.cpp](/home/seth/repos/RotS_Live/src/skill_timer.cpp), replacing the old self-append logic and threading real buffer capacity through the caller in [src/act_info.cpp](/home/seth/repos/RotS_Live/src/act_info.cpp).
- Added focused unit coverage for the new `skill_timer` behavior in [src/tests/skill_timer_tests.cpp](/home/seth/repos/RotS_Live/src/tests/skill_timer_tests.cpp), and wired it into [src/CMakeLists.txt](/home/seth/repos/RotS_Live/src/CMakeLists.txt) and [src/tests/Makefile](/home/seth/repos/RotS_Live/src/tests/Makefile).
- Replaced several overlap and append-at-end builders in [src/boards.cpp](/home/seth/repos/RotS_Live/src/boards.cpp), including board listing output, read/remove display text, and board/mail filename construction.
- Replaced the remaining runtime `sprintf` usage in [src/color.cpp](/home/seth/repos/RotS_Live/src/color.cpp).
- Replaced the remaining runtime `sprintf` usage in [src/graph.cpp](/home/seth/repos/RotS_Live/src/graph.cpp).
- Reworked the touched `spell_divination` string assembly in [src/mystic.cpp](/home/seth/repos/RotS_Live/src/mystic.cpp) to use bounded `snprintf` appends instead of `sprintf`/`strcat` accumulation.
- Added a local bounded append helper in [src/act_info.cpp](/home/seth/repos/RotS_Live/src/act_info.cpp) and migrated multiple high-risk self-append paths there:
  - room title / advanced-view output
  - room affection and room weather output
  - `diag_char_to_char()`
  - `do_affections()`
  - `do_exits()`
  - `do_prof()`
  - socials / commands listings
  - `print_exploits()`
  - item identify/display helpers near the bottom of the file
- Converted additional single-shot formatters in [src/act_info.cpp](/home/seth/repos/RotS_Live/src/act_info.cpp), including object display text, directional look/examine text, door state messages, drink-container descriptions, and `do_read()` / `do_examine()` string assembly.
- Converted more one-shot formatter paths in [src/act_info.cpp](/home/seth/repos/RotS_Live/src/act_info.cpp), including:
  - `report_perception()`
  - `report_mob_age()`
  - `do_toggle()`
  - item comparison output
  - `report_char_mentals()`
  - the basic self-stat output in `do_stat()`
  - `get_level_abbr()`
  - exploit path and exploit-entry formatting
  - several `whois`/player-summary strings
- Converted higher-risk append-builder paths in [src/act_info.cpp](/home/seth/repos/RotS_Live/src/act_info.cpp), including:
  - `add_prompt()`
  - `do_fame_leader_string()`
  - `do_fame()`
  - `do_rank()`
- Finished the remaining runtime `sprintf` migration in [src/act_info.cpp](/home/seth/repos/RotS_Live/src/act_info.cpp), including:
  - the mounted-rider display path in `show_mount_to_char()`
  - room-exit marker generation in `do_look()`
  - the large append-style builders in `do_info()`, `do_score()`, and `do_time()`
  - manual/help chapter output in `do_help()`
  - player listing output in `do_who()` and `do_users()`
  - player/object location output in `perform_mortal_where()` and `perform_immort_where()`
  - search-result text in `do_search()`
- Finished the runtime `sprintf` migration in [src/act_wiz.cpp](/home/seth/repos/RotS_Live/src/act_wiz.cpp), including:
  - wizard command text like `do_emote()`, `do_send()`, `do_echo()`, `do_goto()`, `do_force()`, `do_wiznet()`, `do_wizlock()`, `do_invis()`, `do_dc()`, and shutdown/purge/reset logging
  - room/object/character stat output in `do_stat_room()`, `do_stat_object()`, and `do_stat_character()`
  - zone/show/report helpers like `do_zone()`, `print_zone_to_buf()`, `do_show()`, `do_register()`, `do_findzone()`, and `do_top()`
  - wiz util / wiz set / permission messages in `do_wizutil()`, `do_wizset()`, `advance_perm()`, and related logging
  - follow-up hardening after review so long admin/stat/show builders use size-aware appends consistently, and `print_zone_to_buf()` now takes the real destination size instead of assuming `MAX_STRING_LENGTH`
- Reduced [src/boards.cpp](/home/seth/repos/RotS_Live/src/boards.cpp) to comment-only `sprintf` references; the remaining live behavior in the touched paths now uses bounded formatting.

### Validation Completed
- `cmake --build build --target ageland_tests`
- `./bin/tests --gtest_filter='SkillTimer.*'`
- `ctest --test-dir build --output-on-failure`
- Current result during this migration branch: `177/177` tests passing.

### Reviewer Findings Already Addressed
- Fixed a buffer-size mismatch where `report_skill_status()` was initially called with `MAX_STRING_LENGTH` instead of the real local destination size in `do_affections()`.
- Fixed a test fixture lifetime issue where the `skill_timer` singleton was initially bound to stack-allocated weather data.
- Completed several partial `act_info.cpp` conversions that still mixed bounded appends with raw `strcat`/self-append calls after the first migration pass.
- Fixed `act_wiz.cpp` follow-up issues from review by threading real buffer size into `print_zone_to_buf()` and removing remaining mixed `strcat` tails in the large room/object/character stat and `do_show()` builders.

## Remaining Count For Touched Files
- `src/skill_timer.cpp`: `0`
- `src/color.cpp`: `0`
- `src/graph.cpp`: `0`
- `src/boards.cpp`: `2` comment-only references
- `src/mystic.cpp`: `0`
- `src/act_info.cpp`: `0`
- `src/act_wiz.cpp`: `0`

## Next Recommended Chunks
- Optionally replace or ignore the remaining comment-only `sprintf` references in [src/boards.cpp](/home/seth/repos/RotS_Live/src/boards.cpp).
- Then move on to the next highest-risk overlap-heavy files:
  - `src/db.cpp`
  - `src/shapescript.cpp`
  - `src/shapemob.cpp`

### Regenerate The Exact Inventory
- Line-level inventory:
  - `rg -n "\bsprintf\s*\(" src`
- Per-file counts:
  - `rg --count "\bsprintf\s*\(" src | sort -t: -k2,2nr`

### Per-File Usage Counts
- `src/act_info.cpp`: `309`
- `src/act_wiz.cpp`: `233`
- `src/shapescript.cpp`: `90`
- `src/db.cpp`: `89`
- `src/shapemob.cpp`: `78`
- `src/act_othe.cpp`: `53`
- `src/shapezon.cpp`: `53`
- `src/shaperom.cpp`: `42`
- `src/objsave.cpp`: `34`
- `src/protocol.cpp`: `33`
- `src/shapeobj.cpp`: `32`
- `src/act_obj1.cpp`: `31`
- `src/act_move.cpp`: `28`
- `src/comm.cpp`: `28`
- `src/spec_pro.cpp`: `28`
- `src/shop.cpp`: `26`
- `src/act_comm.cpp`: `24`
- `src/boards.cpp`: `21`
- `src/mudlle.cpp`: `20`
- `src/interpre.cpp`: `17`
- `src/ranger.cpp`: `15`
- `src/act_obj2.cpp`: `14`
- `src/handler.cpp`: `14`
- `src/limits.cpp`: `11`
- `src/mail.cpp`: `11`
- `src/act_offe.cpp`: `10`
- `src/utility.cpp`: `10`
- `src/fight.cpp`: `9`
- `src/profs.cpp`: `8`
- `src/shapemdl.cpp`: `8`
- `src/weapon_master_handler.cpp`: `8`
- `src/ban.cpp`: `7`
- `src/big_brother.cpp`: `7`
- `src/graph.cpp`: `7`
- `src/mage.cpp`: `7`
- `src/olog_hai.cpp`: `7`
- `src/mystic.cpp`: `6`
- `src/script.cpp`: `6`
- `src/clerics.cpp`: `4`
- `src/weather.cpp`: `4`
- `src/char_utils.cpp`: `3`
- `src/color.cpp`: `3`
- `src/act_soci.cpp`: `2`
- `src/mobact.cpp`: `2`
- `src/protocol.h`: `2` comment examples, not runtime code
- `src/skill_timer.cpp`: `2`
- `src/spell_pa.cpp`: `2`
- `src/modify.cpp`: `1`
- `src/zone.cpp`: `1`

## High-Risk Patterns To Tackle First

### 1. Self-Append And Overlapping Source/Destination
- These are already undefined behavior and are the most important to fix first.
- Representative patterns:
  - `sprintf(buf, "%s...", buf, ...)`
  - `sprintf(buf2, "%s...", buf2, ...)`
  - `sprintf(buff, "%s...", buff, ...)`
- Representative files:
  - `src/act_info.cpp`
  - `src/act_wiz.cpp`
  - `src/color.cpp`
  - `src/db.cpp`
  - `src/graph.cpp`
  - `src/mystic.cpp`
  - `src/skill_timer.cpp`
- Compiler warnings already point at some of these in `act_info.cpp`.

### 2. Append-At-End Patterns
- Pattern:
  - `sprintf(buf + strlen(buf), "...", ...)`
- Representative files:
  - `src/act_info.cpp`
  - `src/boards.cpp`
  - `src/handler.cpp`
  - `src/interpre.cpp`
  - `src/mudlle.cpp`
  - `src/shapeobj.cpp`
  - `src/shaperom.cpp`
  - `src/shapezon.cpp`
- These are safer to migrate than self-overlap, but they still need explicit remaining-capacity tracking.

### 3. Pointer-Advancing Builders
- Pattern:
  - `bufpt += sprintf(bufpt, ...)`
- Representative files:
  - `src/act_info.cpp`
- These must be converted carefully because the new code needs to handle truncation and maintain a valid write cursor.

### 4. File Path And Filename Construction
- Pattern:
  - `sprintf(path_buf, "%s/%s", ...)`
  - `sprintf(filename, "...%s...", name)`
- Representative files:
  - `src/db.cpp`
  - `src/boards.cpp`
  - `src/objsave.cpp`
  - `src/shapescript.cpp`
- These often write into fixed-size path buffers and are good candidates for `snprintf`.

### 5. Dynamic Format Strings
- Pattern:
  - `sprintf(buf, format, ...)`
  - `sprintf(str, SHAPE_SCRIPT_DIR, fname)`
- Representative files:
  - `src/ban.cpp`
  - `src/shapescript.cpp`
  - shop/protocol style message templates where the format string comes from data or a shared constant
- These need a format-string audit, not just a mechanical replacement.

### 6. User/Content-Facing Message Assembly Into Global Buffers
- Representative files:
  - `src/act_info.cpp`
  - `src/act_comm.cpp`
  - `src/act_othe.cpp`
  - `src/act_wiz.cpp`
  - `src/shop.cpp`
- A lot of these write into global shared buffers like `buf`, `buf1`, `buf2`, `buf3`, etc. The destination size is often implicit and easy to get wrong.

## Recommended Replacement Strategy

### Phase 1. Create A Small Formatting Policy
- Decide the default replacement by use case:
  - fixed-size `char[]`: `snprintf`
  - appending into a fixed buffer: helper like `append_snprintf(dest, size, fmt, ...)`
  - complex message construction: `std::string` plus `fmt`-style append helper if allowed, or repeated `snprintf` with tracked offsets
- Document the approved patterns before touching hundreds of sites.

### Phase 2. Add Shared Helpers
- Add one or two small helpers to reduce repetition:
  - `append_snprintf(char* dest, size_t size, const char* fmt, ...)`
  - optional `format_to_buffer(...)` helper that returns bytes written / truncation state
- Make helpers explicit about:
  - total capacity
  - current write offset
  - truncation handling

### Phase 3. Fix Undefined-Behavior Sites First
- Prioritize files with overlap/self-append:
  - `src/act_info.cpp`
  - `src/act_wiz.cpp`
  - `src/color.cpp`
  - `src/db.cpp`
  - `src/graph.cpp`
  - `src/mystic.cpp`
  - `src/skill_timer.cpp`
- These are the highest value because some are already warned about by the compiler.

### Phase 4. Migrate Append Builders
- Replace:
  - `sprintf(buf + strlen(buf), ...)`
  - `ptr += sprintf(ptr, ...)`
- Standardize on offset-based appends with capacity checks.

### Phase 5. Migrate Path/Filesystem Builders
- Replace path and filename composition next.
- These tend to be lower-behavior-risk and easy to verify.

### Phase 6. Migrate Message-Heavy Gameplay Files
- Biggest remaining hotspots:
  - `src/act_info.cpp`
  - `src/act_wiz.cpp`
  - `src/shapescript.cpp`
  - `src/db.cpp`
  - `src/shapemob.cpp`
  - `src/act_othe.cpp`
  - `src/shapezon.cpp`
  - `src/shaperom.cpp`
- Expect these to be the longest part of the work, especially where messages are built incrementally.

### Phase 7. Audit Dynamic Formats
- Review every call where the format string is not a literal.
- Confirm whether the format string is:
  - compile-time constant
  - trusted internal template
  - loaded from data or influenced by user content
- Replace only after that trust analysis is done.

## Pitfalls To Watch Out For

### Buffer Size Ambiguity
- Many destinations are global buffers declared elsewhere.
- We cannot safely replace `sprintf` without knowing the actual destination capacity at each call site.

### Silent Truncation
- `snprintf` prevents overflow, but it can still truncate.
- We need a rule for whether truncation is acceptable, logged, or treated as a bug.

### Overlap Semantics Change
- Some current code relies on undefined overlap behavior that “happens to work.”
- Replacing it may slightly change exact text layout if we do not preserve append order carefully.

### Return Value Differences
- `sprintf` and `snprintf` return counts, but `snprintf` returns the number of bytes that would have been written.
- Pointer-advance logic must handle truncation correctly or it will miscompute offsets.

### Shared Global Buffers
- `buf`, `buf1`, `buf2`, `buf3`, `str`, `tmpstr`, etc. are reused heavily.
- Some replacements will be easier if we first reduce reliance on shared globals in the hottest files.

### Dynamic Format Strings
- Replacing `sprintf` with `snprintf` is not enough if the format string itself is unsafe or data-driven.

### Embedded Newlines And MUD Formatting Codes
- A lot of strings include `\n\r`, color codes, and MUD token syntax.
- The migration must preserve exact visible output where gameplay or admin workflows depend on it.

### Path And Filename Buffers
- These may be smaller than the path inputs now allow.
- Some replacements should include explicit overflow handling instead of silently clipping paths.

### Macro And Struct Field Destinations
- Some destinations are struct members or macro expansions whose size is not obvious at the call site.
- We need to inspect the actual field definition before choosing a replacement.

## Verification Work Needed
- Build with warnings enabled and watch for:
  - `-Wformat-overflow`
  - `-Wrestrict`
  - write-strings and related format warnings that expose nearby string issues
- Add or expand tests around:
  - prompt building
  - room/character/object display formatting
  - file path generation
  - board and save/load filename generation
- For risky files, do targeted smoke testing in-game:
  - `look`, `score`, prompts, aliases, wiz commands, board views, rent/save flows, shop output

## Suggested Execution Order
1. Introduce formatting helpers and document the policy.
2. Fix overlap/self-append sites.
3. Fix append-at-end and pointer-advance builders.
4. Fix path/filename builders.
5. Sweep the large gameplay/admin files.
6. Do a final dynamic-format audit.

## Success Criteria
- No runtime `sprintf` remains in production source files.
- Comment-only references in `src/protocol.h` are either updated or intentionally left as documentation.
- The code builds cleanly with the existing warning level.
- High-risk text paths have unit or smoke-test coverage.
