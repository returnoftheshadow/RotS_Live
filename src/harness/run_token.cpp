/* run_token.cpp */

#include "run_token.h"

#include <limits>
#include <random>

namespace harness {

namespace {
// The digits of a token, indexed by their value.
constexpr std::string_view HEX_DIGITS = "0123456789abcdef";
// The largest value of one byte.
constexpr unsigned int MAX_BYTE_VALUE = std::numeric_limits<unsigned char>::max();
// The field the ready reply carries the token in.
constexpr std::string_view ENTRY_FIELD = "entry=";
} // namespace

//============================================================================
std::string create_run_token()
{
    std::random_device random_source;
    std::uniform_int_distribution<unsigned int> byte_values(0, MAX_BYTE_VALUE);
    std::string token;
    token.reserve(RUN_TOKEN_LENGTH);
    for (std::size_t byte_index = 0; byte_index < RUN_TOKEN_BYTES; ++byte_index) {
        const unsigned int byte_value = byte_values(random_source);
        token += HEX_DIGITS[byte_value / HEX_DIGITS.size()];
        token += HEX_DIGITS[byte_value % HEX_DIGITS.size()];
    }
    return token;
}

//============================================================================
std::string with_entry_token(std::string_view ready_state, std::string_view run_token)
{
    std::string state;
    state.reserve(ready_state.size() + 1 + ENTRY_FIELD.size() + run_token.size());
    state.append(ready_state);
    if (!state.empty()) {
        state += ' ';
    }
    state.append(ENTRY_FIELD);
    state.append(run_token);
    return state;
}

} // namespace harness
