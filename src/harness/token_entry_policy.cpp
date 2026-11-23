/* token_entry_policy.cpp */

#include "token_entry_policy.h"

#include "account_management.h"
#include "comm.h"
#include "login_steps.h"
#include "structs.h"
#include "utils.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace harness {

namespace {
// Stands in for the character's name in the log when a line is too malformed to name one.
constexpr std::string_view UNNAMED_CHARACTER = "(no character named)";
// Line endings, which a reason sent or logged on one line may not hold.
constexpr std::string_view LINE_ENDINGS = "\r\n";
// The decimal digits. A name of nothing else is a roster number, which an entry line may not use.
constexpr std::string_view DIGITS = "0123456789";

// The words of an entry line after ENTRY_WORD.
struct EntryLine {
    // The token the line carries.
    std::string_view token;
    // The email of the account the character is linked to.
    std::string_view account_email;
    // The character to enter as.
    std::string_view character_name;
};

// Removes the next space-separated word from the front of remaining, with the spaces before it,
// and returns it; returns an empty word when only spaces are left.
std::string_view take_word(std::string_view& remaining)
{
    const std::size_t word_start = remaining.find_first_not_of(' ');
    if (word_start == std::string_view::npos) {
        remaining = std::string_view();
        return std::string_view();
    }
    remaining.remove_prefix(word_start);
    const std::string_view word = remaining.substr(0, remaining.find(' '));
    remaining.remove_prefix(word.size());
    return word;
}

// Reads the three words that follow ENTRY_WORD. Returns nothing unless there are exactly three.
std::optional<EntryLine> parse_entry_words(std::string_view words)
{
    EntryLine entry;
    entry.token = take_word(words);
    entry.account_email = take_word(words);
    entry.character_name = take_word(words);
    const std::string_view extra_word = take_word(words);
    if (entry.character_name.empty() || !extra_word.empty()) {
        return std::nullopt;
    }
    return entry;
}

// Returns true when offered equals expected. Every character of expected is compared whatever
// the first difference, so the time a refusal takes does not reveal how much of a token was right.
bool tokens_match(std::string_view offered, std::string_view expected)
{
    unsigned int difference = 0;
    if (offered.size() != expected.size()) {
        difference = 1;
    }
    for (std::size_t index = 0; index < expected.size(); ++index) {
        char offered_character = '\0';
        if (index < offered.size()) {
            offered_character = offered[index];
        }
        difference |= static_cast<unsigned char>(offered_character) ^ static_cast<unsigned char>(expected[index]);
    }
    return difference == 0;
}

// Returns text on one line: each run of line endings inside it becomes one space, and those at
// either end are dropped.
std::string one_line(std::string_view text)
{
    std::string line;
    line.reserve(text.size());
    std::string_view remaining = text;
    while (!remaining.empty()) {
        const std::size_t ending_start = remaining.find_first_of(LINE_ENDINGS);
        const std::string_view run = remaining.substr(0, ending_start);
        line.append(run);
        remaining.remove_prefix(run.size());
        const std::size_t next_text = remaining.find_first_not_of(LINE_ENDINGS);
        if (next_text == std::string_view::npos) {
            break;
        }
        if (!line.empty()) {
            line += ' ';
        }
        remaining.remove_prefix(next_text);
    }
    return line;
}

// Returns reason, or fallback when reason is empty.
std::string_view reason_or(std::string_view reason, std::string_view fallback)
{
    if (reason.empty()) {
        return fallback;
    }
    return reason;
}

// Why an entry is refused, which decides how the refusal is logged.
enum class RefusalCause {
    // The line is not an entry line for this run: a fault in whatever sent it, logged as a SYSERR.
    BadEntryLine,
    // The game's rules refuse the account or character, as they would refuse a login. Logged
    // without SYSERR, so a scenario can expect the refusal without the crash monitor failing it.
    GameRule,
};

// Refuses the entry: tells the connection reason, logs it with character_name as cause decides,
// and closes the connection.
void refuse_entry(descriptor_data& connection, RefusalCause cause, std::string_view character_name,
    std::string_view reason)
{
    const std::string reason_line = one_line(reason);

    std::string message(TokenEntryPolicy::REFUSAL_PREFIX);
    message.append(reason_line);
    message += "\n\r";
    SEND_TO_Q(message.c_str(), &connection);

    std::string log_line(TokenEntryPolicy::REFUSAL_PREFIX);
    if (cause == RefusalCause::BadEntryLine) {
        log_line = std::string("SYSERR: ") + __func__ + ": ";
    }
    log_line.append(character_name);
    log_line += ": ";
    log_line.append(reason_line);
    log(log_line.c_str());

    STATE(&connection) = CON_CLOSE;
}
} // namespace

//============================================================================
TokenEntryPolicy::TokenEntryPolicy(std::string run_token)
    : token(std::move(run_token))
{
}

//============================================================================
bool TokenEntryPolicy::try_enter(descriptor_data& connection, std::string_view line)
{
    std::string_view words = line;
    if (take_word(words) != ENTRY_WORD) {
        return false;
    }

    const std::optional<EntryLine> entry = parse_entry_words(words);
    if (!entry.has_value()) {
        const std::string reason
            = "an entry line is \"" + std::string(ENTRY_WORD) + " <token> <account email> <character>\".";
        refuse_entry(connection, RefusalCause::BadEntryLine, UNNAMED_CHARACTER, reason);
        return true;
    }
    // An empty token never matches: an entry line's token is a word, never empty.
    if (!tokens_match(entry->token, token)) {
        refuse_entry(connection, RefusalCause::BadEntryLine, entry->character_name, "the token is not this run's.");
        return true;
    }

    // The roster would read a name of digits alone as a row number and pick whichever character
    // the row holds.
    if (entry->character_name.find_first_not_of(DIGITS) == std::string_view::npos) {
        refuse_entry(connection, RefusalCause::BadEntryLine, entry->character_name,
            "a character is entered by name, not by roster number.");
        return true;
    }

    const std::string storage_root(ACCOUNT_STORAGE_ROOT);
    account::AccountData account_data;
    std::string error_message;
    const std::string email(entry->account_email);
    if (!account::read_account_file_by_email(storage_root, email, &account_data, &error_message)) {
        refuse_entry(connection, RefusalCause::GameRule, entry->character_name, reason_or(error_message, "the account could not be read."));
        return true;
    }
    // Unlike the password login, this read does not clear the account index's unreadable-since-boot
    // mark: that is bookkeeping for a record repaired on a running server, not a check on entry.
    // The password login's two checks that need no password, in its order.
    if (!account::check_account_not_blocked(account_data, &error_message)) {
        refuse_entry(connection, RefusalCause::GameRule, entry->character_name,
            reason_or(error_message, "the account is blocked."));
        return true;
    }
    if (!account::check_account_email_verified(account_data, &error_message)) {
        refuse_entry(connection, RefusalCause::GameRule, entry->character_name,
            reason_or(error_message, "the account's email address is not verified."));
        return true;
    }
    bind_connection_to_account(connection, account_data);
    // The password login's audit line, named for the harness. Unlike that login, the entry leaves
    // the account's failed password count alone: clearing it is what follows telling the owner
    // about those failures, which an entry line never shows.
    const char* host = "<unknown-host>";
    if (*connection.host) {
        host = connection.host;
    }
    vmudlog(BRF, "Harness account login for %s [%s]", account_data.normalized_email.c_str(), host);

    // The roster's own match, in the order and filter the roster opens with, so a name resolves
    // exactly as typing it at the roster would.
    account::RosterSort stored_sort = account::RosterSort::Account;
    account::roster_sort_from_string(account_data.roster_sort, &stored_sort);
    std::string selected_name;
    const std::string typed_name(entry->character_name);
    if (!account::select_linked_character(storage_root, account_data, typed_name,
            stored_sort, account::RosterFilter::None, &selected_name, &error_message)) {
        refuse_entry(connection, RefusalCause::GameRule, entry->character_name,
            reason_or(error_message, "the character is not linked to the account."));
        return true;
    }

    const AccountCharacterSelection selection = select_account_character(connection, account_data, selected_name);
    if (!selection.selected) {
        refuse_entry(connection, RefusalCause::GameRule, entry->character_name,
            reason_or(selection.refusal, "the character could not be selected."));
        return true;
    }

    const Admission admission = admit_selected_character(connection);
    if (admission == Admission::Refused) {
        // The game has told the connection why in its own words; say that it was the harness's
        // entry the game refused, so the process that sent the line reads a refusal.
        refuse_entry(connection, RefusalCause::GameRule, entry->character_name, "the game refused " + selected_name + " at admission.");
        return true;
    }
    if (admission == Admission::Reconnected) {
        return true;
    }

    std::string refusal;
    if (!claim_account_session(connection, account_data, refusal)) {
        refuse_entry(connection, RefusalCause::GameRule, entry->character_name,
            reason_or(refusal, "another session on the account forbids the character."));
        return true;
    }

    const std::string entered_message = "Harness entry: " + selected_name;
    log(entered_message.c_str());
    enter_game(connection);
    return true;
}

} // namespace harness
