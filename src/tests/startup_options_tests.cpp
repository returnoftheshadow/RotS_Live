#include "../comm.h"
#include "../db.h"
#include "../structs.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

SocketType pnew_descriptor(SocketType s);
int process_input(struct descriptor_data* t);

extern descriptor_data* descriptor_list;
extern SocketType maxdesc;
extern int avail_descs;
extern int has_proxy;
extern int nameserver_is_slow;
extern ban_list_element* ban_list;

namespace {

std::vector<char*> build_argv(std::vector<std::string>* storage)
{
    std::vector<char*> argv;
    argv.reserve(storage->size());
    for (std::string& item : *storage)
        argv.push_back(item.data());
    return argv;
}

class AcceptPathTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        saved_descriptor_list_ = descriptor_list;
        saved_maxdesc_ = maxdesc;
        saved_avail_descs_ = avail_descs;
        saved_has_proxy_ = has_proxy;
        saved_nameserver_is_slow_ = nameserver_is_slow;
        saved_ban_list_ = ban_list;

        descriptor_list = nullptr;
        maxdesc = 0;
        avail_descs = 64;
        has_proxy = 0;
        nameserver_is_slow = 1;
        ban_list = nullptr;
    }

    void TearDown() override
    {
        while (descriptor_list)
            close_socket(descriptor_list, FALSE);

        descriptor_list = saved_descriptor_list_;
        maxdesc = saved_maxdesc_;
        avail_descs = saved_avail_descs_;
        has_proxy = saved_has_proxy_;
        nameserver_is_slow = saved_nameserver_is_slow_;
        ban_list = saved_ban_list_;
    }

    int create_listener_socket(in_port_t* port_out)
    {
        int listener = socket(AF_INET, SOCK_STREAM, 0);
        EXPECT_GE(listener, 0) << strerror(errno);
        if (listener < 0)
            return -1;

        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;

        EXPECT_EQ(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0) << strerror(errno);
        if (getsockname(listener, reinterpret_cast<sockaddr*>(&address), reinterpret_cast<socklen_t*>(&socklen_)) == 0)
            *port_out = ntohs(address.sin_port);

        EXPECT_EQ(listen(listener, 1), 0) << strerror(errno);
        return listener;
    }

    int connect_client(in_port_t port)
    {
        int client = socket(AF_INET, SOCK_STREAM, 0);
        EXPECT_GE(client, 0) << strerror(errno);
        if (client < 0)
            return -1;

        timeval timeout {};
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);

        EXPECT_EQ(connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0) << strerror(errno);
        return client;
    }

    std::string read_client_data(int client)
    {
        char buffer[2048];
        const ssize_t bytes_read = recv(client, buffer, sizeof(buffer) - 1, 0);
        EXPECT_GT(bytes_read, 0) << strerror(errno);
        if (bytes_read <= 0)
            return std::string();

        buffer[bytes_read] = '\0';
        return std::string(buffer, static_cast<size_t>(bytes_read));
    }

    void expect_no_client_data_yet(int client)
    {
        char buffer[32];
        errno = 0;
        const ssize_t bytes_read = recv(client, buffer, sizeof(buffer), 0);
        EXPECT_EQ(bytes_read, -1);
        EXPECT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK) << strerror(errno);
    }

    // Waits up to one second for bytes to reach the game's side of `descriptor`, as the game loop
    // waits in select() before it reads: loopback can deliver data a moment after send() returns.
    void wait_for_game_side_data(const descriptor_data* descriptor)
    {
        ASSERT_NE(descriptor, nullptr);
        pollfd readable_check {};
        readable_check.fd = descriptor->descriptor;
        readable_check.events = POLLIN;
        const int ready_count = poll(&readable_check, 1, 1000);
        EXPECT_EQ(ready_count, 1) << "no data reached the game side within one second";
    }

private:
    descriptor_data* saved_descriptor_list_ = nullptr;
    SocketType saved_maxdesc_ = 0;
    int saved_avail_descs_ = 0;
    int saved_has_proxy_ = 0;
    int saved_nameserver_is_slow_ = 0;
    ban_list_element* saved_ban_list_ = nullptr;
    socklen_t socklen_ = sizeof(sockaddr_in);
};

TEST(StartupOptions, UsesDefaultPortAndNoProxyWhenNoArgumentsAreProvided)
{
    StartupOptions options {};
    std::string error_message;
    std::vector<std::string> args = { "ageland" };
    std::vector<char*> argv = build_argv(&args);

    ASSERT_TRUE(parse_startup_options(static_cast<int>(argv.size()), argv.data(), &options, &error_message))
        << error_message;

    EXPECT_EQ(options.port, 1024);
    EXPECT_FALSE(options.has_proxy);
}

TEST(StartupOptions, TreatsDashPArgumentAsPortInsteadOfProxyMode)
{
    StartupOptions options {};
    std::string error_message;
    std::vector<std::string> args = { "ageland", "-p", "3791" };
    std::vector<char*> argv = build_argv(&args);

    ASSERT_TRUE(parse_startup_options(static_cast<int>(argv.size()), argv.data(), &options, &error_message))
        << error_message;

    EXPECT_EQ(options.port, 3791);
    EXPECT_FALSE(options.has_proxy);
}

TEST(StartupOptions, AcceptsExplicitProxyFlagWithPositionalPort)
{
    StartupOptions options {};
    std::string error_message;
    std::vector<std::string> args = { "ageland", "-x", "4001" };
    std::vector<char*> argv = build_argv(&args);

    ASSERT_TRUE(parse_startup_options(static_cast<int>(argv.size()), argv.data(), &options, &error_message))
        << error_message;

    EXPECT_EQ(options.port, 4001);
    EXPECT_TRUE(options.has_proxy);
}

TEST(StartupOptions, AcceptsExplicitProxyFlagWithDashPPort)
{
    StartupOptions options {};
    std::string error_message;
    std::vector<std::string> args = { "ageland", "-x", "-p", "4001" };
    std::vector<char*> argv = build_argv(&args);

    ASSERT_TRUE(parse_startup_options(static_cast<int>(argv.size()), argv.data(), &options, &error_message))
        << error_message;

    EXPECT_EQ(options.port, 4001);
    EXPECT_TRUE(options.has_proxy);
}

TEST(StartupOptions, RejectsUnexpectedExtraArgumentAfterDashPPort)
{
    StartupOptions options {};
    std::string error_message;
    std::vector<std::string> args = { "ageland", "-p", "3791", "4001" };
    std::vector<char*> argv = build_argv(&args);

    EXPECT_FALSE(parse_startup_options(static_cast<int>(argv.size()), argv.data(), &options, &error_message));
    EXPECT_FALSE(error_message.empty());
}

TEST(StartupOptions, RejectsUnexpectedExtraArgumentAfterPositionalPort)
{
    StartupOptions options {};
    std::string error_message;
    std::vector<std::string> args = { "ageland", "3791", "-x" };
    std::vector<char*> argv = build_argv(&args);

    EXPECT_FALSE(parse_startup_options(static_cast<int>(argv.size()), argv.data(), &options, &error_message));
    EXPECT_FALSE(error_message.empty());
}

TEST(StartupOptions, AllowsExplicitProxyFlagAfterDashPPort)
{
    StartupOptions options {};
    std::string error_message;
    std::vector<std::string> args = { "ageland", "-p", "3791", "-x" };
    std::vector<char*> argv = build_argv(&args);

    ASSERT_TRUE(parse_startup_options(static_cast<int>(argv.size()), argv.data(), &options, &error_message))
        << error_message;

    EXPECT_EQ(options.port, 3791);
    EXPECT_TRUE(options.has_proxy);
}

TEST(StartupOptions, AcceptsCompactDashPPortForm)
{
    StartupOptions options {};
    std::string error_message;
    std::vector<std::string> args = { "ageland", "-p3791" };
    std::vector<char*> argv = build_argv(&args);

    ASSERT_TRUE(parse_startup_options(static_cast<int>(argv.size()), argv.data(), &options, &error_message))
        << error_message;

    EXPECT_EQ(options.port, 3791);
    EXPECT_FALSE(options.has_proxy);
}

TEST(StartupOptions, HasNoRandomSeedWhenNoneIsGiven)
{
    StartupOptions options {};
    options.random_seed = 99u;
    std::string error_message;
    std::vector<std::string> args = { "ageland", "4000" };
    std::vector<char*> argv = build_argv(&args);

    ASSERT_TRUE(parse_startup_options(static_cast<int>(argv.size()), argv.data(), &options, &error_message))
        << error_message;

    EXPECT_FALSE(options.random_seed.has_value());
}

TEST(StartupOptions, TakesTheRandomSeedFromTheNextArgument)
{
    StartupOptions options {};
    std::string error_message;
    std::vector<std::string> args = { "ageland", "--random-seed", "20261007" };
    std::vector<char*> argv = build_argv(&args);

    ASSERT_TRUE(parse_startup_options(static_cast<int>(argv.size()), argv.data(), &options, &error_message))
        << error_message;

    ASSERT_TRUE(options.random_seed.has_value());
    EXPECT_EQ(*options.random_seed, 20261007u);
}

TEST(StartupOptions, TakesTheRandomSeedAfterAnEqualsSign)
{
    StartupOptions options {};
    std::string error_message;
    std::vector<std::string> args = { "ageland", "--random-seed=42" };
    std::vector<char*> argv = build_argv(&args);

    ASSERT_TRUE(parse_startup_options(static_cast<int>(argv.size()), argv.data(), &options, &error_message))
        << error_message;

    ASSERT_TRUE(options.random_seed.has_value());
    EXPECT_EQ(*options.random_seed, 42u);
}

TEST(StartupOptions, AcceptsTheLargestUnsignedRandomSeed)
{
    const unsigned int largest_seed = std::numeric_limits<unsigned int>::max();
    StartupOptions options {};
    std::string error_message;
    std::vector<std::string> args = { "ageland", "--random-seed", std::to_string(largest_seed) };
    std::vector<char*> argv = build_argv(&args);

    ASSERT_TRUE(parse_startup_options(static_cast<int>(argv.size()), argv.data(), &options, &error_message))
        << error_message;

    ASSERT_TRUE(options.random_seed.has_value());
    EXPECT_EQ(*options.random_seed, largest_seed);
}

TEST(StartupOptions, RefusesARandomSeedTooLargeForAnUnsignedInt)
{
    const unsigned long long one_past_largest
        = static_cast<unsigned long long>(std::numeric_limits<unsigned int>::max()) + 1;
    StartupOptions options {};
    std::string error_message;
    std::vector<std::string> args = { "ageland", "--random-seed", std::to_string(one_past_largest) };
    std::vector<char*> argv = build_argv(&args);

    EXPECT_FALSE(parse_startup_options(static_cast<int>(argv.size()), argv.data(), &options, &error_message));
    EXPECT_NE(error_message.find("too large"), std::string::npos) << error_message;
}

TEST(StartupOptions, RefusesARandomSeedWithASignOrOtherCharacters)
{
    constexpr std::string_view malformed_seeds[]
        = { "-5", "+5", "12abc", " 7", "0x10", "99999999999abc" };
    for (const std::string_view malformed_seed : malformed_seeds) {
        StartupOptions options {};
        std::string error_message;
        std::vector<std::string> args = { "ageland", "--random-seed", std::string(malformed_seed) };
        std::vector<char*> argv = build_argv(&args);

        EXPECT_FALSE(parse_startup_options(static_cast<int>(argv.size()), argv.data(), &options, &error_message))
            << "seed '" << malformed_seed << "'";
        EXPECT_NE(error_message.find("Illegal random seed"), std::string::npos) << error_message;
    }
}

TEST(StartupOptions, RefusesARandomSeedOptionWithNoValue)
{
    const std::vector<std::vector<std::string>> argument_lists = {
        { "ageland", "--random-seed" },
        { "ageland", "--random-seed=" },
    };
    for (const std::vector<std::string>& argument_list : argument_lists) {
        StartupOptions options {};
        std::string error_message;
        std::vector<std::string> args = argument_list;
        std::vector<char*> argv = build_argv(&args);

        EXPECT_FALSE(parse_startup_options(static_cast<int>(argv.size()), argv.data(), &options, &error_message))
            << args.back();
        EXPECT_NE(error_message.find("Random seed expected"), std::string::npos) << error_message;
    }
}

TEST(StartupOptions, RefusesAnUnknownLongOption)
{
    StartupOptions options {};
    std::string error_message;
    std::vector<std::string> args = { "ageland", "--no-such-option", "4000" };
    std::vector<char*> argv = build_argv(&args);

    EXPECT_FALSE(parse_startup_options(static_cast<int>(argv.size()), argv.data(), &options, &error_message));
    EXPECT_NE(error_message.find("--no-such-option"), std::string::npos) << error_message;
}

TEST(StartupOptions, TakesARandomSeedAmongOtherOptions)
{
    StartupOptions options {};
    std::string error_message;
    std::vector<std::string> args = { "ageland", "-d", "lib", "--random-seed", "7", "-p", "4000" };
    std::vector<char*> argv = build_argv(&args);

    ASSERT_TRUE(parse_startup_options(static_cast<int>(argv.size()), argv.data(), &options, &error_message))
        << error_message;

    EXPECT_EQ(options.dir, "lib");
    EXPECT_EQ(options.port, 4000);
    ASSERT_TRUE(options.random_seed.has_value());
    EXPECT_EQ(*options.random_seed, 7u);
}

// The seed the fake seed source hands out, distinct from every seed a test requests.
constexpr unsigned int FAKE_FRESH_SEED = 424242u;
// How many std::rand results a test compares after seeding.
constexpr int RANDOM_NUMBERS_COMPARED = 5;
// How many times the fake seed source has been asked for a seed.
int fake_seed_draws = 0;

// Counts the draw in fake_seed_draws and returns FAKE_FRESH_SEED.
unsigned int draw_fake_fresh_seed()
{
    ++fake_seed_draws;
    return FAKE_FRESH_SEED;
}

// Returns the next RANDOM_NUMBERS_COMPARED results of std::rand.
std::vector<int> next_random_numbers()
{
    std::vector<int> numbers;
    numbers.reserve(RANDOM_NUMBERS_COMPARED);
    for (int index = 0; index < RANDOM_NUMBERS_COMPARED; ++index) {
        numbers.push_back(std::rand());
    }
    return numbers;
}

// Clears the fake seed source's count before each test and afterwards reseeds std::rand with 1,
// which the C standard makes the sequence a process starts with, so one test's seeding cannot
// change the random numbers a later test sees.
class RandomSeedTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        fake_seed_draws = 0;
    }

    void TearDown() override
    {
        std::srand(1);
    }
};

TEST_F(RandomSeedTest, SeedsAndLogsTheRequestedSeed)
{
    const unsigned int requested_seed = 12345u;
    std::srand(requested_seed);
    const std::vector<int> expected_numbers = next_random_numbers();

    testing::internal::CaptureStderr();
    seed_random_numbers(requested_seed, draw_fake_fresh_seed);
    const std::string log_text = testing::internal::GetCapturedStderr();

    EXPECT_EQ(next_random_numbers(), expected_numbers);
    EXPECT_EQ(fake_seed_draws, 0);
    const std::string expected_line
        = "Random numbers seeded with " + std::to_string(requested_seed) + " from --random-seed.";
    EXPECT_NE(log_text.find(expected_line), std::string::npos) << log_text;
}

TEST_F(RandomSeedTest, SeedsAndLogsAFreshSeedWhenNoneIsRequested)
{
    std::srand(FAKE_FRESH_SEED);
    const std::vector<int> expected_numbers = next_random_numbers();

    testing::internal::CaptureStderr();
    seed_random_numbers(std::nullopt, draw_fake_fresh_seed);
    const std::string log_text = testing::internal::GetCapturedStderr();

    EXPECT_EQ(next_random_numbers(), expected_numbers);
    EXPECT_EQ(fake_seed_draws, 1);
    const std::string fresh_seed_text = std::to_string(FAKE_FRESH_SEED);
    const std::string expected_line = "Random numbers seeded with " + fresh_seed_text
        + "; start with --random-seed " + fresh_seed_text + " to repeat them.";
    EXPECT_NE(log_text.find(expected_line), std::string::npos) << log_text;
}

TEST_F(AcceptPathTest, DirectConnectionsReceiveGreetingWithoutWaitingForInput)
{
    in_port_t port = 0;
    const int listener = create_listener_socket(&port);
    ASSERT_GE(listener, 0);
    const int client = connect_client(port);
    ASSERT_GE(client, 0);

    has_proxy = 0;
    ASSERT_EQ(pnew_descriptor(listener), 1);

    const std::string initial_output = read_client_data(client);
    EXPECT_NE(initial_output.find("RETURN OF THE SHADOW"), std::string::npos);
    EXPECT_NE(initial_output.find("Account email:"), std::string::npos);

    close(client);
    close(listener);
}

TEST_F(AcceptPathTest, ProxyConnectionsWaitForCompleteSplitHeaderBeforeSendingGreeting)
{
    in_port_t port = 0;
    const int listener = create_listener_socket(&port);
    ASSERT_GE(listener, 0);
    const int client = connect_client(port);
    ASSERT_GE(client, 0);

    has_proxy = 1;
    ASSERT_EQ(pnew_descriptor(listener), 1);

    const in_addr_t proxy_header = htonl(INADDR_LOOPBACK);
    const unsigned char* header_bytes = reinterpret_cast<const unsigned char*>(&proxy_header);
    ASSERT_EQ(send(client, header_bytes, 2, 0), 2);
    wait_for_game_side_data(descriptor_list);
    ASSERT_EQ(process_input(descriptor_list), 0);
    expect_no_client_data_yet(client);
    ASSERT_EQ(send(client, header_bytes + 2, 2, 0), 2);
    wait_for_game_side_data(descriptor_list);
    ASSERT_EQ(process_input(descriptor_list), 0);

    const std::string initial_output = read_client_data(client);
    EXPECT_NE(initial_output.find("RETURN OF THE SHADOW"), std::string::npos);
    EXPECT_NE(initial_output.find("Account email:"), std::string::npos);

    close(client);
    close(listener);
}

TEST_F(AcceptPathTest, ProxyConnectionsWaitForHeaderBeforeSendingGreeting)
{
    in_port_t port = 0;
    const int listener = create_listener_socket(&port);
    ASSERT_GE(listener, 0);
    const int client = connect_client(port);
    ASSERT_GE(client, 0);

    has_proxy = 1;
    ASSERT_EQ(pnew_descriptor(listener), 1);

    expect_no_client_data_yet(client);

    const in_addr_t proxy_header = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(send(client, &proxy_header, sizeof(proxy_header), 0), static_cast<ssize_t>(sizeof(proxy_header)));
    wait_for_game_side_data(descriptor_list);
    ASSERT_EQ(process_input(descriptor_list), 0);

    const std::string initial_output = read_client_data(client);
    EXPECT_NE(initial_output.find("RETURN OF THE SHADOW"), std::string::npos);
    EXPECT_NE(initial_output.find("Account email:"), std::string::npos);

    close(client);
    close(listener);
}

TEST_F(AcceptPathTest, ProxyConnectionsRejectBannedHostsBeforeGreeting)
{
    ban_list_element banned {};
    strncpy(banned.site, "127.0.0.1", BANNED_SITE_LENGTH);
    banned.site[BANNED_SITE_LENGTH] = '\0';
    banned.type = BAN_ALL;
    ban_list = &banned;

    in_port_t port = 0;
    const int listener = create_listener_socket(&port);
    ASSERT_GE(listener, 0);
    const int client = connect_client(port);
    ASSERT_GE(client, 0);

    has_proxy = 1;
    ASSERT_EQ(pnew_descriptor(listener), 1);

    const in_addr_t proxy_header = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(send(client, &proxy_header, sizeof(proxy_header), 0), static_cast<ssize_t>(sizeof(proxy_header)));
    wait_for_game_side_data(descriptor_list);
    EXPECT_EQ(process_input(descriptor_list), -1);
    expect_no_client_data_yet(client);

    close(client);
    close(listener);
}

TEST_F(AcceptPathTest, ProxyConnectionsGreetWhenTheHeaderArrivesLate)
{
    in_port_t port = 0;
    const int listener = create_listener_socket(&port);
    ASSERT_GE(listener, 0);
    const int client = connect_client(port);
    ASSERT_GE(client, 0);

    has_proxy = 1;
    ASSERT_EQ(pnew_descriptor(listener), 1);

    // The header reaches the game side only after the test has started waiting for it, which is
    // what a slow loopback delivery looks like to the game.
    const in_addr_t proxy_header = htonl(INADDR_LOOPBACK);
    ssize_t header_bytes_sent = -1;
    std::thread late_sender([client, proxy_header, &header_bytes_sent]() -> void {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        header_bytes_sent = send(client, &proxy_header, sizeof(proxy_header), 0);
    });
    wait_for_game_side_data(descriptor_list);
    const int input_result = process_input(descriptor_list);
    late_sender.join();
    ASSERT_EQ(header_bytes_sent, static_cast<ssize_t>(sizeof(proxy_header)));
    ASSERT_EQ(input_result, 0);

    const std::string initial_output = read_client_data(client);
    EXPECT_NE(initial_output.find("RETURN OF THE SHADOW"), std::string::npos);
    EXPECT_NE(initial_output.find("Account email:"), std::string::npos);

    close(client);
    close(listener);
}

} // namespace
