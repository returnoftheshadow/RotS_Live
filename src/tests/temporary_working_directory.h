/* temporary_working_directory.h */

#ifndef ROTS_TESTS_TEMPORARY_WORKING_DIRECTORY_H
#define ROTS_TESTS_TEMPORARY_WORKING_DIRECTORY_H

#include <gtest/gtest.h>

#include <filesystem>
#include <random>
#include <string>
#include <system_error>

namespace rots_tests {

// A new, empty directory under the system's temporary directory that is the working directory
// for the life of the scope, so files the code under test writes to relative paths land in it.
class TemporaryWorkingDirectory {
public:
    TemporaryWorkingDirectory()
    {
        std::error_code error;
        previous_directory = std::filesystem::current_path(error);
        if (error) {
            ADD_FAILURE() << "Could not read the working directory: " << error.message();
            return;
        }

        const std::filesystem::path temporary_root = std::filesystem::temp_directory_path(error);
        if (error) {
            ADD_FAILURE() << "Could not find the temporary directory: " << error.message();
            return;
        }

        std::random_device random_source;
        const std::string directory_name = "rots-test-" + std::to_string(random_source())
            + "-" + std::to_string(random_source());
        directory = temporary_root / directory_name;
        if (!std::filesystem::create_directory(directory, error)) {
            ADD_FAILURE() << "Could not create " << directory << ": " << error.message();
            directory.clear();
            return;
        }

        std::filesystem::current_path(directory, error);
        if (error) {
            ADD_FAILURE() << "Could not enter " << directory << ": " << error.message();
        }
    }

    ~TemporaryWorkingDirectory()
    {
        std::error_code error;
        if (!previous_directory.empty()) {
            std::filesystem::current_path(previous_directory, error);
        }
        if (!directory.empty()) {
            std::filesystem::remove_all(directory, error);
        }
    }

    TemporaryWorkingDirectory(const TemporaryWorkingDirectory&) = delete;
    TemporaryWorkingDirectory& operator=(const TemporaryWorkingDirectory&) = delete;

private:
    // The working directory before the scope, restored when it ends.
    std::filesystem::path previous_directory;
    // The directory this scope created and removes again; empty if it could not be created.
    std::filesystem::path directory;
};

} // namespace rots_tests

#endif /* ROTS_TESTS_TEMPORARY_WORKING_DIRECTORY_H */
