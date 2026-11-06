/* The value stack, the target list and the subprogram call of a mob running a mudlle program:
 * none reaches outside the memory the mob was given for them. */
#include "../comm.h"
#include "../mudlle.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

void TO_STACK(char_data* host, long value);
long STACK_VALUE(const char_data* host);
long FROM_STACK(char_data* host);
void DOWN_STACK(const char_data* host);
void UP_STACK(const char_data* host);
void TO_LIST(const char_data* host, void* entry, int type);
void REMOVE_LIST(char_data* host);
void UP_LIST(char_data* host);
void DOWN_LIST(char_data* host);

extern char** mobile_program;
extern int num_of_programs;
extern txt_block* txt_block_pool;

namespace {

/* How many values a test pushes onto a full stack. */
constexpr int VALUES_PUSHED = SPECIAL_STACKLEN + 10;
/* How many entries a test adds to a list that fills up. */
constexpr int ENTRIES_ADDED = SPECIAL_STACKLEN + 10;
/* How many values the huge stack turn's tests stack up. */
constexpr int STACKED_VALUES = 5;
/* How many rooms the huge list turn's tests list, above the mark. */
constexpr int ROOMS_LISTED = 5;
/* The length of such a list: the rooms and the mark. */
constexpr int LIST_LENGTH = ROOMS_LISTED + 1;
/* The next slot of a target list's tail. */
constexpr int NO_NEXT_SLOT = -1;
/* A turn count that would take minutes turned one place at a time: 3 more than a multiple of
 * 30, and so of both test depths (STACKED_VALUES and LIST_LENGTH). */
constexpr int HUGE_TURNS = 2'000'000'013;
/* What HUGE_TURNS comes to on those depths. */
constexpr int EQUIVALENT_TURNS = 3;
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
        // The program number at each depth of the call list.
        std::vector<int> program_numbers = std::vector<int>(SPECIAL_CALLLIST);
        // The position in its program at each depth of the call list.
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
            memory.targets.next[entry] = NO_NEXT_SLOT;
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

    // The mob running the program.
    char_data m_host {};
    // The stack and lists m_host runs with.
    program_memory m_host_memory;

private:
    // The game's programs before the test, put back by TearDown.
    char** m_saved_programs = nullptr;
    // The number of programs before the test, put back by TearDown.
    int m_saved_program_count = 0;
    // The test programs' texts.
    std::vector<std::string> m_program_texts;
    // The program table naming m_program_texts, which mobile_program points at during the test.
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
    EXPECT_EQ(NO_NEXT_SLOT, m_host_memory.targets.next[chain.back()]);
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
    EXPECT_EQ(NO_NEXT_SLOT, m_host_memory.targets.next[chain[2]]);
}

/* The values on <host>'s stack, held in <stack>, top first. */
static std::vector<long> stack_values(const char_data& host, const std::vector<long>& stack)
{
    return std::vector<long>(stack.rbegin() + (SPECIAL_STACKLEN - SPECIAL_STACKPOINT(&host)), stack.rend());
}

TEST_F(MudlleStackTest, AHugeTurnOfTheStackEndsAsItsEquivalentSmallTurnDoes)
{
    char_data reference {};
    program_memory reference_memory;
    give_program_memory(reference, reference_memory);
    for (int value = 1; value <= STACKED_VALUES; ++value) {
        TO_STACK(&m_host, value);
        TO_STACK(&reference, value);
    }

    turn_stack(&m_host, HUGE_TURNS, UP_STACK);
    for (int turn = 0; turn < EQUIVALENT_TURNS; ++turn) {
        UP_STACK(&reference);
    }

    EXPECT_EQ(stack_values(reference, reference_memory.stack), stack_values(m_host, m_host_memory.stack));
}

/* A program turning its stack down by a huge count finishes, with the order a small count gives:
 * the count 2000000004 is 2000000003 turns, 2 on three values, and 7 8 9 turned down twice reads
 * 8 7 9 from the top. */
TEST_F(MudlleStackTest, AProgramsHugeStackTurnFinishes)
{
    load_programs({ "7 8 9 2000000004Sb;" });
    char no_argument[] = "";

    intelligent(&m_host, &m_host, 0, no_argument, SPECIAL_DELAY, nullptr);

    const std::vector<long> expected = { 8, 7, 9 };
    EXPECT_EQ(expected, stack_values(m_host, m_host_memory.stack));
}

TEST_F(MudlleStackTest, ANonPositiveStackTurnChangesNothing)
{
    TO_STACK(&m_host, 1);
    TO_STACK(&m_host, 2);

    turn_stack(&m_host, 0, UP_STACK);
    turn_stack(&m_host, -5, DOWN_STACK);

    const std::vector<long> expected = { 2, 1 };
    EXPECT_EQ(expected, stack_values(m_host, m_host_memory.stack));
}

/* Gives <host> an entry for each of <rooms> above its mark, the last at the head. */
static void fill_with_rooms(char_data& host, std::vector<room_data>& rooms)
{
    for (room_data& room : rooms) {
        TO_LIST(&host, &room, TARGET_ROOM);
    }
}

TEST_F(MudlleListTest, AHugeTurnOfTheListEndsAsItsEquivalentSmallTurnDoes)
{
    for (void (*turn_once)(char_data*) : { UP_LIST, DOWN_LIST }) {
        char_data turned {};
        program_memory turned_memory;
        give_program_memory(turned, turned_memory);
        char_data reference {};
        program_memory reference_memory;
        give_program_memory(reference, reference_memory);
        std::vector<room_data> rooms(ROOMS_LISTED);
        fill_with_rooms(turned, rooms);
        fill_with_rooms(reference, rooms);

        turn_list(&turned, HUGE_TURNS, turn_once);
        for (int turn = 0; turn < EQUIVALENT_TURNS; ++turn) {
            turn_once(&reference);
        }

        EXPECT_EQ(list_chain(reference_memory), list_chain(turned_memory));
    }
}

/* A program turning its list up by a huge count finishes: the count 2000000003 is 2000000002
 * turns, as many as 2000000002 % LIST_LENGTH. */
TEST_F(MudlleListTest, AProgramsHugeListTurnFinishes)
{
    constexpr int PROGRAM_TURNS = 2'000'000'002; /* the program's count less one */
    load_programs({ "2000000003SF;" });
    std::vector<room_data> rooms(ROOMS_LISTED);
    fill_with_rooms(m_host, rooms);
    char_data reference {};
    program_memory reference_memory;
    give_program_memory(reference, reference_memory);
    fill_with_rooms(reference, rooms);
    for (int turn = 0; turn < PROGRAM_TURNS % LIST_LENGTH; ++turn) {
        UP_LIST(&reference);
    }
    char no_argument[] = "";

    intelligent(&m_host, &m_host, 0, no_argument, SPECIAL_DELAY, nullptr);

    EXPECT_EQ(list_chain(reference_memory), list_chain(m_host_memory));
}

/* A whole number of turns brings every entry to the head once, which clears a character no longer
 * in the game; the shortened turn clears it as well, and leaves the order as it was. */
TEST_F(MudlleListTest, AWholeTurnOfTheListClearsACharacterNoLongerInTheGame)
{
    room_data room {};
    char_data departed {};
    departed.abs_number = -1; /* in no character slot */
    TO_LIST(&m_host, &room, TARGET_ROOM);
    TO_LIST(&m_host, &departed, TARGET_CHAR);
    const std::vector<int> before = list_chain(m_host_memory);

    turn_list(&m_host, static_cast<int>(before.size()), UP_LIST);

    EXPECT_EQ(before, list_chain(m_host_memory));
    EXPECT_EQ(TARGET_NONE, m_host_memory.targets.field[before[0]].type);
    EXPECT_EQ(nullptr, m_host_memory.targets.field[before[0]].ptr.other);
}
