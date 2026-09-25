#include "test_descriptor_support.h"

#include "../structs.h"

namespace test_support {

void prepare_capture_descriptor(descriptor_data& descriptor) {
    descriptor = descriptor_data{};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
}

} // namespace test_support
