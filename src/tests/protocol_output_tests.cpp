#include "../protocol.h"
#include "../structs.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>

namespace {

// The ANSI escape that clears every colour.
constexpr char CLEAN[] = "\033[0;00m";

// The MXP open and close sequences ProtocolOutput writes around a "\t(" ... "\t)" link.
constexpr char LINK_START[] = "\033[1z<send>\033[7z";
constexpr char LINK_STOP[] = "\033[1z</send>\033[7z";

// What a client's MSDP variables say it can show.
enum class ColourMode {
    NONE,
    ANSI,
    XTERM_256,
};

// A descriptor that owns a fresh protocol from ProtocolCreate and destroys it at scope end.
class OutputDescriptor {
public:
    explicit OutputDescriptor(ColourMode colour_mode = ColourMode::ANSI)
    {
        descriptor.pProtocol = ProtocolCreate();
        set_colour_mode(colour_mode);
    }

    ~OutputDescriptor() { ProtocolDestroy(descriptor.pProtocol); }

    OutputDescriptor(const OutputDescriptor&) = delete;
    OutputDescriptor& operator=(const OutputDescriptor&) = delete;

    void set_colour_mode(ColourMode colour_mode)
    {
        set_variable(eMSDP_ANSI_COLORS, colour_mode != ColourMode::NONE);
        set_variable(eMSDP_XTERM_256_COLORS, colour_mode == ColourMode::XTERM_256);
    }

    void set_variable(int msdp_variable, bool enabled)
    {
        descriptor.pProtocol->variables[msdp_variable]->value_number = static_cast<int>(enabled);
    }

    // Replaces the client's reported MXP version with a heap copy ProtocolDestroy frees.
    void set_mxp_version(const char* version)
    {
        const std::size_t length = std::strlen(version);
        char* const copy = static_cast<char*>(std::malloc(length + 1));
        std::memcpy(copy, version, length + 1);
        std::free(descriptor.pProtocol->mxp_version);
        descriptor.pProtocol->mxp_version = copy;
    }

    // The descriptor passed to the functions under test; its protocol is owned here.
    descriptor_data descriptor {};
};

// Runs ProtocolOutput over the whole of text with no length limit.
std::string render(const OutputDescriptor& output_descriptor, const char* text)
{
    int length = 0;
    return ProtocolOutput(&output_descriptor.descriptor, text, &length);
}

// Builds a string from raw byte values, so expected UTF-8 is written without hex-escape runs.
std::string bytes(std::initializer_list<int> values)
{
    std::string result;
    for (const int value : values) {
        result.push_back(static_cast<char>(value));
    }
    return result;
}

// One predefined colour letter and the exact escape each colour mode gives it.
struct ColourLetterCase {
    // The letter after the tab.
    char letter;
    // The escape under plain ANSI.
    const char* ansi;
    // The escape under xterm-256.
    const char* xterm;
};

constexpr ColourLetterCase COLOUR_LETTERS[] = {
    { 'r', "\033[0;31m", "\033[38;5;088m" },
    { 'R', "\033[1;31m", "\033[38;5;196m" },
    { 'g', "\033[0;32m", "\033[38;5;028m" },
    { 'G', "\033[1;32m", "\033[38;5;046m" },
    { 'y', "\033[0;33m", "\033[38;5;100m" },
    { 'Y', "\033[1;33m", "\033[38;5;226m" },
    { 'b', "\033[0;34m", "\033[38;5;018m" },
    { 'B', "\033[1;34m", "\033[38;5;021m" },
    { 'm', "\033[0;35m", "\033[38;5;090m" },
    { 'M', "\033[1;35m", "\033[38;5;201m" },
    { 'c', "\033[0;36m", "\033[38;5;030m" },
    { 'C', "\033[1;36m", "\033[38;5;051m" },
    { 'w', "\033[0;37m", "\033[38;5;102m" },
    { 'W', "\033[1;37m", "\033[38;5;231m" },
    { 'a', "\033[1;34m", "\033[38;5;026m" },
    { 'A', "\033[1;34m", "\033[38;5;033m" },
    { 'j', "\033[1;32m", "\033[38;5;035m" },
    { 'J', "\033[1;32m", "\033[38;5;048m" },
    { 'l', "\033[1;32m", "\033[38;5;076m" },
    { 'L', "\033[1;32m", "\033[38;5;118m" },
    { 'o', "\033[1;31m", "\033[38;5;208m" },
    { 'O', "\033[1;31m", "\033[38;5;214m" },
    { 'p', "\033[1;31m", "\033[38;5;125m" },
    { 'P', "\033[1;31m", "\033[38;5;198m" },
    { 't', "\033[0;31m", "\033[38;5;094m" },
    { 'T', "\033[1;31m", "\033[38;5;137m" },
    { 'v', "\033[1;34m", "\033[38;5;056m" },
    { 'V', "\033[1;34m", "\033[38;5;093m" },
};

// One four-character RGB argument and the exact escape ColourRGB returns for it.
struct RgbCase {
    // The argument, such as "F500".
    const char* rgb;
    // The escape returned.
    const char* expected;
};

// Every branch of the best-fit ANSI choice, foreground dark and bold and background.
constexpr RgbCase ANSI_BEST_FIT_CASES[] = {
    { "F000", "\033[0;30m" },
    { "F111", "\033[1;30m" },
    { "B000", "\033[1;40m" },
    { "B111", "\033[1;40m" },
    { "F222", "\033[0;37m" },
    { "F333", "\033[0;37m" },
    { "F444", "\033[1;37m" },
    { "B555", "\033[1;47m" },
    { "F200", "\033[0;31m" },
    { "F300", "\033[1;31m" },
    { "F211", "\033[0;31m" },
    { "B300", "\033[1;41m" },
    { "F220", "\033[0;33m" },
    { "F330", "\033[1;33m" },
    { "F221", "\033[0;33m" },
    { "B330", "\033[1;43m" },
    { "F202", "\033[0;35m" },
    { "F303", "\033[1;35m" },
    { "F212", "\033[0;35m" },
    { "B303", "\033[1;45m" },
    { "F020", "\033[0;32m" },
    { "F030", "\033[1;32m" },
    { "F120", "\033[0;32m" },
    { "B030", "\033[1;42m" },
    { "F022", "\033[0;36m" },
    { "F033", "\033[1;36m" },
    { "F122", "\033[0;36m" },
    { "B033", "\033[1;46m" },
    { "F002", "\033[0;34m" },
    { "F003", "\033[1;34m" },
    { "F102", "\033[0;34m" },
    { "B003", "\033[1;44m" },
    { "f500", "\033[1;31m" },
    { "b500", "\033[1;41m" },
};

// The xterm-256 palette index is 16 + 36 * red + 6 * green + blue, always written as three digits.
constexpr RgbCase XTERM_CASES[] = {
    { "F000", "\033[38;5;016m" },
    { "F001", "\033[38;5;017m" },
    { "F010", "\033[38;5;022m" },
    { "F100", "\033[38;5;052m" },
    { "F555", "\033[38;5;231m" },
    { "B000", "\033[48;5;016m" },
    { "B555", "\033[48;5;231m" },
    { "b530", "\033[48;5;214m" },
    { "f135", "\033[38;5;075m" },
};

} // namespace

TEST(ProtocolOutput, ReturnsTheDataPointerUnchangedForANullDescriptor)
{
    const char text[] = "\trred";
    int length = 99;

    EXPECT_EQ(ProtocolOutput(nullptr, text, &length), text);
    EXPECT_EQ(length, 99) << "The length is left alone when there is no descriptor.";
}

TEST(ProtocolOutput, ReturnsTheDataPointerUnchangedForADescriptorWithoutAProtocol)
{
    descriptor_data descriptor {};
    const char text[] = "\trred";
    int length = 99;

    EXPECT_EQ(ProtocolOutput(&descriptor, text, &length), text);
    EXPECT_EQ(length, 99) << "The length is left alone when there is no protocol.";
}

TEST(ProtocolOutput, ReturnsNullForNullData)
{
    OutputDescriptor output_descriptor;
    int length = 99;

    EXPECT_EQ(ProtocolOutput(&output_descriptor.descriptor, nullptr, &length), nullptr);
    EXPECT_EQ(length, 99) << "The length is left alone for null data.";
}

TEST(ProtocolOutput, CopiesPlainTextAndWritesBackItsLength)
{
    OutputDescriptor output_descriptor;
    int length = 0;

    const std::string result = ProtocolOutput(&output_descriptor.descriptor, "Hello, world.", &length);

    EXPECT_EQ(result, "Hello, world.");
    EXPECT_EQ(length, 13);
}

TEST(ProtocolOutput, ReturnsTheSameStaticBufferOnEveryCall)
{
    OutputDescriptor output_descriptor;
    int first_length = 0;
    int second_length = 0;

    const char* const first = ProtocolOutput(&output_descriptor.descriptor, "first", &first_length);
    const char* const second = ProtocolOutput(&output_descriptor.descriptor, "second", &second_length);

    EXPECT_EQ(first, second) << "Each call overwrites the previous call's result.";
    EXPECT_STREQ(first, "second");
}

TEST(ProtocolOutput, WritesEachColourLetterAsPlainAnsiWhenXtermIsOff)
{
    OutputDescriptor output_descriptor(ColourMode::ANSI);

    for (const ColourLetterCase& colour_case : COLOUR_LETTERS) {
        const char text[] = { '\t', colour_case.letter, 'x', '\0' };
        EXPECT_EQ(render(output_descriptor, text), std::string(colour_case.ansi) + "x")
            << "Colour letter '" << colour_case.letter << "' under plain ANSI.";
    }
}

TEST(ProtocolOutput, WritesEachColourLetterAsAnXtermPaletteIndexWhenXtermIsOn)
{
    OutputDescriptor output_descriptor(ColourMode::XTERM_256);

    for (const ColourLetterCase& colour_case : COLOUR_LETTERS) {
        const char text[] = { '\t', colour_case.letter, 'x', '\0' };
        EXPECT_EQ(render(output_descriptor, text), std::string(colour_case.xterm) + "x")
            << "Colour letter '" << colour_case.letter << "' under xterm-256.";
    }
}

TEST(ProtocolOutput, DropsEachColourLetterWhenTheClientHasNoColour)
{
    OutputDescriptor output_descriptor(ColourMode::NONE);

    for (const ColourLetterCase& colour_case : COLOUR_LETTERS) {
        const char text[] = { '\t', colour_case.letter, 'x', '\0' };
        EXPECT_EQ(render(output_descriptor, text), "x")
            << "Colour letter '" << colour_case.letter << "' with colour off.";
    }
}

TEST(ProtocolOutput, WritesTheClearCodeForTabNInEveryColourMode)
{
    for (const ColourMode colour_mode : { ColourMode::NONE, ColourMode::ANSI, ColourMode::XTERM_256 }) {
        OutputDescriptor output_descriptor(colour_mode);
        EXPECT_EQ(render(output_descriptor, "a\tnb"), std::string("a") + CLEAN + "b")
            << "Colour mode " << static_cast<int>(colour_mode)
            << ": \\tn sends the clear code even to a client with colour off.";
    }
}

TEST(ProtocolOutput, WritesOneTabForTwoTabsInARow)
{
    OutputDescriptor output_descriptor;

    EXPECT_EQ(render(output_descriptor, "a\t\tb"), "a\tb");
}

TEST(ProtocolOutput, EatsAnUnknownCodeLetter)
{
    OutputDescriptor output_descriptor;

    EXPECT_EQ(render(output_descriptor, "a\tzb\t+c\t-d\t>e"), "abcde");
}

TEST(ProtocolOutput, StopsAtATrailingTab)
{
    OutputDescriptor output_descriptor;
    // The byte after the terminator is read before the loop sees the stop flag.
    const char text[] = "abc\t\0tail";
    int length = 0;

    const std::string result = ProtocolOutput(&output_descriptor.descriptor, text, &length);

    EXPECT_EQ(result, "abc");
    EXPECT_EQ(length, 3);
}

TEST(ProtocolOutput, LeavesCaretColourCodesAsTextBecauseColourCharIsNotDefined)
{
#ifdef COLOUR_CHAR
    constexpr bool COLOUR_CHAR_DEFINED = true;
#else
    constexpr bool COLOUR_CHAR_DEFINED = false;
#endif
    OutputDescriptor output_descriptor;

    EXPECT_FALSE(COLOUR_CHAR_DEFINED) << "Traditional ^ colour codes are compiled out.";
    EXPECT_EQ(render(output_descriptor, "^rred^^ and ^n"), "^rred^^ and ^n");
}

TEST(ProtocolOutput, WritesAValidForegroundRgbCodeAsPlainAnsi)
{
    OutputDescriptor output_descriptor(ColourMode::ANSI);

    EXPECT_EQ(render(output_descriptor, "\t[F010]x"), "\033[0;32mx");
    EXPECT_EQ(render(output_descriptor, "\t[f500]x"), "\033[1;31mx");
}

TEST(ProtocolOutput, WritesAValidBackgroundRgbCodeAsPlainAnsi)
{
    OutputDescriptor output_descriptor(ColourMode::ANSI);

    EXPECT_EQ(render(output_descriptor, "\t[B530]x"), "\033[1;41mx");
    EXPECT_EQ(render(output_descriptor, "\t[b005]x"), "\033[1;44mx");
}

TEST(ProtocolOutput, WritesValidRgbCodesAsXtermPaletteIndexes)
{
    OutputDescriptor output_descriptor(ColourMode::XTERM_256);

    EXPECT_EQ(render(output_descriptor, "\t[F010]x"), "\033[38;5;022mx");
    EXPECT_EQ(render(output_descriptor, "\t[B530]x"), "\033[48;5;214mx");
}

TEST(ProtocolOutput, DropsAValidRgbCodeWhenTheClientHasNoColour)
{
    OutputDescriptor output_descriptor(ColourMode::NONE);

    EXPECT_EQ(render(output_descriptor, "\t[F500]x"), "x");
}

TEST(ProtocolOutput, DropsAnRgbCodeWithADigitOutsideZeroToFive)
{
    OutputDescriptor output_descriptor(ColourMode::XTERM_256);

    EXPECT_EQ(render(output_descriptor, "\t[F600]x"), "x");
    EXPECT_EQ(render(output_descriptor, "\t[B060]x"), "x");
}

TEST(ProtocolOutput, DropsAnRgbCodeShorterThanThreeDigits)
{
    OutputDescriptor output_descriptor(ColourMode::XTERM_256);

    EXPECT_EQ(render(output_descriptor, "\t[F12]x"), "x");
    EXPECT_EQ(render(output_descriptor, "\t[F]x"), "x");
}

TEST(ProtocolOutput, DropsAnOverlongRgbCodeButPrintsItsClosingBracket)
{
    OutputDescriptor output_descriptor(ColourMode::XTERM_256);

    // The fourth digit is skipped and parsing resumes at the ']'.
    EXPECT_EQ(render(output_descriptor, "\t[F1234]x"), "]x");
    EXPECT_EQ(render(output_descriptor, "\t[B1234]x"), "]x");
}

TEST(ProtocolOutput, ReadsPastTheTerminatorAfterAnUnterminatedRgbCode)
{
    OutputDescriptor output_descriptor(ColourMode::XTERM_256);
    const char text[] = "a\t[F12\0tail";

    EXPECT_EQ(render(output_descriptor, text), "atail")
        << "An unterminated RGB code does not stop the loop at the string's end.";
}

TEST(ProtocolOutput, DropsTheLetterAfterAnUnknownBracketCodeButPrintsTheRest)
{
    OutputDescriptor output_descriptor;

    EXPECT_EQ(render(output_descriptor, "\t[Z]x"), "]x");
}

TEST(ProtocolOutput, WritesUtf8ForAUnicodeCodeWhenTheClientSupportsIt)
{
    OutputDescriptor output_descriptor;
    output_descriptor.set_variable(eMSDP_UTF_8, true);

    EXPECT_EQ(render(output_descriptor, "Boat: \t[U9973/B]."), "Boat: " + bytes({ 0xE2, 0x9B, 0xB5 }) + ".");
    EXPECT_EQ(render(output_descriptor, "\t[u233/e]"), bytes({ 0xC3, 0xA9 }));
}

TEST(ProtocolOutput, WritesTheFallbackForAUnicodeCodeWhenTheClientLacksUtf8)
{
    OutputDescriptor output_descriptor;

    EXPECT_EQ(render(output_descriptor, "Boat: \t[U9973/B]."), "Boat: B.");
    EXPECT_EQ(render(output_descriptor, "\t[u9814/C]"), "C");
}

TEST(ProtocolOutput, WritesAFallbackOfExactlySevenCharacters)
{
    OutputDescriptor output_descriptor;

    EXPECT_EQ(render(output_descriptor, "\t[U9814/ABCDEFG]x"), "ABCDEFGx");
}

TEST(ProtocolOutput, DropsTheWholeUnicodeCodeWhenItsFallbackIsLongerThanSeven)
{
    OutputDescriptor output_descriptor;

    EXPECT_EQ(render(output_descriptor, "\t[U9814/ABCDEFGH]x"), "x");

    output_descriptor.set_variable(eMSDP_UTF_8, true);
    EXPECT_EQ(render(output_descriptor, "\t[U9814/ABCDEFGH]x"), "x")
        << "An overlong fallback drops the character even for a UTF-8 client.";
}

TEST(ProtocolOutput, WritesNothingForAUnicodeCodeWithoutAFallbackWhenTheClientLacksUtf8)
{
    OutputDescriptor output_descriptor;

    EXPECT_EQ(render(output_descriptor, "\t[U65]x"), "x");

    output_descriptor.set_variable(eMSDP_UTF_8, true);
    EXPECT_EQ(render(output_descriptor, "\t[U65]x"), "Ax");
}

TEST(ProtocolOutput, TreatsUnicodeTextWithoutDigitsAsTheFallbackForCodePointZero)
{
    OutputDescriptor output_descriptor;

    EXPECT_EQ(render(output_descriptor, "\t[Uabc]x"), "abcx");

    output_descriptor.set_variable(eMSDP_UTF_8, true);
    EXPECT_EQ(render(output_descriptor, "\t[Uabc]x"), "x")
        << "Code point zero is an empty string to a UTF-8 client.";
}

TEST(ProtocolOutput, StopsAtAnUnterminatedUnicodeCode)
{
    OutputDescriptor output_descriptor;
    // The byte after the terminator is read before the loop sees the stop flag.
    const char text[] = "a\t[U65/B\0tail";
    int length = 0;

    const std::string result = ProtocolOutput(&output_descriptor.descriptor, text, &length);

    EXPECT_EQ(result, "a");
    EXPECT_EQ(length, 1);
}

TEST(ProtocolOutput, WritesMxpLinkTagsWhenTheClientSupportsMxp)
{
    OutputDescriptor output_descriptor;
    output_descriptor.set_variable(eMSDP_MXP, true);

    EXPECT_EQ(render(output_descriptor, "Walk \t(north\t)."),
        std::string("Walk ") + LINK_START + "north" + LINK_STOP + ".");
}

TEST(ProtocolOutput, StripsMxpLinkTagsWhenTheClientLacksMxp)
{
    OutputDescriptor output_descriptor;

    EXPECT_EQ(render(output_descriptor, "Walk \t(north\t)."), "Walk north.");
}

TEST(ProtocolOutput, WritesExplicitMxpTagsWhenTheClientSupportsMxp)
{
    OutputDescriptor output_descriptor;
    output_descriptor.set_variable(eMSDP_MXP, true);

    EXPECT_EQ(render(output_descriptor, "a \t<send href=\"buy pie\">pie\t</send> > b"),
        "a \033[1z<send href=\"buy pie\">\033[7zpie\033[1z</send>\033[7z > b");
}

TEST(ProtocolOutput, StripsExplicitMxpTagsWhenTheClientLacksMxp)
{
    OutputDescriptor output_descriptor;

    EXPECT_EQ(render(output_descriptor, "a \t<send href=\"buy pie\">pie\t</send> > b"), "a pie > b");
}

TEST(ProtocolOutput, ReadsPastTheTerminatorAfterAnUnclosedStrippedMxpTag)
{
    OutputDescriptor output_descriptor;
    const char text[] = "a\t<send\0tail";

    EXPECT_EQ(render(output_descriptor, text), "atail")
        << "An unclosed tag does not stop the loop at the string's end.";
}

TEST(ProtocolOutput, BlocksMxpTagsAfterAVersionCheckWhenTheVersionIsUnknown)
{
    OutputDescriptor output_descriptor;
    output_descriptor.set_variable(eMSDP_MXP, true);

    EXPECT_EQ(render(output_descriptor, "\t[X1.0]\t(north\t)"), "north");
    EXPECT_FALSE(output_descriptor.descriptor.pProtocol->mxp_blocked)
        << "The closing \\t) lifts the block.";

    EXPECT_EQ(render(output_descriptor, "\t[X1.0]\t<b>bold"), "bold");
    EXPECT_FALSE(output_descriptor.descriptor.pProtocol->mxp_blocked)
        << "A blocked \\t< tag lifts the block.";
}

TEST(ProtocolOutput, KeepsTheMxpBlockAcrossCallsUntilALinkCloses)
{
    OutputDescriptor output_descriptor;
    output_descriptor.set_variable(eMSDP_MXP, true);

    EXPECT_EQ(render(output_descriptor, "\t[X1.0]x"), "x");
    EXPECT_TRUE(output_descriptor.descriptor.pProtocol->mxp_blocked);
    EXPECT_EQ(render(output_descriptor, "\t(north"), "north");
}

TEST(ProtocolOutput, BlocksMxpTagsWhenTheClientVersionIsLower)
{
    OutputDescriptor output_descriptor;
    output_descriptor.set_variable(eMSDP_MXP, true);
    output_descriptor.set_mxp_version("1.0");

    EXPECT_EQ(render(output_descriptor, "\t[X1.1]\t(north\t)"), "north");
}

TEST(ProtocolOutput, AllowsMxpTagsWhenTheClientVersionIsHighEnough)
{
    OutputDescriptor output_descriptor;
    output_descriptor.set_variable(eMSDP_MXP, true);
    output_descriptor.set_mxp_version("1.0");
    output_descriptor.descriptor.pProtocol->mxp_blocked = true;

    EXPECT_EQ(render(output_descriptor, "\t[X0.9]\t(north\t)"),
        std::string(LINK_START) + "north" + LINK_STOP);
    EXPECT_EQ(render(output_descriptor, "\t[x1.0]\t(north\t)"),
        std::string(LINK_START) + "north" + LINK_STOP);
}

TEST(ProtocolOutput, IgnoresAnOverlongMxpVersionCheck)
{
    OutputDescriptor output_descriptor;
    output_descriptor.set_variable(eMSDP_MXP, true);

    EXPECT_EQ(render(output_descriptor, "\t[X12345678]\t(north\t)x"),
        std::string(LINK_START) + "north" + LINK_STOP + "x");
}

TEST(ProtocolOutput, StopsAtAnUnterminatedMxpVersionCheck)
{
    OutputDescriptor output_descriptor;
    output_descriptor.set_variable(eMSDP_MXP, true);
    // The byte after the terminator is read before the loop sees the stop flag.
    const char text[] = "a\t[X1.0\0tail";

    EXPECT_EQ(render(output_descriptor, text), "a");
    EXPECT_FALSE(output_descriptor.descriptor.pProtocol->mxp_blocked);
}

TEST(ProtocolOutput, WritesADoubleBangForTabBang)
{
    OutputDescriptor output_descriptor;

    EXPECT_EQ(render(output_descriptor, "\t!SOUND(rain.wav)"), "!!SOUND(rain.wav)");
}

TEST(ProtocolOutput, DefusesASoundTriggerInTextWhenTheClientSupportsMsp)
{
    OutputDescriptor output_descriptor;
    output_descriptor.descriptor.pProtocol->msp_enabled = true;

    EXPECT_EQ(render(output_descriptor, "say !!SOUND(rain.wav)"), "say !?SOUND(rain.wav)");
    EXPECT_EQ(render(output_descriptor, "!!sound(rain.wav)"), "!?sound(rain.wav)");
    EXPECT_EQ(render(output_descriptor, "!!MUSIC(song.mid)"), "!!MUSIC(song.mid)");
    EXPECT_EQ(render(output_descriptor, "\t!!SOUND(rain.wav)"), "!!?SOUND(rain.wav)");
}

TEST(ProtocolOutput, DefusesASoundTriggerInTextWhenTheClientHasSoundOn)
{
    OutputDescriptor output_descriptor;
    output_descriptor.set_variable(eMSDP_SOUND, true);

    EXPECT_EQ(render(output_descriptor, "!!SOUND(rain.wav)"), "!?SOUND(rain.wav)");
}

TEST(ProtocolOutput, LeavesSoundTriggersInTextWhenTheClientHasNoSound)
{
    OutputDescriptor output_descriptor;

    EXPECT_EQ(render(output_descriptor, "!!SOUND(rain.wav)"), "!!SOUND(rain.wav)");
}

TEST(ProtocolOutput, ReadsOnlyThePositiveLengthOfInput)
{
    OutputDescriptor output_descriptor;
    int length = 3;

    const std::string result = ProtocolOutput(&output_descriptor.descriptor, "abcdef", &length);

    EXPECT_EQ(result, "abc");
    EXPECT_EQ(length, 3);
}

TEST(ProtocolOutput, FinishesACodeThatStartsInsideThePositiveLength)
{
    OutputDescriptor output_descriptor(ColourMode::ANSI);
    int length = 3;

    const std::string result = ProtocolOutput(&output_descriptor.descriptor, "ab\trcd", &length);

    EXPECT_EQ(result, "ab\033[0;31m") << "The code letter after the limit is still read.";
    EXPECT_EQ(length, 9);
}

TEST(ProtocolOutput, ReadsTheWholeStringForAZeroOrNegativeLength)
{
    OutputDescriptor output_descriptor(ColourMode::ANSI);

    for (const int input_length : { 0, -1, -50 }) {
        int length = input_length;
        const std::string result = ProtocolOutput(&output_descriptor.descriptor, "ab\trcd", &length);
        EXPECT_EQ(result, "ab\033[0;31mcd") << "Input length " << input_length;
        EXPECT_EQ(length, 11) << "Input length " << input_length;
    }
}

TEST(ProtocolOutput, WritesBackTheOutputLengthIncludingEscapeBytes)
{
    OutputDescriptor output_descriptor(ColourMode::XTERM_256);
    int length = 0;

    const std::string result = ProtocolOutput(&output_descriptor.descriptor, "\tRhi\tn", &length);

    EXPECT_EQ(result, std::string("\033[38;5;196mhi") + CLEAN);
    EXPECT_EQ(length, 20);
}

TEST(ProtocolOutput, KeepsOutputOneByteShorterThanTheBuffer)
{
    OutputDescriptor output_descriptor;
    const std::string text(MAX_OUTPUT_BUFFER - 1, 'a');
    int length = 0;

    const std::string result = ProtocolOutput(&output_descriptor.descriptor, text.c_str(), &length);

    EXPECT_EQ(result, text);
    EXPECT_EQ(length, MAX_OUTPUT_BUFFER - 1);
}

TEST(ProtocolOutput, SendsNothingWhenOutputFillsTheBufferExactly)
{
    OutputDescriptor output_descriptor;
    const std::string text(MAX_OUTPUT_BUFFER, 'a');
    int length = 0;

    const std::string result = ProtocolOutput(&output_descriptor.descriptor, text.c_str(), &length);

    EXPECT_EQ(result, "") << "Output of exactly MAX_OUTPUT_BUFFER bytes is dropped as an overflow.";
    EXPECT_EQ(length, 0);
}

TEST(ProtocolOutput, SendsNothingWhenAColourCodeOverflowsTheBuffer)
{
    OutputDescriptor output_descriptor(ColourMode::ANSI);
    const std::string text = std::string(MAX_OUTPUT_BUFFER - 2, 'a') + "\tr";
    int length = 0;

    const std::string result = ProtocolOutput(&output_descriptor.descriptor, text.c_str(), &length);

    EXPECT_EQ(result, "");
    EXPECT_EQ(length, 0);
}

TEST(ProtocolOutput, SendsNothingWhenAnMxpTagCloseOverflowsTheBuffer)
{
    OutputDescriptor output_descriptor;
    output_descriptor.set_variable(eMSDP_MXP, true);
    // Five bytes of tag opening plus these bytes leave room for two of the five closing bytes.
    const std::string text = "\t<" + std::string(MAX_OUTPUT_BUFFER - 7, 'a') + ">";
    int length = 0;

    const std::string result = ProtocolOutput(&output_descriptor.descriptor, text.c_str(), &length);

    EXPECT_EQ(result, "");
    EXPECT_EQ(length, 0);
}

TEST(ColourRGB, ReturnsAnEmptyStringForANullDescriptor)
{
    EXPECT_STREQ(ColourRGB(nullptr, "F500"), "");
}

TEST(ColourRGB, ReturnsAnEmptyStringWhenTheClientHasNoColour)
{
    OutputDescriptor output_descriptor(ColourMode::NONE);

    EXPECT_STREQ(ColourRGB(&output_descriptor.descriptor, "F500"), "");
    EXPECT_STREQ(ColourRGB(&output_descriptor.descriptor, "F600"), "")
        << "Even an invalid colour sends nothing, not the clear code.";
}

TEST(ColourRGB, ReturnsTheClearCodeForAnInvalidColour)
{
    OutputDescriptor output_descriptor(ColourMode::XTERM_256);

    for (const char* const rgb : { "F600", "F/00", "F0:0", "F00/", "X500", "F50", "" }) {
        EXPECT_STREQ(ColourRGB(&output_descriptor.descriptor, rgb), CLEAN) << "Argument \"" << rgb << "\"";
    }
    EXPECT_STREQ(ColourRGB(&output_descriptor.descriptor, nullptr), CLEAN) << "Null argument";
}

TEST(ColourRGB, IgnoresCharactersAfterTheFourthWhenValidating)
{
    OutputDescriptor output_descriptor(ColourMode::ANSI);

    EXPECT_STREQ(ColourRGB(&output_descriptor.descriptor, "F500junk"), "\033[1;31m");
}

TEST(ColourRGB, ReturnsTheBestFitAnsiColourWhenXtermIsOff)
{
    OutputDescriptor output_descriptor(ColourMode::ANSI);

    for (const RgbCase& rgb_case : ANSI_BEST_FIT_CASES) {
        EXPECT_STREQ(ColourRGB(&output_descriptor.descriptor, rgb_case.rgb), rgb_case.expected)
            << "Argument \"" << rgb_case.rgb << "\"";
    }
}

TEST(ColourRGB, ReturnsAnXtermPaletteEscapeWhenXtermIsOn)
{
    OutputDescriptor output_descriptor(ColourMode::XTERM_256);

    for (const RgbCase& rgb_case : XTERM_CASES) {
        EXPECT_STREQ(ColourRGB(&output_descriptor.descriptor, rgb_case.rgb), rgb_case.expected)
            << "Argument \"" << rgb_case.rgb << "\"";
    }
}

TEST(ColourRGB, ReusesOneStaticBufferForXtermEscapes)
{
    OutputDescriptor output_descriptor(ColourMode::XTERM_256);

    const char* const first = ColourRGB(&output_descriptor.descriptor, "F500");
    const char* const second = ColourRGB(&output_descriptor.descriptor, "F005");

    EXPECT_EQ(first, second) << "A second xterm colour overwrites the first.";
    EXPECT_STREQ(first, "\033[38;5;021m");
}

TEST(UnicodeGet, EncodesEachUtf8LengthBand)
{
    // One code point and the bytes UnicodeGet returns for it.
    struct UnicodeCase {
        // The code point passed in.
        int value;
        // The bytes returned.
        std::string expected;
    };
    const UnicodeCase cases[] = {
        { 0x41, "A" },
        { 0x7F, bytes({ 0x7F }) },
        { 0x80, bytes({ 0xC2, 0x80 }) },
        { 0xE9, bytes({ 0xC3, 0xA9 }) },
        { 0x7FF, bytes({ 0xDF, 0xBF }) },
        { 0x800, bytes({ 0xE0, 0xA0, 0x80 }) },
        { 0x2654, bytes({ 0xE2, 0x99, 0x94 }) },
        { 0xFFFF, bytes({ 0xEF, 0xBF, 0xBF }) },
        { 0x10000, bytes({ 0xF0, 0x90, 0x80, 0x80 }) },
        { 0x10FFFF, bytes({ 0xF4, 0x8F, 0xBF, 0xBF }) },
        { 0x1FFFFF, bytes({ 0xF7, 0xBF, 0xBF, 0xBF }) },
    };

    for (const UnicodeCase& unicode_case : cases) {
        EXPECT_EQ(std::string(UnicodeGet(unicode_case.value)), unicode_case.expected)
            << "Code point " << unicode_case.value;
    }
}

TEST(UnicodeGet, EncodesSurrogatesAndValuesAboveTheUnicodeRange)
{
    EXPECT_EQ(std::string(UnicodeGet(0xD800)), bytes({ 0xED, 0xA0, 0x80 }));
    EXPECT_EQ(std::string(UnicodeGet(0x110000)), bytes({ 0xF4, 0x90, 0x80, 0x80 }));
}

TEST(UnicodeGet, ReturnsAnEmptyStringForCodePointZeroAndValuesFromTwoToTheTwentyFirst)
{
    EXPECT_STREQ(UnicodeGet(0), "");
    EXPECT_STREQ(UnicodeGet(0x200000), "");
    EXPECT_STREQ(UnicodeGet(0x7FFFFFFF), "");
}

TEST(UnicodeGet, WritesTheLowByteOfANegativeValue)
{
    EXPECT_EQ(std::string(UnicodeGet(-1)), bytes({ 0xFF }));
    EXPECT_EQ(std::string(UnicodeGet(-200)), "8");
}

TEST(UnicodeGet, ReturnsTheSameStaticBufferOnEveryCall)
{
    const char* const first = UnicodeGet(0x41);
    const char* const second = UnicodeGet(0x42);

    EXPECT_EQ(first, second);
    EXPECT_STREQ(first, "B");
}

TEST(UnicodeAdd, AppendsEachUtf8LengthBandWithoutATerminator)
{
    // One code point and the bytes UnicodeAdd appends for it.
    struct UnicodeCase {
        // The code point passed in.
        int value;
        // The bytes appended.
        std::string expected;
    };
    const UnicodeCase cases[] = {
        { 0x41, "A" },
        { 0x7FF, bytes({ 0xDF, 0xBF }) },
        { 0xFFFF, bytes({ 0xEF, 0xBF, 0xBF }) },
        { 0x1FFFFF, bytes({ 0xF7, 0xBF, 0xBF, 0xBF }) },
        { 0x200000, "" },
    };

    for (const UnicodeCase& unicode_case : cases) {
        char buffer[8] = { '#', '#', '#', '#', '#', '#', '#', '\0' };
        char* cursor = buffer;

        UnicodeAdd(&cursor, unicode_case.value);

        const std::size_t written = static_cast<std::size_t>(cursor - buffer);
        EXPECT_EQ(std::string(buffer, written), unicode_case.expected) << "Code point " << unicode_case.value;
        EXPECT_EQ(std::string(cursor), std::string(7 - written, '#'))
            << "Code point " << unicode_case.value << ": bytes after the encoding are untouched.";
    }
}

TEST(UnicodeAdd, AppendsAfterEarlierBytesAndAdvancesTheCursor)
{
    char buffer[16] = { '\0' };
    char* cursor = buffer;

    UnicodeAdd(&cursor, 0x41);
    UnicodeAdd(&cursor, 0xE9);
    UnicodeAdd(&cursor, 0x2654);

    EXPECT_EQ(cursor - buffer, 6);
    EXPECT_EQ(std::string(buffer), "A" + bytes({ 0xC3, 0xA9, 0xE2, 0x99, 0x94 }));
}
