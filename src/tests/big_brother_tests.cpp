#include "../big_brother.h"
#include "../interpre.h"
#include "../structs.h"
#include "../world_clock.h"

#include <chrono>
#include <gtest/gtest.h>
#include <string>
#include <string_view>

ACMD(do_afk);

extern struct descriptor_data* descriptor_list;
extern struct room_data world;
extern struct weather_data weather_info;

namespace {
using game_rules::big_brother;

constexpr std::string_view PROTECTION_GRANTED = "You feel the protection of the Gods";
constexpr std::string_view PROTECTION_REFUSED = "You have engaged in PK too recently";

// An arbitrary fixed instant the tests measure from, so each sets the time explicitly.
const WorldClock::time_point REFERENCE_TIME = WorldClock::time_point {} + std::chrono::hours(1);

// A playing character on descriptor_list, so Big Brother's messages to it land in its output
// buffer, and an opponent it can engage in PK. Big Brother forgets both on destruction.
class AfkPlayerContext {
public:
    AfkPlayerContext()
    {
        big_brother::create(weather_info, &world);

        player.abs_number = 4101;
        player.player.name = player_name;
        player.in_room = NOWHERE;
        player.desc = &connection;
        opponent.abs_number = 4102;
        opponent.player.name = opponent_name;

        connection.output = connection.small_outbuf;
        connection.small_outbuf[0] = '\0';
        connection.bufptr = 0;
        connection.bufspace = SMALL_BUFSIZE - 1;
        connection.connected = CON_PLYNG;
        connection.character = &player;
        connection.next = nullptr;
        previous_descriptor_list = descriptor_list;
        descriptor_list = &connection;
    }

    ~AfkPlayerContext()
    {
        big_brother& rules = big_brother::instance();
        rules.on_character_disconnected(&player);
        rules.on_character_disconnected(&opponent);
        descriptor_list = previous_descriptor_list;
    }

    // True when the player has been sent text containing message.
    bool was_told(std::string_view message) const
    {
        const std::string_view output(connection.small_outbuf);
        return output.find(message) != std::string_view::npos;
    }

    // Everything the player has been sent so far.
    std::string output() const { return std::string(connection.small_outbuf); }

    // The character going AFK; connected through connection and in no room.
    char_data player {};
    // The other side of the PK engagement; it never receives messages.
    char_data opponent {};

private:
    // Storage for the player's name, which Big Brother may log.
    char player_name[16] = "AfkPlayer";
    // Storage for the opponent's name, which Big Brother may log.
    char opponent_name[16] = "PkOpponent";
    // The player's connection, holding the output under test.
    descriptor_data connection {};
    // The descriptor list before this context replaced it, restored on destruction.
    descriptor_data* previous_descriptor_list = nullptr;
};
} // namespace

TEST(BigBrotherAfkProtection, GrantsProtectionWithNoPkEngagement)
{
    AfkPlayerContext context;
    big_brother& rules = big_brother::instance();

    rules.on_character_afked(&context.player, REFERENCE_TIME);

    EXPECT_TRUE(context.was_told(PROTECTION_GRANTED)) << context.output();
    EXPECT_FALSE(context.was_told(PROTECTION_REFUSED)) << context.output();
}

TEST(BigBrotherAfkProtection, RefusesProtectionJustBeforeTheDelayAfterAttacking)
{
    AfkPlayerContext context;
    big_brother& rules = big_brother::instance();
    rules.on_character_attacked_player(&context.player, &context.opponent, REFERENCE_TIME);

    const WorldClock::time_point afk_time
        = REFERENCE_TIME + big_brother::PK_AFK_PROTECTION_DELAY - std::chrono::seconds(1);
    rules.on_character_afked(&context.player, afk_time);

    EXPECT_TRUE(context.was_told(PROTECTION_REFUSED)) << context.output();
    EXPECT_FALSE(context.was_told(PROTECTION_GRANTED)) << context.output();
}

TEST(BigBrotherAfkProtection, RefusesProtectionJustBeforeTheDelayAfterBeingAttacked)
{
    AfkPlayerContext context;
    big_brother& rules = big_brother::instance();
    rules.on_character_attacked_player(&context.opponent, &context.player, REFERENCE_TIME);

    const WorldClock::time_point afk_time
        = REFERENCE_TIME + big_brother::PK_AFK_PROTECTION_DELAY - std::chrono::seconds(1);
    rules.on_character_afked(&context.player, afk_time);

    EXPECT_TRUE(context.was_told(PROTECTION_REFUSED)) << context.output();
    EXPECT_FALSE(context.was_told(PROTECTION_GRANTED)) << context.output();
}

TEST(BigBrotherAfkProtection, GrantsProtectionOnceTheDelayHasPassed)
{
    AfkPlayerContext context;
    big_brother& rules = big_brother::instance();
    rules.on_character_attacked_player(&context.player, &context.opponent, REFERENCE_TIME);

    const WorldClock::time_point afk_time = REFERENCE_TIME + big_brother::PK_AFK_PROTECTION_DELAY;
    rules.on_character_afked(&context.player, afk_time);

    EXPECT_TRUE(context.was_told(PROTECTION_GRANTED)) << context.output();
    EXPECT_FALSE(context.was_told(PROTECTION_REFUSED)) << context.output();
}

TEST(BigBrotherAfkProtection, MeasuresTheDelayFromTheLatestEngagement)
{
    AfkPlayerContext context;
    big_brother& rules = big_brother::instance();
    rules.on_character_attacked_player(&context.player, &context.opponent, REFERENCE_TIME);
    const WorldClock::time_point second_attack_time
        = REFERENCE_TIME + big_brother::PK_AFK_PROTECTION_DELAY / 2;
    rules.on_character_attacked_player(&context.player, &context.opponent, second_attack_time);

    const WorldClock::time_point afk_time = REFERENCE_TIME + big_brother::PK_AFK_PROTECTION_DELAY;
    rules.on_character_afked(&context.player, afk_time);

    EXPECT_TRUE(context.was_told(PROTECTION_REFUSED)) << context.output();
    EXPECT_FALSE(context.was_told(PROTECTION_GRANTED)) << context.output();
}

TEST(BigBrotherAfkProtection, AfkCommandGrantsProtectionOnceTheDelayHasPassed)
{
    AfkPlayerContext context;
    big_brother& rules = big_brother::instance();
    // Protection follows only if do_afk passes the current world time.
    const WorldClock::time_point now = WorldClock::now();
    const WorldClock::time_point engagement_time
        = now - big_brother::PK_AFK_PROTECTION_DELAY - std::chrono::seconds(1);
    rules.on_character_attacked_player(&context.player, &context.opponent, engagement_time);

    char no_argument[] = "";
    do_afk(&context.player, no_argument, nullptr, 0, 0);

    EXPECT_TRUE(context.was_told(PROTECTION_GRANTED)) << context.output();
    EXPECT_FALSE(context.was_told(PROTECTION_REFUSED)) << context.output();
}
