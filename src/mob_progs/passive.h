#ifndef MOB_PROGS_PASSIVE_H
#define MOB_PROGS_PASSIVE_H

/* Passive/service mob programs: they react to what players do and don't
 * fight or roam. First resident: the barter vendor (program 33). */

#include "../mob_options.h"

#include <functional>
#include <string>
#include <vector>

constexpr int PROG_BARTER_VENDOR = 33;
constexpr size_t VENDOR_LIST_NAME_COLUMN_MAX = 38;

struct vendor_list_cost {
    int qty;
    std::string name;
};
struct vendor_list_row {
    std::string name;
    int left; /* copies in stock for a deduct item; < 0 otherwise */
    std::vector<vendor_list_cost> costs;
};
std::string format_vendor_list(const std::vector<vendor_list_row>& rows);

struct vendor_shortfall {
    int obj_vnum;
    int need;
    int have;
};
std::vector<vendor_shortfall> vendor_shortfalls(const std::vector<vendor_cost>& costs,
    const std::function<int(int)>& have_count);

#endif
