#include "savebench.h"

#include "account_management.h"
#include "comm.h"
#include "db.h"
#include "save_benchmark.h"
#include "structs.h"
#include "utils.h"

#include <cstdlib>
#include <string>

ACMD(do_savebench)
{
    if (GET_LEVEL(ch) < LEVEL_IMPL) {
        send_to_char("You can't do that.\r\n", ch);
        return;
    }
    if (IS_NPC(ch) || !ch->desc) {
        send_to_char("savebench: needs a real, connected character.\r\n", ch);
        return;
    }

    int iterations = 100;
    if (argument && *argument) {
        iterations = atoi(argument);
    }
    if (iterations < 1)
        iterations = 1;
    if (iterations > 10000)
        iterations = 10000;

    std::string owner, err;
    if (!account::find_linked_character_owner_account(".", GET_NAME(ch), &owner, &err) || owner.empty()) {
        send_to_char("savebench: this character is not account-linked; nothing to profile.\r\n", ch);
        return;
    }

    // SAVE: serialize the live char (read-only snapshot) and profile S1-S5 into a throwaway path.
    char_file_u chd {};
    char_to_store(ch, &chd);
    const std::string scratch = std::string("players/SAVEBENCH_") + GET_NAME(ch) + ".json";

    savebench::PipelineReport save_report, load_report;
    if (!savebench::profile_save(chd, ".", owner, GET_NAME(ch), scratch, iterations, &save_report, &err)) {
        send_to_char("savebench: save profiling failed.\r\n", ch);
        return;
    }

    // LOAD: profile L1-L4 against the real files (read-only). L5 (store_to_char) is offline-only.
    if (!savebench::profile_load(".", owner, GET_NAME(ch), iterations, /*include_store_to_char=*/false,
            &load_report, &err)) {
        send_to_char("savebench: load profiling failed.\r\n", ch);
        return;
    }

    std::string report = "savebench: " + std::to_string(iterations) + " iterations (live char NOT modified)\r\n";
    report += savebench::format_report("SAVE", save_report);
    report += savebench::format_report("LOAD (L1-L4; L5 offline-only)", load_report);
    page_string(ch->desc, const_cast<char*>(report.c_str()), 1);
}
