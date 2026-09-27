#pragma once

struct descriptor_data;

namespace test_support {

// Resets `out_descriptor` and points its output at its own small_outbuf, empty and in the playing
// state, so act()/send_to_char() output can be read back instead of going to a socket. It fills
// in place because `output` points into the descriptor itself: a copy of a returned descriptor
// would point at the original's buffer.
void prepare_capture_descriptor(descriptor_data& out_descriptor);

// Empties the output that `out_descriptor` has captured so far, keeping it ready to capture more.
void clear_captured_output(descriptor_data& out_descriptor);

// Frees the large output buffer write_to_output() moves `out_descriptor` to once its small buffer
// fills, and points its output back at the small buffer. Does nothing when it has none.
void release_large_output(descriptor_data& out_descriptor);

} // namespace test_support
