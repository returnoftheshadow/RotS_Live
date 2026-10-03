#include "ordinal.h"

#include <array>
#include <charconv>
#include <limits>

std::string_view ordinal_suffix(int number)
{
    if (number == 11 || number == 12 || number == 13) {
        return "th";
    }

    switch (number % 10) {
    case 1:
        return "st";
    case 2:
        return "nd";
    case 3:
        return "rd";
    default:
        return "th";
    }
}

void append_ordinal(std::string& out_text, int number)
{
    // Room for every digit of any int plus its sign, so std::to_chars cannot run out of space.
    std::array<char, std::numeric_limits<int>::digits10 + 2> digits;
    const std::to_chars_result written
        = std::to_chars(digits.data(), digits.data() + digits.size(), number);

    out_text.append(digits.data(), written.ptr);
    out_text += ordinal_suffix(number);
}
