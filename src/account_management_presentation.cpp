std::string format_account_character_prompt(const std::string& root_directory, const AccountData& account)
{
    std::ostringstream output;
    output << "\n\rLinked characters for your account:\n\r";
    output << format_account_character_short_roster(root_directory, account);
    output << "\n\r0) Back to Account Menu.\n\r";
    output << "\n\rCharacter number: ";
    return output.str();
}

std::string format_account_character_list(const std::string& root_directory, const AccountData& account)
{
    if (account.characters.empty())
        return "\n\rNo linked characters yet.\n\r";

    std::ostringstream output;
    output << "\n\rLinked characters:\n\r";
    output << format_account_character_short_roster(root_directory, account);
    return output.str();
}

std::string format_account_summary(const AccountData& account)
{
    std::ostringstream output;
    output << "Account email: " << account.normalized_email << "\n\r";
    output << "Internal name: " << account.account_name << "\n\r";
    output << "Email verified: " << (account.email_verified ? "yes" : "no") << "\n\r";
    if (account.email_verified) {
        output << "Verified by: " << account.email_verified_by << "\n\r";
        output << "Verified at: " << account.email_verified_at << "\n\r";
    } else if (!account.verification_code_hash.empty()) {
        output << "Verification code sent at: " << account.verification_code_sent_at << "\n\r";
        output << "Verification code expires at: " << account.verification_code_expires_at << "\n\r";
        output << "Verification attempts: " << account.verification_attempt_count << "\n\r";
    }
    output << "Blocked: " << (account.blocked ? "yes" : "no") << "\n\r";
    if (account.blocked) {
        output << "Blocked by: " << account.blocked_by << "\n\r";
        output << "Block reason: " << account.block_reason << "\n\r";
    }
    output << "Created: " << account.created_at << "\n\r";
    output << "Updated: " << account.updated_at << "\n\r";
    output << "Characters (" << account.characters.size() << "): ";
    if (account.characters.empty()) {
        output << "(none)";
    } else {
        for (size_t index = 0; index < account.characters.size(); ++index) {
            if (index > 0)
                output << ", ";
            output << account.characters[index];
        }
    }
    output << "\n\r";
    return output.str();
}
