#include "../color.h"
#include "../spells.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

extern struct room_data world;
extern int top_of_world;
void clear_char(struct char_data* ch, int mode);
void say_spell(struct char_data* caster, int spell_index);
void send_magic_room_message(struct char_data* caster, const char* message);

namespace {

descriptor_data make_descriptor()
{
    descriptor_data descriptor {};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    return descriptor;
}

void ensure_test_world_room(int room_number)
{
    if (room_data::BASE_WORLD == nullptr)
        world.create_bulk(1);

    top_of_world = 0;
    world[0].number = room_number;
    world[0].people = nullptr;
}

void attach_character_to_room(char_data* character, int room_rnum, char_data* next_in_room)
{
    character->in_room = room_rnum;
    character->next_in_room = next_in_room;
}

void initialize_player_character(char_data* character, const char* name)
{
    clear_char(character, MOB_VOID);
    character->player.name = const_cast<char*>(name);
    character->specials.position = POSITION_STANDING;
}

} // namespace

TEST(SpellParser, SaySpellUsesMagicColorForColorEnabledObservers)
{
    ensure_test_world_room(3001);

    char_data caster {};
    char_data observer {};
    descriptor_data observer_descriptor = make_descriptor();

    initialize_player_character(&caster, "caster");
    initialize_player_character(&observer, "observer");
    observer.desc = &observer_descriptor;
    SET_BIT(PRF_FLAGS(&observer), PRF_COLOR);
    set_colornum(&observer, COLOR_MAGIC, CBBLU);

    attach_character_to_room(&observer, 0, nullptr);
    attach_character_to_room(&caster, 0, &observer);
    world[0].people = &caster;

    say_spell(&caster, SPELL_MAGIC_MISSILE);

    const std::string output = observer_descriptor.output;
    EXPECT_NE(output.find(color_sequence[CBBLU]), std::string::npos) << output;
    EXPECT_NE(output.find("A strange command, 'magic missile'"), std::string::npos) << output;
    EXPECT_NE(output.find(color_sequence[CNRM]), std::string::npos) << output;
}

TEST(SpellParser, MagicRoomMessageOmitsColorCodesForObserversWithoutColorEnabled)
{
    ensure_test_world_room(3002);

    char_data caster {};
    char_data observer {};
    descriptor_data observer_descriptor = make_descriptor();

    initialize_player_character(&caster, "caster");
    initialize_player_character(&observer, "observer");
    observer.desc = &observer_descriptor;
    REMOVE_BIT(PRF_FLAGS(&observer), PRF_COLOR);
    set_colornum(&observer, COLOR_MAGIC, CBBLU);

    attach_character_to_room(&observer, 0, nullptr);
    attach_character_to_room(&caster, 0, &observer);
    world[0].people = &caster;

    send_magic_room_message(&caster, "$n begins quietly muttering some strange, powerful words.\n\r");

    const std::string output = observer_descriptor.output;
    EXPECT_EQ(output.find(color_sequence[CBBLU]), std::string::npos) << output;
    EXPECT_EQ(output.find(color_sequence[CNRM]), std::string::npos) << output;
    EXPECT_NE(output.find("Caster begins quietly muttering some strange, powerful words."), std::string::npos) << output;
}
