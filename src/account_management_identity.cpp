std::string format_character_name_for_display(const std::string& character_name)
{
    if (character_name.empty())
        return character_name;

    std::string formatted_name = character_name;
    formatted_name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(formatted_name[0])));
    return formatted_name;
}

std::string normalize_account_name(const std::string& account_name)
{
    return to_lower_copy(trim_copy(account_name));
}

std::string normalize_email(const std::string& email)
{
    return to_lower_copy(trim_copy(email));
}

bool is_valid_account_name(const std::string& account_name, std::string* error_message)
{
    const std::string normalized_name = normalize_account_name(account_name);
    if (normalized_name.length() < MIN_ACCOUNT_NAME_LENGTH) {
        set_error(error_message, "Account names must be at least 3 characters long.");
        return false;
    }

    if (normalized_name.length() > MAX_ACCOUNT_NAME_LENGTH) {
        set_error(error_message, "Account names must be 20 characters or fewer.");
        return false;
    }

    for (char character : normalized_name) {
        if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_' && character != '-') {
            set_error(error_message, "Account names may only contain letters, numbers, '-' and '_'.");
            return false;
        }
    }

    set_error(error_message, "");
    return true;
}

bool is_valid_email(const std::string& email, std::string* error_message)
{
    const std::string normalized_email = normalize_email(email);
    const size_t at_position = normalized_email.find('@');
    if (normalized_email.empty() || at_position == std::string::npos || at_position == 0 || at_position + 1 >= normalized_email.length()) {
        set_error(error_message, "Email addresses must contain text before and after '@'.");
        return false;
    }

    for (char character : normalized_email) {
        if (std::iscntrl(static_cast<unsigned char>(character)) || std::isspace(static_cast<unsigned char>(character))) {
            set_error(error_message, "Email addresses may not contain whitespace or control characters.");
            return false;
        }
    }

    if (normalized_email.find(',', at_position) != std::string::npos || normalized_email.find(';', at_position) != std::string::npos) {
        set_error(error_message, "Email addresses must contain exactly one recipient address.");
        return false;
    }

    const std::string local_part = normalized_email.substr(0, at_position);
    const std::string domain_part = normalized_email.substr(at_position + 1);
    if (domain_part.find('.') == std::string::npos) {
        set_error(error_message, "Email addresses must include a domain like 'example.com'.");
        return false;
    }

    auto is_valid_local_character = [](char character) {
        return std::isalnum(static_cast<unsigned char>(character))
            || character == '.'
            || character == '_'
            || character == '%'
            || character == '+'
            || character == '-';
    };

    auto is_valid_domain_character = [](char character) {
        return std::isalnum(static_cast<unsigned char>(character))
            || character == '.'
            || character == '-';
    };

    if (local_part.front() == '.' || local_part.back() == '.' || domain_part.front() == '.' || domain_part.back() == '.') {
        set_error(error_message, "Email addresses may not start or end a section with '.'.");
        return false;
    }

    if (local_part.find("..") != std::string::npos || domain_part.find("..") != std::string::npos) {
        set_error(error_message, "Email addresses may not contain repeated '.'.");
        return false;
    }

    for (char character : local_part) {
        if (!is_valid_local_character(character)) {
            set_error(error_message, "Email addresses contain unsupported characters.");
            return false;
        }
    }

    for (char character : domain_part) {
        if (!is_valid_domain_character(character)) {
            set_error(error_message, "Email addresses contain unsupported characters.");
            return false;
        }
    }

    set_error(error_message, "");
    return true;
}

bool is_valid_password(const std::string& password, std::string* error_message)
{
    if (static_cast<int>(password.length()) < MIN_PASSWORD_LENGTH) {
        set_error(error_message, "Passwords must be at least 8 characters long.");
        return false;
    }

    bool has_uppercase = false;
    bool has_lowercase = false;
    bool has_number = false;

    for (char character : password) {
        const unsigned char normalized_character = static_cast<unsigned char>(character);
        has_uppercase = has_uppercase || std::isupper(normalized_character);
        has_lowercase = has_lowercase || std::islower(normalized_character);
        has_number = has_number || std::isdigit(normalized_character);
    }

    if (!has_uppercase) {
        set_error(error_message, "Passwords must include at least one uppercase letter.");
        return false;
    }

    if (!has_lowercase) {
        set_error(error_message, "Passwords must include at least one lowercase letter.");
        return false;
    }

    if (!has_number) {
        set_error(error_message, "Passwords must include at least one number.");
        return false;
    }

    set_error(error_message, "");
    return true;
}

bool generate_password_credentials(const std::string& password, std::string* password_hash, std::string* password_salt, std::string* error_message)
{
    if (password_hash == nullptr || password_salt == nullptr) {
        set_error(error_message, "Password hash and salt outputs must not be null.");
        return false;
    }

    std::string validation_error;
    if (!is_valid_password(password, &validation_error)) {
        set_error(error_message, validation_error);
        return false;
    }

    return generate_hash_for_secret(password, password_hash, password_salt, error_message);
}

bool verify_password(const std::string& password, const std::string& password_hash)
{
    if (password_hash.empty())
        return false;

    char* hashed_password = crypt(password.c_str(), password_hash.c_str());
    if (hashed_password == nullptr)
        return false;

    return password_hash == hashed_password;
}

bool initialize_new_account(const std::string& account_name, const std::string& email, const std::string& password, long created_at, AccountData* account, std::string* error_message)
{
    if (account == nullptr) {
        set_error(error_message, "Account output parameter must not be null.");
        return false;
    }

    std::string validation_error;
    if (!is_valid_account_name(account_name, &validation_error)) {
        set_error(error_message, validation_error);
        return false;
    }

    AccountData new_account;
    new_account.account_name = normalize_account_name(account_name);
    new_account.normalized_email = normalize_email(email);
    new_account.created_at = created_at;
    new_account.updated_at = created_at;
    new_account.email_verified = false;
    new_account.blocked = false;

    if (!generate_password_credentials(password, &new_account.password_hash, &new_account.password_salt, error_message))
        return false;

    *account = std::move(new_account);
    set_error(error_message, "");
    return true;
}

bool prepare_email_verification_code(AccountData* account, long sent_at, std::string* verification_code, std::string* error_message)
{
    if (account == nullptr || verification_code == nullptr) {
        set_error(error_message, "Account and verification-code outputs must not be null.");
        return false;
    }

    const std::string generated_code = generate_numeric_verification_code();
    if (generated_code.empty()) {
        set_error(error_message, "Failed to generate an email verification code.");
        return false;
    }

    std::string verification_salt;
    if (!generate_hash_for_secret(generated_code, &account->verification_code_hash, &verification_salt, error_message))
        return false;

    account->verification_code_sent_at = sent_at;
    account->verification_code_expires_at = sent_at + EMAIL_VERIFICATION_WINDOW_SECONDS;
    account->verification_attempt_count = 0;
    account->verification_last_attempt_at = 0;
    account->updated_at = sent_at;
    *verification_code = generated_code;

    set_error(error_message, "");
    return true;
}

bool confirm_email_verification_code(AccountData* account, const std::string& verification_code, const std::string& verified_by, long verified_at, std::string* error_message)
{
    if (account == nullptr) {
        set_error(error_message, "Account output parameter must not be null.");
        return false;
    }

    const std::string trimmed_code = trim_copy(verification_code);
    if (trimmed_code.empty()) {
        set_error(error_message, "Verification code must not be empty.");
        return false;
    }

    if (account->email_verified) {
        set_error(error_message, "");
        return true;
    }

    if (account->verification_code_hash.empty() || account->verification_code_expires_at == 0) {
        set_error(error_message, "No email verification code is currently pending.");
        return false;
    }

    if (verified_at > account->verification_code_expires_at) {
        set_error(error_message, "That verification code has expired.");
        return false;
    }

    if (!verify_password(trimmed_code, account->verification_code_hash)) {
        ++account->verification_attempt_count;
        account->verification_last_attempt_at = verified_at;
        account->updated_at = verified_at;
        if (account->verification_attempt_count >= MAX_EMAIL_VERIFICATION_ATTEMPTS) {
            account->verification_code_hash.clear();
            account->verification_code_expires_at = 0;
            set_error(error_message, "Too many invalid verification attempts. Please request a new verification code.");
            return false;
        }

        set_error(error_message, "That verification code is invalid.");
        return false;
    }

    verify_email(account, verified_by, verified_at);
    set_error(error_message, "");
    return true;
}

void verify_email(AccountData* account, const std::string& verified_by, long verified_at)
{
    if (account == nullptr)
        return;

    account->email_verified = true;
    account->email_verified_by = verified_by;
    account->email_verified_at = verified_at;
    account->verification_code_hash.clear();
    account->verification_code_sent_at = 0;
    account->verification_code_expires_at = 0;
    account->verification_attempt_count = 0;
    account->verification_last_attempt_at = 0;
    account->updated_at = verified_at;
}

void unverify_email(AccountData* account)
{
    if (account == nullptr)
        return;

    account->email_verified = false;
    account->email_verified_by.clear();
    account->email_verified_at = 0;
    account->verification_code_hash.clear();
    account->verification_code_sent_at = 0;
    account->verification_code_expires_at = 0;
    account->verification_attempt_count = 0;
    account->verification_last_attempt_at = 0;
}

bool account_has_character(const AccountData& account, const std::string& character_name)
{
    const std::string normalized_character_name = normalize_account_name(character_name);
    return std::find_if(account.characters.begin(), account.characters.end(),
               [&normalized_character_name](const std::string& linked_character_name) {
                   return normalize_account_name(linked_character_name) == normalized_character_name;
               })
        != account.characters.end();
}

bool select_linked_character(const AccountData& account, const std::string& character_name, std::string* normalized_character_name, std::string* error_message)
{
    if (normalized_character_name == nullptr) {
        set_error(error_message, "Character output parameter must not be null.");
        return false;
    }

    const std::string trimmed_selection = trim_copy(character_name);
    if (trimmed_selection.empty()) {
        set_error(error_message, "Character selection must not be empty.");
        return false;
    }

    bool selection_is_numeric = !trimmed_selection.empty();
    for (char character : trimmed_selection) {
        if (!std::isdigit(static_cast<unsigned char>(character))) {
            selection_is_numeric = false;
            break;
        }
    }

    if (selection_is_numeric) {
        const size_t displayed_count = std::min(account.characters.size(), kMaxDisplayedAccountCharacters);
        char* end_ptr = nullptr;
        const unsigned long selected_index = std::strtoul(trimmed_selection.c_str(), &end_ptr, 10);
        if (end_ptr == nullptr || *end_ptr != '\0' || selected_index == 0 || selected_index > displayed_count) {
            set_error(error_message, "Select a linked character by number, or enter 0 to return to the account menu.");
            return false;
        }

        *normalized_character_name = normalize_account_name(account.characters[selected_index - 1]);
        set_error(error_message, "");
        return true;
    }

    set_error(error_message, "Select a linked character by number, or enter 0 to return to the account menu.");
    return false;
}

bool add_character_to_account(AccountData* account, const std::string& character_name, std::string* error_message)
{
    if (account == nullptr) {
        set_error(error_message, "Account output parameter must not be null.");
        return false;
    }

    const std::string normalized_character_name = normalize_account_name(character_name);
    if (normalized_character_name.empty()) {
        set_error(error_message, "Character names must not be empty.");
        return false;
    }

    if (account_has_character(*account, normalized_character_name)) {
        set_error(error_message, "Character is already linked to this account.");
        return false;
    }

    account->characters.push_back(normalized_character_name);
    sync_character_links_from_characters(account);
    set_error(error_message, "");
    return true;
}

void block_account(AccountData* account, const std::string& blocked_by, const std::string& block_reason, long blocked_at)
{
    if (account == nullptr)
        return;

    account->blocked = true;
    account->blocked_by = blocked_by;
    account->block_reason = block_reason;
    account->blocked_at = blocked_at;
    account->updated_at = blocked_at;
}

void unblock_account(AccountData* account)
{
    if (account == nullptr)
        return;

    account->blocked = false;
    account->block_reason.clear();
    account->blocked_by.clear();
    account->blocked_at = 0;
}

bool reset_account_password(AccountData* account, const std::string& new_password, const std::string& reset_by, long reset_at, std::string* error_message)
{
    if (account == nullptr) {
        set_error(error_message, "Account output parameter must not be null.");
        return false;
    }

    std::string password_hash;
    std::string password_salt;
    if (!generate_password_credentials(new_password, &password_hash, &password_salt, error_message))
        return false;

    account->password_hash = password_hash;
    account->password_salt = password_salt;
    account->password_reset_by = reset_by;
    account->password_reset_at = reset_at;
    account->updated_at = reset_at;

    set_error(error_message, "");
    return true;
}

bool create_account(const std::string& root_directory, const std::string& account_name, const std::string& email, const std::string& password, long created_at, AccountData* account, std::string* error_message)
{
    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;
    if (!is_valid_email(email, error_message))
        return false;

    std::string storage_error;
    if (account_storage_contains_unreadable_records(root_directory, &storage_error)) {
        set_error(error_message, storage_error);
        return false;
    }

    AccountData existing_email_account;
    if (find_account_by_email_internal(root_directory, email, &existing_email_account, nullptr)) {
        set_error(error_message, "An account already exists for that email address.");
        return false;
    }

    AccountData existing_account;
    std::string read_error;
    if (read_account_file(root_directory, account_name, &existing_account, &read_error)) {
        set_error(error_message, "Account already exists.");
        return false;
    }

    std::string existing_account_path;
    if (find_account_file_path_by_account_name(root_directory, account_name, &existing_account_path, nullptr)) {
        set_error(error_message, "Existing account file could not be read safely.");
        return false;
    }

    AccountData new_account;
    if (!initialize_new_account(account_name, email, password, created_at, &new_account, error_message))
        return false;

    if (!write_account_file(root_directory, new_account, error_message))
        return false;

    if (account)
        *account = new_account;

    set_error(error_message, "");
    return true;
}

bool create_account_for_email(const std::string& root_directory, const std::string& email, const std::string& password, long created_at, AccountData* account, std::string* error_message)
{
    if (!is_valid_email(email, error_message))
        return false;

    std::string storage_error;
    if (account_storage_contains_unreadable_records(root_directory, &storage_error)) {
        set_error(error_message, storage_error);
        return false;
    }

    AccountData existing_account;
    if (find_account_by_email_internal(root_directory, email, &existing_account, nullptr)) {
        set_error(error_message, "An account already exists for that email address.");
        return false;
    }

    const std::string normalized_email = normalize_email(email);
    for (int sequence_number = 0; sequence_number < 1000; ++sequence_number) {
        const std::string candidate_name = make_account_name_candidate_from_email(normalized_email, sequence_number);
        AccountData candidate_account;
        std::string create_error;
        if (create_account(root_directory, candidate_name, normalized_email, password, created_at, &candidate_account, &create_error)) {
            if (account)
                *account = candidate_account;

            set_error(error_message, "");
            return true;
        }

        if (create_error != "Account already exists.") {
            set_error(error_message, create_error);
            return false;
        }
    }

    set_error(error_message, "Unable to allocate a unique account record for that email address.");
    return false;
}

bool authenticate_account(const std::string& root_directory, const std::string& account_name, const std::string& password, AccountData* account, std::string* error_message)
{
    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;

    AccountData stored_account;
    if (!read_account_file(root_directory, account_name, &stored_account, nullptr)) {
        set_error(error_message, "Account authentication failed.");
        return false;
    }

    if (stored_account.blocked) {
        set_error(error_message, "Account authentication failed.");
        return false;
    }

    if (!verify_password(password, stored_account.password_hash)) {
        set_error(error_message, "Account authentication failed.");
        return false;
    }

    if (!stored_account.email_verified) {
        if (account)
            *account = stored_account;
        set_error(error_message, "Account email verification is still pending.");
        return false;
    }

    if (account)
        *account = stored_account;

    set_error(error_message, "");
    return true;
}

bool authenticate_account_by_email(const std::string& root_directory, const std::string& email, const std::string& password, AccountData* account, std::string* error_message)
{
    if (!is_valid_email(email, error_message))
        return false;

    AccountData stored_account;
    if (!find_account_by_email_internal(root_directory, email, &stored_account, nullptr)) {
        set_error(error_message, "Account authentication failed.");
        return false;
    }

    return authenticate_account(root_directory, stored_account.account_name, password, account, error_message);
}

bool start_email_verification(const std::string& root_directory, const std::string& account_name, long sent_at, AccountData* account, std::string* error_message)
{
    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;

    AccountData stored_account;
    if (!read_account_file(root_directory, account_name, &stored_account, error_message))
        return false;

    if (stored_account.email_verified) {
        if (account)
            *account = stored_account;
        set_error(error_message, "");
        return true;
    }

    if (stored_account.verification_code_sent_at != 0
        && sent_at < stored_account.verification_code_sent_at + EMAIL_VERIFICATION_RESEND_COOLDOWN_SECONDS) {
        const long retry_after_seconds = (stored_account.verification_code_sent_at + EMAIL_VERIFICATION_RESEND_COOLDOWN_SECONDS) - sent_at;
        set_error(error_message, "Please wait " + std::to_string(retry_after_seconds) + " seconds before requesting another verification code.");
        return false;
    }

    std::string verification_code;
    if (!prepare_email_verification_code(&stored_account, sent_at, &verification_code, error_message))
        return false;

    if (!write_account_file(root_directory, stored_account, error_message))
        return false;

    if (!send_verification_email(stored_account, verification_code, error_message))
        return false;

    if (account)
        *account = stored_account;

    set_error(error_message, "");
    return true;
}

bool complete_email_verification(const std::string& root_directory, const std::string& account_name, const std::string& verification_code, const std::string& verified_by, long verified_at, AccountData* account, std::string* error_message)
{
    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;

    AccountData stored_account;
    if (!read_account_file(root_directory, account_name, &stored_account, error_message))
        return false;

    const std::string original_verification_code_hash = stored_account.verification_code_hash;
    const int original_attempt_count = stored_account.verification_attempt_count;
    const long original_last_attempt_at = stored_account.verification_last_attempt_at;
    const long original_updated_at = stored_account.updated_at;

    if (!confirm_email_verification_code(&stored_account, verification_code, verified_by, verified_at, error_message)) {
        if (stored_account.verification_code_hash != original_verification_code_hash
            || stored_account.verification_attempt_count != original_attempt_count
            || stored_account.verification_last_attempt_at != original_last_attempt_at
            || stored_account.updated_at != original_updated_at) {
            std::string persistence_error;
            if (!write_account_file(root_directory, stored_account, &persistence_error)) {
                set_error(error_message, persistence_error);
            }
        }
        return false;
    }

    if (!write_account_file(root_directory, stored_account, error_message))
        return false;

    if (account)
        *account = stored_account;

    set_error(error_message, "");
    return true;
}

bool find_linked_character_owner_account_uncached(const std::string& root_directory, const std::string& character_name, std::string* owner_account_name, std::string* error_message)
{
    if (!validate_identifier_for_path(character_name, "Character name", error_message))
        return false;

    return find_character_owner_account(root_directory, character_name, owner_account_name, error_message);
}

bool find_linked_character_owner_account(const std::string& root_directory, const std::string& character_name, std::string* owner_account_name, std::string* error_message)
{
    // Route through the owner cache when enabled (live server); otherwise behave as the uncached
    // resolver. The cache's backing resolver is the uncached function above (no recursion).
    if (account_cache::is_enabled())
        return account_cache::find_linked_character_owner_account_cached(root_directory, character_name, owner_account_name, error_message);
    return find_linked_character_owner_account_uncached(root_directory, character_name, owner_account_name, error_message);
}

bool admin_link_character(const std::string& root_directory, const std::string& account_name, const std::string& character_name, long updated_at, AccountData* account, std::string* error_message)
{
    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;
    if (!validate_identifier_for_path(character_name, "Character name", error_message))
        return false;

    AccountData stored_account;
    if (!read_account_file(root_directory, account_name, &stored_account, error_message))
        return false;

    std::string owner_account_name;
    if (!find_character_owner_account(root_directory, character_name, &owner_account_name, error_message))
        return false;
    if (!owner_account_name.empty() && normalize_account_name(owner_account_name) != normalize_account_name(account_name)) {
        set_error(error_message, "Character is already linked to account '" + owner_account_name + "'.");
        return false;
    }

    if (!add_character_to_account(&stored_account, character_name, error_message))
        return false;

    stored_account.updated_at = updated_at;
    if (!write_account_file(root_directory, stored_account, error_message))
        return false;

    if (account)
        *account = stored_account;

    set_error(error_message, "");
    return true;
}

bool admin_link_and_migrate_character(const std::string& root_directory, const std::string& account_name, const std::string& character_name, long updated_at, AccountData* account, CharacterMigrationData* migration, std::string* error_message)
{
    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;
    if (!validate_identifier_for_path(character_name, "Character name", error_message))
        return false;

    AccountData stored_account;
    if (!read_account_file(root_directory, account_name, &stored_account, error_message))
        return false;

    const bool already_linked = account_has_character(stored_account, character_name);

    if (!already_linked) {
        std::string owner_account_name;
        if (!find_character_owner_account(root_directory, character_name, &owner_account_name, error_message))
            return false;
        if (!owner_account_name.empty() && normalize_account_name(owner_account_name) != normalize_account_name(account_name)) {
            set_error(error_message, "Character is already linked to account '" + owner_account_name + "'.");
            return false;
        }
    }

    CharacterMigrationData migrated_character;
    if (!migrate_legacy_character_by_name(root_directory, account_name, character_name, updated_at, &migrated_character, error_message))
        return false;

    if (!already_linked) {
        if (!add_character_to_account(&stored_account, character_name, error_message))
            return false;

        stored_account.updated_at = updated_at;
        if (!write_account_file(root_directory, stored_account, error_message))
            return false;
    }

    if (account)
        *account = stored_account;
    if (migration)
        *migration = migrated_character;

    set_error(error_message, "");
    return true;
}

bool admin_block_account(const std::string& root_directory, const std::string& account_name, const std::string& blocked_by, const std::string& block_reason, long blocked_at, AccountData* account, std::string* error_message)
{
    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;

    AccountData stored_account;
    if (!read_account_file(root_directory, account_name, &stored_account, error_message))
        return false;

    block_account(&stored_account, blocked_by, block_reason, blocked_at);
    if (!write_account_file(root_directory, stored_account, error_message))
        return false;

    if (account)
        *account = stored_account;

    set_error(error_message, "");
    return true;
}

bool admin_verify_email(const std::string& root_directory, const std::string& account_name, const std::string& verified_by, long verified_at, AccountData* account, std::string* error_message)
{
    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;

    AccountData stored_account;
    if (!read_account_file(root_directory, account_name, &stored_account, error_message))
        return false;

    verify_email(&stored_account, verified_by, verified_at);
    if (!write_account_file(root_directory, stored_account, error_message))
        return false;

    if (account)
        *account = stored_account;

    set_error(error_message, "");
    return true;
}

bool admin_unverify_email(const std::string& root_directory, const std::string& account_name, long updated_at, AccountData* account, std::string* error_message)
{
    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;

    AccountData stored_account;
    if (!read_account_file(root_directory, account_name, &stored_account, error_message))
        return false;

    unverify_email(&stored_account);
    stored_account.updated_at = updated_at;
    if (!write_account_file(root_directory, stored_account, error_message))
        return false;

    if (account)
        *account = stored_account;

    set_error(error_message, "");
    return true;
}

bool admin_unblock_account(const std::string& root_directory, const std::string& account_name, long updated_at, AccountData* account, std::string* error_message)
{
    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;

    AccountData stored_account;
    if (!read_account_file(root_directory, account_name, &stored_account, error_message))
        return false;

    unblock_account(&stored_account);
    stored_account.updated_at = updated_at;
    if (!write_account_file(root_directory, stored_account, error_message))
        return false;

    if (account)
        *account = stored_account;

    set_error(error_message, "");
    return true;
}

bool admin_reset_password(const std::string& root_directory, const std::string& account_name, const std::string& new_password, const std::string& reset_by, long reset_at, AccountData* account, std::string* error_message)
{
    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;

    AccountData stored_account;
    if (!read_account_file(root_directory, account_name, &stored_account, error_message))
        return false;

    if (!reset_account_password(&stored_account, new_password, reset_by, reset_at, error_message))
        return false;

    if (!write_account_file(root_directory, stored_account, error_message))
        return false;

    if (account)
        *account = stored_account;

    set_error(error_message, "");
    return true;
}

bool admin_delete_linked_character(const std::string& root_directory, const std::string& account_name, const std::string& character_name, long updated_at, AccountData* account, std::string* error_message)
{
    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;
    if (!validate_identifier_for_path(character_name, "Character name", error_message))
        return false;

    AccountData stored_account;
    if (!read_account_file(root_directory, account_name, &stored_account, error_message))
        return false;

    const std::string normalized_character_name = normalize_account_name(character_name);
    if (!account_has_character(stored_account, normalized_character_name)) {
        set_error(error_message, "Character is not linked to this account.");
        return false;
    }

    if (!validate_account_owned_character_path(stored_account, normalized_character_name, error_message))
        return false;
    if (!validate_account_owned_object_path(stored_account, normalized_character_name, error_message))
        return false;
    if (!validate_account_owned_exploits_path(stored_account, normalized_character_name, error_message))
        return false;

    const std::string character_path = resolved_character_path(stored_account, root_directory, normalized_character_name);
    const std::string object_path = resolved_object_path(stored_account, root_directory, normalized_character_name);
    const std::string exploits_path = resolved_exploits_path(stored_account, root_directory, normalized_character_name);

    struct StagedRemoval {
        std::string original_path;
        std::string staged_path;
        const char* label = "";
        bool existed = false;
    };

    std::vector<StagedRemoval> staged_removals = {
        { character_path, character_path + ".delete-pending", "account character file", false },
        { object_path, object_path + ".delete-pending", "account object file", false },
        { exploits_path, exploits_path + ".delete-pending", "account exploits file", false }
    };

    auto stage_file_removal = [&](StagedRemoval* staged_removal) -> bool {
        struct stat file_info {};
        if (stat(staged_removal->original_path.c_str(), &file_info) != 0) {
            if (errno == ENOENT) {
                staged_removal->existed = false;
                return true;
            }

            set_error(error_message, std::string("Failed to inspect ") + staged_removal->label + " '" + staged_removal->original_path + "': " + std::strerror(errno));
            return false;
        }

        staged_removal->existed = true;
        if (std::remove(staged_removal->staged_path.c_str()) != 0 && errno != ENOENT) {
            set_error(error_message, std::string("Failed to prepare staged removal for ") + staged_removal->label + " '" + staged_removal->original_path + "': " + std::strerror(errno));
            return false;
        }

        if (std::rename(staged_removal->original_path.c_str(), staged_removal->staged_path.c_str()) != 0) {
            set_error(error_message, std::string("Failed to stage ") + staged_removal->label + " '" + staged_removal->original_path + "' for deletion: " + std::strerror(errno));
            return false;
        }

        return true;
    };

    auto restore_staged_removals = [&]() {
        for (auto it = staged_removals.rbegin(); it != staged_removals.rend(); ++it) {
            if (!it->existed)
                continue;
            if (std::rename(it->staged_path.c_str(), it->original_path.c_str()) != 0) {
                std::fprintf(stderr, "SYSERR: Failed to restore staged account deletion path '%s': %s\n",
                    it->original_path.c_str(), std::strerror(errno));
            }
        }
    };

    for (StagedRemoval& staged_removal : staged_removals) {
        if (!stage_file_removal(&staged_removal)) {
            restore_staged_removals();
            return false;
        }
    }

    AccountData updated_account = stored_account;
    updated_account.characters.erase(std::remove(updated_account.characters.begin(), updated_account.characters.end(), normalized_character_name),
        updated_account.characters.end());
    updated_account.character_links.erase(
        std::remove_if(updated_account.character_links.begin(), updated_account.character_links.end(),
            [&](const AccountData::CharacterLinkReference& link) { return link.character_name == normalized_character_name; }),
        updated_account.character_links.end());
    updated_account.updated_at = updated_at;

    if (!write_account_file(root_directory, updated_account, error_message)) {
        restore_staged_removals();
        return false;
    }

    for (const StagedRemoval& staged_removal : staged_removals) {
        if (!staged_removal.existed)
            continue;
        if (std::remove(staged_removal.staged_path.c_str()) != 0 && errno != ENOENT) {
            set_error(error_message, std::string("Failed to remove staged ") + staged_removal.label + " '" + staged_removal.staged_path + "': " + std::strerror(errno));
            return false;
        }
    }

    if (account)
        *account = updated_account;

    set_error(error_message, "");
    return true;
}

bool link_and_migrate_character(const std::string& root_directory, const std::string& account_name, const std::string& password, const std::string& character_name, long updated_at, AccountData* account, CharacterMigrationData* migration, std::string* error_message)
{
    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;
    if (!validate_identifier_for_path(character_name, "Character name", error_message))
        return false;

    AccountData authenticated_account;
    if (!authenticate_account(root_directory, account_name, password, &authenticated_account, error_message))
        return false;

    return admin_link_and_migrate_character(root_directory, account_name, character_name, updated_at, account, migration, error_message);
}
