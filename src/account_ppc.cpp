#include "account_ppc.h"

#include "account_management.h"
#include "color.h"
#include "structs.h"
#include "utils.h"

#include <cstring>

account::AccountPreferences ppc_read_from_character(const struct char_data* ch)
{
    account::AccountPreferences preferences;
    if (ch == nullptr || ch->profs == nullptr)
        return preferences;

    preferences.present = true;
    preferences.preference_flags = PRF_FLAGS(ch) & PPC_PRF_MASK;
    std::memcpy(preferences.colors, ch->profs->colors, sizeof(preferences.colors));
    std::memcpy(preferences.color_settings, ch->profs->color_settings, sizeof(preferences.color_settings));
    return preferences;
}

void ppc_write_to_character(const account::AccountPreferences& preferences, struct char_data* ch)
{
    if (!preferences.present || ch == nullptr || ch->profs == nullptr)
        return;

    PRF_FLAGS(ch) = (PRF_FLAGS(ch) & ~PPC_PRF_MASK) | (preferences.preference_flags & PPC_PRF_MASK);
    std::memcpy(ch->profs->colors, preferences.colors, sizeof(preferences.colors));
    std::memcpy(ch->profs->color_settings, preferences.color_settings, sizeof(preferences.color_settings));
}

bool ppc_equal(const account::AccountPreferences& left, const account::AccountPreferences& right)
{
    if ((left.preference_flags & PPC_PRF_MASK) != (right.preference_flags & PPC_PRF_MASK))
        return false;
    if (std::memcmp(left.colors, right.colors, sizeof(left.colors)) != 0)
        return false;
    return std::memcmp(left.color_settings, right.color_settings, sizeof(left.color_settings)) == 0;
}

void ppc_apply_account_to_character_in(const std::string& root_directory,
    const char* account_name, struct char_data* ch)
{
    if (account_name == nullptr || *account_name == '\0' || ch == nullptr || ch->profs == nullptr)
        return;

    account::AccountData account;
    std::string error_message;
    if (!account::read_account_file(root_directory, account_name, &account, &error_message)) {
        vmudlog(NRM, "ppc: could not read account %s to apply preferences: %s",
            account_name, error_message.c_str());
        return;
    }

    if (account.preferences.present) {
        ppc_write_to_character(account.preferences, ch);
        return;
    }

    /* No stored PPC yet: seed it from the character being played -- but not from a brand-new
       (level 0) character on an account that already has other linked characters. A player
       who creates a fresh character before returning to an established one must not have their
       tuned scheme clobbered by that new character's untouched defaults. The very first
       character linked to a brand-new account still seeds (account.characters.size() <= 1),
       including whatever colour/latin-1 answers the player just gave during creation. */
    if (GET_LEVEL(ch) == 0 && account.characters.size() > 1)
        return;

    account.preferences = ppc_read_from_character(ch);
    if (!account.preferences.present)
        return;
    if (!account::write_account_file(root_directory, account, &error_message)) {
        vmudlog(NRM, "ppc: could not seed preferences for account %s: %s",
            account_name, error_message.c_str());
    }
}

void ppc_apply_account_to_character(const char* account_name, struct char_data* ch)
{
    ppc_apply_account_to_character_in(".", account_name, ch);
}

bool ppc_store_character_to_account_in(const std::string& root_directory,
    const std::string& account_name, const struct char_data* ch)
{
    if (account_name.empty() || ch == nullptr || ch->profs == nullptr)
        return false;

    const account::AccountPreferences current = ppc_read_from_character(ch);
    if (!current.present)
        return false;

    account::AccountData account;
    std::string error_message;
    if (!account::read_account_file(root_directory, account_name, &account, &error_message)) {
        vmudlog(NRM, "ppc: could not read account %s to store preferences: %s",
            account_name.c_str(), error_message.c_str());
        return false;
    }

    if (account.preferences.present && ppc_equal(account.preferences, current))
        return false;

    account.preferences = current;
    if (!account::write_account_file(root_directory, account, &error_message)) {
        vmudlog(NRM, "ppc: could not store preferences for account %s: %s",
            account_name.c_str(), error_message.c_str());
        return false;
    }
    return true;
}

void ppc_store_character_to_account(const std::string& account_name, const struct char_data* ch)
{
    ppc_store_character_to_account_in(".", account_name, ch);
}
