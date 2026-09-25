#pragma once

struct descriptor_data;

namespace test_support {

// Resets `descriptor` and points its output at its own small_outbuf, empty and in the playing
// state, so act()/send_to_char() output can be read back instead of going to a socket. It fills
// in place because `output` points into the descriptor itself: a copy of a returned descriptor
// would point at the original's buffer.
void prepare_capture_descriptor(descriptor_data& descriptor);

} // namespace test_support
