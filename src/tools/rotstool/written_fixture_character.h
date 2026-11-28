#ifndef WRITTEN_FIXTURE_CHARACTER_H
#define WRITTEN_FIXTURE_CHARACTER_H

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

// What the lib writer reports about one character it wrote.
struct WrittenFixtureCharacter {
    // The character's name as stored, with its first letter capitalised.
    std::string name;
    // The character's idnum, unique in the lib.
    long idnum = 0;
    // Path of the character's file in the lib.
    std::filesystem::path character_file;
    // (skill index, knowledge percentage) for each skill the spec gave practices, in spec order.
    std::vector<std::pair<int, int>> skill_knowledge;
};

#endif /* WRITTEN_FIXTURE_CHARACTER_H */
