/* Control-flow tests for get_next_command() (script.cpp).
 *
 * get_next_command() is what a false IF and an executed END_ELSE_BEGIN use to
 * skip forward, so it decides which command a world script resumes at. It walks
 * nothing but `next` and `command_type`, so it can be exercised on a hand-built
 * node list with no world files, globals or boot.
 *
 * These cases pin the shapes the live world files actually use. The shape in
 * SkipsAWholeNestedIfWithAnEmptyElse is the one that regressed in 8446205:
 * nearly every world script writes its conditionals as
 *
 *     IF / BEGIN / ...body... / END_ELSE_BEGIN / END
 *
 * with an empty else-branch, and when such a block is nested inside an outer
 * block that is being skipped, the scan must step over the nested END rather
 * than mistake it for the outer block's own terminator.
 */
#include "../protos.h"
#include "../script.h"
#include <gtest/gtest.h>
#include <vector>

script_data* get_next_command(script_data* curr);

namespace {

/* Builds a script as a linked list of commands, numbered 1..n in order, and
 * keeps ownership for the lifetime of the test. */
class Script {
public:
    explicit Script(const std::vector<int>& command_types)
    {
        nodes_.resize(command_types.size());
        for (size_t i = 0; i < command_types.size(); ++i) {
            nodes_[i].command_type = command_types[i];
            nodes_[i].number = static_cast<int>(i) + 1;
            nodes_[i].next = (i + 1 < command_types.size()) ? &nodes_[i + 1] : nullptr;
            nodes_[i].prev = (i > 0) ? &nodes_[i - 1] : nullptr;
        }
    }

    /* 1-based, matching the command numbers world scripts are written with. */
    script_data* at(int number) { return &nodes_[number - 1]; }

    /* The command number get_next_command() resumes at when asked to skip the
     * block that opens at `begin_number`, or 0 for "ran off the end". */
    int skip_from(int begin_number)
    {
        script_data* landed = get_next_command(at(begin_number));
        return landed ? landed->number : 0;
    }

private:
    std::vector<script_data> nodes_;
};

/* A command that is neither a block delimiter nor a conditional. */
const int BODY = SCRIPT_DO_SAY;

TEST(GetNextCommand, SkipsABlockClosedByAPlainEnd)
{
    //  1 IF / 2 BEGIN / 3 body / 4 END / 5 body
    Script s({ SCRIPT_IF_INT_EQUAL, SCRIPT_BEGIN, BODY, SCRIPT_END, BODY });
    EXPECT_EQ(5, s.skip_from(2));
}

TEST(GetNextCommand, SkippingAnIfBlockLandsOnTheElseBranch)
{
    //  1 IF / 2 BEGIN / 3 body / 4 ELSE / 5 body / 6 END
    //  A false IF must run the else-branch, so it resumes at 5.
    Script s({ SCRIPT_IF_INT_EQUAL, SCRIPT_BEGIN, BODY,
        SCRIPT_END_ELSE_BEGIN, BODY, SCRIPT_END });
    EXPECT_EQ(5, s.skip_from(2));
}

TEST(GetNextCommand, ExecutingAnElseSkipsTheElseBranch)
{
    //  Same script, but reached by a TRUE IF falling into the ELSE at 4:
    //  the else-branch is skipped and execution resumes after its END.
    Script s({ SCRIPT_IF_INT_EQUAL, SCRIPT_BEGIN, BODY,
        SCRIPT_END_ELSE_BEGIN, BODY, SCRIPT_END, BODY });
    script_data* landed = get_next_command(s.at(4));
    ASSERT_NE(nullptr, landed);
    EXPECT_EQ(7, landed->number);
}

TEST(GetNextCommand, SkipsAWholeNestedIfWithAnEmptyElse)
{
    //  The world-script idiom, and the shape 8446205 regressed:
    //
    //   1 IF            <- false, so its block (2..10) must be skipped
    //   2 BEGIN
    //   3   body
    //   4   IF
    //   5   BEGIN
    //   6     body
    //   7   ELSE        <- nested block's first terminator
    //   8   END         <- must NOT be read as the outer block's END
    //   9   body        <- must NOT execute
    //  10 END
    //  11 body          <- correct landing
    Script s({ SCRIPT_IF_INT_EQUAL, SCRIPT_BEGIN, BODY,
        SCRIPT_IF_INT_EQUAL, SCRIPT_BEGIN, BODY, SCRIPT_END_ELSE_BEGIN, SCRIPT_END,
        BODY, SCRIPT_END, BODY });
    EXPECT_EQ(11, s.skip_from(2));
}

TEST(GetNextCommand, SkipsAWholeNestedIfWithoutAnElse)
{
    //   1 IF / 2 BEGIN / 3 body / 4 IF / 5 BEGIN / 6 body / 7 END / 8 body / 9 END / 10 body
    Script s({ SCRIPT_IF_INT_EQUAL, SCRIPT_BEGIN, BODY,
        SCRIPT_IF_INT_EQUAL, SCRIPT_BEGIN, BODY, SCRIPT_END,
        BODY, SCRIPT_END, BODY });
    EXPECT_EQ(10, s.skip_from(2));
}

TEST(GetNextCommand, SkipsPastTwoSiblingNestedIfsWithEmptyElses)
{
    //  Two nested conditionals in a row inside the skipped block -- the shape
    //  the crafting scripts use to accumulate ingredient counts.
    Script s({ SCRIPT_IF_INT_EQUAL, SCRIPT_BEGIN,
        SCRIPT_IF_INT_EQUAL, SCRIPT_BEGIN, BODY, SCRIPT_END_ELSE_BEGIN, SCRIPT_END,
        SCRIPT_IF_INT_EQUAL, SCRIPT_BEGIN, BODY, SCRIPT_END_ELSE_BEGIN, SCRIPT_END,
        BODY, SCRIPT_END, BODY });
    EXPECT_EQ(15, s.skip_from(2));
}

TEST(GetNextCommand, ReturnsNullWhenTheBlockIsNeverClosed)
{
    //  A truncated/unfinished script must not walk off the list.
    Script s({ SCRIPT_IF_INT_EQUAL, SCRIPT_BEGIN, BODY, BODY });
    EXPECT_EQ(0, s.skip_from(2));
}

} // namespace
