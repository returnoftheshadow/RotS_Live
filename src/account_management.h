#ifndef ACCOUNT_MANAGEMENT_H
#define ACCOUNT_MANAGEMENT_H

#include "account_management_assets.h"
#include "account_management_identity.h"
#include "account_management_migration.h"
#include "account_management_presentation.h"
#include "account_management_storage.h"
#include "account_management_types.h"

#include <string>
#include <vector>

namespace account {

// RosterSort / RosterFilter live in account_management_types.h (see comment there).

// The single ordered, filtered, capped list of positions into account.characters. Rendering and
// selection MUST both consume this rather than each deriving an order, or a character can be shown
// at a number that selects someone else.
std::vector<size_t> ordered_roster_indices(const std::string& root_directory,
    const AccountData& account, RosterSort sort, RosterFilter filter);

const char* roster_sort_to_string(RosterSort sort);
bool roster_sort_from_string(const std::string& value, RosterSort* sort);

} // namespace account

#endif
