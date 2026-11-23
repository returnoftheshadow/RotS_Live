/* password_login_only.h */

#ifndef PASSWORD_LOGIN_ONLY_H
#define PASSWORD_LOGIN_ONLY_H

#include "connection_entry_policy.h"

// The live server's entry policy: every connection logs in through the login dialogue.
class PasswordLoginOnly : public ConnectionEntryPolicy {
public:
    // Always returns false.
    [[nodiscard]] bool try_enter(descriptor_data& /*connection*/, std::string_view /*line*/) override
    {
        return false;
    }
};

#endif /* PASSWORD_LOGIN_ONLY_H */
