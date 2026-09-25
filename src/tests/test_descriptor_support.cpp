#include "test_descriptor_support.h"

#include "../structs.h"
#include "../utils.h"

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

void release_large_output(descriptor_data& out_descriptor) {
    if (out_descriptor.large_outbuf == nullptr) {
        return;
    }
    RELEASE(out_descriptor.large_outbuf->text);
    RELEASE(out_descriptor.large_outbuf);
    out_descriptor.output = out_descriptor.small_outbuf;
}

} // namespace test_support
