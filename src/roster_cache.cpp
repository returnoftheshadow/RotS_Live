#include "roster_cache.h"

#include "account_management.h"

#include <algorithm>
#include <string>
#include <unordered_map>

namespace roster_cache {

namespace {

    std::unordered_map<std::string, RosterSummary> g_summaries;

    // Unit separator: cannot appear in a filesystem path component, so (root, name) pairs never
    // collide or bleed across roots. Same rationale as account_cache's key.
    const char kKeySeparator = '\x1f';

    std::string compose_key(const std::string& root_directory, const std::string& character_name)
    {
        std::string key;
        key.reserve(root_directory.size() + 1 + character_name.size());
        key.append(root_directory);
        key.push_back(kKeySeparator);
        key.append(account::normalize_account_name(character_name));
        return key;
    }

    bool g_enabled = false;

    bool default_reader(const std::string& root_directory, const std::string& account_name,
        const std::string& character_name, char_file_u* stored_character, std::string* error_message)
    {
        return account::read_account_character_file(
            root_directory, account_name, character_name, stored_character, error_message);
    }

    ReaderFn g_reader = &default_reader;

    short clamp_coefficient(int value)
    {
        // Domain of square_root[] is 0..170 (consts.cpp). Clamp so a corrupt file cannot produce a
        // wild sort key or, later, an out-of-bounds index.
        if (value < 0)
            return 0;
        if (value > 170)
            return 170;
        return static_cast<short>(value);
    }

    RosterSummary summarize(const char_file_u& stored_character)
    {
        RosterSummary summary;
        summary.readable = true;
        summary.level = static_cast<unsigned char>(stored_character.level);
        summary.race = static_cast<unsigned char>(stored_character.race);
        for (int profession = 0; profession <= MAX_PROFS; ++profession)
            summary.prof_coof[profession] = clamp_coefficient(stored_character.profs.prof_coof[profession]);
        return summary;
    }

} // namespace

bool get(const std::string& root_directory, const std::string& account_name,
    const std::string& character_name, RosterSummary* summary)
{
    if (summary == nullptr)
        return false;

    const std::string key = compose_key(root_directory, character_name);
    if (g_enabled) {
        const auto cached_entry = g_summaries.find(key);
        if (cached_entry != g_summaries.end()) {
            *summary = cached_entry->second;
            return true;
        }
    }

    char_file_u stored_character {};
    std::string read_error;
    RosterSummary loaded;
    if (g_reader(root_directory, account_name, character_name, &stored_character, &read_error))
        loaded = summarize(stored_character);
    // else: loaded keeps its defaults, readable == false -- the "[ ?? ???]" row.

    if (g_enabled)
        g_summaries[key] = loaded;

    *summary = loaded;
    return true;
}

void invalidate_character(const std::string& root_directory, const std::string& character_name)
{
    g_summaries.erase(compose_key(root_directory, character_name));
}

void clear()
{
    g_summaries.clear();
}

void set_enabled(bool enabled)
{
    g_enabled = enabled;
}

bool is_enabled()
{
    return g_enabled;
}

void set_backing_reader_for_testing(ReaderFn reader)
{
    g_reader = (reader == nullptr) ? &default_reader : reader;
}

} // namespace roster_cache
