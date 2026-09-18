namespace {

bool migration_snapshot_has_persisted_player_payload(const CharacterMigrationData& migration)
{
    return migration.player_file.present || !migration.player_file.content.empty() || !migration.player_file.source_path.empty();
}

CharacterMigrationData sanitized_migration_for_persistence(const CharacterMigrationData& migration)
{
    CharacterMigrationData sanitized = migration;
    sanitized.player_file = LegacyAssetSnapshot {};
    return sanitized;
}

} // namespace

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

    CharacterMigrationData parsed_migration;
    if (!deserialize_character_migration_from_json(json, &parsed_migration, error_message))
        return false;

    if (migration_snapshot_has_persisted_player_payload(parsed_migration)) {
        CharacterMigrationData sanitized_migration = sanitized_migration_for_persistence(parsed_migration);
        if (!write_character_migration_snapshot(root_directory, sanitized_migration, nullptr, error_message))
            return false;
        parsed_migration = std::move(sanitized_migration);
    }

    *migration = std::move(parsed_migration);
    set_error(error_message, "");
    return true;
}

bool ensure_character_migration(const std::string& root_directory, const std::string& account_name, const std::string& character_name, long migrated_at, CharacterMigrationData* migration, std::string* error_message)
{
    bool account_character_exists = false;
    if (!inspect_account_character_file(root_directory, account_name, character_name, &account_character_exists, error_message))
        return false;
    if (account_character_exists)
        return true;

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

    bool had_existing_account_exploit_file = false;
    if (!inspect_account_exploit_file(root_directory, owner_account_name, character_name, &had_existing_account_exploit_file, error_message))
        return false;

    std::vector<exploit_record> existing_account_records;
    if (had_existing_account_exploit_file && !read_account_exploit_file(root_directory, owner_account_name, character_name, &existing_account_records, error_message))
        return false;
    const bool had_existing_account_exploit_history = had_existing_account_exploit_file && !existing_account_records.empty();

    CharacterMigrationData refreshed_migration;
    if (!migrate_legacy_character_by_name(root_directory, owner_account_name, character_name, migrated_at, &refreshed_migration, error_message))
        return false;

    if (!refreshed_migration.exploits_file.present && had_existing_account_exploit_history) {
        if (!write_account_exploit_file(root_directory, owner_account_name, character_name, existing_account_records, error_message))
            return false;

        std::string exploit_bytes;
        if (!exploits_json::exploit_records_to_binary(existing_account_records, &exploit_bytes, error_message))
            return false;

        refreshed_migration.exploits_file.source_path = account_character_exploits_path(root_directory, owner_account_name, character_name);
        refreshed_migration.exploits_file.encoding = "hex";
        refreshed_migration.exploits_file.content = hex_encode(exploit_bytes);
        refreshed_migration.exploits_file.present = true;
    }

    if (migration)
        *migration = refreshed_migration;

    set_error(error_message, "");
    return true;
}
