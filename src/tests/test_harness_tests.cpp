#include "../test_harness.h"

#include "../spells.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

extern struct skill_data skills[];
void affect_update_person(struct char_data* i, int mode);
void clear_char(struct char_data* ch, int mode);

namespace {

class HarnessSeedTest : public ::testing::Test {
protected:
    void TearDown() override
    {
        harness_mode = 0;
        unsetenv("ROTS_RANDOM_SEED");
    }
};

} // namespace

TEST_F(HarnessSeedTest, SeedIsIgnoredOutsideHarnessMode)
{
    harness_mode = 0;
    setenv("ROTS_RANDOM_SEED", "42", 1);

    EXPECT_FALSE(seed_random_from_environment()) << "a seed must not apply unless -t was given";
}

TEST_F(HarnessSeedTest, SeedIsIgnoredWhenTheVariableIsUnset)
{
    harness_mode = 1;
    unsetenv("ROTS_RANDOM_SEED");

    EXPECT_FALSE(seed_random_from_environment());
}

TEST_F(HarnessSeedTest, RejectsASignedPaddedOrOutOfRangeSeed)
{
    harness_mode = 1;

    setenv("ROTS_RANDOM_SEED", "-1", 1);
    EXPECT_FALSE(seed_random_from_environment());

    setenv("ROTS_RANDOM_SEED", " 42", 1);
    EXPECT_FALSE(seed_random_from_environment());

    // One past the largest 64-bit value, so it overflows unsigned on every build.
    setenv("ROTS_RANDOM_SEED", "18446744073709551616", 1);
    EXPECT_FALSE(seed_random_from_environment());
}

TEST_F(HarnessSeedTest, RejectsANonNumericSeed)
{
    harness_mode = 1;
    setenv("ROTS_RANDOM_SEED", "forty-two", 1);

    EXPECT_FALSE(seed_random_from_environment());
}

TEST_F(HarnessSeedTest, SeedMakesTheRandomSequenceRepeatable)
{
    harness_mode = 1;
    setenv("ROTS_RANDOM_SEED", "42", 1);

    ASSERT_TRUE(seed_random_from_environment());
    // Draws go through number(), the generator the seed is for.
    const int first_draw = number(0, 1000000);
    const int second_draw = number(0, 1000000);

    ASSERT_TRUE(seed_random_from_environment());
    EXPECT_EQ(number(0, 1000000), first_draw)
        << "reseeding with the same value must replay the sequence";
    EXPECT_EQ(number(0, 1000000), second_draw);
}

namespace {

// A slow SPELL_CURING affect, whose affect_update_person() case does nothing, on time phase 1.
// The test binary's pulse stays 0, so the live phase is 0 and only the harness flag can make the
// affect tick.
struct ForcedPhaseFixture {
    // The affected character; it has no descriptor, so affect_update_person() processes it.
    char_data character {};
    // The character's only affect; its duration counts the ticks.
    affected_type affect {};
    // SPELL_CURING's is_fast before the fixture cleared it, restored on destruction.
    byte previous_is_fast = 0;

    ForcedPhaseFixture()
    {
        previous_is_fast = skills[SPELL_CURING].is_fast;
        skills[SPELL_CURING].is_fast = 0;
        affect.type = SPELL_CURING;
        affect.duration = 5;
        affect.time_phase = 1;
        character.affected = &affect;
    }

    ~ForcedPhaseFixture()
    {
        skills[SPELL_CURING].is_fast = previous_is_fast;
        harness_force_affect_phase = 0;
    }
};

} // namespace

TEST(HarnessAffects, FlagIsOffByDefault)
{
    EXPECT_EQ(harness_force_affect_phase, 0);
}

TEST(HarnessAffects, SlowAffectDoesNotTickOnAMismatchedPhaseWithoutTheFlag)
{
    ForcedPhaseFixture fixture;
    affect_update_person(&fixture.character, 0);
    EXPECT_EQ(fixture.affect.duration, 5);
}

TEST(HarnessAffects, FlagForcesExactlyOneTickPerCall)
{
    ForcedPhaseFixture fixture;
    harness_force_affect_phase = 1;
    affect_update_person(&fixture.character, 0);
    EXPECT_EQ(fixture.affect.duration, 4);
    affect_update_person(&fixture.character, 0);
    EXPECT_EQ(fixture.affect.duration, 3);
}

namespace {

// A playing character with a descriptor whose output the tests read back.
class HarnessCommandTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_descriptor.output = m_descriptor.small_outbuf;
        m_descriptor.small_outbuf[0] = '\0';
        m_descriptor.bufptr = 0;
        m_descriptor.bufspace = SMALL_BUFSIZE - 1;
        m_descriptor.connected = CON_PLYNG;

        clear_char(&m_character, MOB_VOID);
        m_character.player.name = strdup("Harnesser");
        m_character.player.level = LEVEL_IMPL;
        m_character.desc = &m_descriptor;
        m_descriptor.character = &m_character;
    }

    void TearDown() override
    {
        harness_mode = 0;
        free(m_character.player.name);
        free(m_character.profs);
    }

    void run_harness(std::string_view arguments)
    {
        // do_harness() takes a mutable, null-terminated argument.
        std::string mutable_arguments(arguments);
        do_harness(&m_character, mutable_arguments.data(), nullptr, 0, 0);
    }

    std::string output() const { return std::string(m_descriptor.small_outbuf); }

    // Receives the command's replies in small_outbuf.
    descriptor_data m_descriptor {};
    // The character running the command; an implementor unless a test lowers its level.
    char_data m_character {};
};

} // namespace

TEST_F(HarnessCommandTest, RefusesWithoutHarnessMode)
{
    harness_mode = 0;

    run_harness("tick");

    EXPECT_NE(output().find("started with -t"), std::string::npos) << output();
}

TEST_F(HarnessCommandTest, RefusesACharacterBelowImplementor)
{
    harness_mode = 1;
    m_character.player.level = LEVEL_IMPL - 1;

    run_harness("tick");

    EXPECT_EQ(output(), "You can't do that.\r\n");
}

TEST_F(HarnessCommandTest, ShowsUsageForAnUnknownSubcommand)
{
    harness_mode = 1;

    run_harness("ticks");

    EXPECT_EQ(output(), "Usage: harness tick | harness affects\r\n");
}
