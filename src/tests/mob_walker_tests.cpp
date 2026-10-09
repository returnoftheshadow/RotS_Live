#include "../mob_walker.h"

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

walker_lookups everything()
{
    return { [](int) { return true; }, [](int, int) { return true; } };
}

std::vector<std::string> texts(const std::vector<walker_problem>& problems)
{
    std::vector<std::string> out;
    for (const walker_problem& problem : problems)
        out.push_back(problem.text);
    return out;
}

std::vector<int> lines(const std::vector<walker_problem>& problems)
{
    std::vector<int> out;
    for (const walker_problem& problem : problems)
        out.push_back(problem.line);
    return out;
}

} // namespace

TEST(WalkerRoomsParse, CommasRangesAndSpaces)
{
    std::vector<int> rooms;
    EXPECT_TRUE(walker_rooms_parse("1101-1103, 1162 ,1170-1171", &rooms));
    EXPECT_EQ(rooms, (std::vector<int> { 1101, 1102, 1103, 1162, 1170, 1171 }));
}

TEST(WalkerRoomsParse, DescendingRangeRunsBackwards)
{
    std::vector<int> rooms;
    EXPECT_TRUE(walker_rooms_parse("1105-1103", &rooms));
    EXPECT_EQ(rooms, (std::vector<int> { 1105, 1104, 1103 }));
}

TEST(WalkerRoomsParse, AppendsToWhatIsThere)
{
    std::vector<int> rooms { 5 };
    EXPECT_TRUE(walker_rooms_parse("6,7", &rooms));
    EXPECT_EQ(rooms, (std::vector<int> { 5, 6, 7 }));
}

TEST(WalkerRoomsParse, RefusesBadText)
{
    std::vector<int> rooms;
    for (const char* bad : { "", ",", "12,,13", "abc", "12-", "-12", "1-2-3", "12x", "1 2" })
        EXPECT_FALSE(walker_rooms_parse(bad, &rooms)) << bad;
}

TEST(WalkerRoomsParse, RefusesMoreThanTwoThousandRooms)
{
    std::vector<int> rooms;
    EXPECT_FALSE(walker_rooms_parse("1-999999999", &rooms));
    EXPECT_LE((int)rooms.size(), WALKER_MAX_ROOMS);
    rooms.clear();
    EXPECT_TRUE(walker_rooms_parse("1-2000", &rooms));
    EXPECT_FALSE(walker_rooms_parse("2001", &rooms));
}

TEST(WalkerOptions, NoWalkerLinesIsNotAWalker)
{
    std::vector<walker_problem> problems;
    walker_config config = parse_walker_options("store=100\n\rprice 1 2x3\n\r", everything(), &problems);
    EXPECT_EQ(config.type, WALKER_NONE);
    EXPECT_TRUE(problems.empty());
    EXPECT_EQ(parse_walker_options(nullptr, everything(), &problems).type, WALKER_NONE);
}

TEST(WalkerOptions, FullPathConfig)
{
    std::vector<walker_problem> problems;
    walker_config config = parse_walker_options(
        "walker_type=path\n\rwalker_rooms=10-12\n\rwalker_rooms=20\n\rwalker_move_chance=40\n\r"
        "path_complete_extract=yes\n\rpath_complete_message=The caravan has dispersed.\n\r"
        "// a comment\n\r",
        everything(), &problems);
    EXPECT_TRUE(problems.empty()) << (problems.empty() ? "" : problems[0].text);
    EXPECT_EQ(config.type, WALKER_PATH);
    EXPECT_EQ(config.rooms, (std::vector<int> { 10, 11, 12, 20 }));
    EXPECT_EQ(config.index.at(20), 3);
    EXPECT_EQ(config.move_chance, 40);
    EXPECT_TRUE(config.complete_extract);
    EXPECT_FALSE(config.continue_wandering);
    EXPECT_EQ(config.complete_message, "The caravan has dispersed.");
}

TEST(WalkerOptions, TypeErrorsAndMissingRoomsDisableTheWalker)
{
    std::vector<walker_problem> problems;
    EXPECT_EQ(parse_walker_options("walker_type=circle\n\rwalker_rooms=1,2\n\r", everything(), &problems).type, WALKER_NONE);
    EXPECT_EQ(texts(problems), (std::vector<std::string> { "bad walker_type - walker disabled" }));

    problems.clear();
    EXPECT_EQ(parse_walker_options("walker_rooms=1,2\n\r", everything(), &problems).type, WALKER_NONE);
    EXPECT_EQ(texts(problems), (std::vector<std::string> { "walker_type missing - walker disabled" }));

    problems.clear();
    EXPECT_EQ(parse_walker_options("walker_type=path\n\r", everything(), &problems).type, WALKER_NONE);
    EXPECT_EQ(texts(problems), (std::vector<std::string> { "walker_rooms missing - walker disabled" }));
}

TEST(WalkerOptions, SecondTypeLineIsIgnoredAndTheFirstStands)
{
    std::vector<walker_problem> problems;
    EXPECT_EQ(parse_walker_options("walker_type=path\n\rwalker_type=loop\n\rwalker_rooms=1,2\n\r", everything(), &problems).type,
        WALKER_PATH);
    EXPECT_EQ(texts(problems), (std::vector<std::string> { "duplicate setting - line ignored" }));
    EXPECT_EQ(problems[0].line, 2);
}

TEST(WalkerOptions, RoomErrorsDisableTheWalker)
{
    std::vector<walker_problem> problems;
    EXPECT_EQ(parse_walker_options("walker_type=path\n\rwalker_rooms=1,x\n\r", everything(), &problems).type, WALKER_NONE);
    EXPECT_EQ(texts(problems), (std::vector<std::string> { "bad room list - walker disabled" }));
    EXPECT_EQ(problems[0].line, 2);

    problems.clear();
    EXPECT_EQ(parse_walker_options("walker_type=path\n\rwalker_rooms=1,2,1\n\r", everything(), &problems).type, WALKER_NONE);
    EXPECT_EQ(texts(problems), (std::vector<std::string> { "room 1 listed twice - walker disabled" }));

    problems.clear();
    walker_lookups missing { [](int vnum) { return vnum != 2; }, [](int, int) { return true; } };
    EXPECT_EQ(parse_walker_options("walker_type=path\n\rwalker_rooms=1,2,3\n\r", missing, &problems).type, WALKER_NONE);
    EXPECT_EQ(texts(problems), (std::vector<std::string> { "room 2 not found - walker disabled" }));

    problems.clear();
    EXPECT_EQ(parse_walker_options("walker_type=loop\n\rwalker_rooms=7\n\r", everything(), &problems).type, WALKER_NONE);
    EXPECT_EQ(texts(problems), (std::vector<std::string> { "loop needs 2 rooms - walker disabled" }));
}

TEST(WalkerOptions, BadChanceAndBadYesNoAreIgnoredLines)
{
    std::vector<walker_problem> problems;
    walker_config config = parse_walker_options(
        "walker_type=path\n\rwalker_rooms=1,2\n\rwalker_move_chance=0\n\rpath_complete_extract=maybe\n\r"
        "continue_wandering=no\n\r",
        everything(), &problems);
    EXPECT_EQ(config.type, WALKER_PATH);
    EXPECT_EQ(config.move_chance, 100);
    EXPECT_FALSE(config.complete_extract);
    EXPECT_FALSE(config.continue_wandering);
    EXPECT_EQ(texts(problems),
        (std::vector<std::string> { "bad value - line ignored", "bad value - line ignored" }));
    for (const char* chance : { "101", "-5", "ten", "" }) {
        problems.clear();
        std::string text = std::string("walker_type=path\n\rwalker_rooms=1,2\n\rwalker_move_chance=") + chance + "\n\r";
        EXPECT_EQ(parse_walker_options(text.c_str(), everything(), &problems).move_chance, 100) << chance;
        EXPECT_EQ(problems.size(), 1u) << chance;
    }
}

TEST(WalkerOptions, SettingsThatDoNotFitTheModeAreWarnedAndDropped)
{
    std::vector<walker_problem> problems;
    walker_config config = parse_walker_options(
        "walker_type=loop\n\rwalker_rooms=1,2\n\rpath_complete_extract=yes\n\rcontinue_wandering=yes\n\r"
        "path_complete_message=Round again.\n\rwalker_move_chance=50\n\r",
        everything(), &problems);
    EXPECT_EQ(config.type, WALKER_LOOP);
    EXPECT_FALSE(config.complete_extract);
    EXPECT_FALSE(config.continue_wandering);
    EXPECT_EQ(config.complete_message, "Round again.");
    EXPECT_EQ(config.move_chance, 50);
    EXPECT_EQ(texts(problems),
        (std::vector<std::string> { "no effect with loop", "no effect with loop" }));
    EXPECT_TRUE(problems[0].warning);

    problems.clear();
    config = parse_walker_options(
        "walker_type=bounded\n\rwalker_rooms=1,2\n\rwalker_move_chance=50\n\rpath_complete_message=x\n\r", everything(), &problems);
    EXPECT_EQ(config.move_chance, 100);
    EXPECT_EQ(config.complete_message, "");
    EXPECT_EQ(texts(problems),
        (std::vector<std::string> { "no effect with bounded", "no effect with bounded" }));
}

TEST(WalkerOptions, ExtractAndContinueTogetherIsWarned)
{
    std::vector<walker_problem> problems;
    walker_config config = parse_walker_options(
        "walker_type=path\n\rwalker_rooms=1,2\n\rpath_complete_extract=yes\n\rcontinue_wandering=yes\n\r", everything(), &problems);
    EXPECT_TRUE(config.complete_extract);
    EXPECT_FALSE(config.continue_wandering);
    EXPECT_EQ(texts(problems), (std::vector<std::string> { "no effect when extracted" }));
}

TEST(WalkerOptions, LongMessageIsWarnedAndKept)
{
    std::vector<walker_problem> problems;
    std::string text = "walker_type=path\n\rwalker_rooms=1,2\n\rpath_complete_message=" + std::string(79, 'a') + "\n\r";
    walker_config config = parse_walker_options(text.c_str(), everything(), &problems);
    EXPECT_EQ(config.complete_message.size(), 79u);
    EXPECT_EQ(texts(problems), (std::vector<std::string> { "longer than 78 columns" }));
    EXPECT_TRUE(problems[0].warning);
}

TEST(WalkerOptions, RoomsNotJoinedIsAWarningOnly)
{
    std::set<std::pair<int, int>> asked;
    walker_lookups lookups { [](int) { return true; }, [&](int a, int b) {
                                asked.insert({ a, b });
                                return !(a == 2 && b == 3); } };
    std::vector<walker_problem> problems;
    walker_config config = parse_walker_options("walker_type=path\n\rwalker_rooms=1,2,3\n\r", lookups, &problems);
    EXPECT_EQ(config.type, WALKER_PATH);
    EXPECT_EQ(texts(problems), (std::vector<std::string> { "no exit from room 2 to room 3" }));
    EXPECT_TRUE(problems[0].warning);
    EXPECT_EQ(asked, (std::set<std::pair<int, int>> { { 1, 2 }, { 2, 3 } }));

    asked.clear();
    problems.clear();
    parse_walker_options("walker_type=loop\n\rwalker_rooms=1,2,3\n\r", lookups, &problems);
    EXPECT_TRUE(asked.count({ 3, 1 })); /* a loop also needs last -> first */

    asked.clear();
    parse_walker_options("walker_type=bounded\n\rwalker_rooms=1,2,3\n\r", lookups, &problems);
    EXPECT_TRUE(asked.empty()); /* an area is not a route */
}

namespace {

walker_config route(const char* type, const char* rooms)
{
    std::string text = std::string("walker_type=") + type + "\n\rwalker_rooms=" + rooms + "\n\r";
    return parse_walker_options(text.c_str(), everything(), nullptr);
}

} // namespace

TEST(WalkerDecide, NoConfigIsNormal)
{
    walker_config none;
    EXPECT_EQ(walker_decide(none, false, 10, 11), WALKER_STEP_NORMAL);
    EXPECT_EQ(walker_decide(none, false, 10, -1), WALKER_STEP_NORMAL);
    EXPECT_FALSE(walker_at_path_end(none, false, 10));
}

TEST(WalkerDecide, PathTakesOnlyTheNextRoom)
{
    walker_config path = route("path", "10,11,12");
    EXPECT_EQ(walker_decide(path, false, 10, 11), WALKER_STEP_MOVE);
    EXPECT_EQ(walker_decide(path, false, 11, 12), WALKER_STEP_MOVE);
    EXPECT_EQ(walker_decide(path, false, 11, 10), WALKER_STEP_BLOCKED); /* never backwards */
    EXPECT_EQ(walker_decide(path, false, 10, 12), WALKER_STEP_BLOCKED); /* never skips */
    EXPECT_EQ(walker_decide(path, false, 10, 99), WALKER_STEP_BLOCKED); /* never off the route */
    EXPECT_EQ(walker_decide(path, false, 10, -1), WALKER_STEP_BLOCKED);
}

/* walker_decide only ends a path on a roll that found a usable exit. The other
 * rule - a last room with no usable exit at all ends on any roll - lives in
 * walker_wander, which needs a world and is covered in game, not here. */
TEST(WalkerDecide, PathEndsOnAnyUsableExitRolledInTheLastRoom)
{
    walker_config path = route("path", "10,11,12");
    EXPECT_EQ(walker_decide(path, false, 12, 11), WALKER_STEP_END);
    EXPECT_EQ(walker_decide(path, false, 12, 99), WALKER_STEP_END);
    EXPECT_EQ(walker_decide(path, false, 12, -1), WALKER_STEP_BLOCKED); /* no usable exit rolled */
    EXPECT_TRUE(walker_at_path_end(path, false, 12));
    EXPECT_FALSE(walker_at_path_end(path, false, 11));
    EXPECT_FALSE(walker_at_path_end(path, true, 12));
    walker_config one = route("path", "10");
    EXPECT_EQ(walker_decide(one, false, 10, 99), WALKER_STEP_END);
}

TEST(WalkerDecide, OffTheListIsNormalWandering)
{
    for (const char* type : { "path", "loop", "bounded" }) {
        walker_config config = route(type, "10,11,12");
        EXPECT_EQ(walker_decide(config, false, 50, 51), WALKER_STEP_NORMAL) << type;
        EXPECT_EQ(walker_decide(config, false, 50, 10), WALKER_STEP_NORMAL) << type; /* finds its way back */
        EXPECT_EQ(walker_decide(config, false, 50, -1), WALKER_STEP_NORMAL) << type;
    }
}

TEST(WalkerDecide, FinishedWalkerWandersNormallyEvenOnItsRoute)
{
    walker_config path = route("path", "10,11,12");
    EXPECT_EQ(walker_decide(path, true, 10, 11), WALKER_STEP_NORMAL);
    EXPECT_EQ(walker_decide(path, true, 12, 99), WALKER_STEP_NORMAL);
}

TEST(WalkerDecide, LoopWrapsFromLastToFirst)
{
    walker_config loop = route("loop", "10,11,12");
    EXPECT_EQ(walker_decide(loop, false, 10, 11), WALKER_STEP_MOVE);
    EXPECT_EQ(walker_decide(loop, false, 12, 10), WALKER_STEP_MOVE_LAP);
    EXPECT_EQ(walker_decide(loop, false, 12, 11), WALKER_STEP_BLOCKED);
    EXPECT_EQ(walker_decide(loop, false, 12, 99), WALKER_STEP_BLOCKED);
    EXPECT_FALSE(walker_at_path_end(loop, false, 12));
}

TEST(WalkerDecide, BoundedStaysInsideItsList)
{
    walker_config area = route("bounded", "10,11,12");
    EXPECT_EQ(walker_decide(area, false, 10, 12), WALKER_STEP_NORMAL); /* any listed room, any order */
    EXPECT_EQ(walker_decide(area, false, 12, 10), WALKER_STEP_NORMAL);
    EXPECT_EQ(walker_decide(area, false, 10, 99), WALKER_STEP_BLOCKED);
    EXPECT_EQ(walker_decide(area, false, 10, -1), WALKER_STEP_NORMAL); /* nothing to refuse */
}

TEST(WalkerProblemLines, ErrorWarningAndWholeConfigForms)
{
    std::vector<walker_problem> problems {
        { 3, "bad value - line ignored", false },
        { 0, "walker_rooms missing - walker disabled", false },
        { 0, "no exit from room 2 to room 3", true },
        { 5, "no effect with loop", true },
    };
    EXPECT_EQ(walker_problem_lines(1234, problems),
        (std::vector<std::string> {
            "MOB ERROR: mobile #1234, options line 3: bad value - line ignored",
            "MOB ERROR: mobile #1234: walker_rooms missing - walker disabled",
            "MOB WARNING: mobile #1234: no exit from room 2 to room 3",
            "MOB WARNING: mobile #1234, options line 5: no effect with loop",
        }));
}

/* Every message text a builder can be sent today, produced by real input and
 * checked as the finished line at its longest: mob #99999 (the highest a mob
 * file allows), a four-digit options line, nine-digit rooms. `expected` is the
 * exact set, so a reworded or dropped message fails here. A brand-new message
 * is only checked once an input that produces it is added below. */
TEST(WalkerProblemLines, EveryBuilderMessageFitsSeventyEightColumns)
{
    walker_lookups unjoined { [](int) { return true; }, [](int, int) { return false; } };
    walker_lookups no_rooms { [](int) { return false; }, [](int, int) { return true; } };
    const std::string two = "walker_rooms=100000000,100000001\n\r";
    const std::string long_message = "path_complete_message=" + std::string(79, 'a') + "\n\r";
    const std::vector<std::pair<std::string, walker_lookups>> inputs = {
        { "walker_type=circle\n\r" + two, everything() },
        { two, everything() },
        { "walker_type=path\n\r", everything() },
        { "walker_type=path\n\rwalker_rooms=100000000,x\n\r", everything() },
        { "walker_type=path\n\rwalker_type=loop\n\r" + two, everything() },
        { "walker_type=path\n\r" + two + "walker_move_chance=0\n\r", everything() },
        { "walker_type=path\n\rwalker_rooms=100000000,100000001,100000000\n\r", everything() },
        { "walker_type=path\n\r" + two, no_rooms },
        { "walker_type=loop\n\rwalker_rooms=100000000\n\r", everything() },
        { "walker_type=loop\n\r" + two + "path_complete_extract=yes\n\rcontinue_wandering=yes\n\r", unjoined },
        { "walker_type=bounded\n\r" + two + "walker_move_chance=50\n\rpath_complete_message=x\n\r", everything() },
        { "walker_type=path\n\r" + two + "path_complete_extract=yes\n\rcontinue_wandering=yes\n\r", everything() },
        { "walker_type=path\n\r" + two + long_message, unjoined },
    };
    std::vector<walker_problem> problems;
    for (const auto& input : inputs) {
        std::vector<walker_problem> found;
        parse_walker_options(input.first.c_str(), input.second, &found);
        EXPECT_FALSE(found.empty()) << input.first;
        problems.insert(problems.end(), found.begin(), found.end());
    }
    for (const walker_problem& problem : mob_options_unknown_lines("walker_typ=path\n\r"))
        problems.push_back(problem);

    std::set<std::string> seen;
    for (walker_problem& problem : problems) {
        std::string text; /* each run of digits becomes one N, so "room 100000000 ..." is one message */
        for (char c : problem.text) {
            bool digit = c >= '0' && c <= '9';
            if (!digit)
                text += c;
            else if (text.empty() || text.back() != 'N')
                text += 'N';
        }
        seen.insert(text);
        if (problem.line > 0)
            problem.line = 9999; /* the longest an options line number gets */
    }
    const std::set<std::string> expected = {
        "bad room list - walker disabled",
        "bad value - line ignored",
        "bad walker_type - walker disabled",
        "duplicate setting - line ignored",
        "longer than N columns",
        "loop needs N rooms - walker disabled",
        "no effect when extracted",
        "no effect with bounded",
        "no effect with loop",
        "no exit from room N to room N",
        "room N listed twice - walker disabled",
        "room N not found - walker disabled",
        "unknown setting - line ignored",
        "walker_rooms missing - walker disabled",
        "walker_type missing - walker disabled",
    };
    EXPECT_EQ(seen, expected);
    for (const std::string& line : walker_problem_lines(99999, problems))
        EXPECT_LE(line.size(), 78u) << line;
}

/* ---- limits at their edges, and the faults one wrong character would cause ---- */

TEST(WalkerRoomsParse, NineDigitsAreARoomNumberTenAreNot)
{
    std::vector<int> rooms;
    EXPECT_TRUE(walker_rooms_parse("999999999", &rooms));
    EXPECT_EQ(rooms, (std::vector<int> { 999999999 }));
    rooms.clear();
    EXPECT_FALSE(walker_rooms_parse("1000000000", &rooms));
}

TEST(WalkerRoomsParse, SpacesInsideARangeAreAllowed)
{
    std::vector<int> rooms;
    EXPECT_TRUE(walker_rooms_parse(" 1101 - 1103 , 1105 ", &rooms));
    EXPECT_EQ(rooms, (std::vector<int> { 1101, 1102, 1103, 1105 }));
}

TEST(WalkerOptions, SpacesAroundTheEqualsSignAreAllowed)
{
    std::vector<walker_problem> problems;
    walker_config config = parse_walker_options("  walker_type = loop \n\r walker_rooms = 1 , 2 \n\r walker_move_chance = 40 \n\r",
        everything(), &problems);
    EXPECT_TRUE(problems.empty());
    EXPECT_EQ(config.type, WALKER_LOOP);
    EXPECT_EQ(config.rooms, (std::vector<int> { 1, 2 }));
    EXPECT_EQ(config.move_chance, 40);
}

TEST(WalkerOptions, MoveChanceAcceptsOneAndAHundredAndNothingOutside)
{
    for (int good : { 1, 100 }) {
        std::vector<walker_problem> problems;
        std::string text = "walker_type=path\n\rwalker_rooms=1,2\n\rwalker_move_chance=" + std::to_string(good) + "\n\r";
        EXPECT_EQ(parse_walker_options(text.c_str(), everything(), &problems).move_chance, good);
        EXPECT_TRUE(problems.empty()) << good;
    }
    for (int bad : { 0, 101 }) {
        std::vector<walker_problem> problems;
        std::string text = "walker_type=path\n\rwalker_rooms=1,2\n\rwalker_move_chance=" + std::to_string(bad) + "\n\r";
        EXPECT_EQ(parse_walker_options(text.c_str(), everything(), &problems).move_chance, 100) << bad;
        EXPECT_EQ(texts(problems), (std::vector<std::string> { "bad value - line ignored" })) << bad;
        EXPECT_EQ(lines(problems), (std::vector<int> { 3 })) << bad;
    }
}

TEST(WalkerOptions, AMessageOfExactlySeventyEightColumnsIsNotWarned)
{
    std::vector<walker_problem> problems;
    std::string head = "walker_type=path\n\rwalker_rooms=1,2\n\rpath_complete_message=";
    parse_walker_options((head + std::string(78, 'a') + "\n\r").c_str(), everything(), &problems);
    EXPECT_TRUE(problems.empty());
    parse_walker_options((head + std::string(79, 'a') + "\n\r").c_str(), everything(), &problems);
    EXPECT_EQ(texts(problems), (std::vector<std::string> { "longer than 78 columns" }));
    EXPECT_EQ(lines(problems), (std::vector<int> { 3 }));
}

TEST(WalkerOptions, EachWarningNamesItsOwnLine)
{
    std::vector<walker_problem> problems;
    parse_walker_options("walker_type=loop\n\rwalker_rooms=1,2\n\rpath_complete_extract=yes\n\r\n\rcontinue_wandering=yes\n\r",
        everything(), &problems);
    EXPECT_EQ(texts(problems), (std::vector<std::string> { "no effect with loop", "no effect with loop" }));
    EXPECT_EQ(lines(problems), (std::vector<int> { 3, 5 })); /* extract, then continue; the blank line counts */

    problems.clear();
    parse_walker_options("walker_type=bounded\n\rwalker_rooms=1,2\n\rpath_complete_message=x\n\rwalker_move_chance=50\n\r",
        everything(), &problems);
    EXPECT_EQ(texts(problems), (std::vector<std::string> { "no effect with bounded", "no effect with bounded" }));
    EXPECT_EQ(lines(problems), (std::vector<int> { 4, 3 })); /* the chance line is reported first, then the message line */

    problems.clear();
    parse_walker_options("walker_type=path\n\rwalker_rooms=1,2\n\rcontinue_wandering=yes\n\rpath_complete_extract=yes\n\r",
        everything(), &problems);
    EXPECT_EQ(texts(problems), (std::vector<std::string> { "no effect when extracted" }));
    EXPECT_EQ(lines(problems), (std::vector<int> { 3 })); /* the continue_wandering line, not the extract line */
}

TEST(WalkerOptions, FaultsOfTheSettingsAsAWholeCarryNoLineNumber)
{
    walker_lookups unjoined { [](int) { return true; }, [](int, int) { return false; } };
    std::vector<walker_problem> problems;
    parse_walker_options("walker_rooms=1,2\n\r", everything(), &problems);
    parse_walker_options("walker_type=path\n\r", everything(), &problems);
    parse_walker_options("walker_type=path\n\rwalker_rooms=1,1\n\r", everything(), &problems);
    parse_walker_options("walker_type=loop\n\rwalker_rooms=1\n\r", everything(), &problems);
    parse_walker_options("walker_type=path\n\rwalker_rooms=1,2\n\r", unjoined, &problems);
    ASSERT_EQ(problems.size(), 5u);
    EXPECT_EQ(lines(problems), (std::vector<int> { 0, 0, 0, 0, 0 }));
}

TEST(WalkerOptions, ALoopClosingWarningNamesTheLastRoomThenTheFirst)
{
    walker_lookups open_ring { [](int) { return true; }, [](int a, int b) { return !(a == 3 && b == 1); } };
    std::vector<walker_problem> problems;
    EXPECT_EQ(parse_walker_options("walker_type=loop\n\rwalker_rooms=1,2,3\n\r", open_ring, &problems).type, WALKER_LOOP);
    EXPECT_EQ(texts(problems), (std::vector<std::string> { "no exit from room 3 to room 1" }));
    EXPECT_TRUE(problems[0].warning);
}

TEST(WalkerOptions, ADisabledWalkerReportsItsCauseAndNothingElse)
{
    walker_lookups unjoined { [](int) { return true; }, [](int, int) { return false; } };
    std::vector<walker_problem> problems;
    walker_config config = parse_walker_options(
        "walker_type=circle\n\rwalker_rooms=1,2\n\rpath_complete_extract=yes\n\rwalker_move_chance=50\n\rpath_complete_message=x\n\r",
        unjoined, &problems);
    EXPECT_EQ(config.type, WALKER_NONE);
    EXPECT_TRUE(config.rooms.empty());
    EXPECT_EQ(texts(problems), (std::vector<std::string> { "bad walker_type - walker disabled" }));
}

TEST(WalkerOptions, OnlyTheFirstDisablingFaultIsReported)
{
    std::vector<walker_problem> problems;
    parse_walker_options("walker_type=circle\n\r", everything(), &problems); /* bad type and no rooms */
    EXPECT_EQ(texts(problems), (std::vector<std::string> { "bad walker_type - walker disabled" }));

    problems.clear();
    parse_walker_options("walker_type=path\n\rwalker_rooms=1,1,1\n\r", everything(), &problems);
    EXPECT_EQ(texts(problems), (std::vector<std::string> { "room 1 listed twice - walker disabled" }));

    problems.clear();
    walker_lookups no_rooms { [](int) { return false; }, [](int, int) { return true; } };
    parse_walker_options("walker_type=loop\n\rwalker_rooms=7\n\r", no_rooms, &problems); /* missing room and a one-room loop */
    EXPECT_EQ(texts(problems), (std::vector<std::string> { "room 7 not found - walker disabled" }));
}

TEST(MobOptionsUnknownLines, BlankLinesAreSkippedAndStillCounted)
{
    std::vector<walker_problem> unknown = mob_options_unknown_lines("walker_type=path\n\r\n\r   \n\rwalkr_type=path\n\r");
    ASSERT_EQ(unknown.size(), 1u);
    EXPECT_EQ(unknown[0].line, 4);
}

TEST(MobOptionsUnknownLines, ReportsOnlyLinesNoFeatureKnows)
{
    std::vector<walker_problem> unknown = mob_options_unknown_lines(
        "walker_type=path\n\r// note\n\rwalker_rooms=1,2\n\rwalkr_type=path\n\rstore=5\n\r");
    ASSERT_EQ(unknown.size(), 2u);
    EXPECT_EQ(unknown[0].line, 4);
    EXPECT_EQ(unknown[0].text, "unknown setting - line ignored");
    EXPECT_EQ(unknown[1].line, 5); /* a vendor key on a mob that is not a vendor */
    EXPECT_TRUE(mob_options_unknown_lines(nullptr).empty());
}
