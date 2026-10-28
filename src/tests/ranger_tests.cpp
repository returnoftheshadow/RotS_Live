// Ranger skills: whom an ambush or a sprung trap goes for, and how long calming an animal takes.

#include "../comm.h" /* abort_delay */
#include "../handler.h" /* set_char_exists, remove_char_exists */
#include "../interpre.h"
#include "../spells.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <string>

extern struct room_data world;
extern struct skill_data skills[MAX_SKILLS];
struct char_data* ambush_get_valid_victim(
    struct char_data* character, const struct waiting_type* target);
struct char_data* trap_get_valid_victim(
    struct char_data* character, const struct waiting_type* target);
int in_waiting_list(const struct char_data* character);
ACMD(do_calm);

namespace {

// The room the ranger and the wolf stand in: any slot of the test world.
constexpr int RANGER_ROOM = 972;
// The character slot the wolf is registered in, so char_exists finds it.
constexpr int ANIMAL_SLOT = 4620;
// The slot of a character registered in none.
constexpr int NO_CHARACTER_SLOT = -1;
// The wolf's level.
constexpr int ANIMAL_LEVEL = 10;
// The ranger's level.
constexpr int RANGER_LEVEL = 20;
// do_calm's wait: skills[SKILL_CALM].beats * CALM_WAIT_FACTOR * the animal's level, divided by
// the ranger's ranger level plus its calm knowledge / CALM_KNOWLEDGE_PER_LEVEL.
constexpr int CALM_WAIT_FACTOR = 2;
// The calm knowledge that counts as one ranger level in do_calm's wait.
constexpr int CALM_KNOWLEDGE_PER_LEVEL = 15;
// ch_get_confuse_modifier: knowledge lost is CONFUSION_DURATION_FACTOR times the confusion's
// duration, less CONFUSION_OFFSET.
constexpr int CONFUSION_DURATION_FACTOR = 2;
// See CONFUSION_DURATION_FACTOR.
constexpr int CONFUSION_OFFSET = 10;

// A human ranger and an aggressive wolf standing in a lit RANGER_ROOM. The ranger's output is
// readable; its skills, knowledge and confusion are set by each test.
class RangerSkillTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_saved_people = world[RANGER_ROOM].people;
        m_saved_light = world[RANGER_ROOM].light;
        m_saved_flags = world[RANGER_ROOM].room_flags;
        world[RANGER_ROOM].people = nullptr;
        world[RANGER_ROOM].light = 1; /* lit */
        world[RANGER_ROOM].room_flags = 0;

        m_descriptor.output = m_descriptor.small_outbuf;
        m_descriptor.bufspace = SMALL_BUFSIZE - 1;
        m_descriptor.connected = CON_PLYNG;

        m_ranger.player.name = m_ranger_name;
        m_ranger.player.race = RACE_HUMAN;
        m_ranger.player.level = RANGER_LEVEL;
        m_ranger.profs = &m_ranger_profs;
        m_ranger.skills = m_ranger_skills;
        m_ranger.knowledge = m_ranger_knowledge;
        m_ranger.specials.position = POSITION_STANDING;
        m_ranger.abs_number = NO_CHARACTER_SLOT;
        m_ranger.desc = &m_descriptor;
        m_descriptor.character = &m_ranger;

        m_wolf.player.name = m_wolf_name;
        m_wolf.player.short_descr = m_wolf_name;
        m_wolf.player.level = ANIMAL_LEVEL;
        m_wolf.player.bodytype = BODYTYPE_ANIMAL;
        m_wolf.specials2.act = MOB_ISNPC | MOB_AGGRESSIVE;
        m_wolf.specials.position = POSITION_STANDING;
        m_wolf.abs_number = ANIMAL_SLOT;
        set_char_exists(ANIMAL_SLOT);

        stand_in_room(&m_wolf);
        stand_in_room(&m_ranger);
    }

    void TearDown() override
    {
        if (in_waiting_list(&m_ranger) || m_ranger.delay.wait_value != 0) {
            abort_delay(&m_ranger);
        }
        remove_char_exists(ANIMAL_SLOT);
        world[RANGER_ROOM].people = m_saved_people;
        world[RANGER_ROOM].light = m_saved_light;
        world[RANGER_ROOM].room_flags = m_saved_flags;
    }

    static void stand_in_room(char_data* character)
    {
        character->in_room = RANGER_ROOM;
        character->next_in_room = world[RANGER_ROOM].people;
        world[RANGER_ROOM].people = character;
    }

    // Confuses the ranger enough to take <knowledge_lost> points off every skill it uses.
    void confuse_ranger(int knowledge_lost)
    {
        m_confusion.type = SPELL_CONFUSE;
        m_confusion.duration = (knowledge_lost + CONFUSION_OFFSET) / CONFUSION_DURATION_FACTOR;
        m_ranger.affected = &m_confusion;
        m_ranger.specials.affected_by |= AFF_CONFUSE;
    }

    // The wait do_calm starts at <ranger_level> ranger levels with <calm_knowledge> in calm,
    // calming the wolf.
    int calm_wait(int ranger_level, int calm_knowledge)
    {
        m_ranger_profs.prof_level[PROF_RANGER] = ranger_level;
        m_ranger_knowledge[SKILL_CALM] = calm_knowledge;
        char argument[] = "wolf";
        do_calm(&m_ranger, argument, nullptr, 0, 0);
        return m_ranger.delay.wait_value;
    }

    std::string output() const { return m_descriptor.output; }

    // The ranger, a PC; it holds the arrays and profs below.
    char_data m_ranger {};
    // The ranger's proficiency levels.
    char_prof_data m_ranger_profs {};
    // The ranger's practised skills.
    byte m_ranger_skills[MAX_SKILLS] {};
    // The ranger's skill knowledge, which GET_SKILL reads.
    byte m_ranger_knowledge[MAX_SKILLS] {};
    // The confusion affect confuse_ranger puts on the ranger.
    affected_type m_confusion {};
    // An aggressive animal mob in the room, in slot ANIMAL_SLOT.
    char_data m_wolf {};
    // The ranger's connection; its output buffer holds what the ranger was sent.
    descriptor_data m_descriptor {};
    // The ranger's name.
    char m_ranger_name[8] = "Strider";
    // The wolf's name and short description.
    char m_wolf_name[8] = "wolf";
    // RANGER_ROOM's people from before the test, put back afterwards.
    char_data* m_saved_people = nullptr;
    // RANGER_ROOM's light from before the test, put back afterwards.
    int m_saved_light = 0;
    // RANGER_ROOM's flags from before the test, put back afterwards.
    long m_saved_flags = 0;
};

/* The two lookups below run back to back from one frame, so a victim the second leaves unset reads
 * what the first one found: in an unoptimised build, the same stack slot. */

TEST_F(RangerSkillTest, AnAmbushWithNoTargetFindsNoVictim)
{
    waiting_type named_wolf {};
    named_wolf.targ1.type = TARGET_CHAR;
    named_wolf.targ1.ptr.ch = &m_wolf;
    named_wolf.targ1.ch_num = ANIMAL_SLOT;
    waiting_type no_target {};

    char_data* named_victim = ambush_get_valid_victim(&m_ranger, &named_wolf);
    char_data* unnamed_victim = ambush_get_valid_victim(&m_ranger, &no_target);

    EXPECT_EQ(named_victim, &m_wolf);
    EXPECT_EQ(unnamed_victim, nullptr);
    EXPECT_EQ(output(), "Ambush who?\r\n");
}

TEST_F(RangerSkillTest, AnAmbushOnAVanishedTargetFindsNoVictim)
{
    waiting_type named_wolf {};
    named_wolf.targ1.type = TARGET_CHAR;
    named_wolf.targ1.ptr.ch = &m_wolf;
    named_wolf.targ1.ch_num = ANIMAL_SLOT;

    char_data* named_victim = ambush_get_valid_victim(&m_ranger, &named_wolf);
    remove_char_exists(ANIMAL_SLOT);
    char_data* vanished_victim = ambush_get_valid_victim(&m_ranger, &named_wolf);

    EXPECT_EQ(named_victim, &m_wolf);
    EXPECT_EQ(vanished_victim, nullptr);
    EXPECT_EQ(output(), "Ambush who?\r\n");
}

TEST_F(RangerSkillTest, ATrapSetForAnotherKeywordDoesNotSpringOnANewcomer)
{
    char wolf_keyword[] = "wolf";
    txt_block wolf_text { wolf_keyword, nullptr };
    waiting_type wolf_trap {};
    wolf_trap.targ1.type = TARGET_TEXT;
    wolf_trap.targ1.ptr.text = &wolf_text;
    wolf_trap.targ2.type = TARGET_CHAR;
    wolf_trap.targ2.ptr.ch = &m_wolf;

    char bear_keyword[] = "bear";
    txt_block bear_text { bear_keyword, nullptr };
    waiting_type bear_trap = wolf_trap;
    bear_trap.targ1.ptr.text = &bear_text;

    char_data* wolf_trap_victim = trap_get_valid_victim(&m_ranger, &wolf_trap);
    char_data* bear_trap_victim = trap_get_valid_victim(&m_ranger, &bear_trap);

    EXPECT_EQ(wolf_trap_victim, &m_wolf);
    EXPECT_EQ(bear_trap_victim, nullptr);
}

TEST_F(RangerSkillTest, CalmWaitShrinksWithRangerLevelAndCalmKnowledge)
{
    constexpr int CALM_KNOWLEDGE = 80;
    const int wait = calm_wait(RANGER_LEVEL, CALM_KNOWLEDGE);

    EXPECT_EQ(wait, skills[SKILL_CALM].beats * CALM_WAIT_FACTOR * ANIMAL_LEVEL
            / (RANGER_LEVEL + CALM_KNOWLEDGE / CALM_KNOWLEDGE_PER_LEVEL));
    EXPECT_EQ(m_ranger.delay.cmd, CMD_CALM);
}

TEST_F(RangerSkillTest, ConfusionCancellingTheRangerLevelGivesTheLongestCalmWait)
{
    // 20 calm less 50 for confusion is -30: -30 / 15 cancels 2 ranger levels.
    constexpr int KNOWLEDGE_LOST = 50;
    constexpr int CALM_KNOWLEDGE = 20;
    constexpr int CANCELLED_LEVELS = 2;
    confuse_ranger(KNOWLEDGE_LOST);

    const int wait = calm_wait(CANCELLED_LEVELS, CALM_KNOWLEDGE);

    EXPECT_EQ(wait, skills[SKILL_CALM].beats * CALM_WAIT_FACTOR * ANIMAL_LEVEL);
    EXPECT_EQ(m_ranger.delay.cmd, CMD_CALM);
}

TEST_F(RangerSkillTest, ConfusionOutweighingTheRangerLevelGivesTheLongestCalmWait)
{
    // -30 / 15 outweighs 1 ranger level: the wait is the longest, as when it cancels it.
    constexpr int KNOWLEDGE_LOST = 50;
    constexpr int CALM_KNOWLEDGE = 20;
    constexpr int OUTWEIGHED_LEVELS = 1;
    confuse_ranger(KNOWLEDGE_LOST);

    const int wait = calm_wait(OUTWEIGHED_LEVELS, CALM_KNOWLEDGE);

    EXPECT_EQ(wait, skills[SKILL_CALM].beats * CALM_WAIT_FACTOR * ANIMAL_LEVEL);
    EXPECT_EQ(m_ranger.delay.cmd, CMD_CALM);
}

} // namespace
