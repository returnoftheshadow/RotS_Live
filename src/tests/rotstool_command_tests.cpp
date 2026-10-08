#include "rotstool_command.h"

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

TEST(RotstoolCommand, NoArgumentsListsTheCommandsAndIsBadUsage)
{
    std::ostringstream out;
    std::ostringstream err;
    EXPECT_EQ(rotstool::run_rotstool({}, out, err), rotstool::EXIT_CODE_USAGE);
    EXPECT_NE(err.str().find("help"), std::string::npos);
}

TEST(RotstoolCommand, HelpListsEveryCommandOnStandardOutput)
{
    std::ostringstream out;
    std::ostringstream err;
    EXPECT_EQ(rotstool::run_rotstool({ "help" }, out, err), rotstool::EXIT_CODE_SUCCESS);
    for (const rotstool::RotstoolCommand& command : rotstool::rotstool_commands()) {
        EXPECT_NE(out.str().find(std::string(command.name)), std::string::npos) << command.name;
    }
}

TEST(RotstoolCommand, HelpForOneCommandPrintsItsUsage)
{
    std::ostringstream out;
    std::ostringstream err;
    EXPECT_EQ(rotstool::run_rotstool({ "help", "help" }, out, err), rotstool::EXIT_CODE_SUCCESS);
    EXPECT_NE(out.str().find("rotstool help"), std::string::npos);
}

TEST(RotstoolCommand, UnknownCommandIsBadUsageAndNamesIt)
{
    std::ostringstream out;
    std::ostringstream err;
    EXPECT_EQ(rotstool::run_rotstool({ "frobnicate" }, out, err), rotstool::EXIT_CODE_USAGE);
    EXPECT_NE(err.str().find("frobnicate"), std::string::npos);
}
