#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../spells.h"
#include "../structs.h"
#include "../utils.h"
#include "../zone.h"
#include "test_cleanup.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

extern struct room_data world;
extern struct zone_data* zone_table;
extern struct skill_data skills[MAX_SKILLS];
extern skill_teach_data guildmasters[];
void clear_char(struct char_data* ch, int mode);
void parse_practice_args(const char* arg, int* times, std::string* skill);
SPECIAL(guild);

namespace {

/* ---- Reading the words after "practice" ---- */

TEST(PracticeArgs, NumberAndSkillInEitherOrder)
{
    int times = 1;
    std::string skill;
    parse_practice_args("kick 3", &times, &skill);
    EXPECT_EQ(times, 3);
    EXPECT_EQ(skill, "kick");

    times = 1;
    parse_practice_args("3 kick", &times, &skill);
    EXPECT_EQ(times, 3);
    EXPECT_EQ(skill, "kick");
}

TEST(PracticeArgs, ExtraSpacesAreNotAll)
{
    int times = 1;
    std::string skill;
    parse_practice_args("3  kick", &times, &skill);
    EXPECT_EQ(times, 3);
    EXPECT_EQ(skill, "kick");

    times = 1;
    parse_practice_args("fire  bolt", &times, &skill);
    EXPECT_EQ(times, 1);
    EXPECT_EQ(skill, "fire bolt");
}

TEST(PracticeArgs, TabsSplitWordsLikeSpaces)
{
    int times = 1;
    std::string skill;
    parse_practice_args("fire\tbolt\t3", &times, &skill);
    EXPECT_EQ(times, 3);
    EXPECT_EQ(skill, "fire bolt");
}

TEST(PracticeArgs, AllAndItsShortFormsAreAll)
{
    for (const char* text : { "kick all", "kick al", "kick a", "a kick" }) {
        int times = 1;
        std::string skill;
        parse_practice_args(text, &times, &skill);
        EXPECT_EQ(times, 200) << text;
        EXPECT_EQ(skill, "kick") << text;
    }

    int times = 1;
    std::string skill;
    parse_practice_args("kick alls", &times, &skill);
    EXPECT_EQ(times, 1) << "a longer word is part of the skill name";
    EXPECT_EQ(skill, "kick alls");
}

TEST(PracticeArgs, AllIgnoresCase)
{
    for (const char* text : { "kick ALL", "kick All", "kick A", "Al kick" }) {
        int times = 1;
        std::string skill;
        parse_practice_args(text, &times, &skill);
        EXPECT_EQ(times, 200) << text;
        EXPECT_EQ(skill, "kick") << text;
    }
}

TEST(PracticeArgs, AShortFormThatIsPartOfASkillNameStaysInTheName)
{
    struct {
        const char* text;
        int times;
        const char* skill;
    } cases[] = {
        { "fast a", 1, "fast a" }, /* fast attack */
        { "fast a all", 200, "fast a" },
        { "fast A 3", 3, "fast A" },
        { "word of a", 1, "word of a" }, /* word of agony */
        { "refresh all", 1, "refresh all" },
        { "refresh all all", 200, "refresh all" },
    };
    for (const auto& c : cases) {
        int times = 1;
        std::string skill;
        parse_practice_args(c.text, &times, &skill);
        EXPECT_EQ(times, c.times) << c.text;
        EXPECT_EQ(skill, c.skill) << c.text;
    }
}

TEST(PracticeArgs, TheLaterCountWins)
{
    int times = 1;
    std::string skill;
    parse_practice_args("3 kick all", &times, &skill);
    EXPECT_EQ(times, 200);
    EXPECT_EQ(skill, "kick");

    times = 1;
    parse_practice_args("kick all 3", &times, &skill);
    EXPECT_EQ(times, 3);
    EXPECT_EQ(skill, "kick");
}

TEST(PracticeArgs, CountsAreCappedAtTwoHundred)
{
    struct {
        const char* text;
        int times;
    } cases[] = { { "kick 0", 0 }, { "kick 000", 0 }, { "kick 007", 7 }, { "kick 200", 200 },
        { "kick 250", 200 }, { "kick 99999999999", 200 }, { "kick 00000000005", 5 } };
    for (const auto& c : cases) {
        int times = 1;
        std::string skill;
        parse_practice_args(c.text, &times, &skill);
        EXPECT_EQ(times, c.times) << c.text;
        EXPECT_EQ(skill, "kick") << c.text;
    }
}

/* ---- Practicing at the guildmaster ----
 *
 * Program 1 (ALL SKILLS) teaches every skill to 100. A level 20 human
 * warrior practices kick, whose difficulty is set so that it reaches 100%
 * at 16 sessions without ever passing 100 (where the game cuts it back). */

class GuildPracticeTest : public ::testing::Test {
protected:
    static constexpr int kRoom = 986;
    static constexpr int kSessions = 120;

    void SetUp() override
    {
        m_saved_room = world[kRoom];
        m_saved_zone_table = zone_table;
        zone_table = m_zone_table;
        world[kRoom].zone = 0;
        world[kRoom].number = 99000 + kRoom;
        world[kRoom].people = nullptr;
        world[kRoom].contents = nullptr;
        m_saved_kick = skills[SKILL_KICK];
        skills[SKILL_KICK].learn_diff = 35;
        m_saved_limit = guildmasters[0].knowledge[SKILL_KICK];

        clear_char(&m_host, MOB_ISNPC);
        m_host.specials2.act = MOB_ISNPC;
        m_host.specials.store_prog_number = 1;
        m_host.specials2.will_teach = 1L << RACE_HUMAN;
        m_host.player.name = m_host_name;
        m_host.player.short_descr = m_host_name;
        m_host.specials.position = POSITION_STANDING;
        m_host.in_room = NOWHERE;
        char_to_room(&m_host, kRoom);

        make_player(&m_first, &m_first_profs, m_first_skills, m_first_knowledge);
        make_player(&m_second, &m_second_profs, m_second_skills, m_second_knowledge);
    }

    void TearDown() override
    {
        for (char_data* ch : { &m_host, &m_first, &m_second }) {
            ch->desc = nullptr;
            if (ch->in_room != NOWHERE)
                char_from_room(ch);
        }
        guildmasters[0].knowledge[SKILL_KICK] = m_saved_limit;
        skills[SKILL_KICK] = m_saved_kick;
        world[kRoom] = m_saved_room;
        zone_table = m_saved_zone_table;
        release_cleared_character(&m_host);
        release_test_descriptor(&m_descriptor);
        release_test_descriptor(&m_watcher_descriptor);
    }

    void make_player(char_data* pc, char_prof_data* profs, byte* pc_skills, byte* pc_knowledge)
    {
        clear_char(pc, 0);
        release_cleared_character(pc); /* the fixture's own arrays replace clear_char's */
        *profs = char_prof_data();
        for (int prof = 0; prof <= MAX_PROFS; ++prof) {
            profs->prof_coof[prof] = 1000;
            profs->prof_level[prof] = 20;
        }
        pc->profs = profs;
        std::memset(pc_skills, 0, MAX_SKILLS);
        std::memset(pc_knowledge, 0, MAX_SKILLS);
        pc->skills = pc_skills;
        pc->knowledge = pc_knowledge;
        pc->player.name = m_player_name;
        pc->player.race = RACE_HUMAN;
        pc->player.level = 20;
        pc->specials.position = POSITION_STANDING;
        pc->specials2.spells_to_learn = kSessions;
        pc->in_room = NOWHERE;
        char_to_room(pc, kRoom);
    }

    void practice(char_data* pc, const char* text)
    {
        char arg[100];
        std::strcpy(arg, text);
        guild(&m_host, pc, CMD_PRACTICE, arg, SPECIAL_COMMAND, nullptr);
    }

    /* Gives pc a connection, so what the guildmaster says can be read. */
    void connect(char_data* pc)
    {
        m_descriptor.output = m_descriptor.small_outbuf;
        m_descriptor.small_outbuf[0] = '\0';
        m_descriptor.bufspace = SMALL_BUFSIZE - 1;
        m_descriptor.connected = CON_PLYNG;
        m_descriptor.descriptor = 1;
        m_descriptor.character = pc;
        pc->desc = &m_descriptor;
    }

    void disconnect(char_data* pc)
    {
        pc->desc = nullptr;
    }

    /* Practices kick one session at a time until the guildmaster refuses. */
    void practice_one_at_a_time(char_data* pc, int most)
    {
        for (int i = 0; i < most; ++i) {
            int left = pc->specials2.spells_to_learn;
            practice(pc, "kick");
            if (pc->specials2.spells_to_learn == left)
                break;
        }
    }

    char_data m_host, m_first, m_second;
    descriptor_data m_descriptor {};
    descriptor_data m_watcher_descriptor {};
    char_prof_data m_first_profs, m_second_profs;
    byte m_first_skills[MAX_SKILLS], m_first_knowledge[MAX_SKILLS];
    byte m_second_skills[MAX_SKILLS], m_second_knowledge[MAX_SKILLS];
    room_data m_saved_room;
    zone_data m_zone_table[1] {};
    zone_data* m_saved_zone_table = nullptr;
    skill_data m_saved_kick;
    byte m_saved_limit;
    char m_host_name[16] = "guildmaster";
    char m_player_name[16] = "Pupil";
};

TEST_F(GuildPracticeTest, AllStopsAtOneHundredPercent)
{
    practice_one_at_a_time(&m_first, kSessions);
    ASSERT_EQ(m_first.knowledge[SKILL_KICK], 100);
    ASSERT_LT(m_first.skills[SKILL_KICK], 51) << "the test skill must reach 100% early";

    practice(&m_second, "kick all");
    EXPECT_EQ(m_second.knowledge[SKILL_KICK], 100);
    EXPECT_EQ(m_second.skills[SKILL_KICK], m_first.skills[SKILL_KICK]);
    EXPECT_EQ(m_second.specials2.spells_to_learn, m_first.specials2.spells_to_learn);
}

TEST_F(GuildPracticeTest, ANumberStopsAtTheTeachersLimit)
{
    /* Kick is at 98% after 15 sessions; this teacher goes no further. */
    guildmasters[0].knowledge[SKILL_KICK] = 98;
    practice_one_at_a_time(&m_first, kSessions);
    ASSERT_EQ(m_first.knowledge[SKILL_KICK], 98);
    int needed = m_first.skills[SKILL_KICK];

    practice(&m_second, "kick 30");
    EXPECT_EQ(m_second.skills[SKILL_KICK], needed);
    EXPECT_EQ(m_second.knowledge[SKILL_KICK], 98);
    EXPECT_EQ(m_second.specials2.spells_to_learn, kSessions - needed);
}

TEST_F(GuildPracticeTest, ANumberSpendsExactlyThatMany)
{
    practice(&m_first, "kick 3");
    EXPECT_EQ(m_first.skills[SKILL_KICK], 3);
    EXPECT_EQ(m_first.specials2.spells_to_learn, kSessions - 3);

    practice(&m_second, "3  kick");
    EXPECT_EQ(m_second.skills[SKILL_KICK], 3) << "a double space is not \"all\"";
    EXPECT_EQ(m_second.specials2.spells_to_learn, kSessions - 3);
}

TEST_F(GuildPracticeTest, ACountWithNoSkillSpendsNothing)
{
    for (const char* text : { "all", "5", "a" }) {
        m_descriptor = descriptor_data();
        connect(&m_first);
        practice(&m_first, text);
        EXPECT_NE(std::string(m_descriptor.output).find("Practice which skill?"), std::string::npos)
            << text << ": " << m_descriptor.output;
        EXPECT_EQ(m_first.specials2.spells_to_learn, kSessions) << text;
    }
    disconnect(&m_first);
}

TEST_F(GuildPracticeTest, ACountOfZeroSpendsNothing)
{
    connect(&m_first);
    practice(&m_first, "kick 0");
    EXPECT_NE(std::string(m_descriptor.output).find("Practice how many times?"), std::string::npos)
        << m_descriptor.output;
    EXPECT_EQ(m_first.skills[SKILL_KICK], 0);
    EXPECT_EQ(m_first.specials2.spells_to_learn, kSessions);
    disconnect(&m_first);
}

TEST_F(GuildPracticeTest, AHugeCountStopsWhereAllWould)
{
    practice(&m_first, "kick all");
    practice(&m_second, "kick 99999999999");
    EXPECT_EQ(m_second.knowledge[SKILL_KICK], 100);
    EXPECT_EQ(m_second.skills[SKILL_KICK], m_first.skills[SKILL_KICK]);
    EXPECT_EQ(m_second.specials2.spells_to_learn, m_first.specials2.spells_to_learn);
}

TEST_F(GuildPracticeTest, AShortFormOfAllStopsWhereAllWould)
{
    practice(&m_first, "kick all");
    practice(&m_second, "kick a");
    EXPECT_EQ(m_second.skills[SKILL_KICK], m_first.skills[SKILL_KICK]);
    EXPECT_EQ(m_second.specials2.spells_to_learn, m_first.specials2.spells_to_learn);
}

TEST_F(GuildPracticeTest, AnUnknownSkillIsReportedBeforeACountOfZero)
{
    connect(&m_first);
    practice(&m_first, "zzzz 0");
    EXPECT_NE(std::string(m_descriptor.output).find("There is no such skill as you seek."), std::string::npos)
        << m_descriptor.output;
    EXPECT_EQ(m_first.specials2.spells_to_learn, kSessions);
    disconnect(&m_first);
}

TEST_F(GuildPracticeTest, AnAccentedWordIsNotACount)
{
    connect(&m_first);
    practice(&m_first, "kick\xe9 3");
    EXPECT_NE(std::string(m_descriptor.output).find("There is no such skill as you seek."), std::string::npos)
        << m_descriptor.output;
    EXPECT_EQ(m_first.specials2.spells_to_learn, kSessions);
    disconnect(&m_first);
}

TEST_F(GuildPracticeTest, ASessionPastTheTeachersLimitIsCutBackAndStops)
{
    /* Kick goes from 94% to 96% on its 14th session; this teacher stops at 95. */
    guildmasters[0].knowledge[SKILL_KICK] = 95;
    practice_one_at_a_time(&m_first, kSessions);
    ASSERT_EQ(m_first.knowledge[SKILL_KICK], 95);

    practice(&m_second, "kick 30");
    EXPECT_EQ(m_second.knowledge[SKILL_KICK], 95);
    EXPECT_EQ(m_second.skills[SKILL_KICK], m_first.skills[SKILL_KICK]);
    EXPECT_EQ(m_second.specials2.spells_to_learn, m_first.specials2.spells_to_learn);
}

TEST_F(GuildPracticeTest, AllStopsAtFiftyOneSessions)
{
    /* So hard that 51 sessions do not reach 100%. */
    skills[SKILL_KICK].learn_diff = 200;
    practice_one_at_a_time(&m_first, kSessions);
    ASSERT_EQ(m_first.skills[SKILL_KICK], 51);
    ASSERT_LT(m_first.knowledge[SKILL_KICK], 100);

    practice(&m_second, "kick all");
    EXPECT_EQ(m_second.skills[SKILL_KICK], 51);
    EXPECT_EQ(m_second.knowledge[SKILL_KICK], m_first.knowledge[SKILL_KICK]);
    EXPECT_EQ(m_second.specials2.spells_to_learn, kSessions - 51);
}

TEST_F(GuildPracticeTest, AllStopsWhenTheSessionsRunOut)
{
    m_first.specials2.spells_to_learn = 5;
    connect(&m_first);
    practice(&m_first, "kick all");
    EXPECT_EQ(m_first.skills[SKILL_KICK], 5);
    EXPECT_EQ(m_first.specials2.spells_to_learn, 0);
    EXPECT_NE(std::string(m_descriptor.output).find("You have no practice sessions left now."), std::string::npos)
        << m_descriptor.output;
    disconnect(&m_first);
}

TEST_F(GuildPracticeTest, SkillNamesIgnoreCase)
{
    practice(&m_first, "KICK 3");
    EXPECT_EQ(m_first.skills[SKILL_KICK], 3);
    EXPECT_EQ(m_first.specials2.spells_to_learn, kSessions - 3);
}

TEST_F(GuildPracticeTest, ManySessionsAreShownOnce)
{
    connect(&m_first);
    practice(&m_first, "kick 5");
    EXPECT_EQ(m_first.skills[SKILL_KICK], 5);
    std::string out(m_descriptor.output);
    size_t first = out.find("You practice it for a while.");
    ASSERT_NE(first, std::string::npos) << out;
    EXPECT_EQ(out.find("You practice it for a while.", first + 1), std::string::npos) << out;
    disconnect(&m_first);
}

TEST_F(GuildPracticeTest, ManySessionsEndWithTheTotal)
{
    connect(&m_first);
    practice(&m_first, "kick 5");
    EXPECT_NE(std::string(m_descriptor.output).find("You spend 5 sessions practicing kick."), std::string::npos)
        << m_descriptor.output;

    m_descriptor = descriptor_data();
    connect(&m_first);
    practice(&m_first, "kick");
    EXPECT_EQ(std::string(m_descriptor.output).find("You spend"), std::string::npos) << m_descriptor.output;
    disconnect(&m_first);
}

TEST_F(GuildPracticeTest, TheTotalComesBeforeTheReasonForStopping)
{
    connect(&m_first);
    practice(&m_first, "kick all");
    std::string out(m_descriptor.output);
    size_t total = out.find("You spend 16 sessions practicing kick.");
    size_t perfect = out.find("You're already perfect in this.");
    ASSERT_NE(total, std::string::npos) << out;
    ASSERT_NE(perfect, std::string::npos) << out;
    EXPECT_LT(total, perfect) << out;
    disconnect(&m_first);
}

TEST_F(GuildPracticeTest, ACutBackPartWayShowsTheTotalThenTheLimit)
{
    /* Kick goes from 94% to 96% on its 14th session; this teacher stops at 95. */
    guildmasters[0].knowledge[SKILL_KICK] = 95;
    connect(&m_first);
    practice(&m_first, "kick 30");
    std::string out(m_descriptor.output);
    size_t practiced = out.find("You practice it for a while.");
    size_t total = out.find("You spend 14 sessions practicing kick.");
    size_t limit = out.find("Sorry, I can not teach you better.");
    ASSERT_NE(practiced, std::string::npos) << out;
    ASSERT_NE(total, std::string::npos) << out;
    ASSERT_NE(limit, std::string::npos) << out;
    EXPECT_LT(practiced, total) << out;
    EXPECT_LT(total, limit) << out;
    disconnect(&m_first);
}

TEST_F(GuildPracticeTest, TheRoomSeesManySessionsOnce)
{
    m_watcher_descriptor.output = m_watcher_descriptor.small_outbuf;
    m_watcher_descriptor.bufspace = SMALL_BUFSIZE - 1;
    m_watcher_descriptor.connected = CON_PLYNG;
    m_watcher_descriptor.descriptor = 2;
    m_watcher_descriptor.character = &m_second;
    m_second.desc = &m_watcher_descriptor;

    practice(&m_first, "kick 5");
    std::string seen(m_watcher_descriptor.output);
    size_t first = seen.find("teaches");
    ASSERT_NE(first, std::string::npos) << seen;
    EXPECT_EQ(seen.find("teaches", first + 1), std::string::npos) << seen;
    m_second.desc = nullptr;
}

TEST_F(GuildPracticeTest, StopMessagesHaveNoBlankLineAfterThem)
{
    m_first.specials2.spells_to_learn = 2;
    connect(&m_first);
    practice(&m_first, "kick all");
    std::string out(m_descriptor.output);
    EXPECT_NE(out.find("You have no practice sessions left now.\n\r"), std::string::npos) << out;
    EXPECT_EQ(out.find("You have no practice sessions left now.\n\r\n\r"), std::string::npos) << out;
    disconnect(&m_first);
}

} // namespace
