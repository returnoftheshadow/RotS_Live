// Internal account-management helpers shared across split public fragments.
namespace {
    CharacterLinkReference* find_character_link_reference(AccountData* account, const std::string& character_name)
    {
        if (account == nullptr)
            return nullptr;

        const std::string normalized_character_name = normalize_account_name(character_name);
        for (CharacterLinkReference& link : account->character_links) {
            if (normalize_account_name(link.character_name) == normalized_character_name)
                return &link;
        }

        return nullptr;
    }

    const CharacterLinkReference* find_character_link_reference(const AccountData& account, const std::string& character_name)
    {
        const std::string normalized_character_name = normalize_account_name(character_name);
        for (const CharacterLinkReference& link : account.character_links) {
            if (normalize_account_name(link.character_name) == normalized_character_name)
                return &link;
        }

        return nullptr;
    }

    std::string resolved_character_path(const AccountData& account, const std::string& root_directory, const std::string& character_name)
    {
        return account_character_player_path(root_directory, account.account_name, character_name);
    }

    std::string resolved_object_path(const AccountData& account, const std::string& root_directory, const std::string& character_name)
    {
        const CharacterLinkReference* link = find_character_link_reference(account, character_name);
        if (link != nullptr && !link->object_path.empty())
            return account_character_directory(root_directory, account.account_name, character_name) + "/" + link->object_path;
        return account_character_object_path(root_directory, account.account_name, character_name);
    }

    std::string resolved_exploits_path(const AccountData& account, const std::string& root_directory, const std::string& character_name)
    {
        const CharacterLinkReference* link = find_character_link_reference(account, character_name);
        if (link != nullptr && !link->exploits_path.empty())
            return account_character_directory(root_directory, account.account_name, character_name) + "/" + link->exploits_path;
        return account_character_exploits_path(root_directory, account.account_name, character_name);
    }

    std::string safe_relative_object_path_or_empty(const std::string& object_path, const std::string& expected_basename)
    {
        if (object_path.empty())
            return "";
        if (object_path[0] == '/' || object_path.find('\\') != std::string::npos)
            return "";

        size_t segment_start = 0;
        while (segment_start <= object_path.size()) {
            const size_t slash = object_path.find('/', segment_start);
            const size_t segment_end = (slash == std::string::npos) ? object_path.size() : slash;
            const std::string segment = object_path.substr(segment_start, segment_end - segment_start);
            if (segment.empty() || segment == "." || segment == "..")
                return "";
            if (slash == std::string::npos)
                break;
            segment_start = slash + 1;
        }

        const size_t last_slash = object_path.find_last_of('/');
        const std::string basename = (last_slash == std::string::npos) ? object_path : object_path.substr(last_slash + 1);
        if (basename != expected_basename)
            return "";

        return object_path;
    }

    bool validate_account_owned_object_path(const AccountData& account, const std::string& character_name, std::string* error_message)
    {
        const CharacterLinkReference* link = find_character_link_reference(account, character_name);
        if (link == nullptr || link->object_path.empty()) {
            set_error(error_message, "");
            return true;
        }

        const std::string expected_path = objects_json_file_name(character_name);
        if (safe_relative_object_path_or_empty(link->object_path, expected_path).empty()) {
            set_error(error_message, "Stored object path did not match the expected account-owned object filename.");
            return false;
        }

        set_error(error_message, "");
        return true;
    }

    bool validate_account_owned_character_path(const AccountData& account, const std::string& character_name, std::string* error_message)
    {
        const CharacterLinkReference* link = find_character_link_reference(account, character_name);
        if (link == nullptr || link->character_path.empty()) {
            set_error(error_message, "");
            return true;
        }

        const std::string expected_path = character_json_file_name(character_name);
        if (link->character_path != expected_path) {
            set_error(error_message, "Stored character path did not match the expected account-owned character filename.");
            return false;
        }

        set_error(error_message, "");
        return true;
    }

    bool validate_account_owned_exploits_path(const AccountData& account, const std::string& character_name, std::string* error_message)
    {
        const CharacterLinkReference* link = find_character_link_reference(account, character_name);
        if (link == nullptr || link->exploits_path.empty()) {
            set_error(error_message, "");
            return true;
        }

        const std::string expected_path = exploits_json_file_name(character_name);
        if (link->exploits_path != expected_path) {
            set_error(error_message, "Stored exploits path did not match the expected account-owned exploits filename.");
            return false;
        }

        set_error(error_message, "");
        return true;
    }

    void populate_default_character_link_paths(CharacterLinkReference* link)
    {
        if (link == nullptr)
            return;

        const std::string normalized_character_name = normalize_account_name(link->character_name);
        if (normalized_character_name.empty())
            return;

        if (link->character_path.empty())
            link->character_path = character_json_file_name(normalized_character_name);
        if (link->object_path.empty())
            link->object_path = objects_json_file_name(normalized_character_name);
        if (link->exploits_path.empty())
            link->exploits_path = exploits_json_file_name(normalized_character_name);
    }

    void sync_character_links_from_characters(AccountData* account)
    {
        if (account == nullptr)
            return;

        for (CharacterLinkReference& link : account->character_links) {
            link.character_name = normalize_account_name(link.character_name);
            populate_default_character_link_paths(&link);
        }

        for (const std::string& character_name : account->characters) {
            if (find_character_link_reference(account, character_name) != nullptr)
                continue;

            CharacterLinkReference link;
            link.character_name = normalize_account_name(character_name);
            populate_default_character_link_paths(&link);
            account->character_links.push_back(link);
        }
    }

    bool write_character_migration_snapshot(const std::string& root_directory, const CharacterMigrationData& snapshot_data, CharacterMigrationData* migration, std::string* error_message)
    {
        const std::string account_storage_key = resolve_account_storage_key(root_directory, snapshot_data.account_name);
        const std::string account_root = root_directory + "/accounts";
        const std::string bucket_directory = account_root + "/" + account_bucket_for_name(account_storage_key);
        const std::string account_directory = account_character_directory(root_directory, snapshot_data.account_name, snapshot_data.character_name);
        const std::string final_path = account_character_snapshot_path(root_directory, snapshot_data.account_name, snapshot_data.character_name);
        const std::string temp_path = final_path + ".tmp";

        if (!create_directory_if_missing(account_root, error_message))
            return false;
        if (!create_directory_if_missing(bucket_directory, error_message))
            return false;
        if (!create_directory_if_missing(account_directory, error_message))
            return false;

        FILE* file = open_secure_output_file(temp_path, error_message);
        if (file == nullptr)
            return false;

        const std::string json = serialize_character_migration_to_json(snapshot_data);
        const size_t written_length = std::fwrite(json.data(), sizeof(char), json.size(), file);
        const int close_result = std::fclose(file);
        if (written_length != json.size() || close_result != 0) {
            std::remove(temp_path.c_str());
            set_error(error_message, "Failed to write temporary migration file '" + temp_path + "'.");
            return false;
        }

        if (std::rename(temp_path.c_str(), final_path.c_str()) != 0) {
            std::remove(temp_path.c_str());
            set_error(error_message, "Failed to move temporary migration file into place: " + std::string(std::strerror(errno)));
            return false;
        }

        if (migration)
            *migration = snapshot_data;

        set_error(error_message, "");
        return true;
    }

    bool retire_previous_account_object_path(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const std::string& previous_object_path, std::string* error_message)
    {
        const std::string expected_object_path = objects_json_file_name(character_name);
        if (previous_object_path.empty() || previous_object_path == expected_object_path) {
            set_error(error_message, "");
            return true;
        }

        const std::string legacy_safe_path = safe_relative_object_path_or_empty(previous_object_path, expected_object_path);
        if (legacy_safe_path.empty()) {
            set_error(error_message, "");
            return true;
        }

        const std::string final_path = account_character_object_path(root_directory, account_name, character_name);
        const std::string prior_path = account_character_directory(root_directory, account_name, character_name) + "/" + legacy_safe_path;
        if (prior_path != final_path && std::remove(prior_path.c_str()) != 0 && errno != ENOENT) {
            set_error(error_message, "Failed to retire legacy account-owned object file '" + prior_path + "': " + std::strerror(errno));
            return false;
        }

        set_error(error_message, "");
        return true;
    }

    bool prepare_account_object_file_destination(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* final_path, std::string* previous_object_path, std::string* error_message)
    {
        if (final_path == nullptr || previous_object_path == nullptr) {
            set_error(error_message, "Object-file destination outputs must not be null.");
            return false;
        }

        if (!validate_identifier_for_path(account_name, "Account name", error_message))
            return false;
        if (!validate_identifier_for_path(character_name, "Character name", error_message))
            return false;

        AccountData account;
        if (!read_account_file(root_directory, account_name, &account, error_message))
            return false;
        if (!validate_account_owned_object_path(account, character_name, error_message))
            return false;

        CharacterLinkReference* link = find_character_link_reference(&account, character_name);
        *previous_object_path = "";
        const std::string expected_object_path = objects_json_file_name(character_name);
        if (link != nullptr) {
            *previous_object_path = link->object_path;
        }

        const std::string account_directory = account_character_directory(root_directory, account_name, character_name);
        if (!create_directory_if_missing(root_directory + "/accounts", error_message))
            return false;
        if (!create_directory_if_missing(root_directory + "/accounts/" + account_bucket_for_name(resolve_account_storage_key(root_directory, account_name)), error_message))
            return false;
        if (!create_directory_if_missing(account_directory, error_message))
            return false;

        *final_path = account_character_object_path(root_directory, account_name, character_name);
        set_error(error_message, "");
        return true;
    }

    bool normalize_account_object_path_after_successful_write(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const std::string& previous_object_path, std::string* error_message)
    {
        const std::string expected_object_path = objects_json_file_name(character_name);
        if (previous_object_path.empty() || previous_object_path == expected_object_path) {
            set_error(error_message, "");
            return true;
        }

        AccountData account;
        if (!read_account_file(root_directory, account_name, &account, error_message))
            return false;
        if (!validate_account_owned_object_path(account, character_name, error_message))
            return false;

        CharacterLinkReference* link = find_character_link_reference(&account, character_name);
        if (link == nullptr) {
            set_error(error_message, "Character '" + character_name + "' is not linked to account '" + account_name + "'.");
            return false;
        }

        link->object_path = expected_object_path;
        if (!write_account_file(root_directory, account, error_message))
            return false;

        set_error(error_message, "");
        return true;
    }

    bool write_account_object_json_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const objects_json::ObjectSaveData& object_data, std::string* error_message)
    {
        std::string final_path;
        std::string previous_object_path;
        if (!prepare_account_object_file_destination(root_directory, account_name, character_name, &final_path, &previous_object_path, error_message))
            return false;

        if (!write_text_file_atomically(final_path, objects_json::serialize_objects_to_json(object_data), error_message))
            return false;

        if (!normalize_account_object_path_after_successful_write(root_directory, account_name, character_name, previous_object_path, error_message))
            return false;

        if (!retire_previous_account_object_path(root_directory, account_name, character_name, previous_object_path, error_message))
            return false;

        set_error(error_message, "");
        return true;
    }

    bool hydrate_account_native_object_file_from_migration(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const CharacterMigrationData& snapshot_data, std::string* error_message)
    {
        if (!snapshot_data.object_file.present)
            return write_default_account_object_file(root_directory, account_name, character_name, error_message);

        std::string object_bytes;
        if (!decode_snapshot_content(snapshot_data.object_file, &object_bytes, error_message))
            return false;

        objects_json::ObjectSaveData object_data;
        if (!objects_json::object_save_data_from_binary(object_bytes, &object_data, error_message))
            return false;

        return write_account_object_json_file(root_directory, account_name, character_name, object_data, error_message);
    }

    bool hydrate_account_native_character_file_from_migration(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const CharacterMigrationData& snapshot_data, std::string* error_message)
    {
        std::string player_text;
        if (!decode_snapshot_content(snapshot_data.player_file, &player_text, error_message))
            return false;

        ensure_player_index_entry(character_name.c_str());

        char normalized_name[MAX_INPUT_LENGTH];
        std::snprintf(normalized_name, sizeof(normalized_name), "%s", character_name.c_str());

        char_file_u stored_character {};
        if (load_char_from_text(normalized_name, player_text.c_str(), &stored_character) < 0) {
            set_error(error_message, "Legacy player data for '" + character_name + "' could not be converted into account-native character storage.");
            return false;
        }

        return write_account_character_file(root_directory, account_name, stored_character, error_message);
    }

    bool write_account_exploits_json_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const exploits_json::ExploitHistoryData& exploit_history, std::string* error_message)
    {
        if (!validate_identifier_for_path(account_name, "Account name", error_message))
            return false;
        if (!validate_identifier_for_path(character_name, "Character name", error_message))
            return false;

        AccountData account;
        if (!read_account_file(root_directory, account_name, &account, error_message))
            return false;
        if (!validate_account_owned_exploits_path(account, character_name, error_message))
            return false;

        const std::string account_directory = account_character_directory(root_directory, account_name, character_name);
        if (!create_directory_if_missing(root_directory + "/accounts", error_message))
            return false;
        if (!create_directory_if_missing(root_directory + "/accounts/" + account_bucket_for_name(resolve_account_storage_key(root_directory, account_name)), error_message))
            return false;
        if (!create_directory_if_missing(account_directory, error_message))
            return false;

        return write_text_file_atomically(resolved_exploits_path(account, root_directory, character_name), exploits_json::serialize_exploits_to_json(exploit_history), error_message);
    }

    bool hydrate_account_native_exploit_file_from_migration(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const CharacterMigrationData& snapshot_data, std::string* error_message)
    {
        if (!snapshot_data.exploits_file.present)
            return write_default_account_exploit_file(root_directory, account_name, character_name, error_message);

        std::string exploit_bytes;
        if (!decode_snapshot_content(snapshot_data.exploits_file, &exploit_bytes, error_message))
            return false;

        std::vector<exploit_record> records;
        if (!exploits_json::exploit_records_from_binary(exploit_bytes, &records, error_message))
            return false;

        exploits_json::ExploitHistoryData exploit_history;
        exploit_history.records = std::move(records);
        return write_account_exploits_json_file(root_directory, account_name, character_name, exploit_history, error_message);
    }

    void append_restore_failure(std::string* error_message, const std::string& restore_error)
    {
        if (restore_error.empty())
            return;

        if (error_message == nullptr)
            return;

        if (error_message->empty())
            *error_message = restore_error;
        else
            *error_message += " Restore also failed: " + restore_error;
    }

    void restore_retired_legacy_files(const std::string& player_file_path, const std::string& object_file_path, const std::string& exploits_file_path, const CharacterMigrationData& snapshot_data, bool restore_player, bool restore_object, bool restore_exploits, std::string* error_message)
    {
        if (restore_player) {
            std::string restore_error;
            if (!write_snapshot_bytes(player_file_path, snapshot_data.player_file, true, &restore_error))
                append_restore_failure(error_message, restore_error);
        }

        if (restore_object) {
            std::string restore_error;
            if (!write_snapshot_bytes(object_file_path, snapshot_data.object_file, false, &restore_error))
                append_restore_failure(error_message, restore_error);
        }

        if (restore_exploits) {
            std::string restore_error;
            if (!write_snapshot_bytes(exploits_file_path, snapshot_data.exploits_file, false, &restore_error))
                append_restore_failure(error_message, restore_error);
        }
    }

    bool retire_legacy_character_files_after_migration(const std::string& player_file_path, const std::string& stale_flat_player_file_path, const std::string& object_file_path, const std::string& exploits_file_path, const CharacterMigrationData& snapshot_data, std::string* error_message)
    {
        const bool had_player_file = path_exists(player_file_path);
        const bool had_object_file = path_exists(object_file_path);
        bool retired_player = false;
        bool retired_object = false;

        if (std::remove(player_file_path.c_str()) != 0 && errno != ENOENT) {
            set_error(error_message, "Failed to retire legacy player file '" + player_file_path + "': " + std::strerror(errno));
            return false;
        }
        retired_player = had_player_file;

        if (std::remove(object_file_path.c_str()) != 0 && errno != ENOENT) {
            set_error(error_message, "Failed to retire legacy object file '" + object_file_path + "': " + std::strerror(errno));
            restore_retired_legacy_files(player_file_path, object_file_path, exploits_file_path, snapshot_data, retired_player, false, false, error_message);
            return false;
        }
        retired_object = had_object_file;

        if (std::remove(exploits_file_path.c_str()) != 0 && errno != ENOENT) {
            set_error(error_message, "Failed to retire legacy exploit file '" + exploits_file_path + "': " + std::strerror(errno));
            restore_retired_legacy_files(player_file_path, object_file_path, exploits_file_path, snapshot_data, retired_player, retired_object, false, error_message);
            return false;
        }

        if (!stale_flat_player_file_path.empty() && std::remove(stale_flat_player_file_path.c_str()) != 0 && errno != ENOENT) {
            set_error(error_message, "Failed to retire stale legacy player file '" + stale_flat_player_file_path + "': " + std::strerror(errno));
            restore_retired_legacy_files(player_file_path, object_file_path, exploits_file_path, snapshot_data, retired_player, retired_object, true, error_message);
            return false;
        }

        set_error(error_message, "");
        return true;
    }

    void cleanup_account_native_migration_outputs(const std::string& root_directory, const std::string& account_name, const std::string& character_name)
    {
        std::remove(account_character_player_path(root_directory, account_name, character_name).c_str());
        std::remove(account_character_object_path(root_directory, account_name, character_name).c_str());
        std::remove(account_character_exploits_path(root_directory, account_name, character_name).c_str());
        std::remove(account_character_snapshot_path(root_directory, account_name, character_name).c_str());
    }

    bool migrate_legacy_character_files_internal(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const std::string& player_file_path, const std::string& stale_flat_player_file_path, const std::string& object_file_path, const std::string& exploits_file_path, long migrated_at, CharacterMigrationData* migration, std::string* error_message)
    {
        CharacterMigrationData snapshot_data;
        snapshot_data.account_name = normalize_account_name(account_name);
        snapshot_data.character_name = normalize_account_name(character_name);
        snapshot_data.migrated_at = migrated_at;

        if (!read_file_bytes(player_file_path, true, &snapshot_data.player_file, error_message))
            return false;
        if (!read_file_bytes(object_file_path, false, &snapshot_data.object_file, error_message))
            return false;
        if (!read_file_bytes(exploits_file_path, false, &snapshot_data.exploits_file, error_message))
            return false;
        if (!hydrate_account_native_character_file_from_migration(root_directory, account_name, character_name, snapshot_data, error_message))
            return false;
        if (!hydrate_account_native_object_file_from_migration(root_directory, account_name, character_name, snapshot_data, error_message)) {
            cleanup_account_native_migration_outputs(root_directory, account_name, character_name);
            return false;
        }
        if (!hydrate_account_native_exploit_file_from_migration(root_directory, account_name, character_name, snapshot_data, error_message)) {
            cleanup_account_native_migration_outputs(root_directory, account_name, character_name);
            return false;
        }
        if (!write_character_migration_snapshot(root_directory, snapshot_data, migration, error_message)) {
            cleanup_account_native_migration_outputs(root_directory, account_name, character_name);
            return false;
        }

        if (!retire_legacy_character_files_after_migration(player_file_path, stale_flat_player_file_path, object_file_path, exploits_file_path, snapshot_data, error_message)) {
            cleanup_account_native_migration_outputs(root_directory, account_name, character_name);
            return false;
        }

        return true;
    }

} // namespace
