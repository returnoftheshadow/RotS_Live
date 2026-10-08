#include "creation_points.h"

#include "structs.h"
#include "utils.h"

#include <algorithm>
#include <iterator>
#include <string>

namespace {

// The standard classes the creation menu offers, by menu letter, with their points per
// profession indexed like prof_coof.
const prof_type standard_classes[DEFAULT_PROFS] = {
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

void log_failure(const char* function_name, const std::string& message)
{
    const std::string line = std::string(function_name) + ": " + message;
    log(line.c_str());
}

} // namespace

CreationPoints::CreationPoints(const Split& split)
    : split(split)
{
}

std::optional<CreationPoints> CreationPoints::standard_class(char letter)
{
    for (const prof_type& standard : standard_classes) {
        if (standard.letter == letter) {
            Split split {};
            std::copy(std::begin(standard.Class_points), std::end(standard.Class_points), split.begin());
            return CreationPoints(split);
        }
    }
    return std::nullopt;
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
        if (profession_points > creation_point_budget) {
            out_error_message = "Profession points total more than " + std::to_string(creation_point_budget) + ".";
            return std::nullopt;
        }
        total_points += profession_points;
    }

    if (total_points > creation_point_budget) {
        out_error_message = "Profession points total " + std::to_string(total_points) + "; the most is "
            + std::to_string(creation_point_budget) + ".";
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
