// Expose elements: the mark a specialised mage puts on one target, the sweep that lifts it, and
// what freeing a character does to the list of specialised mages and to the marks naming it.

#include "../char_utils.h"
#include "../db.h"
#include "../spells.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

extern struct room_data world;
extern std::vector<char_data*> specialized_mages;
void clean_expose_elements();
void clear_char(struct char_data* ch, int mode);

namespace {

constexpr int EXPOSE_ROOM = 970;
constexpr char LOST_MARK_MESSAGE[] = "Your target is no longer vulnerable to your spells.\r\n";
constexpr char BATTLE_MAGE_DETAILS[] = "You are specialized in battle mage.\n\r";

size_t count_occurrences(std::string_view text, std::string_view needle)
{
    size_t count = 0;
    for (size_t found_at = text.find(needle); found_at != std::string_view::npos;
         found_at = text.find(needle, found_at + needle.size())) {
        ++count;
    }
    return count;
}

// A human mage and two mobs standing in EXPOSE_ROOM, all allocated the way the game allocates
// characters so a test can free them with free_char. The mage's output is readable.
class ExposeElementsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_saved_mages = specialized_mages;
        specialized_mages.clear();
        m_saved_people = world[EXPOSE_ROOM].people;
        m_saved_light = world[EXPOSE_ROOM].light;
        world[EXPOSE_ROOM].people = nullptr;
        world[EXPOSE_ROOM].light = 1;

        m_descriptor.output = m_descriptor.small_outbuf;
        m_descriptor.connected = CON_PLYNG;
        clear_output();

        CREATE1(m_mage, char_data);
        clear_char(m_mage, MOB_VOID);
        m_mage->player.name = strdup("Mage");
        m_mage->player.race = RACE_HUMAN;
        m_mage->player.level = 30;
        m_mage->desc = &m_descriptor;
        m_descriptor.character = m_mage;

        m_target = make_mob("a grey wolf");
        m_other_target = make_mob("a black bear");

        stand_in_room(m_mage);
        stand_in_room(m_target);
        stand_in_room(m_other_target);
    }

    void TearDown() override
    {
        for (char_data** character : { &m_mage, &m_target, &m_other_target }) {
            if (*character != nullptr) {
                leave_room(*character);
                free_char(*character);
                *character = nullptr;
            }
        }
        world[EXPOSE_ROOM].people = m_saved_people;
        world[EXPOSE_ROOM].light = m_saved_light;
        specialized_mages = m_saved_mages;
    }

    static char_data* make_mob(const char* short_description)
    {
        char_data* mob = nullptr;
        CREATE1(mob, char_data);
        clear_char(mob, MOB_ISNPC);
        mob->nr = -1; // a mob of no prototype: free_char releases its own text
        mob->specials2.act = MOB_ISNPC;
        mob->player.name = strdup(short_description);
        mob->player.short_descr = strdup(short_description);
        mob->player.level = 10;
        return mob;
    }

    static void stand_in_room(char_data* character)
    {
        character->in_room = EXPOSE_ROOM;
        character->next_in_room = world[EXPOSE_ROOM].people;
        world[EXPOSE_ROOM].people = character;
    }

    // Takes <character> off the room's people, as extract_char does before it frees a character.
    static void leave_room(char_data* character)
    {
        char_data** link = &world[EXPOSE_ROOM].people;
        while (*link != nullptr && *link != character) {
            link = &(*link)->next_in_room;
        }
        if (*link == character) {
            *link = character->next_in_room;
        }
        character->next_in_room = nullptr;
        character->in_room = NOWHERE;
    }

    // Leaves the room and frees <character>, which the fixture then no longer frees.
    static void extract_and_free(char_data*& character)
    {
        leave_room(character);
        free_char(character);
        character = nullptr;
    }

    void clear_output()
    {
        m_descriptor.small_outbuf[0] = '\0';
        m_descriptor.bufptr = 0;
        m_descriptor.bufspace = SMALL_BUFSIZE - 1;
    }

    std::string output() const { return m_descriptor.output; }

    void expose(char_data* target)
    {
        spell_expose_elements(m_mage, nullptr, 0, target, nullptr, 0, 0);
    }

    elemental_spec_data* mage_spec() const
    {
        return m_mage->extra_specialization_data.get_mage_spec();
    }

    // The mage, a PC; freed by TearDown unless a test frees it first.
    char_data* m_mage = nullptr;
    // A mob the mage exposes; freed by TearDown unless a test frees it first.
    char_data* m_target = nullptr;
    // A second mob in the room, for a mark made after the first target is gone.
    char_data* m_other_target = nullptr;
    // The mage's connection, whose output buffer the tests read.
    descriptor_data m_descriptor {};
    // The list of specialised mages from before the test, put back afterwards.
    std::vector<char_data*> m_saved_mages;
    // EXPOSE_ROOM's people from before the test, put back afterwards.
    char_data* m_saved_people = nullptr;
    // EXPOSE_ROOM's light from before the test, put back afterwards.
    int m_saved_light = 0;
};

TEST_F(ExposeElementsTest, FreeingASpecialisedMageTakesItOffTheMageList)
{
    utils::set_specialization(*m_mage, game_types::PS_Fire);
    ASSERT_EQ(specialized_mages.size(), 1u);

    const char_data* freed_mage = m_mage;
    extract_and_free(m_mage);

    EXPECT_EQ(std::find(specialized_mages.begin(), specialized_mages.end(), freed_mage),
        specialized_mages.end());
    // The sweep reads every listed mage; a freed one would be read after it was freed.
    clean_expose_elements();
}

TEST_F(ExposeElementsTest, ABattleMageMarksItsTargetForFirebolt)
{
    utils::set_specialization(*m_mage, game_types::PS_BattleMage);

    expose(m_target);

    elemental_spec_data* spec_data = mage_spec();
    ASSERT_NE(spec_data, nullptr);
    EXPECT_EQ(spec_data->exposed_target, m_target);
    EXPECT_EQ(spec_data->spell_id, SPELL_FIREBOLT);
    EXPECT_EQ(output(), "Your target is now exposed to 'Firebolt'.\r\n");
}

TEST_F(ExposeElementsTest, ABattleMagesDetailsDoNotNameItsMark)
{
    utils::set_specialization(*m_mage, game_types::PS_BattleMage);
    expose(m_target);

    EXPECT_EQ(m_mage->extra_specialization_data.to_string(*m_mage), BATTLE_MAGE_DETAILS);
}

TEST_F(ExposeElementsTest, AFireMagesDetailsNameItsMarkAndTheSpell)
{
    utils::set_specialization(*m_mage, game_types::PS_Fire);

    expose(m_target);

    ASSERT_NE(mage_spec(), nullptr);
    EXPECT_EQ(mage_spec()->spell_id, SPELL_FIREBOLT);
    const std::string details = m_mage->extra_specialization_data.to_string(*m_mage);
    EXPECT_NE(details.find("a grey wolf is exposed to the spell [firebolt]."), std::string::npos)
        << details;
}

TEST_F(ExposeElementsTest, TheSweepKeepsAMarkWhileTheTargetStaysInTheRoom)
{
    utils::set_specialization(*m_mage, game_types::PS_BattleMage);
    expose(m_target);
    clear_output();

    clean_expose_elements();

    EXPECT_EQ(mage_spec()->exposed_target, m_target);
    EXPECT_EQ(output(), "");
}

TEST_F(ExposeElementsTest, TheSweepLiftsAMarkWhenTheTargetLeavesTheRoom)
{
    utils::set_specialization(*m_mage, game_types::PS_Cold);
    expose(m_target);
    clear_output();
    leave_room(m_target);

    clean_expose_elements();

    EXPECT_EQ(mage_spec()->exposed_target, nullptr);
    EXPECT_EQ(mage_spec()->spell_id, 0);
    EXPECT_EQ(output(), LOST_MARK_MESSAGE);
}

TEST_F(ExposeElementsTest, TheSweepPassesOverAMageInNoRoom)
{
    utils::set_specialization(*m_mage, game_types::PS_Cold);
    expose(m_target);
    clear_output();
    leave_room(m_mage); // a player gone to the menu

    clean_expose_elements();

    EXPECT_EQ(mage_spec()->exposed_target, m_target);
    EXPECT_EQ(output(), "");
}

TEST_F(ExposeElementsTest, FreeingTheTargetClearsTheMarkAtOnce)
{
    utils::set_specialization(*m_mage, game_types::PS_Fire);
    expose(m_target);

    extract_and_free(m_target);

    EXPECT_EQ(mage_spec()->exposed_target, nullptr);
    EXPECT_EQ(mage_spec()->spell_id, 0);
    // The details name a marked target, which here has been freed.
    const std::string details = m_mage->extra_specialization_data.to_string(*m_mage);
    EXPECT_EQ(details.find("is exposed to the spell"), std::string::npos) << details;
}

TEST_F(ExposeElementsTest, TheSweepAfterAFreedTargetTellsTheMageOnce)
{
    utils::set_specialization(*m_mage, game_types::PS_Fire);
    expose(m_target);
    extract_and_free(m_target);
    clear_output();

    clean_expose_elements();
    clean_expose_elements();

    EXPECT_EQ(count_occurrences(output(), LOST_MARK_MESSAGE), 1u) << output();
}

TEST_F(ExposeElementsTest, AMarkMadeBeforeTheSweepOutlastsAFreedTarget)
{
    utils::set_specialization(*m_mage, game_types::PS_Fire);
    expose(m_target);
    extract_and_free(m_target);
    expose(m_other_target);
    clear_output();

    clean_expose_elements();

    EXPECT_EQ(mage_spec()->exposed_target, m_other_target);
    EXPECT_EQ(output(), "");
}

TEST_F(ExposeElementsTest, FreeingAnotherMobLeavesTheMarkAlone)
{
    utils::set_specialization(*m_mage, game_types::PS_Fire);
    expose(m_target);

    extract_and_free(m_other_target);

    EXPECT_EQ(mage_spec()->exposed_target, m_target);
    EXPECT_EQ(mage_spec()->spell_id, SPELL_FIREBOLT);
}

} // namespace
