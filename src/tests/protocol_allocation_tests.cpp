// Pins how many C++ heap allocations the MSDP setters, the MSDP senders and the per-input protocol
// path make, so a change that brings a temporary std::string back into them fails a test.
//
// This file replaces the global operator new and operator delete for the whole test binary, except
// under AddressSanitizer, which keeps its own so it can report a new freed with free; there these
// tests are skipped. No other file may replace them.

#include "../protocol.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include "test_cleanup.h"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <variant>

// Defined when the build runs under AddressSanitizer: GCC's macro, or clang's feature check.
#if defined(__SANITIZE_ADDRESS__)
#define ROTS_TESTS_UNDER_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ROTS_TESTS_UNDER_ASAN 1
#endif
#endif

namespace {

#if defined(ROTS_TESTS_UNDER_ASAN)
// Whether this binary counts operator new calls; AddressSanitizer keeps its own allocation functions.
constexpr bool COUNTS_ALLOCATIONS = false;
#else
// Whether this binary counts operator new calls.
constexpr bool COUNTS_ALLOCATIONS = true;
#endif

// The allocations counted on this thread so far. It only grows, so each guard counts from where
// it began and nested guards each see their own work.
thread_local std::size_t t_counted_allocations = 0;

// How many ScopedAllocationCount guards are alive on this thread; counting is on while above 0.
thread_local int t_active_guards = 0;

#if !defined(ROTS_TESTS_UNDER_ASAN)
// Counts one allocation request when a guard on this thread is alive.
void note_allocation()
{
    if (t_active_guards > 0) {
        ++t_counted_allocations;
    }
}

// Memory from std::malloc for size bytes, calling the installed new-handler until malloc succeeds
// as the standard operator new does. Returns nullptr when malloc fails and no handler is set.
void* allocate_or_null(std::size_t size)
{
    note_allocation();
    // operator new returns a distinct pointer for size 0, which malloc(0) need not.
    std::size_t request = size;
    if (request == 0) {
        request = 1;
    }
    while (true) {
        void* const memory = std::malloc(request);
        if (memory != nullptr) {
            return memory;
        }
        const std::new_handler handler = std::get_new_handler();
        if (handler == nullptr) {
            return nullptr;
        }
        handler();
    }
}

// The throwing operator new's result for size: the memory, or std::bad_alloc when there is none.
void* allocate_or_throw(std::size_t size)
{
    void* const memory = allocate_or_null(size);
    if (memory == nullptr) {
        throw std::bad_alloc();
    }
    return memory;
}

// The nothrow operator new's result for size: the memory, or nullptr where the throwing form
// would throw, including a new-handler that throws std::bad_alloc.
void* allocate_without_throwing(std::size_t size) noexcept
{
    try {
        return allocate_or_throw(size);
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
#endif

} // namespace

#if !defined(ROTS_TESTS_UNDER_ASAN)
// The usual allocation functions, replaced so a ScopedAllocationCount can count them. They forward
// to std::malloc and std::free. The C++17 aligned forms are left to the library: its defaults pair
// aligned_alloc with free on their own, never calling these, and the protocol code makes no
// over-aligned allocation.
void* operator new(std::size_t size) { return allocate_or_throw(size); }

void* operator new[](std::size_t size) { return allocate_or_throw(size); }

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    return allocate_without_throwing(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return allocate_without_throwing(size);
}

void operator delete(void* memory) noexcept { std::free(memory); }

void operator delete[](void* memory) noexcept { std::free(memory); }

void operator delete(void* memory, std::size_t /* size */) noexcept { std::free(memory); }

void operator delete[](void* memory, std::size_t /* size */) noexcept { std::free(memory); }

void operator delete(void* memory, const std::nothrow_t&) noexcept { std::free(memory); }

void operator delete[](void* memory, const std::nothrow_t&) noexcept { std::free(memory); }
#endif

namespace {

// Counts the operator new and new[] calls this thread makes while the guard is alive. Other
// threads' allocations and direct std::malloc calls are not counted.
class ScopedAllocationCount {
public:
    ScopedAllocationCount()
        : m_start(t_counted_allocations)
    {
        ++t_active_guards;
    }

    ~ScopedAllocationCount() { --t_active_guards; }

    ScopedAllocationCount(const ScopedAllocationCount&) = delete;
    ScopedAllocationCount& operator=(const ScopedAllocationCount&) = delete;

    // The allocations this thread has made since the guard began.
    std::size_t count() const { return t_counted_allocations - m_start; }

private:
    // t_counted_allocations when the guard began.
    std::size_t m_start;
};

// Skips each allocation test in a build that does not count allocations.
class ProtocolAllocation : public ::testing::Test {
protected:
    void SetUp() override
    {
        if (!COUNTS_ALLOCATIONS) {
            GTEST_SKIP() << "AddressSanitizer keeps its own operator new, so nothing is counted.";
        }
    }
};

// The allocations work makes on this thread. Read the result outside work: a failed assertion
// allocates.
template <typename Work>
std::size_t allocations_made_by(Work&& work)
{
    const ScopedAllocationCount counting;
    work();
    return counting.count();
}

// Longer than the 22 bytes the largest standard library small-string buffer holds, so a
// std::string copy of a value this long allocates.
constexpr char ROOM_NAME[] = "The Common Room of the Prancing Pony";
constexpr char OTHER_ROOM_NAME[] = "The Great Hall of Elrond in Rivendell, beside the fire";

// A value shorter than the stored one, short enough to fit a small-string buffer.
constexpr char SHORT_ROOM_NAME[] = "Bree Gate";

// A room name holding every kind of byte MSDPSanitizeValue escapes: a quote, a backslash, a line
// break and a control character.
constexpr char ESCAPED_ROOM_NAME[] = "The \"Prancing Pony\" C:\\Bree\nCellar\x01 Stairs";

// The value a client's MSDPSendList call carries: exits separated by spaces.
constexpr char EXIT_LIST[] = "north east south west up down \"hidden\"";

// A socket write_to_descriptor skips once protocol.cpp has built the packet, so the sends run
// their whole protocol path and nothing needs reading back.
constexpr SocketType NO_SOCKET = -1;

// A one-pair table payload, which MSDPSetTable stores unescaped.
std::string table_payload(std::string_view room_name)
{
    std::string payload;
    payload.push_back(static_cast<char>(MSDP_VAR));
    payload += "NAME";
    payload.push_back(static_cast<char>(MSDP_VAL));
    payload += room_name;
    return payload;
}

// The bytes MSDPSetTable or MSDPSetArray stores for payload: open_marker, payload, close_marker.
std::string wrapped_value(int open_marker, std::string_view payload, int close_marker)
{
    std::string value;
    value.push_back(static_cast<char>(open_marker));
    value += payload;
    value.push_back(static_cast<char>(close_marker));
    return value;
}

// An array payload of north, east, south and last_exit, which MSDPSetArray stores unescaped.
std::string array_payload(std::string_view last_exit)
{
    std::string payload;
    for (const std::string_view exit_name : { "north", "east", "south" }) {
        payload.push_back(static_cast<char>(MSDP_VAL));
        payload += exit_name;
    }
    payload.push_back(static_cast<char>(MSDP_VAL));
    payload += last_exit;
    return payload;
}

// The text a string MSDP variable holds.
const std::string& msdp_text(const MSDP_t& variable)
{
    return std::get<std::string>(variable.value);
}

// The number a number MSDP variable holds.
int msdp_number(const MSDP_t& variable) { return std::get<int>(variable.value); }

// A descriptor with a fresh protocol and a character that asks for MSDP, writing to no socket.
class AllocationDescriptor {
public:
    AllocationDescriptor()
        : m_descriptor_release(&descriptor)
        , m_character_release(&character)
    {
        descriptor.descriptor = NO_SOCKET;
        descriptor.pProtocol = ProtocolCreate();
        descriptor.character = &character;
        SET_BIT(character.specials2.pref, PRF_MSDP);
    }

    AllocationDescriptor(const AllocationDescriptor&) = delete;
    AllocationDescriptor& operator=(const AllocationDescriptor&) = delete;

    // The stored MSDP record of msdp_variable.
    MSDP_t* variable(variable_t msdp_variable) const
    {
        return &descriptor.pProtocol->variables[msdp_variable];
    }

    // Makes the client one that speaks ATCP and not MSDP.
    void use_atcp()
    {
        descriptor.pProtocol->msdp_enabled = false;
        descriptor.pProtocol->atcp_enabled = true;
    }

    // Arms wrote(): protocol.cpp's send_to_client turns a positive write_oob into 2.
    void watch_for_write() { descriptor.pProtocol->write_oob = 1; }

    // Whether a packet reached send_to_client since watch_for_write.
    bool wrote() const { return descriptor.pProtocol->write_oob == 2; }

    // The descriptor given to the functions under test; its protocol is released at scope end.
    descriptor_data descriptor {};

    // The descriptor's character, flagged for MSDP so MSDPSend sends.
    char_data character {};

private:
    // Destroys the descriptor's protocol when the test ends.
    TestDescriptorRelease m_descriptor_release;

    // Frees whatever the game gave the character when the test ends.
    ClearedCharacterRelease m_character_release;
};

TEST_F(ProtocolAllocation, CounterCountsALongStringBuiltInsideAGuard)
{
    std::string built;
    const std::size_t allocations = allocations_made_by([&built]() -> void { built = ROOM_NAME; });

    EXPECT_GE(allocations, 1u) << "A string too long for its small buffer allocates.";
    EXPECT_EQ(built, ROOM_NAME);
}

TEST_F(ProtocolAllocation, CounterIgnoresAllocationsMadeOutsideAGuard)
{
    // A guard that has ended counts nothing more.
    {
        const ScopedAllocationCount finished;
    }
    const std::size_t counted_before = t_counted_allocations;

    std::string built(ROOM_NAME);

    EXPECT_EQ(t_counted_allocations, counted_before)
        << "Allocations with no guard alive are never counted.";
    EXPECT_EQ(built, ROOM_NAME);
}

TEST_F(ProtocolAllocation, CounterIgnoresAllocationsOnAnotherThread)
{
    std::atomic<bool> start_allocating { false };
    std::atomic<bool> finished_allocating { false };
    std::size_t other_thread_length = 0;
    // Started before the guard, since starting a thread allocates on the thread that starts it.
    std::thread other_thread([&]() -> void {
        while (!start_allocating.load()) {
            std::this_thread::yield();
        }
        const std::string built(ROOM_NAME);
        other_thread_length = built.size();
        finished_allocating.store(true);
    });

    const std::size_t allocations = allocations_made_by([&]() -> void {
        start_allocating.store(true);
        while (!finished_allocating.load()) {
            std::this_thread::yield();
        }
    });
    other_thread.join();

    EXPECT_EQ(allocations, 0u) << "Another thread's allocations are not this guard's.";
    EXPECT_EQ(other_thread_length, std::strlen(ROOM_NAME));
}

TEST_F(ProtocolAllocation, CounterSeesAnAllocationTheProtocolCodeMakes)
{
    std::string sanitized;
    const std::size_t allocations = allocations_made_by(
        [&sanitized]() -> void { sanitized = MSDPSanitizeValue(ROOM_NAME); });

    EXPECT_GE(allocations, 1u)
        << "MSDPSanitizeValue returns a new std::string, so the counter must see protocol.cpp's "
           "allocations.";
    EXPECT_EQ(sanitized, ROOM_NAME);
}

TEST_F(ProtocolAllocation, CreateAllocatesTheProtocolAndItsLongDefaultTexts)
{
    protocol_t* protocol = nullptr;
    const std::size_t allocations
        = allocations_made_by([&protocol]() -> void { protocol = ProtocolCreate(); });

    // One for protocol_t, which holds every variable, and one for each stored text too long for
    // the library's small-string buffer, which an empty string's capacity measures.
    const std::size_t small_string_capacity = std::string().capacity();
    std::size_t expected_allocations = 1;
    for (const MSDP_t& variable : protocol->variables) {
        const std::string* const text = std::get_if<std::string>(&variable.value);
        if (text != nullptr && text->size() > small_string_capacity) {
            ++expected_allocations;
        }
    }
    ProtocolDestroy(protocol);

    EXPECT_EQ(allocations, expected_allocations);
}

TEST_F(ProtocolAllocation, SetStringDoesNotAllocateForAnUnchangedValue)
{
    AllocationDescriptor context;
    MSDP_t* room_name = context.variable(eMSDP_ROOM_NAME);
    MSDPSetString(&context.descriptor, eMSDP_ROOM_NAME, ROOM_NAME);
    room_name->dirty = false;
    const char* const stored = msdp_text(*room_name).data();

    const std::size_t allocations = allocations_made_by(
        [&context]() -> void { MSDPSetString(&context.descriptor, eMSDP_ROOM_NAME, ROOM_NAME); });

    EXPECT_EQ(allocations, 0u) << "An unchanged value is compared in place.";
    EXPECT_EQ(msdp_text(*room_name).data(), stored);
    EXPECT_FALSE(room_name->dirty);
}

TEST_F(ProtocolAllocation, SetStringDoesNotAllocateForAnUnchangedEscapedValue)
{
    AllocationDescriptor context;
    MSDP_t* room_name = context.variable(eMSDP_ROOM_NAME);
    MSDPSetString(&context.descriptor, eMSDP_ROOM_NAME, ESCAPED_ROOM_NAME);
    room_name->dirty = false;
    const char* const stored = msdp_text(*room_name).data();

    const std::size_t allocations = allocations_made_by([&context]() -> void {
        MSDPSetString(&context.descriptor, eMSDP_ROOM_NAME, ESCAPED_ROOM_NAME);
    });

    EXPECT_EQ(allocations, 0u) << "An unchanged value is escaped and compared byte by byte.";
    EXPECT_EQ(msdp_text(*room_name).data(), stored);
    EXPECT_FALSE(room_name->dirty);
}

TEST_F(ProtocolAllocation, SetStringDoesNotAllocateForAnUnchangedEmptyValue)
{
    AllocationDescriptor context;
    MSDP_t* room_name = context.variable(eMSDP_ROOM_NAME);
    ASSERT_EQ(msdp_text(*room_name), "") << "ProtocolCreate stores an empty room name.";
    room_name->dirty = false;

    const std::size_t allocations = allocations_made_by(
        [&context]() -> void { MSDPSetString(&context.descriptor, eMSDP_ROOM_NAME, ""); });

    EXPECT_EQ(allocations, 0u);
    EXPECT_FALSE(room_name->dirty);
}

TEST_F(ProtocolAllocation, SetStringViewDoesNotAllocateForAnUnchangedValue)
{
    AllocationDescriptor context;
    MSDP_t* room_name = context.variable(eMSDP_ROOM_NAME);
    MSDPSetString(&context.descriptor, eMSDP_ROOM_NAME, ROOM_NAME);
    room_name->dirty = false;
    // The room name followed by a line break the view leaves out, as a message's text is passed.
    const std::string line = std::string(ROOM_NAME) + "\r\n";
    const std::string_view without_line_break(line.data(), std::strlen(ROOM_NAME));

    const std::size_t allocations = allocations_made_by([&context, without_line_break]() -> void {
        MSDPSetString(&context.descriptor, eMSDP_ROOM_NAME, without_line_break);
    });

    EXPECT_EQ(allocations, 0u) << "A view is compared in place without a terminated copy.";
    EXPECT_FALSE(room_name->dirty);
}

TEST_F(ProtocolAllocation, SetStringReusesItsBufferForAChangedValueThatFits)
{
    AllocationDescriptor context;
    MSDP_t* room_name = context.variable(eMSDP_ROOM_NAME);
    // Longer than a small-string buffer, so the stored text is on the heap, and longer than each
    // value below once escaped.
    MSDPSetString(&context.descriptor, eMSDP_ROOM_NAME, OTHER_ROOM_NAME);
    const char* const buffer = msdp_text(*room_name).data();
    // A shorter value, a much shorter one and an escaped one.
    for (const char* const value : { ROOM_NAME, SHORT_ROOM_NAME, ESCAPED_ROOM_NAME }) {
        const std::string sanitized = MSDPSanitizeValue(value);
        ASSERT_LE(sanitized.size(), msdp_text(*room_name).capacity()) << value;
        room_name->dirty = false;

        const std::size_t allocations = allocations_made_by([&context, value]() -> void {
            MSDPSetString(&context.descriptor, eMSDP_ROOM_NAME, value);
        });

        EXPECT_EQ(allocations, 0u) << "Setting \"" << value << "\" allocated.";
        EXPECT_EQ(msdp_text(*room_name).data(), buffer) << "A value that fits reuses the buffer.";
        EXPECT_TRUE(room_name->dirty);
        EXPECT_EQ(msdp_text(*room_name), sanitized);
    }
}

TEST_F(ProtocolAllocation, SetStringAllocatesOnceForALongerValue)
{
    AllocationDescriptor context;
    MSDP_t* room_name = context.variable(eMSDP_ROOM_NAME);
    MSDPSetString(&context.descriptor, eMSDP_ROOM_NAME, ROOM_NAME);
    ASSERT_GT(std::strlen(OTHER_ROOM_NAME), msdp_text(*room_name).capacity());
    room_name->dirty = false;

    const std::size_t allocations = allocations_made_by([&context]() -> void {
        MSDPSetString(&context.descriptor, eMSDP_ROOM_NAME, OTHER_ROOM_NAME);
    });

    EXPECT_EQ(allocations, 1u) << "A value longer than the stored capacity grows the string once.";
    EXPECT_TRUE(room_name->dirty);
    EXPECT_EQ(msdp_text(*room_name), MSDPSanitizeValue(OTHER_ROOM_NAME));
}

TEST_F(ProtocolAllocation, SetNumberDoesNotAllocateForAnUnchangedValue)
{
    AllocationDescriptor context;
    MSDPSetNumber(&context.descriptor, eMSDP_HEALTH, 125);
    context.variable(eMSDP_HEALTH)->dirty = false;

    const std::size_t allocations = allocations_made_by(
        [&context]() -> void { MSDPSetNumber(&context.descriptor, eMSDP_HEALTH, 125); });

    EXPECT_EQ(allocations, 0u);
    EXPECT_FALSE(context.variable(eMSDP_HEALTH)->dirty);
}

TEST_F(ProtocolAllocation, SetNumberDoesNotAllocateForAChangedValue)
{
    AllocationDescriptor context;
    context.variable(eMSDP_HEALTH)->dirty = false;

    const std::size_t allocations = allocations_made_by(
        [&context]() -> void { MSDPSetNumber(&context.descriptor, eMSDP_HEALTH, 98); });

    EXPECT_EQ(allocations, 0u) << "A number is stored in the variable itself.";
    EXPECT_EQ(msdp_number(*context.variable(eMSDP_HEALTH)), 98);
    EXPECT_TRUE(context.variable(eMSDP_HEALTH)->dirty);
}

TEST_F(ProtocolAllocation, SetTableDoesNotAllocateForAnUnchangedValue)
{
    AllocationDescriptor context;
    MSDP_t* room = context.variable(eMSDP_ROOM);
    // A quote, a backslash and a line break, which a table keeps raw.
    const std::string payload = table_payload(ESCAPED_ROOM_NAME);
    MSDPSetTable(&context.descriptor, eMSDP_ROOM, payload.c_str());
    room->dirty = false;
    const char* const stored = msdp_text(*room).data();

    const std::size_t allocations = allocations_made_by([&context, &payload]() -> void {
        MSDPSetTable(&context.descriptor, eMSDP_ROOM, payload.c_str());
    });

    EXPECT_EQ(allocations, 0u) << "An unchanged table is compared inside its markers in place.";
    EXPECT_EQ(msdp_text(*room).data(), stored);
    EXPECT_FALSE(room->dirty);
}

TEST_F(ProtocolAllocation, SetTableDoesNotAllocateForAnUnchangedEmptyValue)
{
    AllocationDescriptor context;
    MSDP_t* room = context.variable(eMSDP_ROOM);
    ASSERT_EQ(msdp_text(*room), "") << "ProtocolCreate stores an empty room table.";
    room->dirty = false;

    const std::size_t allocations = allocations_made_by(
        [&context]() -> void { MSDPSetTable(&context.descriptor, eMSDP_ROOM, ""); });

    EXPECT_EQ(allocations, 0u) << "An empty table goes through MSDPSetString's comparison.";
    EXPECT_FALSE(room->dirty);
}

TEST_F(ProtocolAllocation, SetTableReusesItsBufferForAChangedValueThatFits)
{
    AllocationDescriptor context;
    MSDP_t* room = context.variable(eMSDP_ROOM);
    // Longer than a small-string buffer, so the stored table is on the heap, and longer than each
    // payload below.
    MSDPSetTable(&context.descriptor, eMSDP_ROOM, table_payload(OTHER_ROOM_NAME).c_str());
    const char* const buffer = msdp_text(*room).data();
    // A shorter payload, a much shorter one and one whose name its caller escaped.
    const std::string payloads[] = {
        table_payload(ROOM_NAME),
        table_payload(SHORT_ROOM_NAME),
        table_payload(MSDPSanitizeValue(ESCAPED_ROOM_NAME)),
    };
    for (const std::string& payload : payloads) {
        const std::string wrapped = wrapped_value(MSDP_TABLE_OPEN, payload, MSDP_TABLE_CLOSE);
        ASSERT_LE(wrapped.size(), msdp_text(*room).capacity()) << payload;
        room->dirty = false;

        const std::size_t allocations = allocations_made_by([&context, &payload]() -> void {
            MSDPSetTable(&context.descriptor, eMSDP_ROOM, payload.c_str());
        });

        EXPECT_EQ(allocations, 0u) << "The table is written inside its markers in place.";
        EXPECT_EQ(msdp_text(*room).data(), buffer) << "A table that fits reuses the buffer.";
        EXPECT_TRUE(room->dirty);
        EXPECT_EQ(msdp_text(*room), wrapped);
    }
}

TEST_F(ProtocolAllocation, SetTableAllocatesOnceForALongerValue)
{
    AllocationDescriptor context;
    MSDP_t* room = context.variable(eMSDP_ROOM);
    MSDPSetTable(&context.descriptor, eMSDP_ROOM, table_payload(ROOM_NAME).c_str());
    const std::string payload = table_payload(OTHER_ROOM_NAME);
    const std::string wrapped = wrapped_value(MSDP_TABLE_OPEN, payload, MSDP_TABLE_CLOSE);
    ASSERT_GT(wrapped.size(), msdp_text(*room).capacity());
    room->dirty = false;

    const std::size_t allocations = allocations_made_by([&context, &payload]() -> void {
        MSDPSetTable(&context.descriptor, eMSDP_ROOM, payload.c_str());
    });

    EXPECT_EQ(allocations, 1u) << "A table longer than the stored capacity grows the string once.";
    EXPECT_TRUE(room->dirty);
    EXPECT_EQ(msdp_text(*room), wrapped);
}

TEST_F(ProtocolAllocation, SetArrayDoesNotAllocateForAnUnchangedValue)
{
    AllocationDescriptor context;
    MSDP_t* exits = context.variable(eMSDP_ROOM_EXITS);
    const std::string payload = array_payload("\"hidden\" \\ door");
    MSDPSetArray(&context.descriptor, eMSDP_ROOM_EXITS, payload.c_str());
    exits->dirty = false;
    const char* const stored = msdp_text(*exits).data();

    const std::size_t allocations = allocations_made_by([&context, &payload]() -> void {
        MSDPSetArray(&context.descriptor, eMSDP_ROOM_EXITS, payload.c_str());
    });

    EXPECT_EQ(allocations, 0u) << "An unchanged array is compared inside its markers in place.";
    EXPECT_EQ(msdp_text(*exits).data(), stored);
    EXPECT_FALSE(exits->dirty);
}

TEST_F(ProtocolAllocation, SetArrayDoesNotAllocateForAnUnchangedEmptyValue)
{
    AllocationDescriptor context;
    MSDP_t* exits = context.variable(eMSDP_ROOM_EXITS);
    ASSERT_EQ(msdp_text(*exits), "") << "ProtocolCreate stores an empty exit array.";
    exits->dirty = false;

    const std::size_t allocations = allocations_made_by(
        [&context]() -> void { MSDPSetArray(&context.descriptor, eMSDP_ROOM_EXITS, ""); });

    EXPECT_EQ(allocations, 0u) << "An empty array goes through MSDPSetString's comparison.";
    EXPECT_FALSE(exits->dirty);
}

TEST_F(ProtocolAllocation, SetArrayReusesItsBufferForAChangedValueThatFits)
{
    AllocationDescriptor context;
    MSDP_t* exits = context.variable(eMSDP_ROOM_EXITS);
    // Longer than a small-string buffer, so the stored array is on the heap, and longer than each
    // payload below.
    MSDPSetArray(
        &context.descriptor, eMSDP_ROOM_EXITS, array_payload("down into the cellar").c_str());
    const char* const buffer = msdp_text(*exits).data();
    // A shorter payload, a much shorter one and one whose last exit its caller escaped.
    const std::string payloads[] = {
        array_payload("west"),
        array_payload("up"),
        array_payload(MSDPSanitizeValue("\"hidden\" \\ door")),
    };
    for (const std::string& payload : payloads) {
        const std::string wrapped = wrapped_value(MSDP_ARRAY_OPEN, payload, MSDP_ARRAY_CLOSE);
        ASSERT_LE(wrapped.size(), msdp_text(*exits).capacity()) << payload;
        exits->dirty = false;

        const std::size_t allocations = allocations_made_by([&context, &payload]() -> void {
            MSDPSetArray(&context.descriptor, eMSDP_ROOM_EXITS, payload.c_str());
        });

        EXPECT_EQ(allocations, 0u) << "The array is written inside its markers in place.";
        EXPECT_EQ(msdp_text(*exits).data(), buffer) << "An array that fits reuses the buffer.";
        EXPECT_TRUE(exits->dirty);
        EXPECT_EQ(msdp_text(*exits), wrapped);
    }
}

TEST_F(ProtocolAllocation, SetArrayAllocatesOnceForALongerValue)
{
    AllocationDescriptor context;
    MSDP_t* exits = context.variable(eMSDP_ROOM_EXITS);
    MSDPSetArray(&context.descriptor, eMSDP_ROOM_EXITS, array_payload("west").c_str());
    const std::string payload = array_payload("down into the cellar");
    const std::string wrapped = wrapped_value(MSDP_ARRAY_OPEN, payload, MSDP_ARRAY_CLOSE);
    ASSERT_GT(wrapped.size(), msdp_text(*exits).capacity());
    exits->dirty = false;

    const std::size_t allocations = allocations_made_by([&context, &payload]() -> void {
        MSDPSetArray(&context.descriptor, eMSDP_ROOM_EXITS, payload.c_str());
    });

    EXPECT_EQ(allocations, 1u) << "An array longer than the stored capacity grows the string once.";
    EXPECT_TRUE(exits->dirty);
    EXPECT_EQ(msdp_text(*exits), wrapped);
}

// MSDPSend's allocations for a string and a number variable, checking each reached
// send_to_client.
void expect_send_does_not_allocate(AllocationDescriptor& context)
{
    MSDPSetString(&context.descriptor, eMSDP_ROOM_NAME, ESCAPED_ROOM_NAME);
    MSDPSetNumber(&context.descriptor, eMSDP_HEALTH, 125);

    for (const variable_t msdp_variable : { eMSDP_ROOM_NAME, eMSDP_HEALTH }) {
        context.watch_for_write();
        bool sent = false;
        const std::size_t allocations
            = allocations_made_by([&context, &sent, msdp_variable]() -> void {
                  sent = MSDPSend(&context.descriptor, msdp_variable);
              });

        EXPECT_EQ(allocations, 0u) << "Sending variable " << msdp_variable << " allocated.";
        EXPECT_TRUE(sent);
        EXPECT_TRUE(context.wrote());
    }
}

TEST_F(ProtocolAllocation, SendDoesNotAllocateOverMsdp)
{
    AllocationDescriptor context;
    expect_send_does_not_allocate(context);
}

TEST_F(ProtocolAllocation, SendDoesNotAllocateOverAtcp)
{
    AllocationDescriptor context;
    context.use_atcp();
    expect_send_does_not_allocate(context);
}

// MSDPUpdate's allocations while it sends every variable ProtocolCreate left dirty.
void expect_update_does_not_allocate(AllocationDescriptor& context)
{
    MSDPSetString(&context.descriptor, eMSDP_ROOM_NAME, ESCAPED_ROOM_NAME);
    MSDPSetTable(&context.descriptor, eMSDP_ROOM, table_payload(ROOM_NAME).c_str());
    ASSERT_TRUE(context.variable(eMSDP_HEALTH)->dirty)
        << "ProtocolCreate primes server variables.";
    context.watch_for_write();

    const std::size_t allocations
        = allocations_made_by([&context]() -> void { MSDPUpdate(&context.descriptor); });

    EXPECT_EQ(allocations, 0u);
    EXPECT_TRUE(context.wrote());
    for (const variable_t msdp_variable : { eMSDP_ROOM_NAME, eMSDP_ROOM, eMSDP_HEALTH }) {
        EXPECT_FALSE(context.variable(msdp_variable)->dirty)
            << "Variable " << msdp_variable << " was not sent.";
    }
}

TEST_F(ProtocolAllocation, UpdateDoesNotAllocateWhileSendingDirtyVariablesOverMsdp)
{
    AllocationDescriptor context;
    expect_update_does_not_allocate(context);
}

TEST_F(ProtocolAllocation, UpdateDoesNotAllocateWhileSendingDirtyVariablesOverAtcp)
{
    AllocationDescriptor context;
    context.use_atcp();
    expect_update_does_not_allocate(context);
}

// MSDPFlush's allocations while it sends one dirty reported variable.
void expect_flush_does_not_allocate(AllocationDescriptor& context)
{
    MSDPSetString(&context.descriptor, eMSDP_ROOM_NAME, ESCAPED_ROOM_NAME);
    context.watch_for_write();

    const std::size_t allocations = allocations_made_by(
        [&context]() -> void { MSDPFlush(&context.descriptor, eMSDP_ROOM_NAME); });

    EXPECT_EQ(allocations, 0u);
    EXPECT_TRUE(context.wrote());
    EXPECT_FALSE(context.variable(eMSDP_ROOM_NAME)->dirty);
}

TEST_F(ProtocolAllocation, FlushDoesNotAllocateWhileSendingADirtyVariableOverMsdp)
{
    AllocationDescriptor context;
    expect_flush_does_not_allocate(context);
}

TEST_F(ProtocolAllocation, FlushDoesNotAllocateWhileSendingADirtyVariableOverAtcp)
{
    AllocationDescriptor context;
    context.use_atcp();
    expect_flush_does_not_allocate(context);
}

// MSDPSendPair's allocations for a value it must escape into the packet.
void expect_send_pair_does_not_allocate(AllocationDescriptor& context)
{
    context.watch_for_write();

    const std::size_t allocations = allocations_made_by([&context]() -> void {
        MSDPSendPair(&context.descriptor, "ROOM_NAME", ESCAPED_ROOM_NAME);
    });

    EXPECT_EQ(allocations, 0u) << "The value is escaped straight into the packet buffer.";
    EXPECT_TRUE(context.wrote());
}

TEST_F(ProtocolAllocation, SendPairDoesNotAllocateOverMsdp)
{
    AllocationDescriptor context;
    expect_send_pair_does_not_allocate(context);
}

TEST_F(ProtocolAllocation, SendPairDoesNotAllocateOverAtcp)
{
    AllocationDescriptor context;
    context.use_atcp();
    expect_send_pair_does_not_allocate(context);
}

// MSDPSendList's allocations for a list it must escape into the packet.
void expect_send_list_does_not_allocate(AllocationDescriptor& context)
{
    context.watch_for_write();

    const std::size_t allocations = allocations_made_by(
        [&context]() -> void { MSDPSendList(&context.descriptor, "ROOM_EXITS", EXIT_LIST); });

    EXPECT_EQ(allocations, 0u) << "The list is escaped straight into the packet buffer.";
    EXPECT_TRUE(context.wrote());
}

TEST_F(ProtocolAllocation, SendListDoesNotAllocateOverMsdp)
{
    AllocationDescriptor context;
    expect_send_list_does_not_allocate(context);
}

TEST_F(ProtocolAllocation, SendListDoesNotAllocateOverAtcp)
{
    AllocationDescriptor context;
    context.use_atcp();
    expect_send_list_does_not_allocate(context);
}

TEST_F(ProtocolAllocation, AppendSanitizedDoesNotAllocateIntoAStringWithRoom)
{
    // The group table msdp_update rebuilds every pulse in one string that keeps its capacity.
    std::string payload;
    payload.reserve(256);

    const std::size_t allocations = allocations_made_by([&payload]() -> void {
        payload.clear();
        MSDPAppendSanitized(payload, ESCAPED_ROOM_NAME);
    });

    EXPECT_EQ(allocations, 0u);
    EXPECT_EQ(payload, MSDPSanitizeValue(ESCAPED_ROOM_NAME));
}

TEST_F(ProtocolAllocation, ProtocolInputDoesNotAllocateForACommandLine)
{
    AllocationDescriptor context;
    constexpr char command_line[] = "say The Common Room of the Prancing Pony is full tonight\r\n";
    char filtered[MAX_PROTOCOL_BUFFER + 1] = "";

    const int command_length = static_cast<int>(std::strlen(command_line));

    const std::size_t allocations
        = allocations_made_by([&context, &command_line, command_length, &filtered]() -> void {
              ProtocolInput(&context.descriptor, command_line, command_length, filtered);
          });

    EXPECT_EQ(allocations, 0u) << "Every line a player types passes through ProtocolInput.";
    EXPECT_STREQ(filtered, command_line);
}

} // namespace
