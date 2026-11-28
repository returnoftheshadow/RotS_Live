std::string account_bucket_for_name(const std::string& name)
{
    const std::string normalized_name = normalize_account_name(name);
    if (normalized_name.empty())
        return "ZZZ";

    switch (normalized_name[0]) {
    case 'a':
    case 'b':
    case 'c':
    case 'd':
    case 'e':
        return "A-E";
    case 'f':
    case 'g':
    case 'h':
    case 'i':
    case 'j':
        return "F-J";
    case 'k':
    case 'l':
    case 'm':
    case 'n':
    case 'o':
        return "K-O";
    case 'p':
    case 'q':
    case 'r':
    case 's':
    case 't':
        return "P-T";
    case 'u':
    case 'v':
    case 'w':
    case 'x':
    case 'y':
    case 'z':
        return "U-Z";
    default:
        return "ZZZ";
    }
}

std::string account_file_path(const std::string& root_directory, const std::string& account_name)
{
    return account_file_path_from_email(root_directory, account_name);
}

std::string legacy_player_file_path(const std::string& root_directory, const std::string& character_name)
{
    const std::string normalized_name = normalize_account_name(character_name);
    return root_directory + "/players/" + account_bucket_for_name(normalized_name) + "/" + normalized_name;
}

std::string legacy_object_file_path(const std::string& root_directory, const std::string& character_name)
{
    const std::string normalized_name = normalize_account_name(character_name);
    return root_directory + "/plrobjs/" + account_bucket_for_name(normalized_name) + "/" + normalized_name + ".obj";
}

std::string legacy_exploits_file_path(const std::string& root_directory, const std::string& character_name)
{
    const std::string normalized_name = normalize_account_name(character_name);
    return root_directory + "/exploits/" + account_bucket_for_name(normalized_name) + "/" + normalized_name + ".exploits";
}

std::string serialize_account_to_json(const AccountData& account)
{
    std::ostringstream output;
    output << "{\n";
    output << "  \"version\": " << account.version << ",\n";
    output << "  \"account_name\": \"" << json_utils::escape_json_string(account.account_name) << "\",\n";
    output << "  \"normalized_email\": \"" << json_utils::escape_json_string(account.normalized_email) << "\",\n";
    output << "  \"password_hash\": \"" << json_utils::escape_json_string(account.password_hash) << "\",\n";
    output << "  \"password_salt\": \"" << json_utils::escape_json_string(account.password_salt) << "\",\n";
    output << "  \"characters\": [";
    for (size_t index = 0; index < account.characters.size(); ++index) {
        if (index > 0)
            output << ", ";
        output << "\"" << json_utils::escape_json_string(account.characters[index]) << "\"";
    }
    output << "],\n";
    output << "  \"character_links\": [";
    for (size_t index = 0; index < account.character_links.size(); ++index) {
        const AccountData::CharacterLinkReference& link = account.character_links[index];
        if (index > 0)
            output << ", ";
        output << "{";
        output << "\"character_name\": \"" << json_utils::escape_json_string(link.character_name) << "\", ";
        output << "\"character_path\": \"" << json_utils::escape_json_string(json_path_or_empty(link.character_path)) << "\", ";
        output << "\"object_path\": \"" << json_utils::escape_json_string(json_path_or_empty(link.object_path)) << "\", ";
        output << "\"exploits_path\": \"" << json_utils::escape_json_string(json_path_or_empty(link.exploits_path)) << "\"";
        output << "}";
    }
    output << "],\n";
    output << "  \"email_verified\": " << (account.email_verified ? "true" : "false") << ",\n";
    output << "  \"email_verified_by\": \"" << json_utils::escape_json_string(account.email_verified_by) << "\",\n";
    output << "  \"email_verified_at\": " << account.email_verified_at << ",\n";
    output << "  \"verification_code_hash\": \"" << json_utils::escape_json_string(account.verification_code_hash) << "\",\n";
    output << "  \"verification_code_sent_at\": " << account.verification_code_sent_at << ",\n";
    output << "  \"verification_code_expires_at\": " << account.verification_code_expires_at << ",\n";
    output << "  \"verification_attempt_count\": " << account.verification_attempt_count << ",\n";
    output << "  \"verification_last_attempt_at\": " << account.verification_last_attempt_at << ",\n";
    output << "  \"blocked\": " << (account.blocked ? "true" : "false") << ",\n";
    output << "  \"block_reason\": \"" << json_utils::escape_json_string(account.block_reason) << "\",\n";
    output << "  \"blocked_by\": \"" << json_utils::escape_json_string(account.blocked_by) << "\",\n";
    output << "  \"blocked_at\": " << account.blocked_at << ",\n";
    output << "  \"created_at\": " << account.created_at << ",\n";
    output << "  \"updated_at\": " << account.updated_at << ",\n";
    output << "  \"password_reset_at\": " << account.password_reset_at << ",\n";
    output << "  \"password_reset_by\": \"" << json_utils::escape_json_string(account.password_reset_by) << "\",\n";
    output << "  \"failed_login_count\": " << account.failed_login_count << ",\n";
    output << "  \"failed_login_last_at\": " << account.failed_login_last_at << ",\n";
    output << "  \"failed_login_last_host\": \"" << json_utils::escape_json_string(account.failed_login_last_host) << "\",\n";
    output << "  \"password_reset_code_hash\": \"" << json_utils::escape_json_string(account.password_reset_code_hash) << "\",\n";
    output << "  \"password_reset_code_sent_at\": " << account.password_reset_code_sent_at << ",\n";
    output << "  \"password_reset_code_expires_at\": " << account.password_reset_code_expires_at << ",\n";
    // roster_sort is written ahead of password_reset_attempt_count, which stays the true last
    // (comma-less) field: a strict JSON reader rejects a trailing comma before "}", so any field
    // that a test (or a future one) strips out by deleting its own line must not be the very last
    // entry in the object.
    output << "  \"roster_sort\": \"" << json_utils::escape_json_string(account.roster_sort) << "\",\n";
    if (account.preferences.present) {
        output << "  \"preferences\": {\"flags\": [";
        const std::vector<std::string> flag_names
            = character_json::encode_preference_flags(account.preferences.preference_flags & PPC_PRF_MASK);
        for (size_t index = 0; index < flag_names.size(); ++index) {
            if (index > 0)
                output << ", ";
            output << "\"" << json_utils::escape_json_string(flag_names[index]) << "\"";
        }
        output << "], \"colors\": {"
               << character_json::encode_color_slots_object(
                      account.preferences.colors, account.preferences.color_settings)
               << "}},\n";
    }
    output << "  \"password_reset_attempt_count\": " << account.password_reset_attempt_count << "\n";
    output << "}\n";
    return output.str();
}

bool deserialize_account_from_json(const std::string& json, AccountData* account, std::string* error_message)
{
    if (account == nullptr) {
        set_error(error_message, "Account output parameter must not be null.");
        return false;
    }

    AccountData parsed_account;
    json_utils::JsonReader reader(json);
    if (!reader.parse_root_object([&parsed_account](const std::string& key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
            return parse_account_property(key, nested_reader, &parsed_account, nested_error_message);
        },
            error_message))
        return false;

    if (parsed_account.version != ACCOUNT_SCHEMA_VERSION) {
        set_error(error_message, "Unsupported account schema version.");
        return false;
    }

    if (!is_valid_account_name(parsed_account.account_name, error_message))
        return false;

    parsed_account.account_name = normalize_account_name(parsed_account.account_name);
    parsed_account.normalized_email = normalize_email(parsed_account.normalized_email);
    for (std::string& character_name : parsed_account.characters) {
        if (!is_valid_character_name(character_name, error_message))
            return false;
        character_name = normalize_account_name(character_name);
    }
    for (AccountData::CharacterLinkReference& link : parsed_account.character_links) {
        if (!is_valid_character_name(link.character_name, error_message))
            return false;
        link.character_name = normalize_account_name(link.character_name);
    }
    sync_character_links_from_characters(&parsed_account);

    *account = std::move(parsed_account);
    set_error(error_message, "");
    return true;
}

bool write_account_file(const std::string& root_directory, const AccountData& account, std::string* error_message,
    bool* record_committed)
{
    if (!validate_identifier_for_path(account.account_name, "Account name", error_message))
        return false;
    if (!is_valid_email(account.normalized_email, error_message))
        return false;

    const std::string accounts_directory = root_directory + "/accounts";
    const std::string normalized_email = normalize_email(account.normalized_email);
    const std::string bucket_directory = accounts_directory + "/" + account_bucket_for_name(normalized_email);
    const std::string account_directory = account_directory_path_from_email(root_directory, normalized_email);
    const std::string final_path = account_file_path_from_email(root_directory, normalized_email);
    const std::string temp_path = final_path + ".tmp";
    const std::string legacy_flat_path = legacy_account_file_path_from_account_name(root_directory, account.account_name);

    std::string existing_account_path;
    const bool found_existing_account_path = find_account_file_path_by_account_name(root_directory, account.account_name, &existing_account_path, nullptr);

    if (!create_directory_if_missing(accounts_directory, error_message))
        return false;
    if (!create_directory_if_missing(bucket_directory, error_message))
        return false;
    if (!create_directory_if_missing(account_directory, error_message))
        return false;

    AccountData normalized_account = account;
    normalized_account.account_name = normalize_account_name(account.account_name);
    normalized_account.normalized_email = normalized_email;
    sync_character_links_from_characters(&normalized_account);
    for (AccountData::CharacterLinkReference& link : normalized_account.character_links) {
        link.character_name = normalize_account_name(link.character_name);
        link.character_path = character_json_file_name(link.character_name);
        const std::string expected_object_path = objects_json_file_name(link.character_name);
        if (!safe_relative_object_path_or_empty(link.object_path, expected_object_path).empty())
            link.object_path = expected_object_path;
    }

    // Both refusals below happen BEFORE the temp file is opened, deliberately. They used to sit
    // after open_secure_output_file and returned without fclose() and without removing
    // "<final>.tmp", so every one of them leaked a descriptor and left a stray temp file behind.
    // Every account write comes through here -- a password change, linking or unlinking a
    // character, email verification, a roster-sort change on menu exit, a login that had failures
    // to clear -- so a single account whose record will not parse leaked one descriptor per such
    // write and walked a process that runs for weeks towards EMFILE, at which point the server can
    // neither accept connections nor write player files. Nothing between here and the open touches the
    // filesystem, so moving the checks up changes no on-disk ordering on the success path.
    if (path_exists(final_path)) {
        AccountData existing_account_at_target;
        std::string existing_read_error;
        if (!read_account_file_from_path(final_path, &existing_account_at_target, &existing_read_error)) {
            // The post-boot half of the quarantine story. Boot walks every record and reports what
            // it cannot read; nothing walks the tree again afterwards, so a record that goes bad
            // later was reported NOWHERE. We already have the file open and already know it will
            // not parse, so marking it here costs no walk and no extra I/O.
            //
            // Note what this does NOT catch: a quiet account nobody writes to. A plain login writes
            // nothing (interpre.cpp calls clear_account_login_failures only when there is a failure
            // notice to show, and that early-returns when the counters are already zero), so a
            // record that corrupts after boot goes unnoticed until some write touches that account.
            // Marking on the READ path would close that, and is its own change.
            //
            // Marked, not quarantined: quarantine withdraws the record's account-name and character
            // claims with no way back short of a reboot, so a transient EIO would cost a live player
            // every save until the next boot. This changes no lookup's answer -- see
            // note_unreadable_at_runtime. A later successful write re-upserts the entry and clears
            // the mark, so a repaired record heals without a reboot.
            if (account_index_is_authoritative_for(root_directory)
                && account_index::note_unreadable_at_runtime(normalized_email,
                    existing_read_error.empty() ? "Account record could not be read." : existing_read_error)) {
                char log_buffer[MAX_STRING_LENGTH];
                std::snprintf(log_buffer, sizeof(log_buffer),
                    "Account record '%s' could not be read after boot: %s (the account is still indexed; `account index` lists it)",
                    final_path.c_str(),
                    existing_read_error.empty() ? "Account record could not be read." : existing_read_error.c_str());
                log(log_buffer);
                mudlog(log_buffer, BRF, LEVEL_IMMORT, TRUE);
            }
            set_error(error_message, "Existing account file could not be read safely.");
            return false;
        }

        if (normalize_account_name(existing_account_at_target.account_name) != normalized_account.account_name) {
            set_error(error_message, "Account storage path is already occupied by a different account.");
            return false;
        }
    }

    // Nothing this function DELETES may go unread first. The two retirement steps after the commit
    // std::remove() these paths outright, and legacy_flat_path is composed from the ACCOUNT NAME --
    // which a fresh registration derives from the email, so "bob@example.com" derives "bob" and
    // lands exactly on an existing accounts/<bucket>/bob.json. An unparseable record there is
    // quarantined under its own PATH (it has disclosed no email to be keyed by), so the
    // email-keyed creation guards all miss and registration walks straight into deleting a real
    // player's only copy. A record the server cannot read is precisely the one whose contents it
    // must not assume; refuse the write instead, before anything is committed.
    const auto refuses_retirement_target = [&](const std::string& path) -> bool {
        if (path == final_path || !path_exists(path))
            return false;

        AccountData existing_record;
        std::string existing_error;
        if (!read_account_file_from_path(path, &existing_record, &existing_error)) {
            set_error(error_message, "Existing account file could not be read safely.");
            return true;
        }

        if (normalize_account_name(existing_record.account_name) != normalized_account.account_name) {
            set_error(error_message, "Account storage path is already occupied by a different account.");
            return true;
        }

        return false;
    };

    if (refuses_retirement_target(legacy_flat_path))
        return false;
    if (found_existing_account_path && refuses_retirement_target(existing_account_path))
        return false;

    FILE* file = open_secure_output_file(temp_path, error_message);
    if (file == nullptr)
        return false;

    const std::string json = serialize_account_to_json(normalized_account);

    const size_t written_length = std::fwrite(json.data(), sizeof(char), json.size(), file);
    const int close_result = std::fclose(file);
    if (written_length != json.size() || close_result != 0) {
        std::remove(temp_path.c_str());
        set_error(error_message, "Failed to write temporary account file '" + temp_path + "'.");
        return false;
    }

    if (std::rename(temp_path.c_str(), final_path.c_str()) != 0) {
        std::remove(temp_path.c_str());
        set_error(error_message, "Failed to move temporary account file into place: " + std::string(std::strerror(errno)));
        return false;
    }

    // The record is on disk from here on. Every step below can still fail, and the caller must not
    // undo its own half of the operation when one does.
    if (record_committed != nullptr)
        *record_committed = true;

    // The record is at its final path from here on, so the cache and the index must describe it
    // BEFORE the retirement steps below -- each of those can fail and return, and returning with the
    // new account.json in place but the old keys still indexed loses every save for a character that
    // write just linked (it resolves as unlinked, and save_char refuses the legacy fallback).
    //
    // Single account.json write chokepoint: flush the cache so subsequent reads see the new state.
    if (account_cache::is_enabled())
        account_cache::invalidate_all();

    // Same chokepoint. The index re-derives every key from the record just written, so link, unlink,
    // rename and an account-name change are all handled without diffing against what was there
    // before.
    //
    // Root-guarded, the other half of the resolvers' matches_root() guard: final_path was composed
    // against THIS caller's root, and the index holds paths for one tree only. Indexing a foreign
    // root's path would leave lookups against the real (".") tree serving a path into that other
    // tree. Deliberately NOT gated on is_enabled(): the index is maintained on every write whether
    // or not the resolvers currently consult it.
    if (account_index::matches_root(root_directory))
        account_index::upsert(normalized_account, final_path);

    if (found_existing_account_path && existing_account_path != final_path) {
        if (std::remove(existing_account_path.c_str()) != 0 && errno != ENOENT) {
            set_error(error_message, "Failed to retire stale account file '" + existing_account_path + "': " + std::strerror(errno));
            return false;
        }
    }

    if (legacy_flat_path != final_path && std::remove(legacy_flat_path.c_str()) != 0 && errno != ENOENT) {
        set_error(error_message, "Failed to retire legacy account file '" + legacy_flat_path + "': " + std::strerror(errno));
        return false;
    }

    set_error(error_message, "");
    return true;
}

bool read_account_file_uncached(const std::string& root_directory, const std::string& account_name, AccountData* account, std::string* error_message)
{
    if (account == nullptr) {
        set_error(error_message, "Account output parameter must not be null.");
        return false;
    }

    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;

    std::string account_path;
    if (!find_account_file_path_by_account_name(root_directory, account_name, &account_path, error_message))
        return false;

    std::string read_error;
    if (read_account_file_from_path(account_path, account, &read_error)) {
        set_error(error_message, "");
        return true;
    }

    // The by-name twin of the by-email case in find_account_by_email_internal. Keyed by email
    // because that is what the index keys a record by; if the name no longer resolves to one there
    // is nothing to mark, and the read failure still reports normally.
    if (account_index_is_authoritative_for(root_directory)) {
        std::string record_key;
        if (account_index::find_email_by_account_name(account_name, &record_key, nullptr))
            note_unreadable_record_at_runtime(record_key, account_path, read_error);
    }
    set_error(error_message, read_error);
    return false;
}

bool read_account_file(const std::string& root_directory, const std::string& account_name, AccountData* account, std::string* error_message)
{
    // When the cache is enabled (live server) route through it; otherwise (tests, non-server callers)
    // behave exactly as the uncached read. read_account_file_cached's backing resolver is the uncached
    // read above, so there is no recursion.
    if (account_cache::is_enabled())
        return account_cache::read_account_file_cached(root_directory, account_name, account, error_message);
    return read_account_file_uncached(root_directory, account_name, account, error_message);
}

bool read_account_file_by_email(const std::string& root_directory, const std::string& email, AccountData* account, std::string* error_message)
{
    if (!is_valid_email(email, error_message))
        return false;

    return find_account_by_email_internal(root_directory, email, account, error_message);
}

bool read_account_file_by_identifier(const std::string& root_directory, const std::string& identifier, AccountData* account, std::string* error_message)
{
    if (identifier.find('@') != std::string::npos)
        return read_account_file_by_email(root_directory, identifier, account, error_message);

    return read_account_file(root_directory, identifier, account, error_message);
}

bool for_each_account_record_on_disk(const std::string& root_directory,
    const std::function<void(const AccountRecordOnDisk&)>& visitor,
    std::string* error_message)
{
    const std::string accounts_directory = root_directory + "/accounts";
    DIR* accounts_dir = opendir(accounts_directory.c_str());
    if (accounts_dir == nullptr) {
        set_error(error_message, "Failed to open accounts directory '" + accounts_directory + "': " + std::strerror(errno));
        return false;
    }

    int accounts_read_errno = 0;
    for (;;) {
        errno = 0;
        dirent* bucket_entry = readdir(accounts_dir);
        if (bucket_entry == nullptr) {
            accounts_read_errno = errno;
            break;
        }
        // Skip "." / ".." and any other hidden entry (e.g. a stray .DS_Store or editor swap file) --
        // classify_bucket_entry applies the same rule to the entries inside a bucket, so both
        // walkers agree that the whole dotfile class is litter.
        if (bucket_entry->d_name[0] == '.')
            continue;

        const std::string bucket_path = accounts_directory + "/" + bucket_entry->d_name;

        // A bucket the walk cannot read is EVERY account in it leaving the index at once, and
        // account_storage_contains_unreadable_records hard-fails on the same condition. Skipping one
        // silently left a state where the guard refused every registration on the server while
        // nothing was quarantined and nothing was logged. Visit it as one unreadable record instead:
        // that is what makes it visible in `account index`, counted at boot, and armed as the
        // guard's escape hatch. Keyed by the bucket's own path -- see
        // account_index_key_for_unreadable_bucket. Every way of failing to read a bucket goes
        // through here, because the index's authority does not depend on WHICH call failed.
        const auto visit_unreadable_bucket = [&](const std::string& failure_reason) {
            AccountRecordOnDisk record;
            record.directory_entry_name = bucket_entry->d_name;
            record.record_path = bucket_path;
            record.directory_layout = false;
            record.parsed = false;
            record.unreadable_bucket = true;
            record.failure_reason = failure_reason;
            visitor(record);
        };

        struct stat bucket_info { };
        if (stat(bucket_path.c_str(), &bucket_info) != 0) {
            // ENOENT is the entry disappearing between readdir and stat: nothing to read, and
            // nothing hidden either. Anything else hides a bucket exactly the way a failing opendir
            // does -- an accounts/ directory that lost its search bit fails EVERY bucket stat with
            // EACCES, which used to yield an empty index that still called itself authoritative.
            if (errno == ENOENT)
                continue;
            visit_unreadable_bucket("Failed to stat account bucket directory '" + bucket_path + "': " + std::strerror(errno));
            continue;
        }
        if (!S_ISDIR(bucket_info.st_mode))
            continue;

        DIR* bucket_dir = opendir(bucket_path.c_str());
        if (bucket_dir == nullptr) {
            visit_unreadable_bucket("Failed to open account bucket directory '" + bucket_path + "': " + std::strerror(errno));
            continue;
        }

        // readdir() returns NULL both at the end of the directory and on an error, and the two are
        // told apart only by errno -- which every stat and file read inside this loop also sets, so
        // it is cleared immediately before each call rather than once around the loop.
        int bucket_read_errno = 0;
        for (;;) {
            errno = 0;
            dirent* account_entry = readdir(bucket_dir);
            if (account_entry == nullptr) {
                bucket_read_errno = errno;
                break;
            }
            // One shared rule for "what is even a record", used by this walker and by
            // account_storage_contains_unreadable_records. Litter is not visited at all, so it never
            // counts against MAX_QUARANTINED_RECORDS_AT_BOOT; a candidate that cannot be stat'ed is
            // VISITED with parsed == false, which is what quarantines it.
            const BucketEntryClassification classification = classify_bucket_entry(bucket_path, account_entry->d_name);
            if (classification.kind == BucketEntryKind::NotARecord)
                continue;

            AccountRecordOnDisk record;
            record.directory_entry_name = account_entry->d_name;
            record.record_path = classification.record_path;
            record.directory_layout = classification.directory_layout;

            if (classification.kind == BucketEntryKind::Unreadable) {
                record.parsed = false;
                record.failure_reason = classification.failure_reason;
                visitor(record);
                continue;
            }

            AccountData parsed_account;
            std::string read_error;
            if (read_account_file_from_path(classification.record_path, &parsed_account, &read_error)) {
                record.parsed = true;
                record.account = std::move(parsed_account);
            } else {
                record.parsed = false;
                // Never an empty reason. The reader can return false without setting a message, and
                // an empty reason becomes a log line and a wizard "QUARANTINED <path>: " line that
                // trail off into nothing -- the one piece of evidence about a record we refused,
                // with the evidence missing.
                record.failure_reason = read_error.empty() ? "Account record could not be read." : read_error;
            }

            visitor(record);
        }

        closedir(bucket_dir);

        // Reported after the directory is closed, and after every record the walk DID see: the
        // records already visited are real, and the bucket is unreadable in addition to them.
        if (bucket_read_errno != 0)
            visit_unreadable_bucket("Failed to read account bucket directory '" + bucket_path + "': " + std::strerror(bucket_read_errno));
    }

    closedir(accounts_dir);
    if (accounts_read_errno != 0) {
        // The accounts directory itself is the one failure this walk reports as its own: there is no
        // bucket to attribute it to, and an unknown number of buckets went unseen.
        set_error(error_message, "Failed to read accounts directory '" + accounts_directory + "': " + std::strerror(accounts_read_errno));
        return false;
    }
    set_error(error_message, "");
    return true;
}

std::string account_index_quarantine_key(const AccountRecordOnDisk& record)
{
    if (record.directory_layout) {
        // Email-shaped, so normalize it: the server writes this directory name normalized, but a
        // hand-made or restored directory need not be, and on a case-sensitive filesystem the raw
        // name is a key is_quarantined() can never produce -- leaving the address readable as free
        // and registration free to proceed over an unreadable record. The path-shaped cases below
        // are deliberately NOT normalized; lowercasing a real path corrupts it.
        return normalize_email(record.directory_entry_name);
    }
    if (record.parsed) {
        // A parsed legacy flat record is keyed by its own email UNLESS that email is empty (the
        // "no usable email address" case, db.cpp), in which case it has revealed nothing to key it
        // by and falls through to record_path below, exactly like the unparsed case.
        const std::string email = normalize_email(record.account.normalized_email);
        if (!email.empty())
            return email;
    }
    return record.record_path;
}

std::string account_character_directory(const std::string& root_directory, const std::string& account_name, const std::string&)
{
    const std::string account_storage_key = resolve_account_storage_key(root_directory, account_name);
    if (account_storage_key.empty())
        return root_directory + "/accounts/" + std::string(kInvalidAccountDirectoryName);
    return root_directory + "/accounts/" + account_bucket_for_name(account_storage_key) + "/" + account_storage_key;
}

bool is_invalid_account_storage_directory(const std::string& directory_path)
{
    const size_t separator = directory_path.find_last_of('/');
    const std::string leaf = (separator == std::string::npos) ? directory_path : directory_path.substr(separator + 1);
    return leaf == kInvalidAccountDirectoryName;
}

std::string account_character_snapshot_path(const std::string& root_directory, const std::string& account_name, const std::string& character_name)
{
    return account_character_directory(root_directory, account_name, character_name) + "/" + character_asset_slug(character_name) + ".migration.json";
}

std::string account_character_player_path(const std::string& root_directory, const std::string& account_name, const std::string& character_name)
{
    return account_character_directory(root_directory, account_name, character_name) + "/" + character_json_file_name(character_name);
}

std::string account_character_object_path(const std::string& root_directory, const std::string& account_name, const std::string& character_name)
{
    return account_character_directory(root_directory, account_name, character_name) + "/" + objects_json_file_name(character_name);
}

std::string account_character_exploits_path(const std::string& root_directory, const std::string& account_name, const std::string& character_name)
{
    return account_character_directory(root_directory, account_name, character_name) + "/" + exploits_json_file_name(character_name);
}

std::string serialize_character_migration_to_json(const CharacterMigrationData& migration)
{
    auto write_snapshot = [](std::ostringstream& output, const char* name, const LegacyAssetSnapshot& snapshot) {
        output << "  \"" << name << "\": {\n";
        output << "    \"source_path\": \"" << json_utils::escape_json_string(snapshot.source_path) << "\",\n";
        output << "    \"encoding\": \"" << json_utils::escape_json_string(snapshot.encoding) << "\",\n";
        output << "    \"content\": \"" << json_utils::escape_json_string(snapshot.content) << "\",\n";
        output << "    \"present\": " << (snapshot.present ? "true" : "false") << "\n";
        output << "  }";
    };

    std::ostringstream output;
    output << "{\n";
    output << "  \"version\": " << migration.version << ",\n";
    output << "  \"account_name\": \"" << json_utils::escape_json_string(migration.account_name) << "\",\n";
    output << "  \"character_name\": \"" << json_utils::escape_json_string(migration.character_name) << "\",\n";
    output << "  \"migrated_at\": " << migration.migrated_at << ",\n";
    // The on-disk migration artifact is transitional only. Do not persist raw
    // legacy player bytes that still carry legacy password/host state.
    write_snapshot(output, "object_file", migration.object_file);
    output << ",\n";
    write_snapshot(output, "exploits_file", migration.exploits_file);
    output << "\n}\n";
    return output.str();
}

bool deserialize_character_migration_from_json(const std::string& json, CharacterMigrationData* migration, std::string* error_message)
{
    if (migration == nullptr) {
        set_error(error_message, "Migration output parameter must not be null.");
        return false;
    }

    CharacterMigrationData parsed_migration;
    json_utils::JsonReader reader(json);
    if (!reader.parse_root_object([&parsed_migration](const std::string& key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
            return parse_migration_property(key, nested_reader, &parsed_migration, nested_error_message);
        },
            error_message))
        return false;

    if (parsed_migration.version != ACCOUNT_SCHEMA_VERSION) {
        set_error(error_message, "Unsupported character migration schema version.");
        return false;
    }

    if (!is_valid_account_name(parsed_migration.account_name, error_message))
        return false;

    parsed_migration.account_name = normalize_account_name(parsed_migration.account_name);
    parsed_migration.character_name = normalize_account_name(parsed_migration.character_name);

    std::string decoded_content;
    if (parsed_migration.player_file.present && parsed_migration.player_file.encoding == "hex" && !hex_decode(parsed_migration.player_file.content, &decoded_content, error_message))
        return false;
    if (parsed_migration.object_file.present && parsed_migration.object_file.encoding == "hex" && !hex_decode(parsed_migration.object_file.content, &decoded_content, error_message))
        return false;
    if (parsed_migration.exploits_file.present && parsed_migration.exploits_file.encoding == "hex" && !hex_decode(parsed_migration.exploits_file.content, &decoded_content, error_message))
        return false;

    *migration = std::move(parsed_migration);
    set_error(error_message, "");
    return true;
}
