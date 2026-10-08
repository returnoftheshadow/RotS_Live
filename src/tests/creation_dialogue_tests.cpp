#include "db.h"
#include "interpre.h"
#include "structs.h"
#include "utils.h"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

// The pool write_to_output takes large output buffers from (comm.cpp).
extern struct txt_block* bufpool;

namespace {

// A connection in the creation dialogue holding a fresh character, for driving nanny() one line
// at a time. Its account name stays empty, so the dialogue never reads account files.
class CreationDialogueConnection {
public:
    explicit CreationDialogueConnection(int state)
    {
        descriptor.pos = -1;
        descriptor.connected = state;
        clear_output();
        CREATE(descriptor.character, struct char_data, 1);
        clear_char(descriptor.character, MOB_VOID);
        descriptor.character->desc = &descriptor;
    }

    ~CreationDialogueConnection()
    {
        release_large_output_buffer();
        descriptor.character->desc = nullptr;
        free_char(descriptor.character);
    }

    CreationDialogueConnection(const CreationDialogueConnection&) = delete;
    CreationDialogueConnection& operator=(const CreationDialogueConnection&) = delete;

    // Sends one line of player input, after discarding the output of the previous line.
    void send(const std::string& line)
    {
        clear_output();
        std::vector<char> input(line.begin(), line.end());
        input.push_back('\0');
        nanny(&descriptor, input.data());
    }

    // The text the last line produced.
    std::string output() const
    {
        return descriptor.output;
    }

    // The dialogue state the connection is in.
    int state() const
    {
        return descriptor.connected;
    }

    // The character's creation points in one profession.
    int points(int profession) const
    {
        return GET_PROF_POINTS(profession, descriptor.character);
    }

private:
    // Returns the large output buffer, if write_to_output switched to one, to the shared pool,
    // as flush_queues does when a connection closes.
    void release_large_output_buffer()
    {
        if (descriptor.large_outbuf) {
            descriptor.large_outbuf->next = bufpool;
            bufpool = descriptor.large_outbuf;
            descriptor.large_outbuf = nullptr;
        }
    }

    // Empties the output and returns the connection to its small buffer.
    void clear_output()
    {
        release_large_output_buffer();
        descriptor.output = descriptor.small_outbuf;
        descriptor.small_outbuf[0] = '\0';
        descriptor.bufptr = 0;
        descriptor.bufspace = SMALL_BUFSIZE - 1;
    }

    // The connection nanny() reads and writes; owns the character through the destructor.
    descriptor_data descriptor {};
};

// One standard class as the creation menu offers it: its letter and its points per profession,
// indexed like prof_coof.
struct PinnedStandardClass {
    // The creation-menu letter that picks the class.
    char letter;
    // The points it gives, slot 0 unused, then mage, mystic, ranger and warrior.
    std::array<int, 5> points;
};

// Today's standard classes, pinned by value.
const std::array<PinnedStandardClass, 10> pinned_standard_classes = { {
    { 'm', { 0, 100, 25, 16, 9 } },
    { 't', { 0, 25, 100, 9, 16 } },
    { 'r', { 0, 16, 9, 100, 25 } },
    { 'w', { 0, 9, 16, 25, 100 } },
    { 'n', { 0, 64, 64, 9, 13 } },
    { 'i', { 0, 121, 16, 9, 4 } },
    { 'h', { 0, 25, 121, 0, 4 } },
    { 's', { 0, 9, 13, 64, 64 } },
    { 'b', { 0, 0, 4, 25, 121 } },
    { 'a', { 0, 36, 36, 36, 42 } },
} };

} // namespace

TEST(CreationDialogue, EachStandardClassLetterSetsItsPointsAndReportsWhatRemains)
{
    for (const PinnedStandardClass& standard_class : pinned_standard_classes) {
        CreationDialogueConnection connection(CON_CREATE);
        connection.send(std::string(1, standard_class.letter));

        int total = 0;
        for (int profession = PROF_MAGE; profession <= PROF_WARRIOR; ++profession) {
            EXPECT_EQ(connection.points(profession), standard_class.points[profession]) << standard_class.letter;
            total += standard_class.points[profession];
        }
        EXPECT_EQ(connection.state(), CON_CREATE);
        const std::string remaining_line = "Points remaining: " + std::to_string(150 - total);
        EXPECT_NE(connection.output().find(remaining_line), std::string::npos) << standard_class.letter;
    }
}

TEST(CreationDialogue, StandardClassAtTheClassPromptMovesOnToTheColourPrompt)
{
    CreationDialogueConnection connection(CON_QPROF);
    connection.send("i");

    EXPECT_EQ(connection.points(PROF_MAGE), 121);
    EXPECT_EQ(connection.points(PROF_CLERIC), 16);
    EXPECT_EQ(connection.points(PROF_RANGER), 9);
    EXPECT_EQ(connection.points(PROF_WARRIOR), 4);
    EXPECT_EQ(connection.state(), CON_COLOR);
}

TEST(CreationDialogue, CustomClassStartsWithNoPoints)
{
    CreationDialogueConnection connection(CON_QPROF);
    connection.send("o");

    EXPECT_EQ(connection.state(), CON_CREATE);
    for (int profession = PROF_MAGE; profession <= PROF_WARRIOR; ++profession) {
        EXPECT_EQ(connection.points(profession), 0);
    }
}

TEST(CreationDialogue, AddsTypedPointsToTheChosenProfession)
{
    CreationDialogueConnection connection(CON_CREATE);
    connection.send("60");
    EXPECT_EQ(connection.state(), CON_CREATE2);
    connection.send("m");

    EXPECT_EQ(connection.points(PROF_MAGE), 60);
    EXPECT_EQ(connection.state(), CON_CREATE);
    EXPECT_NE(connection.output().find("Points remaining: 90"), std::string::npos);
}

TEST(CreationDialogue, HoldsOneProfessionAtTheStepCap)
{
    CreationDialogueConnection connection(CON_CREATE);
    connection.send("200");
    connection.send("w");

    EXPECT_EQ(connection.points(PROF_WARRIOR), 165);
}

TEST(CreationDialogue, NeverTakesAProfessionBelowZero)
{
    CreationDialogueConnection connection(CON_CREATE);
    connection.send("-30");
    connection.send("t");

    EXPECT_EQ(connection.points(PROF_CLERIC), 0);
}

TEST(CreationDialogue, RefusesToFinishOverTheBudget)
{
    CreationDialogueConnection connection(CON_CREATE);
    connection.send("100");
    connection.send("m");
    connection.send("51");
    connection.send("t");
    connection.send("=");

    EXPECT_EQ(connection.state(), CON_CREATE);
    EXPECT_NE(connection.output().find("You've allocated more than 150 creation points."), std::string::npos);
}

TEST(CreationDialogue, FinishesAtExactlyTheBudget)
{
    CreationDialogueConnection connection(CON_CREATE);
    connection.send("100");
    connection.send("m");
    connection.send("50");
    connection.send("t");
    connection.send("=");

    EXPECT_EQ(connection.state(), CON_COLOR);
}
