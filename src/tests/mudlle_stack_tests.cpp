/* The value stack, the target list and the subprogram call of a mob running a mudlle program:
 * none reaches outside the memory the mob was given for them. */
#include "../comm.h"
#include "../mudlle.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

void TO_STACK(char_data* host, long x);
long STACK_VALUE(const char_data* host);
long FROM_STACK(char_data* host);
void DOWN_STACK(const char_data* host);
void UP_STACK(const char_data* host);
void TO_LIST(const char_data* host, void* entr, int type);
void REMOVE_LIST(char_data* host);
void UP_LIST(char_data* host);

extern char** mobile_program;
extern int num_of_programs;
extern txt_block* txt_block_pool;

namespace {

/* How many values a test pushes onto a full stack. */
constexpr int VALUES_PUSHED = SPECIAL_STACKLEN + 10;
/* How many entries a test adds to a list that fills up. */
constexpr int ENTRIES_ADDED = SPECIAL_STACKLEN + 10;
/* Every program number a mob's call list can hold one of. */
constexpr int ALL_CALLFLAGS = 255;

class MudlleStackTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_saved_programs = mobile_program;
        m_saved_program_count = num_of_programs;
        give_program_memory(m_host, m_host_memory);
        m_host.specials.union1.prog_number[0] = 1;
    }

    void TearDown() override
    {
        mobile_program = m_saved_programs;
        num_of_programs = m_saved_program_count;
    }

    /* What read_mobile gives a mob that runs a program: a value stack, a target list and a call
     * list, here held by <memory>, which must outlive <mob>'s use of them. */
    struct program_memory {
        // The value stack, exactly as long as read_mobile makes it.
        std::vector<long> stack = std::vector<long>(SPECIAL_STACKLEN);
        // The target list.
        special_list targets {};
        // The program number and position at each depth of the call list.
        std::vector<int> program_numbers = std::vector<int>(SPECIAL_CALLLIST);
        std::vector<int> program_points = std::vector<int>(SPECIAL_CALLLIST);
    };

    static void give_program_memory(char_data& mob, program_memory& memory)
    {
        mob.specials.poofIn = reinterpret_cast<char*>(memory.stack.data());
        mob.specials.poofOut = reinterpret_cast<char*>(&memory.targets);
        mob.specials.union1.prog_number = memory.program_numbers.data();
        mob.specials.union2.prog_point = memory.program_points.data();
        mob.specials.tactics = 0;
        mob.specials.invis_level = 0;
        CALL_MASK(&mob) = ALL_CALLFLAGS;
        for (int entry = 0; entry < SPECIAL_STACKLEN; ++entry) {
            memory.targets.next[entry] = -1;
            memory.targets.field[entry].type = SPECIAL_VOID;
        }
        memory.targets.head = 0;
        memory.targets.field[0].type = SPECIAL_MARK;
    }

    /* Makes the loaded programs <texts>, numbered from 1 as the loader numbers them; program 0,
     * which the loader leaves empty, is given an empty program so that running it is harmless. */
    void load_programs(const std::vector<const char*>& texts)
    {
        m_program_texts.assign(1, std::string(","));
        for (const char* text : texts) {
            m_program_texts.emplace_back(text);
        }
        m_programs.clear();
        for (std::string& text : m_program_texts) {
            m_programs.push_back(&text[0]);
        }
        mobile_program = m_programs.data();
        num_of_programs = static_cast<int>(texts.size());
    }

    /* The slots of <memory>'s target list from its head to its tail, stopping at a slot seen
     * before (a loop) or after more steps than the list has slots. */
    static std::vector<int> list_chain(const program_memory& memory)
    {
        std::vector<int> chain;
        std::vector<bool> seen(SPECIAL_STACKLEN, false);
        int slot = memory.targets.head;
        while (slot >= 0 && slot < SPECIAL_STACKLEN && !seen[slot]) {
            seen[slot] = true;
            chain.push_back(slot);
            slot = memory.targets.next[slot];
        }
        if (slot >= 0) {
            chain.push_back(slot); /* the slot that closed a loop, or one out of range */
        }
        return chain;
    }

    // The mob running the program, and what it runs with.
    char_data m_host {};
    program_memory m_host_memory;

private:
    // The game's programs before the test, put back by TearDown.
    char** m_saved_programs = nullptr;
    int m_saved_program_count = 0;
    // The test programs' texts and the table naming them.
    std::vector<std::string> m_program_texts;
    std::vector<char*> m_programs;
};

} // namespace

TEST_F(MudlleStackTest, AFullStackDropsItsOldestValues)
{
    for (int value = 0; value < VALUES_PUSHED; ++value) {
        TO_STACK(&m_host, value);
    }

    EXPECT_EQ(SPECIAL_STACKLEN, SPECIAL_STACKPOINT(&m_host));
    EXPECT_EQ(VALUES_PUSHED - SPECIAL_STACKLEN, m_host_memory.stack.front());
    EXPECT_EQ(VALUES_PUSHED - 1, m_host_memory.stack.back());
    EXPECT_EQ(VALUES_PUSHED - 1, STACK_VALUE(&m_host));
}

TEST_F(MudlleStackTest, ValuesComeOffTheStackLastFirst)
{
    TO_STACK(&m_host, 4);
    TO_STACK(&m_host, 9);

    EXPECT_EQ(9, FROM_STACK(&m_host));
    EXPECT_EQ(4, FROM_STACK(&m_host));
    EXPECT_EQ(0, FROM_STACK(&m_host)) << "an empty stack gives 0";
}

TEST_F(MudlleStackTest, AnEmptyStacksTopValueIsZero)
{
    EXPECT_EQ(0, STACK_VALUE(&m_host));
}

TEST_F(MudlleStackTest, TurningTheStackDownMovesTheBottomValueToTheTop)
{
    TO_STACK(&m_host, 1);
    TO_STACK(&m_host, 2);
    TO_STACK(&m_host, 3);

    DOWN_STACK(&m_host);

    EXPECT_EQ(1, FROM_STACK(&m_host));
    EXPECT_EQ(3, FROM_STACK(&m_host));
    EXPECT_EQ(2, FROM_STACK(&m_host));
}

TEST_F(MudlleStackTest, TurningTheStackUpMovesTheTopValueToTheBottom)
{
    TO_STACK(&m_host, 1);
    TO_STACK(&m_host, 2);
    TO_STACK(&m_host, 3);

    UP_STACK(&m_host);

    EXPECT_EQ(2, FROM_STACK(&m_host));
    EXPECT_EQ(1, FROM_STACK(&m_host));
    EXPECT_EQ(3, FROM_STACK(&m_host));
}

TEST_F(MudlleStackTest, TurningAnEmptyStackDownChangesNothing)
{
    DOWN_STACK(&m_host);

    EXPECT_EQ(0, SPECIAL_STACKPOINT(&m_host));
}

/* Program 1 calls program 2, which leaves 7 on the stack. The one whose action set the program
 * off has a call list of its own, which is not touched. */
TEST_F(MudlleStackTest, ACallRunsTheSubprogramOnTheHostsOwnCallList)
{
    load_programs({ "2K", "7;", ";" });
    char_data caller {};
    program_memory caller_memory;
    give_program_memory(caller, caller_memory);
    char no_argument[] = "";

    EXPECT_TRUE(intelligent(&m_host, &caller, 0, no_argument, SPECIAL_DELAY, nullptr));

    EXPECT_EQ(1, m_host.specials.tactics);
    EXPECT_EQ(2, m_host_memory.program_numbers[1]);
    EXPECT_EQ(7, STACK_VALUE(&m_host));
    EXPECT_EQ(0, caller_memory.program_numbers[0]) << "the caller's call list is not written";
}

/* Programs are numbered from 1: a call of program 0 is refused, as one past the last is. */
TEST_F(MudlleStackTest, ACallOfProgramZeroIsRefused)
{
    load_programs({ "0K;", ";" });
    char no_argument[] = "";

    intelligent(&m_host, &m_host, 0, no_argument, SPECIAL_DELAY, nullptr);

    EXPECT_EQ(0, m_host.specials.tactics);
}

using MudlleListTest = MudlleStackTest;

/* A full list takes no more entries, and stays one chain ending at its mark. */
TEST_F(MudlleListTest, AFullListTakesNoMoreEntriesAndKeepsItsMark)
{
    for (int entry = 0; entry < ENTRIES_ADDED; ++entry) {
        TO_LIST(&m_host, nullptr, TARGET_NONE);
    }

    const std::vector<int> chain = list_chain(m_host_memory);
    ASSERT_EQ(static_cast<size_t>(SPECIAL_STACKLEN), chain.size());
    EXPECT_EQ(SPECIAL_MARK, m_host_memory.targets.field[chain.back()].type);
    EXPECT_EQ(-1, m_host_memory.targets.next[chain.back()]);
}

/* A text a full list refuses goes back to the pool rather than being lost. */
TEST_F(MudlleListTest, AFullListGivesARefusedTextBackToThePool)
{
    for (int entry = 1; entry < SPECIAL_STACKLEN; ++entry) {
        TO_LIST(&m_host, nullptr, TARGET_NONE);
    }
    txt_block* refused = get_from_txt_block_pool("refused");

    TO_LIST(&m_host, refused, TARGET_TEXT);

    EXPECT_EQ(refused, txt_block_pool);
    for (const target_data& field : m_host_memory.targets.field) {
        EXPECT_NE(TARGET_TEXT, field.type);
    }
}

/* Emptying a list that filled up leaves its mark, the one entry a removal never takes. */
TEST_F(MudlleListTest, EmptyingAFullListLeavesItsMark)
{
    for (int entry = 0; entry < ENTRIES_ADDED; ++entry) {
        TO_LIST(&m_host, nullptr, TARGET_NONE);
    }

    for (int entry = 1; entry < SPECIAL_STACKLEN; ++entry) {
        REMOVE_LIST(&m_host);
    }

    const std::vector<int> chain = list_chain(m_host_memory);
    ASSERT_EQ(1u, chain.size());
    EXPECT_EQ(SPECIAL_MARK, m_host_memory.targets.field[chain.front()].type);
}

/* Turning the list up makes the second entry the head and moves the head to the tail. */
TEST_F(MudlleListTest, TurningTheListUpMovesTheHeadToTheTail)
{
    room_data first_room {};
    room_data second_room {};
    TO_LIST(&m_host, &first_room, TARGET_ROOM);
    TO_LIST(&m_host, &second_room, TARGET_ROOM);

    UP_LIST(&m_host);

    const std::vector<int> chain = list_chain(m_host_memory);
    ASSERT_EQ(3u, chain.size());
    EXPECT_EQ(&first_room, m_host_memory.targets.field[chain[0]].ptr.room);
    EXPECT_EQ(SPECIAL_MARK, m_host_memory.targets.field[chain[1]].type);
    EXPECT_EQ(&second_room, m_host_memory.targets.field[chain[2]].ptr.room);
    EXPECT_EQ(-1, m_host_memory.targets.next[chain[2]]);
}
