#include "fixture_lib_writer.h"

#include "account_management.h"
#include "fixture_character_maker.h"
#include "structs.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <limits>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

// The legacy players/ buckets, as the account layer names them. Boot does not index ZZZ, the
// archive of deleted characters; counting it too also refuses the names archived there and can
// raise the first idnum.
constexpr std::array<std::string_view, 6> legacy_player_buckets = { "A-E", "F-J", "K-O", "P-T", "U-Z", "ZZZ" };

// Fields in a legacy player file name: name.level.race.idnum.logtime.flags.
constexpr std::size_t legacy_player_file_field_count = 6;

// Position of the idnum among a legacy player file name's fields.
constexpr std::size_t legacy_player_file_idnum_field = 3;

// The largest idnum boot can hold: it reads idnums into an int.
constexpr long long largest_idnum = std::numeric_limits<int>::max();

// Who the account's email verification is recorded as coming from.
constexpr const char* email_verifier = "rotstool";

// Makes a directory the process's working directory for one scope and restores the previous one.
class ScopedWorkingDirectory {
public:
    // Makes directory the working directory. error is set, and the working directory is left as
    // it was, when the current one cannot be read or directory cannot be entered.
    ScopedWorkingDirectory(const std::filesystem::path& directory, std::error_code& error)
    {
        saved_directory = std::filesystem::current_path(error);
        if (error) {
            return;
        }
        std::filesystem::current_path(directory, error);
        changed = !error;
    }

    ~ScopedWorkingDirectory()
    {
        std::error_code ignored;
        restore(ignored);
    }

    ScopedWorkingDirectory(const ScopedWorkingDirectory&) = delete;
    ScopedWorkingDirectory& operator=(const ScopedWorkingDirectory&) = delete;

    // Restores the previous working directory now, so a caller can report a failure the
    // destructor would have to ignore. error is set when it cannot be entered.
    void restore(std::error_code& error)
    {
        error.clear();
        if (!changed) {
            return;
        }
        std::filesystem::current_path(saved_directory, error);
        changed = static_cast<bool>(error);
    }

private:
    // The working directory before this object changed it.
    std::filesystem::path saved_directory;
    // Whether the working directory is the one this object set and not yet restored.
    bool changed = false;
};

// Returns text in lower case.
std::string lower_case_copy(std::string_view text)
{
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char letter) -> char {
        return static_cast<char>(std::tolower(letter));
    });
    return lowered;
}

// Sets out_lib to lib_directory made absolute, so the server's writers find it after the working
// directory changes to it. Returns false, with the reason in out_error_message, when it is not a
// directory.
bool resolve_lib_directory(const std::filesystem::path& lib_directory, std::filesystem::path& out_lib,
    std::string& out_error_message)
{
    std::error_code error;
    out_lib = std::filesystem::absolute(lib_directory, error);
    if (error) {
        out_error_message = "The lib '" + lib_directory.string() + "' cannot be resolved: " + error.message();
        return false;
    }
    if (!std::filesystem::is_directory(out_lib, error)) {
        out_error_message = "The lib '" + out_lib.string() + "' is not a directory";
        if (error) {
            out_error_message += " (" + error.message() + ")";
        }
        out_error_message += ".";
        return false;
    }
    return true;
}

// Adds the lower-cased name of every character the lib's accounts list to out_names_lower, and
// raises out_highest_idnum to the highest of their idnums. Returns false, with the reason in
// out_error_message, for an account record or a listed character file it cannot read: the server
// would set either aside, and its idnum could clash with one written here.
bool collect_account_characters(const std::filesystem::path& lib, std::set<std::string>& out_names_lower,
    long long& out_highest_idnum, std::string& out_error_message)
{
    const std::filesystem::path accounts_directory = lib / "accounts";
    std::error_code error;
    const bool has_accounts = std::filesystem::exists(accounts_directory, error);
    if (error) {
        out_error_message = "Could not check for '" + accounts_directory.string() + "': " + error.message();
        return false;
    }
    // A lib with no accounts yet is the usual case; the server's walk reports it as a failure.
    if (!has_accounts) {
        return true;
    }

    const std::string root = lib.string();
    std::string record_error;
    const auto visit_record = [&](const account::AccountRecordOnDisk& record) -> void {
        if (!record_error.empty()) {
            return;
        }
        if (!record.parsed) {
            record_error = "The account record '" + record.record_path + "' cannot be read: " + record.failure_reason;
            return;
        }
        for (const std::string& character_name : record.account.characters) {
            out_names_lower.insert(lower_case_copy(character_name));
            char_file_u stored {};
            std::string read_error;
            if (!account::read_account_character_file_from_record(root, record.account, character_name, &stored,
                    &read_error)) {
                record_error = "The character " + character_name + " of the account record '" + record.record_path
                    + "' cannot be read: " + read_error;
                return;
            }
            out_highest_idnum = std::max(out_highest_idnum, static_cast<long long>(stored.specials2.idnum));
        }
    };

    std::string walk_error;
    if (!account::for_each_account_record_on_disk(root, visit_record, &walk_error)) {
        out_error_message = "Could not read the lib's accounts: " + walk_error;
        return false;
    }
    if (!record_error.empty()) {
        out_error_message = record_error;
        return false;
    }
    return true;
}

// Returns the name boot indexes a legacy player entry under: the text before the first '.'. Returns
// an empty name for an entry boot skips, one starting with '.' or "CVS".
std::string_view legacy_player_entry_name(std::string_view entry_name)
{
    if (entry_name.empty() || entry_name.front() == '.' || entry_name.compare(0, 3, "CVS") == 0) {
        return std::string_view();
    }
    return entry_name.substr(0, entry_name.find('.'));
}

// Splits a legacy player file name into its fields. Returns false when it does not have exactly
// legacy_player_file_field_count fields.
bool split_legacy_player_file_name(std::string_view file_name,
    std::array<std::string_view, legacy_player_file_field_count>& out_fields)
{
    std::string_view remaining = file_name;
    for (std::size_t field_index = 0; field_index < legacy_player_file_field_count; ++field_index) {
        const std::size_t separator = remaining.find('.');
        const bool is_last_field = field_index + 1 == legacy_player_file_field_count;
        if (is_last_field != (separator == std::string_view::npos)) {
            return false;
        }
        out_fields[field_index] = remaining.substr(0, separator);
        if (!is_last_field) {
            remaining.remove_prefix(separator + 1);
        }
    }
    return true;
}

// Raises out_highest_idnum to the idnum in a six-field legacy player file name. An idnum too large
// to read raises it to the largest long long, so the overflow check refuses it.
void raise_to_legacy_idnum(std::string_view file_name, long long& out_highest_idnum)
{
    std::array<std::string_view, legacy_player_file_field_count> fields;
    if (!split_legacy_player_file_name(file_name, fields)) {
        return;
    }
    const std::string_view idnum_field = fields[legacy_player_file_idnum_field];
    const char* const idnum_end = idnum_field.data() + idnum_field.size();
    long long idnum = 0;
    const std::from_chars_result parsed = std::from_chars(idnum_field.data(), idnum_end, idnum);
    if (parsed.ec == std::errc::result_out_of_range && idnum_field.front() != '-') {
        out_highest_idnum = std::numeric_limits<long long>::max();
        return;
    }
    if (parsed.ec == std::errc() && parsed.ptr == idnum_end) {
        out_highest_idnum = std::max(out_highest_idnum, idnum);
    }
}

// Adds the lower-cased name of every legacy player entry in the lib to out_names_lower, under the
// name boot indexes it by, and raises out_highest_idnum to the highest idnum among the six-field
// names. A missing bucket holds nothing. Returns false, with the reason in out_error_message, when
// a bucket cannot be listed.
bool collect_legacy_characters(const std::filesystem::path& lib, std::set<std::string>& out_names_lower,
    long long& out_highest_idnum, std::string& out_error_message)
{
    for (const std::string_view bucket : legacy_player_buckets) {
        const std::filesystem::path bucket_directory = lib / "players" / bucket;
        std::error_code error;
        std::filesystem::directory_iterator entry(bucket_directory, error);
        if (error == std::errc::no_such_file_or_directory) {
            continue;
        }
        const std::filesystem::directory_iterator end;
        while (!error && entry != end) {
            const std::string file_name = entry->path().filename().string();
            const std::string_view indexed_name = legacy_player_entry_name(file_name);
            if (!indexed_name.empty()) {
                out_names_lower.insert(lower_case_copy(indexed_name));
                raise_to_legacy_idnum(file_name, out_highest_idnum);
            }
            entry.increment(error);
        }
        if (error) {
            out_error_message = "Could not list '" + bucket_directory.string() + "': " + error.message();
            return false;
        }
    }
    return true;
}

// Collects the lower-cased names of the characters already in the lib, the way the server's boot
// index finds them, and the highest idnum among them (0 when there are none). Returns false, with
// the reason in out_error_message, when the lib cannot be read.
bool collect_existing_characters(const std::filesystem::path& lib, std::set<std::string>& out_names_lower,
    long long& out_highest_idnum, std::string& out_error_message)
{
    out_names_lower.clear();
    out_highest_idnum = 0;
    return collect_account_characters(lib, out_names_lower, out_highest_idnum, out_error_message)
        && collect_legacy_characters(lib, out_names_lower, out_highest_idnum, out_error_message);
}

// Returns false, with the reason in out_error_message, when a character in spec has a name in
// existing_names_lower.
bool refuse_names_in_lib(const FixtureSpec& spec, const std::set<std::string>& existing_names_lower,
    std::string& out_error_message)
{
    for (const FixtureSpec::Character& character : spec.characters) {
        const std::string name_lower = lower_case_copy(character.name);
        if (existing_names_lower.count(name_lower) != 0) {
            out_error_message = "A character named " + character.name + " is already in the lib.";
            return false;
        }
    }
    return true;
}

// Sets out_first_idnum to the first idnum for spec's characters: one above highest_idnum, or
// first_fixture_idnum if that is higher. Returns false, with the reason in out_error_message, when
// the last of them would be above largest_idnum.
bool choose_first_idnum(const FixtureSpec& spec, long long highest_idnum, long& out_first_idnum,
    std::string& out_error_message)
{
    bool idnums_fit = highest_idnum < largest_idnum;
    long long first_idnum = 0;
    if (idnums_fit) {
        first_idnum = std::max(highest_idnum + 1, static_cast<long long>(first_fixture_idnum));
        // At least 1, because first_idnum is at most largest_idnum.
        const unsigned long long idnums_left = static_cast<unsigned long long>(largest_idnum - first_idnum + 1);
        idnums_fit = spec.characters.size() <= idnums_left;
    }
    if (!idnums_fit) {
        out_error_message = "The lib's highest idnum, " + std::to_string(highest_idnum)
            + ", leaves no room for the spec's characters (" + std::to_string(spec.characters.size())
            + ") at or below " + std::to_string(largest_idnum) + ".";
        return false;
    }
    // At most largest_idnum, which a long holds.
    out_first_idnum = static_cast<long>(first_idnum);
    return true;
}

// Writes a made character's character, objects and exploits files into the account named
// account_name. Returns false, with the reason in out_error_message, when a write fails.
bool write_character_files(const std::string& root, const std::string& account_name, const char_file_u& stored,
    std::string& out_error_message)
{
    const std::string character_name = stored.name;
    std::string write_error;
    if (!account::write_account_character_file(root, account_name, stored, &write_error)) {
        out_error_message = "Could not write " + character_name + "'s character file: " + write_error;
        return false;
    }
    if (!account::write_default_account_object_file(root, account_name, character_name, &write_error)) {
        out_error_message = "Could not write " + character_name + "'s objects file: " + write_error;
        return false;
    }
    if (!account::write_default_account_exploit_file(root, account_name, character_name, &write_error)) {
        out_error_message = "Could not write " + character_name + "'s exploits file: " + write_error;
        return false;
    }
    return true;
}

// Makes each of spec's characters, in order, with idnums counting up from first_idnum, and writes
// its files into the account named account_name, which must be on disk and list none of them.
// Appends what it wrote to out_written. Returns false, with the reason in out_error_message, when
// a character cannot be made or written.
bool make_and_write_characters(const FixtureSpec& spec, const std::filesystem::path& lib,
    const std::string& account_name, long now, long first_idnum, std::vector<WrittenFixtureCharacter>& out_written,
    std::string& out_error_message)
{
    // The character maker writes nothing only while the lib is the working directory.
    std::error_code error;
    ScopedWorkingDirectory working_directory(lib, error);
    if (error) {
        out_error_message = "Could not make '" + lib.string() + "' the working directory: " + error.message();
        return false;
    }

    const std::string root = lib.string();
    long idnum = first_idnum;
    std::vector<int> knowledge;
    for (const FixtureSpec::Character& character : spec.characters) {
        char_file_u stored {};
        std::string make_error;
        if (!make_fixture_character(character, account_name, idnum, now, stored, knowledge, make_error)) {
            out_error_message = "Could not make the character " + character.name + ": " + make_error;
            return false;
        }
        if (knowledge.size() != character.skill_practices.size()) {
            out_error_message = "The maker reported knowledge for " + std::to_string(knowledge.size()) + " of "
                + character.name + "'s " + std::to_string(character.skill_practices.size()) + " skills.";
            return false;
        }
        if (!write_character_files(root, account_name, stored, out_error_message)) {
            return false;
        }

        WrittenFixtureCharacter written_character;
        written_character.name = stored.name;
        written_character.idnum = idnum;
        written_character.skill_knowledge.reserve(knowledge.size());
        for (std::size_t skill = 0; skill < knowledge.size(); ++skill) {
            written_character.skill_knowledge.emplace_back(character.skill_practices[skill].first, knowledge[skill]);
        }
        out_written.push_back(std::move(written_character));
        ++idnum;
    }

    working_directory.restore(error);
    if (error) {
        out_error_message = "Could not restore the working directory: " + error.message();
        return false;
    }
    return true;
}

// Verifies the account's email, links every written character to it and writes it. Returns
// false, with the reason in out_error_message, when a link or the write fails.
bool link_characters_and_write_account(const std::filesystem::path& lib, long now,
    const std::vector<WrittenFixtureCharacter>& written, account::AccountData& account, std::string& out_error_message)
{
    account::verify_email(&account, email_verifier, now);
    std::string account_error;
    for (const WrittenFixtureCharacter& written_character : written) {
        if (!account::add_character_to_account(&account, written_character.name, &account_error)) {
            out_error_message = "Could not link " + written_character.name + " to the account: " + account_error;
            return false;
        }
    }
    if (!account::write_account_file(lib.string(), account, &account_error)) {
        out_error_message = "Could not write the account: " + account_error;
        return false;
    }
    return true;
}

// Reads one written character's files back with the server's readers and checks the character
// file holds its name, the spec's level and its idnum. Sets its character_file. Returns false,
// with the reason in out_error_message, when a file cannot be read or does not match.
bool read_back_character(const std::filesystem::path& lib, const std::string& account_name,
    const FixtureSpec::Character& character, WrittenFixtureCharacter& written_character,
    std::string& out_error_message)
{
    const std::string root = lib.string();
    const std::string& name = written_character.name;
    char_file_u stored {};
    std::string read_error;
    if (!account::read_account_character_file(root, account_name, name, &stored, &read_error)) {
        out_error_message = "Could not read back " + name + "'s character file: " + read_error;
        return false;
    }
    const std::string stored_name = stored.name;
    if (lower_case_copy(stored_name) != lower_case_copy(name) || stored.level != character.level
        || stored.specials2.idnum != written_character.idnum) {
        out_error_message = "The character file read back for " + name + " holds " + stored_name + " at level "
            + std::to_string(stored.level) + " with idnum " + std::to_string(stored.specials2.idnum) + ".";
        return false;
    }

    std::string object_bytes;
    if (!account::read_account_object_file(root, account_name, name, &object_bytes, &read_error)) {
        out_error_message = "Could not read back " + name + "'s objects file: " + read_error;
        return false;
    }
    std::vector<exploit_record> exploit_records;
    if (!account::read_account_exploit_file(root, account_name, name, &exploit_records, &read_error)) {
        out_error_message = "Could not read back " + name + "'s exploits file: " + read_error;
        return false;
    }
    if (!exploit_records.empty()) {
        out_error_message = name + "'s exploits file holds " + std::to_string(exploit_records.size())
            + " records; it was written empty.";
        return false;
    }

    written_character.character_file = account::account_character_player_path(root, account_name, name);
    return true;
}

// Reads the account and every written character back with the server's readers: the account
// must be verified and link each character, and each character's files must read back. Sets each
// character_file. Returns false, with the reason in out_error_message, at the first mismatch.
bool read_back(const FixtureSpec& spec, const std::filesystem::path& lib,
    std::vector<WrittenFixtureCharacter>& written, std::string& out_error_message)
{
    account::AccountData stored_account;
    std::string read_error;
    if (!account::read_account_file_by_email(lib.string(), spec.email, &stored_account, &read_error)) {
        out_error_message = "Could not read back the account for " + spec.email + ": " + read_error;
        return false;
    }
    if (!stored_account.email_verified) {
        out_error_message = "The account for " + spec.email + " was read back unverified.";
        return false;
    }
    for (std::size_t index = 0; index < written.size(); ++index) {
        WrittenFixtureCharacter& written_character = written[index];
        if (!account::account_has_character(stored_account, written_character.name)) {
            out_error_message = "The account for " + spec.email + " was read back without " + written_character.name
                + ".";
            return false;
        }
        if (!read_back_character(lib, stored_account.account_name, spec.characters[index], written_character,
                out_error_message)) {
            return false;
        }
    }
    return true;
}

// Writes the characters, with idnums counting up from first_idnum, into the account
// create_account_for_email has just written, then links them and writes the account. The
// characters go first because the maker needs an account that does not list them yet. Returns
// false, with the reason in out_error_message, at the first failure; what was written stays for
// the caller to remove.
bool write_account_contents(const FixtureSpec& spec, const std::filesystem::path& lib, long now, long first_idnum,
    account::AccountData& account, std::vector<WrittenFixtureCharacter>& out_written, std::string& out_error_message)
{
    return make_and_write_characters(spec, lib, account.account_name, now, first_idnum, out_written,
               out_error_message)
        && link_characters_and_write_account(lib, now, out_written, account, out_error_message)
        && read_back(spec, lib, out_written, out_error_message);
}

// Removes the directory of an account this run created, appending to out_error_message when it
// cannot.
void remove_account_directory(const std::filesystem::path& lib, const account::AccountData& account,
    std::string& out_error_message)
{
    // An empty email would name the bucket, which holds other accounts.
    if (account.normalized_email.empty()) {
        out_error_message += " The account's directory was not removed: the account has no email.";
        return;
    }
    const std::string account_file = account::account_file_path(lib.string(), account.normalized_email);
    const std::filesystem::path account_directory = std::filesystem::path(account_file).parent_path();
    std::error_code error;
    std::filesystem::remove_all(account_directory, error);
    if (error) {
        out_error_message += " The account directory '" + account_directory.string()
            + "' could not be removed: " + error.message();
    }
}

} // namespace

bool write_fixtures_to_lib(const FixtureSpec& spec, const std::filesystem::path& lib_directory, long now,
    std::vector<WrittenFixtureCharacter>& out_written, std::string& out_error_message)
{
    out_written.clear();

    std::filesystem::path lib;
    if (!resolve_lib_directory(lib_directory, lib, out_error_message)) {
        return false;
    }
    std::set<std::string> existing_names_lower;
    long long highest_idnum = 0;
    long first_idnum = 0;
    if (!collect_existing_characters(lib, existing_names_lower, highest_idnum, out_error_message)
        || !refuse_names_in_lib(spec, existing_names_lower, out_error_message)
        || !choose_first_idnum(spec, highest_idnum, first_idnum, out_error_message)) {
        return false;
    }

    // Refuses an account that already exists, or a password the server's policy refuses, before
    // writing anything.
    account::AccountData account;
    std::string account_error;
    if (!account::create_account_for_email(lib.string(), spec.email, spec.password, now, &account, &account_error)) {
        out_error_message = "Could not create the account for " + spec.email + ": " + account_error;
        return false;
    }

    std::vector<WrittenFixtureCharacter> written;
    if (!write_account_contents(spec, lib, now, first_idnum, account, written, out_error_message)) {
        remove_account_directory(lib, account, out_error_message);
        return false;
    }
    out_written = std::move(written);
    return true;
}
