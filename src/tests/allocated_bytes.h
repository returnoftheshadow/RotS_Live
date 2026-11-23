/* How many bytes the process has allocated and not yet freed, for tests that a game function frees
 * what it should.
 *
 * Not portable: standard C++ cannot observe a free, and the game's CREATE and RELEASE keep no
 * counts, so this asks the allocator itself, AddressSanitizer's under ASan and glibc's otherwise.
 * Porting the tests to another allocator means replacing only this function. */
#ifndef ALLOCATED_BYTES_H
#define ALLOCATED_BYTES_H

#include <cstddef>
#include <optional>

#if defined(__SANITIZE_ADDRESS__)
#define ROTS_ALLOCATED_BYTES_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ROTS_ALLOCATED_BYTES_ASAN 1
#endif
#endif

#if defined(ROTS_ALLOCATED_BYTES_ASAN)
/* Declared here because the image's 32-bit toolchain ships no <sanitizer/allocator_interface.h>;
 * the ASan runtime defines it. */
extern "C" std::size_t __sanitizer_get_current_allocated_bytes();
#elif defined(__GLIBC__)
#include <malloc.h>
#endif

/* The bytes allocated and not yet freed, or nothing where the allocator cannot tell. Under ASan
 * glibc's mallinfo2 reports zeros, so ASan's own count is used there. */
inline std::optional<std::size_t> allocated_bytes()
{
#if defined(ROTS_ALLOCATED_BYTES_ASAN)
    return __sanitizer_get_current_allocated_bytes();
#elif defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
    const struct mallinfo2 info = mallinfo2();
    /* uordblks is the heap in use; hblkhd is what large allocations mapped on their own. */
    return info.uordblks + info.hblkhd;
#else
    return std::nullopt;
#endif
}

#endif /* ALLOCATED_BYTES_H */
