#include "test_descriptor_support.h"

#include "../structs.h"

namespace test_support {

void prepare_capture_descriptor(descriptor_data& out_descriptor) {
    out_descriptor = descriptor_data{};
    out_descriptor.output = out_descriptor.small_outbuf;
    out_descriptor.connected = CON_PLYNG;
    clear_captured_output(out_descriptor);
}

void clear_captured_output(descriptor_data& out_descriptor) {
    out_descriptor.small_outbuf[0] = '\0';
    out_descriptor.bufptr = 0;
    out_descriptor.bufspace = SMALL_BUFSIZE - 1;
}

} // namespace test_support
