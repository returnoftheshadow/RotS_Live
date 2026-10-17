#include "command_line_option.h"
#include "command_line_parse_result.h"
#include "command_line_parser.h"
#include "test_command_line.h"

#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
// The option most tests register.
constexpr std::string_view COLOUR_OPTION = "--colour";
// The second option, for tests that need two.
constexpr std::string_view SHAPE_OPTION = "--shape";
// The value the colour option's validator refuses.
constexpr std::string_view REFUSED_COLOUR = "plaid";
// The reason the colour option's validator gives.
constexpr std::string_view REFUSED_COLOUR_REASON = "plaid is not a colour";
// A value placed in a result to show a refused parse left it alone.
constexpr std::string_view UNTOUCHED_VALUE = "untouched";
// The words the parser logs when it refuses a duplicate name.
constexpr std::string_view DUPLICATE_PHRASE = "already registered";
// The words the parser logs when it refuses a name without the prefix.
constexpr std::string_view NO_PREFIX_PHRASE = "does not start with";
// The words the parser logs when it refuses a reserved name.
constexpr std::string_view RESERVED_PHRASE = "ageland's own parser";

// The colour option: value name COLOUR, refusing REFUSED_COLOUR, required when asked.
harness::CommandLineOption make_colour_option(bool required)
{
    harness::CommandLineOption option;
    option.name = std::string(COLOUR_OPTION);
    option.value_name = "COLOUR";
    option.help = "the colour to paint";
    option.required = required;
    option.validate = [](std::string_view value) -> std::optional<std::string> {
        if (value == REFUSED_COLOUR) {
            return std::string(REFUSED_COLOUR_REASON);
        }
        return std::nullopt;
    };
    return option;
}

// An optional option named option_name, with no validator.
harness::CommandLineOption make_plain_option(std::string_view option_name)
{
    harness::CommandLineOption option;
    option.name = std::string(option_name);
    option.value_name = "VALUE";
    option.help = "a plain option";
    return option;
}

// A parser holding the required colour option.
harness::CommandLineParser make_colour_parser()
{
    harness::CommandLineParser parser;
    const bool registered = parser.register_option(make_colour_option(true));
    EXPECT_TRUE(registered) << "Expected the colour option to register.";
    return parser;
}

// A parser holding the required colour option and the optional shape option.
harness::CommandLineParser make_colour_and_shape_parser()
{
    harness::CommandLineParser parser = make_colour_parser();
    const bool registered = parser.register_option(make_plain_option(SHAPE_OPTION));
    EXPECT_TRUE(registered) << "Expected the shape option to register.";
    return parser;
}

// The unclaimed arguments as strings, for comparison.
std::vector<std::string> unclaimed_words(const harness::CommandLineParseResult& result)
{
    std::vector<std::string> words;
    for (const char* const argument : result.unclaimed_arguments) {
        words.emplace_back(argument);
    }
    return words;
}

// Parses arguments with parser, expects a refusal that leaves the result unchanged, and returns
// the error.
std::string expect_refused(
    const harness::CommandLineParser& parser, std::vector<std::string> arguments)
{
    harness_tests::CommandLine command_line(std::move(arguments));
    harness::CommandLineParseResult result;
    result.option_values.emplace(std::string(UNTOUCHED_VALUE), std::string(UNTOUCHED_VALUE));
    std::string error;

    EXPECT_FALSE(parser.parse(command_line.count(), command_line.vector(), result, error))
        << "Expected the command line to be refused.";
    EXPECT_EQ(result.option_values.size(), 1u) << "Expected the result unchanged.";
    EXPECT_EQ(result.option_values.count(UNTOUCHED_VALUE), 1u) << "Expected the result unchanged.";
    EXPECT_TRUE(result.unclaimed_arguments.empty()) << "Expected the result unchanged.";
    return error;
}

// Registers option on parser, expects the registration refused and logged with phrase, and
// expects the parser's usage unchanged.
void expect_registration_refused(harness::CommandLineParser& parser,
    harness::CommandLineOption option, std::string_view phrase)
{
    const std::string usage_before = parser.usage();
    testing::internal::CaptureStderr();
    const bool registered = parser.register_option(std::move(option));
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_FALSE(registered) << "Expected the registration to be refused.";
    EXPECT_NE(logged.find(phrase), std::string::npos)
        << "Expected the refusal logged with '" << phrase << "'; got: " << logged;
    EXPECT_EQ(parser.usage(), usage_before) << "Expected the parser unchanged.";
}
} // namespace

TEST(CommandLineParser, ClaimsAnOptionRightAfterTheProgramName)
{
    const harness::CommandLineParser parser = make_colour_parser();
    harness_tests::CommandLine command_line(
        { "program", std::string(COLOUR_OPTION), "red", "-d", "lib" });
    harness::CommandLineParseResult result;
    std::string error;

    ASSERT_TRUE(parser.parse(command_line.count(), command_line.vector(), result, error)) << error;
    ASSERT_EQ(result.option_values.count(COLOUR_OPTION), 1u) << "Expected the colour claimed.";
    EXPECT_EQ(result.option_values.find(COLOUR_OPTION)->second, "red") << "Unexpected colour.";
    EXPECT_EQ(unclaimed_words(result), (std::vector<std::string> { "program", "-d", "lib" }))
        << "Expected the program name and -d lib unclaimed.";
}

TEST(CommandLineParser, ClaimsAnOptionAtTheEnd)
{
    const harness::CommandLineParser parser = make_colour_parser();
    harness_tests::CommandLine command_line(
        { "program", "-d", "lib", "4321", std::string(COLOUR_OPTION), "blue" });
    harness::CommandLineParseResult result;
    std::string error;

    ASSERT_TRUE(parser.parse(command_line.count(), command_line.vector(), result, error)) << error;
    ASSERT_EQ(result.option_values.count(COLOUR_OPTION), 1u) << "Expected the colour claimed.";
    EXPECT_EQ(result.option_values.find(COLOUR_OPTION)->second, "blue") << "Unexpected colour.";
    EXPECT_EQ(
        unclaimed_words(result), (std::vector<std::string> { "program", "-d", "lib", "4321" }))
        << "Expected ageland's arguments unclaimed, in order.";
}

TEST(CommandLineParser, ClaimsAnOptionWrittenAsAnotherOptionsValue)
{
    const harness::CommandLineParser parser = make_colour_parser();
    harness_tests::CommandLine command_line(
        { "program", "-d", std::string(COLOUR_OPTION), "green" });
    harness::CommandLineParseResult result;
    std::string error;

    ASSERT_TRUE(parser.parse(command_line.count(), command_line.vector(), result, error)) << error;
    ASSERT_EQ(result.option_values.count(COLOUR_OPTION), 1u) << "Expected the colour claimed.";
    EXPECT_EQ(result.option_values.find(COLOUR_OPTION)->second, "green") << "Unexpected colour.";
    EXPECT_EQ(unclaimed_words(result), (std::vector<std::string> { "program", "-d" }))
        << "Expected the option claimed even after -d.";
}

TEST(CommandLineParser, PassesTheRandomSeedThroughInBothForms)
{
    const harness::CommandLineParser parser = make_colour_parser();
    const std::vector<std::string> arguments = { "program", "--random-seed=7", "-d", "lib",
        std::string(COLOUR_OPTION), "red", "--random-seed", "7", "4321" };
    harness_tests::CommandLine command_line(arguments);
    harness::CommandLineParseResult result;
    std::string error;

    ASSERT_TRUE(parser.parse(command_line.count(), command_line.vector(), result, error)) << error;
    EXPECT_EQ(unclaimed_words(result),
        (std::vector<std::string> {
            "program", "--random-seed=7", "-d", "lib", "--random-seed", "7", "4321" }))
        << "Expected every unregistered argument unclaimed, in its original order.";
}

TEST(CommandLineParser, HandsBackTheArgvPointersUnchanged)
{
    const harness::CommandLineParser parser = make_colour_parser();
    harness_tests::CommandLine command_line(
        { "program", std::string(COLOUR_OPTION), "red", "-d", "lib" });
    harness::CommandLineParseResult result;
    std::string error;

    ASSERT_TRUE(parser.parse(command_line.count(), command_line.vector(), result, error)) << error;
    ASSERT_EQ(result.unclaimed_arguments.size(), 3u) << "Expected three unclaimed arguments.";
    EXPECT_EQ(result.unclaimed_arguments[0], command_line.vector()[0]) << "Expected argv[0].";
    EXPECT_EQ(result.unclaimed_arguments[1], command_line.vector()[3]) << "Expected argv[3].";
    EXPECT_EQ(result.unclaimed_arguments[2], command_line.vector()[4]) << "Expected argv[4].";
}

TEST(CommandLineParser, LeavesAMissingOptionalOptionOut)
{
    harness::CommandLineParser parser;
    ASSERT_TRUE(parser.register_option(make_colour_option(false)))
        << "Expected the colour option to register.";
    harness_tests::CommandLine command_line({ "program", "4321" });
    harness::CommandLineParseResult result;
    std::string error;

    ASSERT_TRUE(parser.parse(command_line.count(), command_line.vector(), result, error)) << error;
    EXPECT_TRUE(result.option_values.empty()) << "Expected no claimed values.";
    EXPECT_EQ(unclaimed_words(result), (std::vector<std::string> { "program", "4321" }))
        << "Expected the arguments unclaimed.";
}

TEST(CommandLineParser, ClaimsTwoOptions)
{
    harness::CommandLineParser parser;
    ASSERT_TRUE(parser.register_option(make_colour_option(true)))
        << "Expected the colour option to register.";
    ASSERT_TRUE(parser.register_option(make_plain_option(SHAPE_OPTION)))
        << "Expected the shape option to register.";
    harness_tests::CommandLine command_line({ "program", std::string(SHAPE_OPTION), "round",
        "-q", std::string(COLOUR_OPTION), "red" });
    harness::CommandLineParseResult result;
    std::string error;

    ASSERT_TRUE(parser.parse(command_line.count(), command_line.vector(), result, error)) << error;
    ASSERT_EQ(result.option_values.count(COLOUR_OPTION), 1u) << "Expected the colour claimed.";
    ASSERT_EQ(result.option_values.count(SHAPE_OPTION), 1u) << "Expected the shape claimed.";
    EXPECT_EQ(result.option_values.find(COLOUR_OPTION)->second, "red") << "Unexpected colour.";
    EXPECT_EQ(result.option_values.find(SHAPE_OPTION)->second, "round") << "Unexpected shape.";
    EXPECT_EQ(unclaimed_words(result), (std::vector<std::string> { "program", "-q" }))
        << "Expected only -q unclaimed.";
}

TEST(CommandLineParser, RefusesNoCommandLine)
{
    const harness::CommandLineParser parser = make_colour_parser();
    harness::CommandLineParseResult result;
    std::string error;

    EXPECT_FALSE(parser.parse(0, nullptr, result, error)) << "Expected no command line refused.";
    EXPECT_EQ(error, "no command line") << "Unexpected refusal.";
}

TEST(CommandLineParser, RefusesTheValueAttachedWithEquals)
{
    const std::string error = expect_refused(
        make_colour_parser(), { "program", std::string(COLOUR_OPTION) + "=red", "4321" });

    EXPECT_EQ(error, "--colour takes its value as the next argument") << "Unexpected refusal.";
}

TEST(CommandLineParser, RefusesAnEmptyValueAttachedWithEquals)
{
    const std::string error
        = expect_refused(make_colour_parser(), { "program", std::string(COLOUR_OPTION) + "=" });

    EXPECT_EQ(error, "--colour takes its value as the next argument") << "Unexpected refusal.";
}

TEST(CommandLineParser, RefusesARepeatedOption)
{
    const std::string option(COLOUR_OPTION);
    const std::string error
        = expect_refused(make_colour_parser(), { "program", option, "red", option, "blue" });

    EXPECT_EQ(error, "--colour is given more than once") << "Unexpected refusal.";
}

TEST(CommandLineParser, RefusesAMissingValue)
{
    const std::string error = expect_refused(
        make_colour_parser(), { "program", "-d", "lib", std::string(COLOUR_OPTION) });

    EXPECT_EQ(error, "--colour needs a value (COLOUR)") << "Unexpected refusal.";
}

TEST(CommandLineParser, RefusesAnOptionFollowedByARegisteredName)
{
    const harness::CommandLineParser parser = make_colour_and_shape_parser();
    const std::string colour(COLOUR_OPTION);
    const std::string shape(SHAPE_OPTION);

    EXPECT_EQ(expect_refused(parser, { "program", shape, colour, "red" }),
        "--shape needs a value (VALUE)")
        << "Expected a registered name after --shape to leave it without a value.";
    EXPECT_EQ(expect_refused(parser, { "program", colour, shape, "round" }),
        "--colour needs a value (COLOUR)")
        << "Expected a registered name after --colour to leave it without a value.";
}

TEST(CommandLineParser, TakesAnUnregisteredDoubleDashArgumentAsTheValue)
{
    const harness::CommandLineParser parser = make_colour_and_shape_parser();
    harness_tests::CommandLine command_line(
        { "program", std::string(COLOUR_OPTION), "--random-seed", "7" });
    harness::CommandLineParseResult result;
    std::string error;

    ASSERT_TRUE(parser.parse(command_line.count(), command_line.vector(), result, error)) << error;
    ASSERT_EQ(result.option_values.count(COLOUR_OPTION), 1u) << "Expected the colour claimed.";
    EXPECT_EQ(result.option_values.find(COLOUR_OPTION)->second, "--random-seed")
        << "Expected the unregistered name taken as the value.";
    EXPECT_EQ(unclaimed_words(result), (std::vector<std::string> { "program", "7" }))
        << "Expected only the 7 unclaimed.";
}

TEST(CommandLineParser, PassesAnEmptyValueToTheValidator)
{
    harness::CommandLineParser parser;
    harness::CommandLineOption option = make_plain_option(SHAPE_OPTION);
    option.validate = [](std::string_view value) -> std::optional<std::string> {
        if (value.empty()) {
            return std::string("the shape is empty");
        }
        return std::nullopt;
    };
    ASSERT_TRUE(parser.register_option(std::move(option)))
        << "Expected the shape option to register.";

    const std::string error = expect_refused(parser, { "program", std::string(SHAPE_OPTION), "" });

    EXPECT_EQ(error, "the shape is empty") << "Expected the validator to see the empty value.";
}

TEST(CommandLineParser, AcceptsAnEmptyValue)
{
    const harness::CommandLineParser parser = make_colour_parser();
    harness_tests::CommandLine command_line({ "program", std::string(COLOUR_OPTION), "" });
    harness::CommandLineParseResult result;
    std::string error;

    ASSERT_TRUE(parser.parse(command_line.count(), command_line.vector(), result, error)) << error;
    ASSERT_EQ(result.option_values.count(COLOUR_OPTION), 1u) << "Expected the colour claimed.";
    EXPECT_EQ(result.option_values.find(COLOUR_OPTION)->second, "") << "Expected an empty colour.";
}

TEST(CommandLineParser, RefusesARequiredOptionWithOnlyTheProgramName)
{
    const std::string error = expect_refused(make_colour_parser(), { "program" });

    EXPECT_EQ(error, "--colour is required") << "Unexpected refusal.";
}

TEST(CommandLineParser, ReturnsOnlyTheProgramNameWhenItIsAllThereIs)
{
    harness::CommandLineParser parser;
    ASSERT_TRUE(parser.register_option(make_colour_option(false)))
        << "Expected the colour option to register.";
    harness_tests::CommandLine command_line({ "program" });
    harness::CommandLineParseResult result;
    std::string error;

    ASSERT_TRUE(parser.parse(command_line.count(), command_line.vector(), result, error)) << error;
    EXPECT_TRUE(result.option_values.empty()) << "Expected no claimed values.";
    EXPECT_EQ(unclaimed_words(result), (std::vector<std::string> { "program" }))
        << "Expected the program name alone.";
}

TEST(CommandLineParser, RefusesAValueItsValidatorRefuses)
{
    const std::string error = expect_refused(make_colour_parser(),
        { "program", std::string(COLOUR_OPTION), std::string(REFUSED_COLOUR) });

    EXPECT_EQ(error, REFUSED_COLOUR_REASON) << "Expected the validator's reason.";
}

TEST(CommandLineParser, RefusesAMissingRequiredOption)
{
    const std::string error = expect_refused(make_colour_parser(), { "program", "-d", "lib" });

    EXPECT_EQ(error, "--colour is required") << "Unexpected refusal.";
}

TEST(CommandLineParser, RefusesADuplicateRegistration)
{
    harness::CommandLineParser parser = make_colour_parser();

    expect_registration_refused(parser, make_colour_option(false), DUPLICATE_PHRASE);
}

TEST(CommandLineParser, RefusesANameWithoutTheDoubleDashPrefix)
{
    harness::CommandLineParser parser;

    expect_registration_refused(parser, make_plain_option("colour"), NO_PREFIX_PHRASE);
    expect_registration_refused(parser, make_plain_option("-c"), NO_PREFIX_PHRASE);
    expect_registration_refused(parser, make_plain_option("--"), NO_PREFIX_PHRASE);
}

TEST(CommandLineParser, RefusesTheRandomSeedName)
{
    harness::CommandLineParser parser;

    expect_registration_refused(parser, make_plain_option("--random-seed"), RESERVED_PHRASE);
}

TEST(CommandLineParser, ReservesOnlyTheRandomSeed)
{
    ASSERT_EQ(harness::RESERVED_OPTION_NAMES.size(), 1u) << "Expected one reserved name.";
    EXPECT_EQ(harness::RESERVED_OPTION_NAMES[0], "--random-seed") << "Unexpected reserved name.";
}

TEST(CommandLineParser, GivesEachOptionsNameValueNameAndHelpInTheUsage)
{
    harness::CommandLineParser parser;
    ASSERT_TRUE(parser.register_option(make_colour_option(true)))
        << "Expected the colour option to register.";
    ASSERT_TRUE(parser.register_option(make_plain_option(SHAPE_OPTION)))
        << "Expected the shape option to register.";

    EXPECT_EQ(parser.usage(),
        "  --colour COLOUR  the colour to paint (required)\n"
        "  --shape VALUE  a plain option\n")
        << "Unexpected usage text.";
}

TEST(CommandLineParser, GivesAnEmptyUsageWithNoOptions)
{
    const harness::CommandLineParser parser;

    EXPECT_TRUE(parser.usage().empty()) << "Expected no usage lines.";
}
