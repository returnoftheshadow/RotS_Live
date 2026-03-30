bool migrate_legacy_character_by_name(const std::string& root_directory, const std::string& account_name, const std::string& character_name, long migrated_at, CharacterMigrationData* migration, std::string* error_message)
{
    std::string player_file_path;
    if (!resolve_legacy_player_file_path(root_directory, character_name, &player_file_path, error_message))
        return false;

    std::string stale_flat_player_file_path;
    const std::string canonical_player_file_path = legacy_player_file_path(root_directory, character_name);
    if (player_file_path != canonical_player_file_path && path_exists(canonical_player_file_path))
        stale_flat_player_file_path = canonical_player_file_path;

    return migrate_legacy_character_files_internal(root_directory, account_name, character_name,
        player_file_path,
        stale_flat_player_file_path,
        legacy_object_file_path(root_directory, character_name),
        legacy_exploits_file_path(root_directory, character_name),
        migrated_at, migration, error_message);
}

bool read_character_migration(const std::string& root_directory, const std::string& account_name, const std::string& character_name, CharacterMigrationData* migration, std::string* error_message)
{
    if (migration == nullptr) {
        set_error(error_message, "Migration output parameter must not be null.");
        return false;
    }

    const std::string path = account_character_snapshot_path(root_directory, account_name, character_name);
    FILE* file = std::fopen(path.c_str(), "r");
    if (file == nullptr) {
        set_error(error_message, "Failed to open migration file '" + path + "': " + std::strerror(errno));
        return false;
    }

    std::string json;
    char buffer[1024];
    while (true) {
        const size_t bytes_read = std::fread(buffer, sizeof(char), sizeof(buffer), file);
        if (bytes_read > 0)
            json.append(buffer, bytes_read);

        if (bytes_read < sizeof(buffer)) {
            if (std::ferror(file)) {
                std::fclose(file);
                set_error(error_message, "Failed to read migration file '" + path + "'.");
                return false;
            }
            break;
        }
    }

    std::fclose(file);
    return deserialize_character_migration_from_json(json, migration, error_message);
}

bool ensure_character_migration(const std::string& root_directory, const std::string& account_name, const std::string& character_name, long migrated_at, CharacterMigrationData* migration, std::string* error_message)
{
    bool account_character_exists = false;
    if (!inspect_account_character_file(root_directory, account_name, character_name, &account_character_exists, error_message))
        return false;
    if (account_character_exists)
        return true;

    const std::string path = account_character_snapshot_path(root_directory, account_name, character_name);
    struct stat file_info {};
    if (stat(path.c_str(), &file_info) == 0) {
        CharacterMigrationData loaded_migration;
        CharacterMigrationData* migration_target = migration != nullptr ? migration : &loaded_migration;
        if (read_character_migration(root_directory, account_name, character_name, migration_target, error_message)) {
            set_error(error_message, "Authoritative character.json is missing for '" + character_name + "'; the transitional migration snapshot alone is insufficient.");
            return false;
        }
    } else if (errno != ENOENT) {
        set_error(error_message, "Failed to inspect migration file '" + path + "': " + std::strerror(errno));
        return false;
    }

    return migrate_legacy_character_by_name(root_directory, account_name, character_name, migrated_at, migration, error_message);
}

bool restore_character_migration(const std::string& root_directory, const std::string& expected_account_name, const std::string& expected_character_name, const CharacterMigrationData& migration, std::string* error_message)
{
    if (!validate_migration_identity(migration, expected_account_name, expected_character_name, error_message))
        return false;

    if (!write_snapshot_bytes(legacy_player_file_path(root_directory, migration.character_name), migration.player_file, true, error_message))
        return false;
    if (!write_snapshot_bytes(legacy_object_file_path(root_directory, migration.character_name), migration.object_file, false, error_message))
        return false;
    if (!write_snapshot_bytes(legacy_exploits_file_path(root_directory, migration.character_name), migration.exploits_file, false, error_message))
        return false;

    set_error(error_message, "");
    return true;
}

bool clear_character_runtime_support_files_for_account_play(const std::string& root_directory, const std::string& expected_account_name, const std::string& expected_character_name, const CharacterMigrationData& migration, std::string* error_message)
{
    if (!validate_migration_identity(migration, expected_account_name, expected_character_name, error_message))
        return false;

    return clear_account_character_runtime_support_files(root_directory, migration.character_name, error_message);
}

bool refresh_linked_character_snapshot(const std::string& root_directory, const std::string& character_name, long migrated_at, CharacterMigrationData* migration, std::string* error_message)
{
    std::string owner_account_name;
    if (!find_linked_character_owner_account(root_directory, character_name, &owner_account_name, error_message))
        return false;

    if (owner_account_name.empty()) {
        if (migration)
            *migration = CharacterMigrationData {};
        set_error(error_message, "");
        return true;
    }

    CharacterMigrationData existing_migration;
    const bool had_existing_snapshot = read_character_migration(root_directory, owner_account_name, character_name, &existing_migration, nullptr);

    CharacterMigrationData refreshed_migration;
    if (!migrate_legacy_character_by_name(root_directory, owner_account_name, character_name, migrated_at, &refreshed_migration, error_message))
        return false;

    if (!refreshed_migration.exploits_file.present && had_existing_snapshot && existing_migration.exploits_file.present) {
        refreshed_migration.exploits_file = existing_migration.exploits_file;
        if (!write_character_migration_snapshot(root_directory, refreshed_migration, &refreshed_migration, error_message))
            return false;
    }

    if (migration)
        *migration = refreshed_migration;

    set_error(error_message, "");
    return true;
}
