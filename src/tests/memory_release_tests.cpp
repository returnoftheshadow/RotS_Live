/* Game functions that must give back the memory of what they let go: freeing a mob with no
 * prototype, and a zone reset that gives a mob another script.
 *
 * Not portable standard C++, by owner ruling of 2026-10-26: a free cannot be observed without
 * changing the game, so these tests count the allocator's bytes in use (allocated_bytes.h) and
 * are skipped where it cannot tell. Each does its operation many times, so a leak far outgrows
 * what the allocator keeps cached. */
#include "allocated_bytes.h"
#include "scripted_room_fixture.h"

void do_say(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);

using namespace scripted_room;

namespace {

/* How many times a test repeats the operation it measures. */
constexpr int ROUNDS = 128;
/* Rounds run before measuring: glibc keeps the first few freed blocks of each size cached, and
 * mallinfo2 counts them as in use. */
constexpr int WARM_UP_ROUNDS = 32;
/* The fewest bytes a leak would keep each round: str_dup's 256-byte block for a short text. */
constexpr std::size_t SMALLEST_LEAK_PER_ROUND = 256;
/* Growth the allocator may show over all the rounds without a leak: a quarter of the smallest. */
constexpr std::size_t ALLOWED_GROWTH = ROUNDS * SMALLEST_LEAK_PER_ROUND / 4;

class MemoryReleaseTest : public ScriptedRoomFixture {
protected:
    /* How many bytes in use grew over ROUNDS calls of <round>, measured after WARM_UP_ROUNDS
     * calls, so neither what the game allocates once nor what the allocator caches counts. */
    template <typename Round>
    static std::size_t growth_over_rounds(Round round)
    {
        for (int warm_up = 0; warm_up < WARM_UP_ROUNDS; ++warm_up) {
            round();
        }
        const std::size_t before = *allocated_bytes();
        for (int measured = 0; measured < ROUNDS; ++measured) {
            round();
        }
        const std::size_t after = *allocated_bytes();
        /* Less in use than before is no growth. */
        return after > before ? after - before : 0;
    }

    /* Empties what the player was sent, so the output of many rounds never makes
     * write_to_output allocate a large buffer. */
    void clear_output()
    {
        release_test_descriptor(&m_descriptor);
        m_descriptor.small_outbuf[0] = '\0';
        m_descriptor.bufptr = 0;
        m_descriptor.bufspace = SMALL_BUFSIZE - 1;
    }

    /* Makes the zone table one zone whose reset finds the first mob with no prototype in room 0
     * ('L') and gives it script <script_vnum> (A 12). */
    void install_script_assigning_zone(int script_vnum)
    {
        m_commands[0].command = 'L';
        m_commands[0].arg1 = 0;
        m_commands[0].arg2 = 0;
        m_commands[0].arg3 = -1;
        m_commands[0].arg4 = 1;
        m_commands[1].command = 'A';
        m_commands[1].arg1 = 12;
        m_commands[1].arg2 = script_vnum;
        m_zone.cmd = m_commands;
        m_zone.cmdno = 2;
        zone_table = &m_zone;
    }

private:
    // The reset commands install_script_assigning_zone writes.
    reset_com m_commands[2] {};
    // The zone those commands belong to; TearDown puts the game's zone table back.
    zone_data m_zone {};
};

} // namespace

TEST_F(MemoryReleaseTest, FreeingAMobWithNoPrototypeGivesBackItsKeywords)
{
    if (!allocated_bytes()) {
        GTEST_SKIP() << "this allocator cannot report the bytes in use";
    }

    const std::size_t growth = growth_over_rounds([this]() -> void {
        extract_char(make_mob("listener", 0));
    });

    EXPECT_LT(growth, ALLOWED_GROWTH);
}

TEST_F(MemoryReleaseTest, AZoneResetGivingAMobAnotherScriptGivesBackItsOldScriptState)
{
    if (!allocated_bytes()) {
        GTEST_SKIP() << "this allocator cannot report the bytes in use";
    }
    install_scripts({ { SCRIPT_VNUM,
        { { ON_HEAR_SAY,
            { { SCRIPT_SEND_TO_ROOM, "The mob heard [%s].",
                { SCRIPT_PARAM_CH1_ROOM, SCRIPT_PARAM_STR1 } } } } } } });
    char_data* mob = make_mob("listener", SCRIPT_VNUM);
    /* The reset gives the mob the same script again, so each say starts it afresh. */
    install_script_assigning_zone(SCRIPT_VNUM);
    run_command(do_say, "hello");
    ASSERT_NE(nullptr, mob->specials.script_info) << "hearing gave the mob script state";

    const std::size_t growth = growth_over_rounds([this]() -> void {
        reset_zone(0);
        run_command(do_say, "hello");
        clear_output();
    });

    EXPECT_LT(growth, ALLOWED_GROWTH);
}
