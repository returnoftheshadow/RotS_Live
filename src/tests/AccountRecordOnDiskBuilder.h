#ifndef ROTS_TESTS_ACCOUNT_RECORD_ON_DISK_BUILDER_H
#define ROTS_TESTS_ACCOUNT_RECORD_ON_DISK_BUILDER_H

// Plants an account record whose address the validation layer refuses.
//
// MAX_EMAIL_LENGTH bounds an address at the one place every address rule lives, so
// account::create_account can no longer produce a record whose account-native path overruns
// player_index_element::ch_file. The guards against such a path are still worth having and still
// need testing: a record restored or edited by hand on disk is the one route that can still put one
// there, and it is the route an operator actually takes. So these helpers write the record the way
// that route does -- with the real serializers, straight to disk -- instead of registering an
// address no player could type.
//
// Nothing here goes near create_account, write_account_file or write_account_character_file: all
// three validate, and all three resolve the account name back through the index, which has no
// entry for a record planted behind its back.

#include "../account_management.h"
#include "../account_management_identity.h"
#include "../account_management_storage.h"
#include "../account_management_types.h"
#include "../character_json.h"
#include "../structs.h"

#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace rots_tests {

inline void make_directory_for_planted_record(const std::string& path)
{
    mkdir(path.c_str(), 0700);
}

inline void write_planted_file(const std::string& path, const std::string& contents)
{
    FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr)
        return;
    std::fwrite(contents.data(), sizeof(char), contents.size(), file);
    std::fclose(file);
}

// The directory the record lands in: "<root>/accounts/<bucket>/<email>". Composed exactly as the
// server composes it, because the boot walk refuses a record that is not at the path its own
// address resolves to.
inline std::string planted_account_directory(const std::string& root_directory, const std::string& email)
{
    const std::string normalized_email = account::normalize_email(email);
    return root_directory + "/accounts/" + account::account_bucket_for_name(normalized_email) + "/" + normalized_email;
}

// Writes "<root>/accounts/<bucket>/<email>/account.json" for an address create_account would
// refuse, listing `character_names`. Returns the account directory.
inline std::string plant_account_record_with_unvalidated_email(const std::string& root_directory,
    const std::string& account_name, const std::string& email,
    const std::vector<std::string>& character_names, long timestamp = 1700010101)
{
    // Built through initialize_new_account with a placeholder address so the password credentials,
    // schema version and timestamps are the real ones, then the address is swapped for the one
    // under test. add_character_to_account fills in the link paths the same way a link does.
    account::AccountData account;
    std::string error_message;
    account::initialize_new_account(account_name, "placeholder@example.com", "ValidPass1", timestamp, &account, &error_message);
    account.normalized_email = account::normalize_email(email);
    for (const std::string& character_name : character_names)
        account::add_character_to_account(&account, character_name, &error_message);

    const std::string account_directory = planted_account_directory(root_directory, email);
    make_directory_for_planted_record(root_directory + "/accounts");
    make_directory_for_planted_record(root_directory + "/accounts/" + account::account_bucket_for_name(account.normalized_email));
    make_directory_for_planted_record(account_directory);
    write_planted_file(account_directory + "/account.json", account::serialize_account_to_json(account));
    return account_directory;
}

// The character file beside a planted record. Written with the real character serializer, so what
// lands on disk is what a save would have written.
inline void plant_account_character_file(const std::string& account_directory, const char_file_u& stored_character)
{
    const character_json::CharacterData character_data = character_json::character_data_from_store(stored_character);
    // The same slug the server files the asset under -- normalize_account_name, not a hand-rolled
    // lowercase -- so a planted file is named the way a written one would be.
    write_planted_file(account_directory + "/" + account::normalize_account_name(stored_character.name) + ".character.json",
        character_json::serialize_character_to_json(character_data));
}

} // namespace rots_tests

#endif
