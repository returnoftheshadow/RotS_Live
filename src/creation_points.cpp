#include "creation_points.h"

#include "structs.h"
#include "utils.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>

namespace {

// The standard classes the creation menu offers, by menu letter, with their points per
// profession indexed like prof_coof.
const prof_type STANDARD_CLASSES[DEFAULT_PROFS] = {
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
};

static_assert(std::size(prof_type {}.Class_points) == MAX_PROFS + 1,
    "a standard class has one entry per prof_coof slot");

void log_failure(std::string_view function_name, std::string_view message)
{
    std::string line;
    line.reserve(function_name.size() + 2 + message.size());
    line.append(function_name);
    line.append(": ");
    line.append(message);
    log(line.c_str());
}

} // namespace

CreationPoints::CreationPoints(const Split& split)
    : split(split)
{
}

std::optional<CreationPoints> CreationPoints::standard_class(char letter)
{
    const prof_type* const classes_end = std::end(STANDARD_CLASSES);
    const prof_type* const found = std::find_if(std::begin(STANDARD_CLASSES), classes_end,
        [letter](const prof_type& standard) -> bool { return standard.letter == letter; });
    if (found == classes_end) {
        return std::nullopt;
    }

    Split split {};
    std::copy(std::begin(found->Class_points), std::end(found->Class_points), split.begin());
    return CreationPoints(split);
}

std::optional<CreationPoints> CreationPoints::custom(const Split& split, std::string& out_error_message)
{
    int total_points = 0;
    for (int profession = PROF_MAGE; profession <= PROF_WARRIOR; ++profession) {
        const int profession_points = split[profession];
        if (profession_points < 0) {
            out_error_message = "Profession points cannot be negative.";
            return std::nullopt;
        }
        // One profession over the budget already decides the answer, and checking it first keeps
        // the running total from overflowing on huge values.
        if (profession_points > CREATION_POINT_BUDGET) {
            out_error_message = "One profession has more than " + std::to_string(CREATION_POINT_BUDGET) + " points.";
            return std::nullopt;
        }
        total_points += profession_points;
    }

    if (total_points > CREATION_POINT_BUDGET) {
        out_error_message = "Profession points total " + std::to_string(total_points) + "; the most is "
            + std::to_string(CREATION_POINT_BUDGET) + ".";
        return std::nullopt;
    }

    Split checked_split = split;
    checked_split[PROF_GENERAL] = 0;
    return CreationPoints(checked_split);
}

std::optional<CreationPoints> CreationPoints::from_character(const char_data& character, std::string& out_error_message)
{
    if (character.profs == nullptr) {
        out_error_message = "The character has no profession data.";
        log_failure(__func__, out_error_message);
        return std::nullopt;
    }

    Split split {};
    for (int profession = PROF_MAGE; profession <= PROF_WARRIOR; ++profession) {
        split[profession] = character.profs->prof_coof[profession];
    }
    return custom(split, out_error_message);
}

int CreationPoints::points(int profession) const
{
    if (profession < PROF_MAGE || profession > PROF_WARRIOR) {
        log_failure(__func__, "profession " + std::to_string(profession) + " is not one of the four.");
        return 0;
    }
    return split[profession];
}

int CreationPoints::total() const
{
    int total_points = 0;
    for (int profession = PROF_MAGE; profession <= PROF_WARRIOR; ++profession) {
        total_points += split[profession];
    }
    return total_points;
}

void CreationPoints::apply_to(char_data& character) const
{
    if (character.profs == nullptr) {
        log_failure(__func__, "the character has no profession data.");
        return;
    }

    std::copy(split.begin(), split.end(), std::begin(character.profs->prof_coof));
}

int points_used(const char_data& character)
{
    if (character.profs == nullptr) {
        log_failure(__func__, "the character has no profession data.");
        return 0;
    }

    int total_points = 0;
    for (int profession = PROF_MAGE; profession <= PROF_WARRIOR; ++profession) {
        total_points += character.profs->prof_coof[profession];
    }
    return total_points;
}
