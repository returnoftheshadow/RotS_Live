bool write_account_character_file(const std::string& root_directory, const std::string& account_name, const char_file_u& stored_character, std::string* error_message)
{
    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;
    if (!validate_identifier_for_path(stored_character.name, "Character name", error_message))
        return false;

    AccountData account;
    if (!read_account_file(root_directory, account_name, &account, error_message))
        return false;
    if (!validate_account_owned_character_path(account, stored_character.name, error_message))
        return false;

    const std::string account_directory = account_character_directory(root_directory, account_name, stored_character.name);
    if (!create_directory_if_missing(root_directory + "/accounts", error_message))
        return false;
    if (!create_directory_if_missing(root_directory + "/accounts/" + account_bucket_for_name(resolve_account_storage_key(root_directory, account_name)), error_message))
        return false;
    if (!create_directory_if_missing(account_directory, error_message))
        return false;

    const std::string final_path = resolved_character_path(account, root_directory, stored_character.name);
    const std::string json = character_json::serialize_character_to_json(character_json::character_data_from_store(stored_character));
    return write_text_file_atomically(final_path, json, error_message);
}

bool write_linked_character_file(const std::string& root_directory, const std::string& character_name, const char_file_u& stored_character, std::string* error_message)
{
    std::string owner_account_name;
    if (!find_linked_character_owner_account(root_directory, character_name, &owner_account_name, error_message))
        return false;
    if (owner_account_name.empty()) {
        set_error(error_message, "");
        return true;
    }

    return write_account_character_file(root_directory, owner_account_name, stored_character, error_message);
}

bool read_account_character_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, char_file_u* stored_character, std::string* error_message)
{
    if (stored_character == nullptr) {
        set_error(error_message, "Stored character output parameter must not be null.");
        return false;
    }

    AccountData account;
    if (!read_account_file(root_directory, account_name, &account, error_message))
        return false;
    if (!validate_account_owned_character_path(account, character_name, error_message))
        return false;

    const std::string path = resolved_character_path(account, root_directory, character_name);
    std::string json;
    if (!read_text_file(path, &json, error_message))
        return false;

    character_json::CharacterData character;
    if (!character_json::deserialize_character_from_json(json, &character, error_message))
        return false;
    if (!character_json::apply_character_data_to_store(character, stored_character, error_message))
        return false;

    set_error(error_message, "");
    return true;
}

bool account_character_file_exists(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message)
{
    bool exists = false;
    if (!inspect_account_character_file(root_directory, account_name, character_name, &exists, error_message))
        return false;
    return exists;
}

bool inspect_account_character_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, bool* exists, std::string* error_message)
{
    AccountData account;
    if (!read_account_file(root_directory, account_name, &account, error_message))
        return false;
    if (!validate_account_owned_character_path(account, character_name, error_message))
        return false;

    return inspect_path_existence(resolved_character_path(account, root_directory, character_name), "account character file", exists, error_message);
}

bool remove_account_character_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message)
{
    AccountData account;
    if (!read_account_file(root_directory, account_name, &account, error_message))
        return false;
    if (!validate_account_owned_character_path(account, character_name, error_message))
        return false;

    const std::string path = resolved_character_path(account, root_directory, character_name);
    if (std::remove(path.c_str()) != 0 && errno != ENOENT) {
        set_error(error_message, "Failed to remove account character file '" + path + "': " + std::strerror(errno));
        return false;
    }

    set_error(error_message, "");
    return true;
}

bool write_account_object_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const std::string& object_bytes, std::string* error_message)
{
    objects_json::ObjectSaveData object_data;
    if (!objects_json::object_save_data_from_binary(object_bytes, &object_data, error_message))
        return false;

    return write_account_object_json_file(root_directory, account_name, character_name, object_data, error_message);
}

bool write_default_account_object_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message)
{
    objects_json::ObjectSaveData empty_object_data;
    empty_object_data.rent.rentcode = RENT_CRASH;
    return write_account_object_json_file(root_directory, account_name, character_name, empty_object_data, error_message);
}

bool write_linked_character_object_file(const std::string& root_directory, const std::string& character_name, const std::string& object_bytes, std::string* error_message)
{
    std::string owner_account_name;
    if (!find_linked_character_owner_account(root_directory, character_name, &owner_account_name, error_message))
        return false;
    if (owner_account_name.empty()) {
        set_error(error_message, "");
        return true;
    }

    return write_account_object_file(root_directory, owner_account_name, character_name, object_bytes, error_message);
}

bool read_account_object_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* object_bytes, std::string* error_message)
{
    if (object_bytes == nullptr) {
        set_error(error_message, "Object-bytes output parameter must not be null.");
        return false;
    }

    AccountData account;
    if (!read_account_file(root_directory, account_name, &account, error_message))
        return false;
    if (!validate_account_owned_object_path(account, character_name, error_message))
        return false;

    const std::string path = resolved_object_path(account, root_directory, character_name);
    std::string json;
    if (!read_text_file(path, &json, error_message))
        return false;

    objects_json::ObjectSaveData object_data;
    if (!objects_json::deserialize_objects_from_json(json, &object_data, error_message))
        return false;
    if (!objects_json::object_save_data_to_binary(object_data, object_bytes, error_message))
        return false;

    set_error(error_message, "");
    return true;
}

bool account_object_file_exists(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message)
{
    bool exists = false;
    if (!inspect_account_object_file(root_directory, account_name, character_name, &exists, error_message))
        return false;
    return exists;
}

bool inspect_account_object_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, bool* exists, std::string* error_message)
{
    AccountData account;
    if (!read_account_file(root_directory, account_name, &account, error_message))
        return false;
    if (!validate_account_owned_object_path(account, character_name, error_message))
        return false;

    return inspect_path_existence(resolved_object_path(account, root_directory, character_name), "account object file", exists, error_message);
}

bool remove_account_object_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message)
{
    AccountData account;
    if (!read_account_file(root_directory, account_name, &account, error_message))
        return false;
    if (!validate_account_owned_object_path(account, character_name, error_message))
        return false;

    const std::string path = resolved_object_path(account, root_directory, character_name);
    if (std::remove(path.c_str()) != 0 && errno != ENOENT) {
        set_error(error_message, "Failed to remove account object file '" + path + "': " + std::strerror(errno));
        return false;
    }

    set_error(error_message, "");
    return true;
}

bool write_account_exploit_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const std::vector<exploit_record>& records, std::string* error_message)
{
    exploits_json::ExploitHistoryData exploit_history;
    exploit_history.records = records;
    return write_account_exploits_json_file(root_directory, account_name, character_name, exploit_history, error_message);
}

bool write_default_account_exploit_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message)
{
    const exploits_json::ExploitHistoryData empty_history;
    return write_account_exploits_json_file(root_directory, account_name, character_name, empty_history, error_message);
}

bool write_linked_character_exploit_file(const std::string& root_directory, const std::string& character_name, const std::vector<exploit_record>& records, std::string* error_message)
{
    std::string owner_account_name;
    if (!find_linked_character_owner_account(root_directory, character_name, &owner_account_name, error_message))
        return false;
    if (owner_account_name.empty()) {
        set_error(error_message, "");
        return true;
    }

    return write_account_exploit_file(root_directory, owner_account_name, character_name, records, error_message);
}

bool read_account_exploit_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::vector<exploit_record>* records, std::string* error_message)
{
    if (records == nullptr) {
        set_error(error_message, "Exploit-record output parameter must not be null.");
        return false;
    }

    AccountData account;
    if (!read_account_file(root_directory, account_name, &account, error_message))
        return false;
    if (!validate_account_owned_exploits_path(account, character_name, error_message))
        return false;

    std::string json;
    if (!read_text_file(resolved_exploits_path(account, root_directory, character_name), &json, error_message))
        return false;

    exploits_json::ExploitHistoryData exploit_history;
    if (!exploits_json::deserialize_exploits_from_json(json, &exploit_history, error_message))
        return false;

    *records = std::move(exploit_history.records);
    set_error(error_message, "");
    return true;
}

bool account_exploit_file_exists(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message)
{
    bool exists = false;
    if (!inspect_account_exploit_file(root_directory, account_name, character_name, &exists, error_message))
        return false;
    return exists;
}

bool inspect_account_exploit_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, bool* exists, std::string* error_message)
{
    AccountData account;
    if (!read_account_file(root_directory, account_name, &account, error_message))
        return false;
    if (!validate_account_owned_exploits_path(account, character_name, error_message))
        return false;

    return inspect_path_existence(resolved_exploits_path(account, root_directory, character_name), "account exploits file", exists, error_message);
}

bool remove_account_exploit_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message)
{
    AccountData account;
    if (!read_account_file(root_directory, account_name, &account, error_message))
        return false;
    if (!validate_account_owned_exploits_path(account, character_name, error_message))
        return false;

    const std::string path = resolved_exploits_path(account, root_directory, character_name);
    if (std::remove(path.c_str()) != 0 && errno != ENOENT) {
        set_error(error_message, "Failed to remove account exploits file '" + path + "': " + std::strerror(errno));
        return false;
    }

    set_error(error_message, "");
    return true;
}

bool clear_account_character_runtime_support_files(const std::string& root_directory, const std::string& character_name, std::string* error_message)
{
    if (!validate_identifier_for_path(character_name, "Character name", error_message))
        return false;

    const std::string object_path = legacy_object_file_path(root_directory, character_name);
    if (std::remove(object_path.c_str()) != 0 && errno != ENOENT) {
        set_error(error_message, "Failed to remove stale legacy file '" + object_path + "': " + std::strerror(errno));
        return false;
    }

    const std::string exploits_path = legacy_exploits_file_path(root_directory, character_name);
    if (std::remove(exploits_path.c_str()) != 0 && errno != ENOENT) {
        set_error(error_message, "Failed to remove stale legacy file '" + exploits_path + "': " + std::strerror(errno));
        return false;
    }

    set_error(error_message, "");
    return true;
}

bool decode_snapshot_content(const LegacyAssetSnapshot& snapshot, std::string* contents, std::string* error_message)
{
    if (contents == nullptr) {
        set_error(error_message, "Snapshot content output parameter must not be null.");
        return false;
    }

    if (!snapshot.present) {
        contents->clear();
        set_error(error_message, "Requested snapshot content is not present.");
        return false;
    }

    if (snapshot.encoding == "hex")
        return hex_decode(snapshot.content, contents, error_message);

    set_error(error_message, "Unsupported snapshot encoding '" + snapshot.encoding + "'.");
    return false;
}
