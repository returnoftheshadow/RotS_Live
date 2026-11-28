#ifndef CREATION_POINTS_H
#define CREATION_POINTS_H

#include "professions.h"

#include <array>
#include <optional>
#include <string>

struct char_data;

// Most points a new character may spread across the four professions.
constexpr int CREATION_POINT_BUDGET = 150;

// Most points the creation dialogue lets one profession hold while a player is still adjusting.
// A finished split never reaches it, because the budget is lower.
constexpr int CREATION_POINT_STEP_CAP = 165;

// A split of creation points across mage, mystic, ranger and warrior that a player could finish
// creation with: none below zero and no more than CREATION_POINT_BUDGET in total. Instances
// come only from the factories, which enforce that.
class CreationPoints {
public:
    // Points per profession, indexed like prof_coof: PROF_MAGE to PROF_WARRIOR, slot 0 unused.
    using Split = std::array<int, MAX_PROFS + 1>;

    // Returns the standard class with this creation-menu letter, or nothing if no class has it.
    [[nodiscard]] static std::optional<CreationPoints> standard_class(char letter);

    // Returns the split if a player could finish creation with it; otherwise nothing, with the
    // reason in out_error_message. Slot 0 is ignored.
    [[nodiscard]] static std::optional<CreationPoints> custom(const Split& split, std::string& out_error_message);

    // Returns the points a character has allotted, under the same rule as custom().
    [[nodiscard]] static std::optional<CreationPoints> from_character(
        const char_data& character, std::string& out_error_message);

    // Returns the points in one profession, PROF_MAGE to PROF_WARRIOR; 0 for any other value.
    int points(int profession) const;

    // Returns the points used across all four professions.
    int total() const;

    // Sets a character's profession points to this split. A character without profession data
    // is left unchanged and the problem is logged.
    void apply_to(char_data& character) const;

private:
    explicit CreationPoints(const Split& split);

    // Points per profession, indexed like prof_coof, slot 0 zero; within the budget (see the
    // class comment).
    Split split;
};

// Returns the points a character in the creation dialogue has allotted so far, which may still be
// over budget while the player is adjusting. 0 for a character without profession data.
int points_used(const char_data& character);

#endif /* CREATION_POINTS_H */
