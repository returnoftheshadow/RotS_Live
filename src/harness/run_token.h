/* run_token.h */

#ifndef HARNESS_RUN_TOKEN_H
#define HARNESS_RUN_TOKEN_H

#include <cstddef>
#include <string>
#include <string_view>

namespace harness {

// The random bytes a run token is drawn from.
inline constexpr std::size_t RUN_TOKEN_BYTES = 16;
// The characters in a run token: two lowercase hexadecimal digits per byte.
inline constexpr std::size_t RUN_TOKEN_LENGTH = 2 * RUN_TOKEN_BYTES;

// Returns a new run token: RUN_TOKEN_LENGTH lowercase hexadecimal characters drawn from
// RUN_TOKEN_BYTES bytes of std::random_device. Only the process that started the server learns it,
// from the ready reply.
[[nodiscard]] std::string create_run_token();

// Returns ready_state with "entry=<run_token>" appended, after a space unless ready_state is empty.
[[nodiscard]] std::string with_entry_token(std::string_view ready_state, std::string_view run_token);

} // namespace harness

#endif /* HARNESS_RUN_TOKEN_H */
