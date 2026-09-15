#include "../interpre.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

ACMD(do_alias);
void clear_char(struct char_data* ch, int mode);

namespace {

descriptor_data make_descriptor()
{
    descriptor_data descriptor {};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    return descriptor;
}

class AliasingCharacter {
public:
    AliasingCharacter()
        : m_descriptor(make_descriptor())
    {
        clear_char(&m_character, MOB_VOID);
        m_character.player.name = strdup("Aliaser");
        m_character.player.level = 10;
        m_character.desc = &m_descriptor;
        m_descriptor.character = &m_character;
    }

    ~AliasingCharacter()
    {
        while (m_character.specials.alias != nullptr) {
            alias_list* next_alias = m_character.specials.alias->next;
            free(m_character.specials.alias->command);
            free(m_character.specials.alias);
            m_character.specials.alias = next_alias;
        }
        free(m_character.player.name);
    }

    void alias(const std::string& arguments)
    {
        std::string mutable_arguments = arguments;
        mutable_arguments.push_back('\0');
        do_alias(&m_character, &mutable_arguments[0], nullptr, 0, 0);
    }

    char_data& character() { return m_character; }
    std::string output() const { return std::string(m_descriptor.small_outbuf); }

private:
    descriptor_data m_descriptor;
    char_data m_character {};
};

TEST(ActCommAlias, RefusesAKeywordTooLongForTheStoredField)
{
    AliasingCharacter player;

    player.alias("twenty_characters_ab kill orc");

    EXPECT_EQ(player.character().specials.alias, nullptr);
    EXPECT_NE(player.output().find("19 characters"), std::string::npos);
}

TEST(ActCommAlias, StoresAKeywordThatFillsEveryUsableCharacter)
{
    AliasingCharacter player;

    player.alias("nineteen_characters kill orc");

    ASSERT_NE(player.character().specials.alias, nullptr);
    EXPECT_STREQ(player.character().specials.alias->keyword, "nineteen_characters");
    EXPECT_STREQ(player.character().specials.alias->command, "kill orc");
}

} // namespace
