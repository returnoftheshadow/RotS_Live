#pragma once

#include "../db.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>

/* Makes a new temporary folder the working folder, holding an empty mail file where the mail system
 * looks for one, and on destruction puts the working folder back and removes the temporary one. */
class scoped_mail_file {
public:
    scoped_mail_file()
    {
        std::error_code error;
        m_previous_folder = std::filesystem::current_path(error);
        EXPECT_FALSE(error) << error.message();
        m_folder = std::filesystem::temp_directory_path(error)
            / ("rots_mail_" + std::to_string(std::random_device {}()));
        EXPECT_FALSE(error) << error.message();
        std::filesystem::create_directories((m_folder / MAIL_FILE).parent_path(), error);
        EXPECT_FALSE(error) << error.message();
        std::filesystem::current_path(m_folder, error);
        EXPECT_FALSE(error) << error.message();
        const std::ofstream empty_mail_file(MAIL_FILE, std::ios::binary);
        EXPECT_TRUE(empty_mail_file.good());
    }

    ~scoped_mail_file()
    {
        std::error_code error;
        std::filesystem::current_path(m_previous_folder, error);
        std::filesystem::remove_all(m_folder, error);
    }

    scoped_mail_file(const scoped_mail_file&) = delete;
    scoped_mail_file& operator=(const scoped_mail_file&) = delete;

private:
    std::filesystem::path m_previous_folder; // the working folder to go back to
    std::filesystem::path m_folder; // the temporary folder holding the mail file
};
