/* A command completing from a delay or called by a mob program (command_interpreter's callback
 * mode) has no typed text: its procedures and its handler are given an empty argument. */
#include "scripted_room_fixture.h"

#include <string>

extern command_info cmd_info[];
void assign_command_pointers(void);
SPECIAL(mob_warrior);

using namespace scripted_room;

namespace {

/* The roll that picks the second of the warrior program's turns, its bash, for a level 10 mob:
 * the roll over 0 to 10, taken modulo the program's nine turns, must come out at 1. */
constexpr double WARRIOR_BASH_TURN_ROLL = 0.12;

/* The arguments the recording procedure and handler were given, in order. */
std::vector<std::string> seen_arguments;

/* Records the argument a command offered to the room was given, and lets it go on. Written out
 * rather than through SPECIAL, so it names only the parameters it reads. */
int record_argument(char_data*, char_data*, int, char* argument, int callflag, waiting_type*)
{
    if (callflag == SPECIAL_COMMAND) {
        seen_arguments.emplace_back(argument);
    }
    return 0;
}

/* Stands in for bash's handler: records its argument. */
void record_handler_argument(char_data*, char* argument, waiting_type*, int, int)
{
    seen_arguments.emplace_back(argument);
}

class CommandInterpreterCallbackTest : public ScriptedRoomFixture {
protected:
    void SetUp() override
    {
        ScriptedRoomFixture::SetUp();
        assign_command_pointers();
        seen_arguments.clear();
        m_saved_bash_handler = cmd_info[CMD_BASH].command_pointer;
        m_saved_room_procedure = world[0].funct;
        world[0].funct = nullptr;
    }

    void TearDown() override
    {
        cmd_info[CMD_BASH].command_pointer = m_saved_bash_handler;
        world[0].funct = m_saved_room_procedure;
        seen_arguments.clear();
        ScriptedRoomFixture::TearDown();
    }

    /* A bash at nobody in particular, completing for the player as a delay ends or a mob
     * program calls it. */
    waiting_type* delayed_bash()
    {
        m_delayed = waiting_type {};
        m_delayed.cmd = CMD_BASH;
        m_delayed.targ1.type = TARGET_NONE;
        m_delayed.targ2.type = TARGET_NONE;
        return &m_delayed;
    }

    /* Runs the interpreter for the player on <line> when one is typed, otherwise on <delayed>.
     * The interpreter keeps a command's text in its own frame: a test that runs a typed command
     * and then a completed one from the same place gives the second the first one's frame. */
    void interpret(const char* line, waiting_type* delayed)
    {
        char typed[MAX_INPUT_LENGTH] = "";
        if (line) {
            std::strncpy(typed, line, sizeof(typed) - 1);
            typed[sizeof(typed) - 1] = '\0';
        }
        command_interpreter(&m_player, typed, delayed);
    }

private:
    // The completed bash delayed_bash makes.
    waiting_type m_delayed {};
    // Bash's handler before the test, put back by TearDown.
    decltype(command_info::command_pointer) m_saved_bash_handler = nullptr;
    // Room 0's procedure before the test, put back by TearDown.
    special_func m_saved_room_procedure = nullptr;
};

} // namespace

/* A typed command first leaves its words behind; the completed bash is still given none. */
TEST_F(CommandInterpreterCallbackTest, ACompletedCommandGivesItsProceduresAndHandlerNoText)
{
    const char* const lines[] = { "emote looks around for a while", nullptr };
    for (const char* line : lines) {
        waiting_type* delayed = nullptr;
        if (!line) {
            seen_arguments.clear();
            world[0].funct = record_argument;
            cmd_info[CMD_BASH].command_pointer = record_handler_argument;
            delayed = delayed_bash();
        }
        interpret(line, delayed);
    }

    const std::vector<std::string> expected = { "", "" };
    EXPECT_EQ(expected, seen_arguments) << "the procedure's and then the handler's";
}

TEST_F(CommandInterpreterCallbackTest, ATypedCommandGivesItsProceduresAndHandlerTheRestOfTheLine)
{
    world[0].funct = record_argument;
    cmd_info[CMD_BASH].command_pointer = record_handler_argument;

    interpret("bash", nullptr);

    const std::vector<std::string> expected = { "", "" };
    EXPECT_EQ(expected, seen_arguments);
}

/* A delayed command the player may no longer use (it fell below the level) is not taken for a
 * social: it has no typed word to name one. */
TEST_F(CommandInterpreterCallbackTest, ACompletedCommandThePlayerMayNoLongerUseIsUnrecognized)
{
    m_player.player.level = 0;
    cmd_info[CMD_BASH].command_pointer = record_handler_argument;

    interpret(nullptr, delayed_bash());

    EXPECT_TRUE(seen_arguments.empty());
    EXPECT_TRUE(output_has("Unrecognized command.")) << output();
}

/* A warrior mob's program, on a bash turn, calls bash for it with no typed text: bash's procedures
 * and handler are given an empty argument. The program acts only with a player in the room, as
 * the test's player is. */
TEST_F(CommandInterpreterCallbackTest, AWarriorProgramsBashTurnGivesBashNoText)
{
    char_data* warrior = make_mob("warrior", 0);
    char_data* foe = make_mob("foe", 0);
    warrior->specials.fighting = foe;
    warrior->specials.position = POSITION_FIGHTING;
    world[0].funct = record_argument;
    cmd_info[CMD_BASH].command_pointer = record_handler_argument;
    push_test_random_value(WARRIOR_BASH_TURN_ROLL);
    char no_argument[] = "";

    mob_warrior(warrior, warrior, 0, no_argument, SPECIAL_SELF, nullptr);

    const std::vector<std::string> expected = { "", "" };
    EXPECT_EQ(expected, seen_arguments) << "the procedure's and then the handler's";
}
