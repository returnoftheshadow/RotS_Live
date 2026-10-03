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

    // The account directory for a record we are already holding. account_character_directory() takes
    // an account NAME and resolves it back to the record's email (a read, or an index lookup) purely
    // to learn a string this record already carries; that round trip is what made the boot walker
    // resolve through the index while the index was still being built. The result is identical:
    // resolve_account_storage_key returns normalize_email() of the record it reads, which is exactly
    // what is used here, and the "" case maps to the same __invalid_account__ path.
    std::string account_record_directory(const std::string& root_directory, const AccountData& account)
    {
        const std::string account_storage_key = normalize_email(account.normalized_email);
        if (account_storage_key.empty())
            return root_directory + "/accounts/" + std::string(kInvalidAccountDirectoryName);
        return account_directory_path_from_email(root_directory, account_storage_key);
    }

    // Every write that composes a destination from an account's storage key funnels through this
    // before it creates a directory or a file. Returning TRUE means "refuse, error_message is set".
    //
    // Writing into the invalid-account sentinel is worse than writing nothing: the file lands
    // somewhere nothing enumerates, the caller is told it succeeded, the real file goes stale, and
    // the next boot does not see either. Refusing costs a save; writing there costs every save from
    // that point on, silently. This is the same trade the adjacent "refusing legacy fallback for
    // account-native character" branch in save_char (db.cpp) already accepts.
    bool refuse_invalid_account_storage_directory(const std::string& account_directory,
        const std::string& account_name, std::string* error_message)
    {
        if (!is_invalid_account_storage_directory(account_directory))
            return false;

        set_error(error_message,
            "Account '" + normalize_account_name(account_name) + "' has no resolvable storage key; refusing to write into '" + account_directory + "'.");
        return true;
    }

    std::string resolved_character_path(const AccountData& account, const std::string& root_directory, const std::string& character_name)
    {
        return account_record_directory(root_directory, account) + "/" + character_json_file_name(character_name);
    }

    std::string resolved_object_path(const AccountData& account, const std::string& root_directory, const std::string& character_name)
    {
        const std::string account_directory = account_record_directory(root_directory, account);
        const CharacterLinkReference* link = find_character_link_reference(account, character_name);
        if (link != nullptr && !link->object_path.empty())
            return account_directory + "/" + link->object_path;
        return account_directory + "/" + objects_json_file_name(character_name);
    }

    std::string resolved_exploits_path(const AccountData& account, const std::string& root_directory, const std::string& character_name)
    {
        const std::string account_directory = account_record_directory(root_directory, account);
        const CharacterLinkReference* link = find_character_link_reference(account, character_name);
        if (link != nullptr && !link->exploits_path.empty())
            return account_directory + "/" + link->exploits_path;
        return account_directory + "/" + exploits_json_file_name(character_name);
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
        CharacterMigrationData persisted_snapshot = snapshot_data;
        // The authoritative migrated player state lives in character.json. Keep
        // raw legacy player bytes in-memory for the in-flight conversion, but
        // do not persist them into the transitional migration artifact.
        persisted_snapshot.player_file = LegacyAssetSnapshot {};

        const std::string account_storage_key = resolve_account_storage_key(root_directory, snapshot_data.account_name);
        const std::string account_root = root_directory + "/accounts";
        const std::string bucket_directory = account_root + "/" + account_bucket_for_name(account_storage_key);
        const std::string account_directory = account_character_directory(root_directory, snapshot_data.account_name, snapshot_data.character_name);
        if (refuse_invalid_account_storage_directory(account_directory, snapshot_data.account_name, error_message))
            return false;
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

        const std::string json = serialize_character_migration_to_json(persisted_snapshot);
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

    bool retire_character_migration_snapshot_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message)
    {
        const std::string path = account_character_snapshot_path(root_directory, account_name, character_name);
        if (std::remove(path.c_str()) != 0 && errno != ENOENT) {
            set_error(error_message, "Failed to retire transitional migration file '" + path + "': " + std::strerror(errno));
            return false;
        }

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
        if (!is_valid_character_name(character_name, error_message))
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
        if (refuse_invalid_account_storage_directory(account_directory, account_name, error_message))
            return false;
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

    // `converted_source`, when given, receives what was parsed from the legacy .obj so the caller can
    // verify the written file against it. Untouched when there is no legacy file: a default file is
    // written instead, and the caller only checks that it reads back.
    bool hydrate_account_native_object_file_from_migration(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const CharacterMigrationData& snapshot_data, std::string* error_message, objects_json::ObjectSaveData* converted_source = nullptr)
    {
        if (!snapshot_data.object_file.present)
            return write_default_account_object_file(root_directory, account_name, character_name, error_message);

        std::string object_bytes;
        if (!decode_snapshot_content(snapshot_data.object_file, &object_bytes, error_message))
            return false;

        objects_json::ObjectSaveData object_data;
        bool accepted_missing_follower_section = false;
        if (!objects_json::legacy_object_save_data_from_binary(object_bytes, &object_data, &accepted_missing_follower_section, error_message))
            return false;
        if (accepted_missing_follower_section) {
            char log_buffer[MAX_STRING_LENGTH];
            std::snprintf(log_buffer, sizeof(log_buffer),
                "Accepted legacy object file without follower section while migrating %s for account %s.",
                character_name.c_str(), account_name.c_str());
            log(log_buffer);
        }

        if (!write_account_object_json_file(root_directory, account_name, character_name, object_data, error_message))
            return false;

        if (converted_source != nullptr)
            *converted_source = std::move(object_data);
        return true;
    }

    bool hydrate_account_native_character_file_from_migration(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const CharacterMigrationData& snapshot_data, std::string* error_message, char_file_u* converted_source)
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

        if (!write_account_character_file(root_directory, account_name, stored_character, error_message))
            return false;

        // Handed back so the caller can verify the written file against it. Deliberately not
        // verified in here: this is the one migration step whose failure path does not run
        // cleanup_account_native_migration_outputs, safe only because its write is the last thing it
        // does. A check after the write would leave the JSON behind on failure.
        if (converted_source != nullptr)
            *converted_source = stored_character;
        return true;
    }

    bool write_account_exploits_json_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const exploits_json::ExploitHistoryData& exploit_history, std::string* error_message)
    {
        if (!validate_identifier_for_path(account_name, "Account name", error_message))
            return false;
        if (!is_valid_character_name(character_name, error_message))
            return false;

        AccountData account;
        if (!read_account_file(root_directory, account_name, &account, error_message))
            return false;
        if (!validate_account_owned_exploits_path(account, character_name, error_message))
            return false;

        const std::string account_directory = account_character_directory(root_directory, account_name, character_name);
        if (refuse_invalid_account_storage_directory(account_directory, account_name, error_message))
            return false;
        // The record-in-hand twin resolves independently (the record's own email rather than the
        // name -> email lookup), so it has to be tested too -- the write below uses that one.
        if (refuse_invalid_account_storage_directory(account_record_directory(root_directory, account), account_name, error_message))
            return false;
        if (!create_directory_if_missing(root_directory + "/accounts", error_message))
            return false;
        if (!create_directory_if_missing(root_directory + "/accounts/" + account_bucket_for_name(resolve_account_storage_key(root_directory, account_name)), error_message))
            return false;
        if (!create_directory_if_missing(account_directory, error_message))
            return false;

        return write_text_file_atomically(resolved_exploits_path(account, root_directory, character_name), exploits_json::serialize_exploits_to_json(exploit_history), error_message);
    }

    // `converted_source` as for the object file above: the parsed legacy records, left untouched when
    // there is no legacy .exploits file.
    bool hydrate_account_native_exploit_file_from_migration(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const CharacterMigrationData& snapshot_data, std::string* error_message, std::vector<exploit_record>* converted_source = nullptr)
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
        if (!write_account_exploits_json_file(root_directory, account_name, character_name, exploit_history, error_message))
            return false;

        if (converted_source != nullptr)
            *converted_source = std::move(exploit_history.records);
        return true;
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

    // True when every legacy file is exactly as the conversion found it -- present if the snapshot
    // read one, absent if it did not.
    //
    // This is the condition that makes deleting the account-native outputs a ROLLBACK rather than a
    // deletion. Undoing a conversion that never took hold is safe because the originals are still
    // there; the moment they are not, those outputs are the only copy of the character and removing
    // them loses it outright. The two ways that happened: a failure AFTER
    // retire_legacy_character_files_after_migration had already deleted the originals, and a failure
    // DURING it whose restore did not work -- restore_retired_legacy_files only appends to the error
    // string, it never reports that the character is still missing.
    bool legacy_originals_are_intact(const CharacterMigrationData& snapshot_data, const std::string& player_file_path, const std::string& object_file_path, const std::string& exploits_file_path)
    {
        return path_exists(player_file_path) == snapshot_data.player_file.present
            && path_exists(object_file_path) == snapshot_data.object_file.present
            && path_exists(exploits_file_path) == snapshot_data.exploits_file.present;
    }

    // --- Conversion loss guard --------------------------------------------------------------------
    //
    // A legacy character is converted to JSON exactly once, and the legacy file is deleted straight
    // afterwards. If the conversion drops a field, the evidence is gone with it. So before anything
    // is retired we read the file we just wrote back off disk and check it still describes the same
    // character.
    //
    // Fields that legitimately do not survive, and are therefore not compared. This list is the
    // whole value of the check -- everything on it is never verified again, so it stays short and
    // every entry says why:
    //
    //   player_index  recomputed from player_table on load; the stored value means nothing.
    //   pwd           accounts own authentication now; the character password is deliberately not
    //                 written to character JSON.
    //   host          last-logon host, not serialized. Accepted loss, not a fault.
    //   prof          char_file_u::prof is an NPC field PCs carry vestigially -- nothing in the
    //                 codebase writes it for a player character (only mob prototypes, the OLC, and
    //                 load/save copying it through), so there is no maintained value to lose.
    //   profs slot 0  PROF_GENERAL's entry in prof_coof/prof_level/prof_exp: never read, never
    //                 serialized, often junk on disk (see neutralize_known_lossy_transforms).
    //   unnamed flags act/pref bits with no kPlayerFlags/kPreferenceFlags entry: JSON cannot name
    //                 them, nothing reads them (PRF_NOTHING2, undefined act bit 22).
    //   affect slots  the position of each affect, not its content: JSON reloads them packed.
    //
    // Three more differ only when the SOURCE is zero, i.e. never set: apply_character_data_to_store
    // substitutes a default for tactics, shooting and casting, and for each color slot's
    // foreground/background mode. A
    // zero there is "unset", so accepting the default is right -- but a CHANGE from one non-zero
    // value to another is real loss and is still caught.
    // True when `readback`'s affects are exactly `source`'s non-empty affects, in order, packed from
    // slot 0 with every later slot empty -- i.e. the round trip moved affects but changed none.
    bool affects_match_once_packed(const char_file_u& source, const char_file_u& readback)
    {
        int packed = 0;
        for (int index = 0; index < MAX_AFFECT; ++index) {
            const affected_type& from = source.affected[index];
            if (from.type == 0)
                continue;
            const affected_type& to = readback.affected[packed++];
            if (to.type != from.type || to.duration != from.duration || to.time_phase != from.time_phase
                || to.modifier != from.modifier || to.location != from.location
                || to.bitvector != from.bitvector || to.counter != from.counter)
                return false;
        }
        for (int index = packed; index < MAX_AFFECT; ++index) {
            const affected_type& to = readback.affected[index];
            if (to.type != 0 || to.duration != 0 || to.time_phase != 0 || to.modifier != 0
                || to.location != 0 || to.bitvector != 0 || to.counter != 0)
                return false;
        }
        return true;
    }

    // Fields the JSON path deliberately does not round-trip. Neutralizing them here keeps the guard
    // pointed at real, unintended loss instead of refusing conversions over transforms we chose.
    void neutralize_known_lossy_transforms(const char_file_u& source, char_file_u* comparable)
    {
        // PLR_CRASH is a transient inventory-dirty marker. The legacy saver persists it, but
        // apply_character_data_to_store masks it off on load on purpose (character_json.cpp), so a
        // readback can never carry it. It is set on any inventory change, so nearly half of live
        // characters have it on disk; comparing it would refuse them all.
        comparable->specials2.act = (comparable->specials2.act & ~PLR_CRASH) | (source.specials2.act & PLR_CRASH);

        // bad_pws is persisted by the legacy loader/saver (db.cpp) but is not part of the character
        // JSON at all. It is a failed-login counter that is shown once at the next login and then
        // reset, so conversion drops at most one such notice.
        comparable->specials2.bad_pws = source.specials2.bad_pws;

        // Profession slot 0 is PROF_GENERAL. Every accessor answers it without reading the arrays
        // (char_utils.cpp get/set_prof_level, get_prof_coof; the GET_PROF_* macros), and character
        // JSON carries only MAGE..WARRIOR, so the slot always reads back as 0. The legacy saver
        // still writes it, and 53 live characters hold junk there (-27008, 32000, 1, ...);
        // comparing it would refuse them all. Slots 1..MAX_PROFS are still compared.
        comparable->profs.prof_coof[PROF_GENERAL] = source.profs.prof_coof[PROF_GENERAL];
        comparable->profs.prof_level[PROF_GENERAL] = source.profs.prof_level[PROF_GENERAL];
        comparable->profs.prof_exp[PROF_GENERAL] = source.profs.prof_exp[PROF_GENERAL];

        // act/pref bits character JSON has no name for are dropped by the writer and cannot come
        // back. Today that is PRF_NOTHING2 (pref bit 6, read by nothing; 58 live characters carry
        // it) and act bit 22, which has no PLR_ definition (1 live character). Named bits are still
        // compared -- a new PLR_/PRF_ flag must get a kPlayerFlags/kPreferenceFlags entry, or its
        // loss would be accepted here too.
        const long unnamed_act = ~character_json::serializable_player_flag_mask();
        const long unnamed_pref = ~character_json::serializable_preference_flag_mask();
        comparable->specials2.act = (comparable->specials2.act & ~unnamed_act) | (source.specials2.act & unnamed_act);
        comparable->specials2.pref = (comparable->specials2.pref & ~unnamed_pref) | (source.specials2.pref & unnamed_pref);

        // Affect POSITIONS. Character JSON keeps only non-empty affects (type != 0) and
        // apply_character_data_to_store reloads them packed from slot 0, but the legacy loader puts
        // each one at the index written in the file (db.cpp KEY_AFF), so a file with an empty slot
        // before a used one reads back shifted. Position means nothing to the game -- store_to_char
        // loads every non-empty slot wherever it is. So when the source, packed the same way,
        // matches the readback field for field, only positions moved; any changed, missing or extra
        // affect still leaves the arrays different and is refused.
        // memcpy, not element assignment: the guard compares these bytes with memcmp, padding included.
        if (affects_match_once_packed(source, *comparable))
            std::memcpy(comparable->affected, source.affected, sizeof(source.affected));
    }

    void neutralize_defaulted_on_unset(const char_file_u& source, char_file_u* comparable)
    {
        if (source.specials2.tactics == 0)
            comparable->specials2.tactics = 0;
        if (source.specials2.shooting == 0)
            comparable->specials2.shooting = 0;
        if (source.specials2.casting == 0)
            comparable->specials2.casting = 0;
        for (int slot = 0; slot < MAX_COLOR_FIELDS; ++slot) {
            if (source.profs.color_settings[slot].foreground.mode == 0)
                comparable->profs.color_settings[slot].foreground.mode = 0;
            if (source.profs.color_settings[slot].background.mode == 0)
                comparable->profs.color_settings[slot].background.mode = 0;
        }
    }

    // Empty when the two describe the same character. Otherwise the name of the first field that
    // does not match, so the failure says WHICH field rather than only that something changed.
    std::string first_lossy_conversion_field(const char_file_u& source, const char_file_u& readback)
    {
        char_file_u expected = readback;
        neutralize_defaulted_on_unset(source, &expected);
        neutralize_known_lossy_transforms(source, &expected);

#define FIELD_DIFFERS(f) (std::memcmp(&source.f, &expected.f, sizeof(source.f)) != 0)
        if (source.sex != expected.sex) return "sex";
        if (source.race != expected.race) return "race";
        if (source.bodytype != expected.bodytype) return "bodytype";
        if (source.level != expected.level) return "level";
        if (source.language != expected.language) return "language";
        if (source.birth != expected.birth) return "birth";
        if (source.played != expected.played) return "played";
        if (source.weight != expected.weight) return "weight";
        if (source.height != expected.height) return "height";
        if (source.hometown != expected.hometown) return "hometown";
        if (source.last_logon != expected.last_logon) return "last_logon";
        if (std::strncmp(source.name, expected.name, sizeof(source.name)) != 0) return "name";
        if (std::strncmp(source.title, expected.title, sizeof(source.title)) != 0) return "title";
        if (std::strncmp(source.description, expected.description, sizeof(source.description)) != 0) return "description";
        if (FIELD_DIFFERS(talks)) return "talks";
        if (FIELD_DIFFERS(tmpabilities)) return "tmpabilities";
        if (FIELD_DIFFERS(constabilities)) return "constabilities";
        if (FIELD_DIFFERS(points)) return "points";
        if (FIELD_DIFFERS(skills)) return "skills";
        if (FIELD_DIFFERS(affected)) return "affected";
        if (FIELD_DIFFERS(specials2)) return "specials2";
        if (FIELD_DIFFERS(profs)) return "profs";
#undef FIELD_DIFFERS
        return std::string();
    }

    // Reads back the character file just written and proves it still describes `source`. Runs once
    // per character in its lifetime, at conversion, so the extra parse costs nothing that matters.
    bool verify_converted_character_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const char_file_u& source, std::string* error_message)
    {
        const std::string path = account_character_player_path(root_directory, account_name, character_name);

        std::string json_text;
        if (!read_text_file(path, &json_text, error_message)) {
            set_error(error_message, "Converted character file '" + path + "' could not be read back for verification.");
            return false;
        }

        character_json::CharacterData parsed;
        std::string parse_error;
        if (!character_json::deserialize_character_from_json(json_text, &parsed, &parse_error)) {
            // The writer produced a file the reader refuses. Reachable today: skills are keyed by
            // NAME and the skill table has duplicate names, so a character with values in two
            // same-named slots serializes to duplicate keys and will not parse back.
            set_error(error_message, "Converted character file for '" + character_name + "' cannot be read back: " + parse_error);
            return false;
        }

        char_file_u readback {};
        std::string apply_error;
        if (!character_json::apply_character_data_to_store(parsed, &readback, &apply_error)) {
            set_error(error_message, "Converted character file for '" + character_name + "' cannot be loaded back: " + apply_error);
            return false;
        }

        const std::string lossy_field = first_lossy_conversion_field(source, readback);
        if (!lossy_field.empty()) {
            set_error(error_message, "Conversion of '" + character_name + "' would lose data: field '" + lossy_field + "' does not survive the round trip.");
            return false;
        }

        return true;
    }

    // The same proof for the object file, read back the way login reads it -- JSON to structure to
    // the legacy binary the object loader consumes -- so a file login could not load is refused here
    // while the .obj still exists. No exemption list: unlike the character file, nothing in it is
    // deliberately not carried over, so any difference is real loss.
    bool verify_converted_object_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const objects_json::ObjectSaveData& source, std::string* error_message)
    {
        std::string object_bytes;
        std::string read_error;
        if (!read_account_object_file(root_directory, account_name, character_name, &object_bytes, &read_error)) {
            set_error(error_message, "Converted object file for '" + character_name + "' cannot be read back: " + read_error);
            return false;
        }

        objects_json::ObjectSaveData readback;
        if (!objects_json::object_save_data_from_binary(object_bytes, &readback, &read_error)) {
            set_error(error_message, "Converted object file for '" + character_name + "' cannot be loaded back: " + read_error);
            return false;
        }

        const std::string lossy_field = objects_json::first_differing_field(source, readback);
        if (!lossy_field.empty()) {
            set_error(error_message, "Conversion of '" + character_name + "' would lose data: object field '" + lossy_field + "' does not survive the round trip.");
            return false;
        }

        return true;
    }

    // A character with no legacy .obj gets a default file instead. It holds nothing to lose, but login
    // refuses a character whose account object file exists and will not read
    // (load_object_save_bytes_for_character), and the player file is retired straight after -- so it
    // must at least read back. Its contents are not compared: that would mean a second copy of what
    // write_default_account_object_file writes, which new-character creation shares.
    bool verify_default_object_file_reads_back(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message)
    {
        std::string object_bytes;
        std::string read_error;
        if (!read_account_object_file(root_directory, account_name, character_name, &object_bytes, &read_error)) {
            set_error(error_message, "Default object file for '" + character_name + "' cannot be read back: " + read_error);
            return false;
        }

        return true;
    }

    // The same for a default exploits file, which is an empty list, so checking that is free.
    bool verify_default_exploit_file_reads_back_empty(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message)
    {
        std::vector<exploit_record> readback;
        std::string read_error;
        if (!read_account_exploit_file(root_directory, account_name, character_name, &readback, &read_error)) {
            set_error(error_message, "Default exploit file for '" + character_name + "' cannot be read back: " + read_error);
            return false;
        }
        if (!readback.empty()) {
            set_error(error_message, "Default exploit file for '" + character_name + "' is not empty: it holds " + std::to_string(readback.size()) + " record(s).");
            return false;
        }

        return true;
    }

    // And for the exploits file, read back through the reader login uses.
    bool verify_converted_exploit_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const std::vector<exploit_record>& source, std::string* error_message)
    {
        std::vector<exploit_record> readback;
        std::string read_error;
        if (!read_account_exploit_file(root_directory, account_name, character_name, &readback, &read_error)) {
            set_error(error_message, "Converted exploit file for '" + character_name + "' cannot be read back: " + read_error);
            return false;
        }

        const std::string lossy_field = exploits_json::first_differing_field(source, readback);
        if (!lossy_field.empty()) {
            set_error(error_message, "Conversion of '" + character_name + "' would lose data: exploit field '" + lossy_field + "' does not survive the round trip.");
            return false;
        }

        return true;
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
        char_file_u converted_source {};
        if (!hydrate_account_native_character_file_from_migration(root_directory, account_name, character_name, snapshot_data, error_message, &converted_source))
            return false;

        // Before ANY legacy file is retired. retire_legacy_character_files_after_migration below is
        // what deletes the originals, so a refusal here leaves the character exactly as it was:
        // legacy files intact, and admin_link_and_migrate_character never reaches
        // add_character_to_account, so the account is not touched either. The player is told it
        // failed and can convert later once the cause is fixed -- far better than a converted
        // character that is quietly missing a field with the original already deleted.
        // Every abandonment below goes through here rather than calling cleanup directly. Rolling
        // back means putting the character back where it was, and that is only what this does while
        // the originals are still on disk -- see legacy_originals_are_intact.
        const auto roll_back_account_native_outputs = [&]() {
            if (legacy_originals_are_intact(snapshot_data, player_file_path, object_file_path, exploits_file_path)) {
                cleanup_account_native_migration_outputs(root_directory, account_name, character_name);
                return;
            }

            std::fprintf(stderr, "SYSERR: Conversion of '%s' failed after its legacy files were retired; keeping the account-native files at '%s' because they are now the only copy of the character.\n",
                character_name.c_str(), account_character_directory(root_directory, account_name, character_name).c_str());
        };

        if (!verify_converted_character_file(root_directory, account_name, character_name, converted_source, error_message)) {
            roll_back_account_native_outputs();
            return false;
        }
        objects_json::ObjectSaveData converted_objects;
        if (!hydrate_account_native_object_file_from_migration(root_directory, account_name, character_name, snapshot_data, error_message, &converted_objects)) {
            roll_back_account_native_outputs();
            return false;
        }
        const bool object_file_verified = snapshot_data.object_file.present
            ? verify_converted_object_file(root_directory, account_name, character_name, converted_objects, error_message)
            : verify_default_object_file_reads_back(root_directory, account_name, character_name, error_message);
        if (!object_file_verified) {
            roll_back_account_native_outputs();
            return false;
        }
        std::vector<exploit_record> converted_exploits;
        if (!hydrate_account_native_exploit_file_from_migration(root_directory, account_name, character_name, snapshot_data, error_message, &converted_exploits)) {
            roll_back_account_native_outputs();
            return false;
        }
        const bool exploit_file_verified = snapshot_data.exploits_file.present
            ? verify_converted_exploit_file(root_directory, account_name, character_name, converted_exploits, error_message)
            : verify_default_exploit_file_reads_back_empty(root_directory, account_name, character_name, error_message);
        if (!exploit_file_verified) {
            roll_back_account_native_outputs();
            return false;
        }
        if (!retire_legacy_character_files_after_migration(player_file_path, stale_flat_player_file_path, object_file_path, exploits_file_path, snapshot_data, error_message)) {
            roll_back_account_native_outputs();
            return false;
        }

        // Deliberately not a failure. The legacy originals are gone by now, so the conversion has
        // happened whatever becomes of this transitional file -- a leftover <name>.migration.json is
        // litter, and nothing reads it unless a later conversion asks for it by name. Failing here
        // used to abandon a conversion that had already succeeded and take the only copy of the
        // character with it.
        std::string snapshot_retirement_error;
        if (!retire_character_migration_snapshot_file(root_directory, account_name, character_name, &snapshot_retirement_error)) {
            std::fprintf(stderr, "SYSERR: Converted '%s' but could not retire its transitional migration file: %s\n",
                character_name.c_str(), snapshot_retirement_error.c_str());
        }

        if (migration)
            *migration = snapshot_data;

        return true;
    }

} // namespace
