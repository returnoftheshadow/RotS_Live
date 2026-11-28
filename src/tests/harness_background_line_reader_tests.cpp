#include "background_line_reader.h"
#include "reader_test_input.h"

#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <vector>

namespace {
// A line far longer than any request, to show the reader does not cut lines.
constexpr std::size_t LONG_LINE_LENGTH = 200;

// Reads all of input_text through a reader and returns the lines it received.
std::vector<std::string> lines_read_from(const std::string& input_text)
{
    harness_tests::ReaderTestInput input(input_text);
    harness::BackgroundLineReader reader(input.stream());
    EXPECT_TRUE(input.wait_until_closed(reader)) << "Expected the reader to reach end of input.";
    return reader.take_lines();
}

// A buffer that serves one line, then fails by throwing, which the stream reports as badbit.
class FailsAfterOneLineBuffer : public std::streambuf {
public:
    // The line served before the failure, newline included.
    static constexpr std::string_view SERVED_TEXT = "first\n";

protected:
    int_type underflow() override
    {
        if (served) {
            throw std::runtime_error("the test stream fails after its first line");
        }

        served = true;
        setg(served_text.data(), served_text.data(), served_text.data() + served_text.size());
        return traits_type::to_int_type(*gptr());
    }

private:
    // The characters the get area points into.
    std::string served_text { SERVED_TEXT };
    // True once SERVED_TEXT has been handed to the stream.
    bool served = false;
};
} // namespace

TEST(BackgroundLineReader, ReadsLinesInOrderWithoutTheirNewlines)
{
    const std::vector<std::string> lines = lines_read_from("first\nsecond\nthird\n");

    const std::vector<std::string> expected = { "first", "second", "third" };
    EXPECT_EQ(lines, expected) << "Expected every line, in order, without newlines.";
}

TEST(BackgroundLineReader, ReadsALastLineWithoutANewline)
{
    const std::vector<std::string> lines = lines_read_from("first\nlast");

    const std::vector<std::string> expected = { "first", "last" };
    EXPECT_EQ(lines, expected) << "Expected the unterminated last line.";
}

TEST(BackgroundLineReader, AddsNoLineAfterAFinalNewline)
{
    const std::vector<std::string> lines = lines_read_from("first\nlast\n");

    const std::vector<std::string> expected = { "first", "last" };
    EXPECT_EQ(lines, expected) << "Expected no empty line after the final newline.";
}

TEST(BackgroundLineReader, LeavesACarriageReturnForTheDispatcherToRemove)
{
    const std::vector<std::string> lines = lines_read_from("first\r\n");

    const std::vector<std::string> expected = { "first\r" };
    EXPECT_EQ(lines, expected) << "Expected the CR to stay on the line.";
}

TEST(BackgroundLineReader, ReadsAnEmptyLineAsAnEmptyString)
{
    const std::vector<std::string> lines = lines_read_from("\nnext\n");

    const std::vector<std::string> expected = { "", "next" };
    EXPECT_EQ(lines, expected) << "Expected the empty line to be delivered.";
}

TEST(BackgroundLineReader, DeliversALongLineWhole)
{
    const std::string long_line(LONG_LINE_LENGTH, 'a');
    const std::vector<std::string> lines = lines_read_from(long_line + "\nnext\n");

    const std::vector<std::string> expected = { long_line, "next" };
    EXPECT_EQ(lines, expected) << "Expected the long line whole, then the next line.";
}

TEST(BackgroundLineReader, ReportsInputClosedAtTheEndOfInput)
{
    harness_tests::ReaderTestInput input("only\n");
    harness::BackgroundLineReader reader(input.stream());

    EXPECT_TRUE(input.wait_until_closed(reader)) << "Expected input closed after its end.";
}

TEST(BackgroundLineReader, ReportsInputClosedAtTheEndOfEmptyInput)
{
    harness_tests::ReaderTestInput input("");
    harness::BackgroundLineReader reader(input.stream());

    ASSERT_TRUE(input.wait_until_closed(reader)) << "Expected input closed on empty input.";
    EXPECT_TRUE(reader.take_lines().empty()) << "Expected no line from empty input.";
}

TEST(BackgroundLineReader, TakingLinesEmptiesTheQueue)
{
    harness_tests::ReaderTestInput input("only\n");
    harness::BackgroundLineReader reader(input.stream());
    ASSERT_TRUE(input.wait_until_closed(reader)) << "Expected the reader to reach end of input.";

    EXPECT_EQ(reader.take_lines().size(), 1u) << "Expected the line on the first take.";
    EXPECT_TRUE(reader.take_lines().empty()) << "Expected nothing on the second take.";
}

TEST(BackgroundLineReader, KeepsTheLinesReadAndReportsInputClosedWhenTheStreamFails)
{
    harness_tests::ReaderTestInput input(std::make_unique<FailsAfterOneLineBuffer>());
    harness::BackgroundLineReader reader(input.stream());

    ASSERT_TRUE(input.wait_until_closed(reader)) << "Expected input closed after the failure.";
    const std::vector<std::string> expected = { "first" };
    EXPECT_EQ(reader.take_lines(), expected) << "Expected the line read before the failure.";
}
