#ifndef ACCOUNT_MANAGEMENT_PRESENTATION_H
#define ACCOUNT_MANAGEMENT_PRESENTATION_H

#include "account_management_types.h"

namespace account {

std::string format_character_name_for_display(const std::string& character_name);
std::string format_account_character_prompt(const std::string& root_directory, const AccountData& account);
std::string format_account_character_list(const std::string& root_directory, const AccountData& account);
std::string format_account_summary(const AccountData& account);
// The account-menu equivalent of the character login's "N LOGIN FAILURES" banner. Empty when the
// account has no failures recorded since its last successful login.
std::string format_account_login_failure_notice(const AccountData& account);

} // namespace account

#endif
