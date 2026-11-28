#ifndef ACCOUNT_ERRORS_H
#define ACCOUNT_ERRORS_H

#include <cstddef>
#include <ctime>
#include <string>
#include <vector>

// Account and character failures this boot, kept so an immortal can ask about them afterwards.
//
// Every one of these already reached the log at the moment it happened, and the log is where the
// detail lives. What was missing is retrieval: a player reports "my character is gone" or "it would
// not convert" hours later, and the lines have scrolled away. This holds the last
// MAX_RECORDED_ERRORS of them in memory -- fixed size, nothing allocated until something fails, and
// no disk touched at any point.
namespace account_errors {

static constexpr std::size_t MAX_RECORDED_ERRORS = 100;

// Bounds one stored entry so a long path or reader message cannot make the ring grow without limit.
static constexpr std::size_t MAX_RECORDED_FIELD_LENGTH = 240;

enum class Source {
    Boot, // the boot walk could not read an account-native character file, or quarantined an account record
    Migration, // a legacy character would not convert into account storage
    Save, // a character save was refused rather than written
};

struct Entry {
    std::time_t when = 0; // when it last happened
    // How many times this exact failure has been recorded. A refused save recurs on every autosave,
    // so appending each one would overwrite the whole ring with duplicates within the hour.
    std::size_t occurrences = 1;
    Source source = Source::Boot;
    std::string account;
    std::string character;
    std::string actor; // the immortal who asked for it, when a person did
    std::string reason;
};

// Records one failure AND announces it, so the two can never disagree: an
// "ACCTERR <source> acct=<account> char=<character>: <reason>" line to the log and the mudlog.
// The fixed field order is what makes `acct=<x> char=<y>` a precise grep for one character on one
// account. Either name may be empty; it renders as "?" rather than vanishing, so the shape of the
// line never changes.
// `actor` names the immortal who asked for the operation, when a person did; it renders as an
// extra " by=<actor>" field and is omitted entirely otherwise, so the acct=/char= prefix a grep
// keys on stays the same either way.
void record(Source source, const std::string& account, const std::string& character,
    const std::string& reason, const std::string& actor = std::string());

// Newest first, at most `limit` entries.
std::vector<Entry> recent(std::size_t limit);

std::size_t size();
void clear();

const char* source_name(Source source);

} // namespace account_errors

#endif
