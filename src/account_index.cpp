#include "account_index.h"

#include "account_management_identity.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace account_index {

namespace {

    // email -> entry. The email is the storage key on disk, so it is the identity here too.
    std::unordered_map<std::string, Entry> g_entries;
    // normalized account name -> email.
    std::unordered_map<std::string, std::string> g_account_names;
    // normalized character name -> email.
    std::unordered_map<std::string, std::string> g_characters;
    // email -> the character keys that email currently owns, so upsert can drop what it no longer owns.
    std::unordered_map<std::string, std::vector<std::string>> g_owned_characters;
    // --- Contention -----------------------------------------------------------------------------
    // Two records claiming one key is not merely ambiguous, it is dangerous, so the lookups refuse.
    // What matters just as much is that the refusal LOWERS again once the duplicate is repaired: a
    // contested character key makes save_char write nothing for that character, silently and to the
    // log only, so a flag that only clear() could reset would keep eating a player's progress long
    // after an operator fixed the data on disk.
    //
    // So contention is tracked as the set of CLAIMANTS, not as a bare flag, and "contested" is
    // simply "more than one claimant". The uncontested case -- every key, essentially always -- pays
    // nothing for this: it stays a single entry in the plain key -> owner maps above, and a key only
    // moves into one of the maps below when a second, different claimant actually turns up. A key is
    // in exactly one of the two places, never both.
    using ClaimantSet = std::set<std::string>;

    // Account names claimed by more than one email. A map cannot represent that, and silently
    // resolving to whichever record was upserted last is destructive, not merely lossy:
    // write_account_file std::remove()s the path find_account_file_path_by_account_name returns when
    // it differs from the target (account_management_storage.cpp:240-245), so the record that lost
    // the race would have its file deleted. The directory scan refused to answer at all in this
    // situation, and so does the index -- see find_path_by_account_name.
    std::unordered_map<std::string, ClaimantSet> g_contested_account_names;
    // Character keys claimed by more than one account record. Same shape as the account-name map
    // above and the same reason for existing, except that the consequence is worse: save_char
    // (db.cpp:3269-3296) chooses the directory it writes a character file into from the owner this
    // index resolves, and the branch at db.cpp:3286 CREATES that file when the resolved account has
    // none -- so answering with whichever record was upserted last would migrate a player's saves
    // into an account that does not own the character, and would flip on every write to either
    // record. find_character_owner_account refused to answer here, and so does
    // find_owner_email_by_character.
    std::unordered_map<std::string, ClaimantSet> g_contested_characters;
    // Emails claimed by more than one record under different account names. Mirrors
    // find_account_by_email_internal's own duplicate handling (account_management.cpp:1060-1075):
    // two records at one address are a duplicate only when their account NAMES differ; when they
    // agree it is the ordinary legacy-flat-plus-directory pair, which that scan deduplicates by
    // preferring the directory record and upsert's precedence rule resolves the same way.
    //
    // Claimants here are record PATHS rather than emails -- the email IS the key, and what disputes
    // it is two files -- each carrying the account name its record declares, because "contested"
    // for an address means the claims disagree about that name. The single-claimant case needs no
    // storage at all: g_entries already holds one path and one account name per email, so a second
    // claimant is what creates the map entry, and the claims collapse back out of it the moment a
    // rewrite makes the names agree again.
    std::unordered_map<std::string, std::map<std::string, std::string>> g_contested_emails;

    bool g_enabled = false;
    // The root directory every record_path in here was composed against. The resolvers ignore their
    // own root_directory argument on the fast path, so answering a caller working against a
    // different tree would hand back paths from this one. "." is what boot_db and every live call
    // site use.
    std::string g_root_directory = ".";

    void set_error(std::string* error_message, const std::string& text)
    {
        if (error_message != nullptr)
            *error_message = text;
    }

    // Records `owner` as a claimant of `key`. Uncontested keys stay in `owners` (one string, no
    // allocation beyond the map node the old code already paid for); the second, DIFFERENT claimant
    // is what promotes the key into `contested`.
    void claim_key(std::unordered_map<std::string, std::string>& owners,
        std::unordered_map<std::string, ClaimantSet>& contested, const std::string& key,
        const std::string& owner)
    {
        const auto claims = contested.find(key);
        if (claims != contested.end()) {
            // Already contested by somebody else; this claimant joins them. It cannot drop back to
            // one claimant by an insert, so there is nothing to collapse here.
            claims->second.insert(owner);
            return;
        }

        const auto existing = owners.find(key);
        if (existing == owners.end()) {
            owners[key] = owner;
            return;
        }
        // The overwhelmingly common case: a record re-claiming its own key on every write. Must not
        // look like a second claimant, or one save would break every later one.
        if (existing->second == owner)
            return;

        ClaimantSet claimants;
        claimants.insert(existing->second);
        claimants.insert(owner);
        owners.erase(existing);
        contested.emplace(key, std::move(claimants));
    }

    // Withdraws `owner`'s claim on `key`. This is the half that makes contention self-healing: when
    // the withdrawal leaves exactly one claimant the key goes back to being an ordinary resolvable
    // entry, so repairing the duplicate on disk and writing the repaired record is enough -- no
    // reboot.
    void withdraw_key_claim(std::unordered_map<std::string, std::string>& owners,
        std::unordered_map<std::string, ClaimantSet>& contested, const std::string& key,
        const std::string& owner)
    {
        const auto claims = contested.find(key);
        if (claims != contested.end()) {
            claims->second.erase(owner);
            if (claims->second.size() == 1)
                owners[key] = *claims->second.begin();
            if (claims->second.size() <= 1)
                contested.erase(claims);
            return;
        }

        const auto existing = owners.find(key);
        // The value test matters: with contention the key may have been handed to somebody else
        // since, and erasing it then would delete another record's key.
        if (existing != owners.end() && existing->second == owner)
            owners.erase(existing);
    }

    // Recomputes whether an email is contested from its claims, collapsing the map entry away when
    // the claimants have stopped disagreeing (or when only one is left). Contested means the claims
    // disagree about the ACCOUNT NAME, which is find_account_by_email_internal's own duplicate test.
    void settle_email_claims(const std::string& email)
    {
        const auto claims = g_contested_emails.find(email);
        if (claims == g_contested_emails.end())
            return;

        bool names_disagree = false;
        for (const auto& claim : claims->second) {
            if (claim.second != claims->second.begin()->second) {
                names_disagree = true;
                break;
            }
        }

        if (!names_disagree || claims->second.size() <= 1)
            g_contested_emails.erase(claims);
    }

    // Drops every key owned by this email except the entry itself.
    // Every account name this address currently claims. A disputed address holds more than one, and
    // the entry can only ever remember the last -- which stranded the others in g_account_names for
    // good. Mirrors g_owned_characters so both key kinds are withdrawn the same way.
    std::map<std::string, std::vector<std::string>> g_owned_account_names;

    void erase_owned_keys(const std::string& email)
    {
        // O(1) rather than a walk of every indexed account name: an email owns exactly one account
        // name and the entry already stores it. This runs on every upsert, and upserts run on the
        // login path (clear_account_login_failures, on a login that had failures to clear), so the walk was
        // O(accounts) per login for a key we already had in hand.
        const auto owned_names = g_owned_account_names.find(email);
        if (owned_names != g_owned_account_names.end()) {
            for (const std::string& account_name_key : owned_names->second)
                withdraw_key_claim(g_account_names, g_contested_account_names, account_name_key, email);
            g_owned_account_names.erase(owned_names);
        }

        const auto owned = g_owned_characters.find(email);
        if (owned != g_owned_characters.end()) {
            for (const std::string& character_key : owned->second)
                withdraw_key_claim(g_characters, g_contested_characters, character_key, email);
            g_owned_characters.erase(owned);
        }
    }

} // namespace

void upsert(const account::AccountData& account, const std::string& record_path,
    bool legacy_flat_layout)
{
    const std::string email = account::normalize_email(account.normalized_email);
    if (email.empty())
        return;

    const std::string normalized_account_name = account::normalize_account_name(account.account_name);

    const auto existing = g_entries.find(email);
    // Two DIFFERENT records at one address, disagreeing about the account name: the case
    // find_account_by_email_internal refuses to answer for. Handled before the precedence return
    // below, because the flat-record-ignored case is one of the two ways this arises.
    const auto email_claims = g_contested_emails.find(email);
    if (email_claims != g_contested_emails.end()) {
        // Already contested. Restate THIS path's claim and re-settle: when the rewrite brings the
        // claims back into agreement about the account name, the address stops being contested and
        // resolves again -- which is the whole point of holding claims rather than a flag.
        email_claims->second[record_path] = normalized_account_name;
        settle_email_claims(email);
    } else if (existing != g_entries.end() && !existing->second.quarantined
        && existing->second.record_path != record_path
        && existing->second.normalized_account_name != normalized_account_name) {
        // Three conditions, each load-bearing:
        //   - a differing record_path is what makes this two records rather than one being
        //     rewritten. A live write cannot fail it: final_path is composed purely from the email,
        //     so every rewrite (an account rename included) lands on the path already indexed.
        //   - a differing account name is the scan's own duplicate test; equal names are the
        //     ordinary flat-plus-directory pair, deduplicated by precedence, not a duplicate.
        //   - a quarantined incumbent is excluded: it holds no account name to compare and its email
        //     is deliberately reserved, so "could not be read" must stay the answer for that address
        //     rather than being reworded into a duplicate report.
        std::map<std::string, std::string> claims;
        claims[existing->second.record_path] = existing->second.normalized_account_name;
        claims[record_path] = normalized_account_name;
        g_contested_emails.emplace(email, std::move(claims));
    }

    // Directory-over-flat precedence: a legacy flat record must not displace a directory record (or
    // a quarantined directory record -- quarantine() leaves legacy_flat_layout at its default of
    // false) already indexed under the same email. Without this, whichever one readdir happens to
    // visit last would silently win.
    if (legacy_flat_layout && existing != g_entries.end() && !existing->second.legacy_flat_layout)
        return;

    // A disputed address is two records on disk, which only an operator can produce (a half-finished
    // migration, a restored backup). Before the index, the email lookup refused but the SEPARATE
    // character scan still resolved every character on both records. Withdrawing the incumbent's
    // claims here made its characters read as unlinked instead, and save_char then refuses the
    // legacy fallback for them -- silent save loss for the record that happened to be indexed first.
    // Keep both records' claims: the address itself still refuses, downstream, where paths resolve.
    const bool address_is_disputed = g_contested_emails.count(email) != 0;
    if (!address_is_disputed)
        erase_owned_keys(email);

    Entry entry;
    entry.normalized_email = email;
    entry.record_path = record_path;
    entry.normalized_account_name = normalized_account_name;
    entry.legacy_flat_layout = legacy_flat_layout;
    g_entries[email] = entry;

    // On an undisputed address erase_owned_keys above withdrew every claim this email held, so
    // claim_key sees this
    // record as a fresh claimant of exactly the keys the record declares NOW. A key that is still
    // held is held by a DIFFERENT email, and claim_key turns that into contention rather than
    // overwriting -- overwriting an account name would let write_account_file delete the other
    // record's file, and overwriting a character would send that character's saves to whichever
    // record was written last.
    if (!entry.normalized_account_name.empty()) {
        std::vector<std::string>& owned_names = g_owned_account_names[email];
        if (std::find(owned_names.begin(), owned_names.end(), entry.normalized_account_name) == owned_names.end()) {
            claim_key(g_account_names, g_contested_account_names, entry.normalized_account_name, email);
            owned_names.push_back(entry.normalized_account_name);
        }
    }

    std::vector<std::string> owned;
    if (address_is_disputed) {
        // Carry the incumbent's keys forward so a later erase still withdraws all of them.
        const auto previously_owned = g_owned_characters.find(email);
        if (previously_owned != g_owned_characters.end())
            owned = previously_owned->second;
    }
    owned.reserve(owned.size() + account.characters.size());
    for (const std::string& character_name : account.characters) {
        const std::string character_key = account::normalize_account_name(character_name);
        if (character_key.empty())
            continue;
        if (std::find(owned.begin(), owned.end(), character_key) != owned.end())
            continue;
        claim_key(g_characters, g_contested_characters, character_key, email);
        owned.push_back(character_key);
    }
    g_owned_characters[email] = std::move(owned);
}

void quarantine(const std::string& normalized_email, const std::string& record_path,
    const std::string& reason)
{
    // Deliberately NOT re-run through normalize_email: the caller's key is already final. For the
    // two email-shaped quarantine cases it is already normalize_email()'d (normalizing an
    // already-normalized email is a harmless no-op, so this is not a behavior change for them), but
    // for the two path-shaped cases (an unparsed or emailless legacy flat record, keyed by its own
    // record_path) the key is a case-sensitive filesystem path -- lowercasing it here previously
    // silently produced a key ("accounts/k-o/...") that never matched the record's real path
    // ("accounts/K-O/...", bucket letters are always uppercase), so the quarantine for that shape was
    // filed under a key nothing could ever look it up by, even after both callers agreed on the same
    // key rule.
    if (normalized_email.empty())
        return;

    // A readable record already indexed at this address keeps it. The address-shaped quarantine
    // exists to stop registration writing a fresh account over a record the game cannot reach
    // (db.cpp reserves the address a misfiled record DECLARES as well as the one it is filed
    // under) -- but when a second record on disk merely declares an address a healthy record
    // already holds, that address is not free to begin with. Overwriting the entry here withdrew
    // the healthy record's account-name and character claims too, so its owner could no longer log
    // in and its characters read as unlinked, until the next reboot happened to walk the two in the
    // other readdir order -- upsert already lets a readable record displace a quarantine entry, so
    // the outcome depended purely on which one the scan reached first. The misfiled record stays
    // quarantined under its own key, where the operator report still shows it.
    const auto incumbent = g_entries.find(normalized_email);
    if (incumbent != g_entries.end() && !incumbent->second.quarantined
        && incumbent->second.record_path != record_path) {
        // Not simply dropped. A record filed in the wrong BUCKET under the right directory name is
        // keyed by the same address as the healthy one, so returning here would leave it unindexed,
        // uncounted and absent from `account index` -- one boot log line the only trace of a file
        // the game cannot reach. File it under its own path instead, which is where the other
        // path-shaped quarantines live.
        //
        // Recurses exactly once: the path key can only hold a quarantined entry (nothing indexes a
        // readable record under a path), so the guard above cannot fire on the second call.
        if (record_path != normalized_email && !record_path.empty())
            quarantine(record_path, record_path, reason);
        return;
    }

    // Withdraws the record's account-name and character claims as well as dropping its keys, so
    // setting a duplicate record aside is itself a repair: the surviving claimant of a key the two
    // disputed goes back to resolving immediately.
    erase_owned_keys(normalized_email);
    // A quarantined record declares no usable account name, so it can no longer be one side of a
    // "these two records disagree about the name at this address" dispute. Its email stays occupied
    // by the entry below, and "could not be read" is the answer for that address from here on --
    // which must not be reworded into a duplicate report.
    g_contested_emails.erase(normalized_email);

    Entry entry;
    entry.normalized_email = normalized_email;
    entry.record_path = record_path;
    entry.quarantined = true;
    entry.quarantine_reason = reason;
    g_entries[normalized_email] = entry;
}

bool find_path_by_email(const std::string& email, std::string* record_path,
    std::string* error_message)
{
    const std::string normalized_email = account::normalize_email(email);
    if (g_contested_emails.count(normalized_email) != 0) {
        // Verbatim find_account_by_email_internal's duplicate text (account_management.cpp:1073).
        set_error(error_message, "Multiple account records exist for that email address.");
        return false;
    }

    const auto entry = g_entries.find(normalized_email);
    if (entry == g_entries.end()) {
        set_error(error_message, "No account exists for that email address.");
        return false;
    }
    if (entry->second.quarantined) {
        set_error(error_message, "That account record could not be read.");
        return false;
    }
    if (record_path != nullptr)
        *record_path = entry->second.record_path;
    set_error(error_message, "");
    return true;
}

bool find_path_by_account_name(const std::string& account_name, std::string* record_path,
    std::string* error_message)
{
    const std::string normalized_account_name = account::normalize_account_name(account_name);
    if (g_contested_account_names.count(normalized_account_name) != 0) {
        // Verbatim find_account_file_path_by_account_name's duplicate text (account_management.cpp).
        // Returning a path here would let write_account_file delete the other record's file.
        set_error(error_message, "Multiple account records exist for account '" + normalized_account_name + "'.");
        return false;
    }

    const auto name_entry = g_account_names.find(normalized_account_name);
    if (name_entry == g_account_names.end()) {
        // Verbatim the text find_account_file_path_by_account_name sets when its directory scan
        // finds no match (account_management.cpp). This function replaces that scan, so callers --
        // and the tests that assert on the message -- must not be able to tell which one answered.
        set_error(error_message, "Failed to open account file for account '" + normalized_account_name + "': " + std::strerror(ENOENT));
        return false;
    }
    // Note that this inherits find_path_by_email's refusal when the resolved email is claimed by two
    // records under different names -- with that function's message, not this one's. The name scan
    // would have answered (it matches on the name, which is unique in that configuration), so this
    // is a deliberate divergence in the refusing direction: only one of the two records is indexed
    // under the email, so a path returned here could well be the other record's, and both callers
    // that matter treat a refusal as "no existing file" (write_account_file then retires nothing,
    // create_account then proceeds exactly as it does after the scan's own duplicate-email refusal).
    return find_path_by_email(name_entry->second, record_path, error_message);
}

bool find_owner_email_by_character(const std::string& character_name, std::string* owner_email,
    std::string* error_message, std::string* owner_account_name)
{
    const std::string character_key = account::normalize_account_name(character_name);
    if (g_contested_characters.count(character_key) != 0) {
        // Verbatim find_character_owner_account's duplicate text (account_management.cpp:979).
        // Setting a message is what makes this the ERROR form for the caller: the owner resolver
        // (account_management_identity.cpp:937-949) reads an EMPTY message as "resolved, this
        // character is linked to no account", which save_char would act on by treating the character
        // as unlinked -- the exact silent mis-save this refusal exists to prevent.
        set_error(error_message, "Multiple account records claim that linked character.");
        return false;
    }

    const auto character_entry = g_characters.find(character_key);
    if (character_entry == g_characters.end()) {
        set_error(error_message, "");
        return false;
    }

    const auto entry = g_entries.find(character_entry->second);
    if (entry == g_entries.end() || entry->second.quarantined) {
        set_error(error_message, "That account record could not be read.");
        return false;
    }

    if (owner_email != nullptr)
        *owner_email = character_entry->second;
    // The owner's account name comes straight out of the entry rather than out of a re-read of the
    // record: deserialize_account_from_json normalizes account_name with the same
    // normalize_account_name() upsert applied (account_management_storage.cpp:151), so the two are
    // byte-identical strings.
    if (owner_account_name != nullptr)
        *owner_account_name = entry->second.normalized_account_name;
    set_error(error_message, "");
    return true;
}

bool find_email_by_account_name(const std::string& account_name, std::string* email,
    std::string* error_message)
{
    const std::string normalized_account_name = account::normalize_account_name(account_name);
    if (g_contested_account_names.count(normalized_account_name) != 0) {
        set_error(error_message, "Multiple account records exist for account '" + normalized_account_name + "'.");
        return false;
    }

    const auto name_entry = g_account_names.find(normalized_account_name);
    if (name_entry == g_account_names.end()) {
        // Deliberately NOT the scan text used by find_path_by_account_name above: the scan this one
        // replaces (resolve_account_storage_key) reports nothing at all, it just returns "". Its
        // only caller passes a null error_message, so this string is never shown to a player.
        set_error(error_message, "No account exists with that name.");
        return false;
    }

    const auto entry = g_entries.find(name_entry->second);
    if (entry == g_entries.end() || entry->second.quarantined) {
        set_error(error_message, "That account record could not be read.");
        return false;
    }

    if (email != nullptr)
        *email = name_entry->second;
    set_error(error_message, "");
    return true;
}

bool is_contested_email(const std::string& email)
{
    return g_contested_emails.count(account::normalize_email(email)) != 0;
}

bool is_quarantined(const std::string& email)
{
    return is_quarantined_record_key(account::normalize_email(email));
}

bool is_quarantined_record_key(const std::string& record_key)
{
    const auto entry = g_entries.find(record_key);
    return entry != g_entries.end() && entry->second.quarantined;
}

std::vector<Entry> quarantined_entries()
{
    std::vector<Entry> quarantined;
    for (const auto& entry : g_entries) {
        if (entry.second.quarantined)
            quarantined.push_back(entry.second);
    }
    return quarantined;
}

std::size_t quarantined_count()
{
    std::size_t count = 0;
    for (const auto& entry : g_entries) {
        if (entry.second.quarantined)
            ++count;
    }
    return count;
}

bool note_unreadable_at_runtime(const std::string& record_key, const std::string& reason)
{
    // No normalization here, for the same reason quarantine() does none: callers key a record by
    // account_index_quarantine_key()'s result, which is already normalized for the email-shaped
    // cases and is a case-sensitive path for the others.
    const auto entry = g_entries.find(record_key);
    if (entry == g_entries.end())
        return false;

    // Already known bad. A quarantined record was refused at boot and is listed as such; saying it
    // again under a second heading would double-count it in the wizard output.
    if (entry->second.quarantined)
        return false;

    // Idempotent: every write to an account comes through the chokepoint that calls this, so a
    // record that stays broken reaches this line once per write. Only the first arrival is news.
    if (entry->second.unreadable_at_runtime)
        return false;

    entry->second.unreadable_at_runtime = true;
    entry->second.unreadable_at_runtime_reason = reason;
    return true;
}

bool clear_unreadable_at_runtime(const std::string& record_key)
{
    // No normalization, matching note_unreadable_at_runtime: callers key a record the same way it
    // was keyed when the mark was set.
    const auto entry = g_entries.find(record_key);
    if (entry == g_entries.end() || !entry->second.unreadable_at_runtime)
        return false;

    entry->second.unreadable_at_runtime = false;
    entry->second.unreadable_at_runtime_reason.clear();
    return true;
}

std::vector<Entry> unreadable_at_runtime_entries()
{
    std::vector<Entry> unreadable;
    for (const auto& entry : g_entries) {
        if (entry.second.unreadable_at_runtime && !entry.second.quarantined)
            unreadable.push_back(entry.second);
    }
    return unreadable;
}

std::size_t unreadable_at_runtime_count()
{
    std::size_t count = 0;
    for (const auto& entry : g_entries) {
        if (entry.second.unreadable_at_runtime && !entry.second.quarantined)
            ++count;
    }
    return count;
}

std::size_t size()
{
    return g_entries.size();
}

bool is_account_name_ambiguous(const std::string& account_name)
{
    return g_contested_account_names.count(account::normalize_account_name(account_name)) != 0;
}

bool is_character_ambiguous(const std::string& character_name)
{
    return g_contested_characters.count(account::normalize_account_name(character_name)) != 0;
}

bool is_email_ambiguous(const std::string& email)
{
    return g_contested_emails.count(account::normalize_email(email)) != 0;
}

std::vector<ContestedKey> contested_keys()
{
    std::vector<ContestedKey> contested;

    for (const auto& claims : g_contested_characters) {
        ContestedKey key;
        key.kind = "character";
        key.key = claims.first;
        key.claimants.assign(claims.second.begin(), claims.second.end());
        contested.push_back(std::move(key));
    }

    for (const auto& claims : g_contested_account_names) {
        ContestedKey key;
        key.kind = "account name";
        key.key = claims.first;
        key.claimants.assign(claims.second.begin(), claims.second.end());
        contested.push_back(std::move(key));
    }

    for (const auto& claims : g_contested_emails) {
        ContestedKey key;
        key.kind = "email";
        key.key = claims.first;
        for (const auto& claim : claims.second)
            key.claimants.push_back(claim.first);
        contested.push_back(std::move(key));
    }

    // The maps are unordered, so without this the listing would shuffle between calls and an
    // immortal comparing two runs could not tell a changed state from a reordered one.
    std::sort(contested.begin(), contested.end(), [](const ContestedKey& left, const ContestedKey& right) {
        if (left.kind != right.kind)
            return left.kind < right.kind;
        return left.key < right.key;
    });
    return contested;
}

void clear()
{
    g_owned_account_names.clear();
    g_entries.clear();
    g_account_names.clear();
    g_characters.clear();
    g_owned_characters.clear();
    g_contested_account_names.clear();
    g_contested_characters.clear();
    g_contested_emails.clear();
    g_root_directory = ".";
}

void set_root_directory(const std::string& root_directory)
{
    g_root_directory = root_directory;
}

const std::string& root_directory()
{
    return g_root_directory;
}

bool matches_root(const std::string& root_directory)
{
    return root_directory == g_root_directory;
}

void set_enabled(bool enabled)
{
    g_enabled = enabled;
}

bool is_enabled()
{
    return g_enabled;
}

namespace {

} // namespace

} // namespace account_index
