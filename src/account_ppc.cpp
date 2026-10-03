#include "account_ppc.h"

#include "account_management.h"
#include "color.h"
#include "protocol.h"
#include "structs.h"
#include "utils.h"

#include <cstring>

extern struct descriptor_data* descriptor_list;

namespace {

/* A character's name for a log line. GET_NAME is not safe on a half-built character -- the
   login state machine reaches these paths before the player file has been read -- and a log
   line is never worth a crash. */
const char* ppc_log_name(const struct char_data* ch)
{
    if (ch == nullptr || IS_NPC(ch) || ch->player.name == nullptr || *ch->player.name == '\0')
        return "an unnamed character";
    return ch->player.name;
}

/* The normalized account name a character is playing under, or empty when it has no
   descriptor or no account name on it.

   An NPC never has one, even with a live descriptor attached. An immortal who has SWITCHed is
   driving a mob: ch is the mob while ch->desc is still their own socket, account name and all.
   read_mobile copies the prototype wholesale (`*mob = mob_proto[i]`), so every instance of a
   mob shares the prototype's char_prof_data -- reading a PPC off one means reading the
   prototype's zeroed colours, and writing one means scribbling on the prototype. Checked in
   this one helper because both the propagation walk and the sibling lookup go through it, so
   `sort`, `colour` and every toggle are covered by a single guard. */
std::string playing_account_name(const struct char_data* ch)
{
    if (ch == nullptr || IS_NPC(ch) || ch->desc == nullptr || *ch->desc->account_name == '\0')
        return std::string();
    return account::normalize_account_name(ch->desc->account_name);
}

/* The one definition of "this descriptor is holding another character of the same account":
   not this character's own descriptor, an attached non-NPC character that still points back at
   the descriptor it is listed under, and a matching normalized account name. Both the
   propagation walk and the login-time live-sibling lookup go through it so the matching rules
   exist in exactly one place. Connection state is deliberately NOT checked here -- the two
   callers want different sets of states, and each says which. */
struct char_data* account_sibling_on_descriptor(const struct descriptor_data* descriptor,
    const std::string& account_name, const struct char_data* ch)
{
    if (descriptor == nullptr || ch == nullptr || descriptor == ch->desc)
        return nullptr;

    char_data* other = descriptor->character;
    if (other == nullptr || other == ch || IS_NPC(other) || other->desc != descriptor)
        return nullptr;
    if (account::normalize_account_name(descriptor->account_name) != account_name)
        return nullptr;
    return other;
}

} // namespace

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

namespace {

/* The account-file half of the login-time apply. Split out from
   ppc_apply_account_to_character_in so that the "now consider a live sibling" step below it is
   ordered -- and conditional -- in exactly one place. */
void apply_account_file_to_character(const std::string& root_directory,
    const char* account_name, struct char_data* ch)
{
    account::AccountData account;
    std::string error_message;
    if (!account::read_account_file(root_directory, account_name, &account, &error_message)) {
        vmudlog(NRM, "ppc: could not read account %s to apply preferences: %s",
            account_name, error_message.c_str());
        return;
    }

    if (account.preferences.present) {
        ppc_write_to_character(account.preferences, ch);
        /* The character has now read the account: it is allowed to write back. */
        ch->ppc_account_loaded = true;
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
        return;
    }

    /* Logged because it happens once per account, ever, and it decides which character's
       scheme every other character on the account inherits. When a player reports "my colours
       changed", this is the line that says whose scheme won. */
    vmudlog(NRM, "ppc: seeded account %s preferences from %s", account_name, ppc_log_name(ch));

    /* The account now holds exactly this character's PPC, which is the same reconciled state
       the apply branch above leaves behind, so this character may write back too. Note what is
       deliberately NOT marked: a failed read, a failed seed write, and the level-0 "refused to
       seed" case above all leave the flag false, because in none of them is the character's PPC
       known to agree with the account's. */
    ch->ppc_account_loaded = true;
}

} // namespace

void ppc_apply_account_to_character_in(const std::string& root_directory,
    const char* account_name, struct char_data* ch)
{
    if (account_name == nullptr || *account_name == '\0' || ch == nullptr || ch->profs == nullptr)
        return;

    /* Step one: the stored PPC. Sets ppc_account_loaded, and only when this character's PPC is
       known to agree with the account's. */
    apply_account_file_to_character(root_directory, account_name, ch);

    /* Step two, and only ever after step one: the account file is only as fresh as the last
       save that wrote it, so it can be up to one autosave behind. Change `brief` on character
       A, log in character B before A's next autosave, and B would read -- and then write back
       -- the superseded value. A sibling that is already CON_PLYNG holds the current value in
       memory by construction, so prefer it.

       Conditional on the apply having actually reconciled this character. When the account
       could not be read, or seeding was refused, ch still holds whatever its own character
       file gave it, and the promise the feature makes is that an unreadable account never
       blanks a player's colours -- reaching for a sibling here would break that promise by the
       side door. Kept inside this function rather than in the "." wrapper below so no entry
       point into the login state machine can get the ordering wrong. */
    if (ch->ppc_account_loaded)
        ppc_prefer_playing_sibling(ch);
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

    /* The invariant: a character may never write its PPC to the account before it has read the
       account's PPC. Enforced here rather than by ordering the callers, because save_char is
       reached from several connection-state branches (bad password, character selection, enter
       the game, self-delete, character creation) and the state machine keeps growing new ones.
       Without this, a character whose local copy has gone stale -- every login loads the
       character file before the account is consulted -- silently overwrites whatever a sibling
       character last changed, which is exactly the account-level promise the feature makes. */
    if (!ch->ppc_account_loaded)
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

    if (account.preferences.present) {
        if (ppc_equal(account.preferences, current))
            return false;
    } else {
        /* Mirror the seeding guard in ppc_apply_account_to_character_in: a brand-new
           (level 0) character on an account that already has other linked characters must
           not seed the account either, on save any more than on login -- otherwise the
           very autosave a minute after login writes back exactly the default scheme the
           login-time guard refused to write. */
        if (GET_LEVEL(ch) == 0 && account.characters.size() > 1)
            return false;
    }

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

void ppc_copy_between_characters(const struct char_data* source, struct char_data* destination)
{
    if (source == nullptr || destination == nullptr || source == destination)
        return;

    /* THE INVARIANT, the other half of it. ppc_store_character_to_account_in refuses to write
       the account from a character that has not read it; this refuses to let such a character
       launder its PPC through one that has. Without it: an account with no stored preferences
       yet, a freshly created alt that the level-0 guard rightly refused to seed from (blank
       defaults, flag false), and then the player's established character logs in, seeds the
       account from its tuned scheme -- and immediately adopts the alt's blanks from the
       sibling walk, which its next save writes into both its character file and the account.
       Both copy paths (propagation and the login-time sibling preference) funnel through here,
       so the check is in the one place a future caller cannot route around. */
    if (!source->ppc_account_loaded)
        return;

    ppc_write_to_character(ppc_read_from_character(source), destination);
}

void ppc_propagate_from(const struct char_data* ch)
{
    /* playing_account_name is where the NPC guard lives: a SWITCHed immortal is holding a mob
       whose char_prof_data belongs to the shared mob prototype, and must propagate nothing. */
    const std::string account_name = playing_account_name(ch);
    if (account_name.empty())
        return;

    /* Only a reconciled character may be a source (see ppc_copy_between_characters). Checked
       again here so the whole walk is skipped rather than copying nothing once per descriptor. */
    if (!ch->ppc_account_loaded)
        return;

    for (descriptor_data* descriptor = descriptor_list; descriptor; descriptor = descriptor->next) {
        /* Cover CON_LINKLS as well as CON_PLYNG: a linkdead body is still resident in
           character_list with ch->desc attached (see comm.cpp's close_socket, which
           deliberately leaves that pointer set), and the shutdown/reboot save loop in
           comm.cpp saves every non-NPC character with a desc regardless of connection
           state. Without this, a sibling who goes linkless right before another
           character's change would have that change clobbered by its own stale
           in-memory copy the next time the server saves everyone at shutdown. */
        if (descriptor->connected != CON_PLYNG && descriptor->connected != CON_LINKLS)
            continue;
        char_data* other = account_sibling_on_descriptor(descriptor, account_name, ch);
        if (other == nullptr)
            continue;

        const bool sibling_msdp_was_on = PRF_FLAGGED(other, PRF_MSDP) != 0;
        ppc_copy_between_characters(ch, other);

        /* The same thing do_gen_tog does for the character that typed the command, for the
           same reason: on a genuine off->on transition the client has to be re-sent every
           reported variable, including the login-time constants (name, level, race, profession
           caps) that are set once and would otherwise never be sent again. A sibling switched
           on by propagation needs that snapshot just as much as the player who typed it. */
        if (!sibling_msdp_was_on && PRF_FLAGGED(other, PRF_MSDP) && descriptor->pProtocol != nullptr)
            MSDPMarkAllReportedDirty(descriptor);
    }
}

struct char_data* ppc_find_playing_sibling(const struct char_data* ch)
{
    const std::string account_name = playing_account_name(ch);
    if (account_name.empty())
        return nullptr;

    for (descriptor_data* descriptor = descriptor_list; descriptor; descriptor = descriptor->next) {
        /* CON_PLYNG only, unlike ppc_propagate_from above. A playing sibling is by construction
           at least as fresh as the account file: it reconciled with the account at its own
           login, and every change since then reached its memory before it could reach the file.
           A CON_LINKLS body carries no such guarantee. Its own transition to linkdead flushed
           its PPC to the account (close_socket saves before setting CON_LINKLS), so it starts
           out no fresher than the file, and from then on it only stays current for as long as
           every future PPC-changing path remembers to call ppc_propagate_from. The account is
           the store that receives every change by definition. So a linkdead body can only ever
           match the file or lag it -- never lead it -- which makes preferring it pure risk. */
        if (descriptor->connected != CON_PLYNG)
            continue;
        char_data* other = account_sibling_on_descriptor(descriptor, account_name, ch);
        if (other == nullptr)
            continue;

        /* Freshness is the whole argument for preferring a sibling, and only a sibling that
           has itself reconciled with the account has any. One that has not is still carrying
           whatever its own character file held -- the blank defaults of a just-created alt, in
           the case that actually bites -- so it is not a fresher copy of the account's PPC, it
           is a different one. Skip it and keep looking. */
        if (!other->ppc_account_loaded)
            continue;
        return other;
    }
    return nullptr;
}

void ppc_prefer_playing_sibling(struct char_data* ch)
{
    /* Note what is deliberately not touched: ch->ppc_account_loaded. Whether this character is
       allowed to write the account is decided by its reconciliation with the account file, and
       adopting a sibling's copy does not change that analysis. When the flag is set, ch's next
       save carries the sibling's fresher value into the account, which is the point. */
    char_data* sibling = ppc_find_playing_sibling(ch);
    if (sibling == nullptr)
        return;

    ppc_copy_between_characters(sibling, ch);

    /* One line per login that finds another of the player's characters already online -- rare
       enough to be cheap, and the only record that a character started the session with
       something other than what its own file and the account file held. */
    vmudlog(NRM, "ppc: %s adopted the preferences of its playing sibling %s",
        ppc_log_name(ch), ppc_log_name(sibling));
}

bool ppc_account_has_preferences_in(const std::string& root_directory, const char* account_name)
{
    if (account_name == nullptr || *account_name == '\0')
        return false;

    account::AccountData account;
    if (!account::read_account_file(root_directory, account_name, &account, nullptr))
        return false;
    return account.preferences.present;
}

bool ppc_account_has_preferences(const char* account_name)
{
    return ppc_account_has_preferences_in(".", account_name);
}
