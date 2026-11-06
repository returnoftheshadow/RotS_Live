#include "../color.h"
#include "../interpre.h"
#include "../spells.h"
#include "../structs.h"
#include "../utils.h"
#include "test_cleanup.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <string>

extern struct room_data world;
extern int top_of_world;
void clear_char(struct char_data* ch, int mode);
void say_spell(struct char_data* caster, int spell_index);
void send_magic_room_message(struct char_data* caster, const char* message);
bool can_cast_spell(char_data& character, int spell_index, const skill_data& spell);
extern struct skill_data skills[];

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

/* Puts back room 0's people, as it found them, when a test that seats its own local characters
 * there ends, so that no later test walks into characters gone out of scope. */
class ScopedRoomZeroPeople {
public:
    ScopedRoomZeroPeople()
        : m_saved_people(world[0].people)
    {
    }
    ~ScopedRoomZeroPeople() { world[0].people = m_saved_people; }

    ScopedRoomZeroPeople(const ScopedRoomZeroPeople&) = delete;
    ScopedRoomZeroPeople& operator=(const ScopedRoomZeroPeople&) = delete;

private:
    // Room 0's people when the scope began.
    char_data* m_saved_people;
};

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
    ScopedRoomZeroPeople room_people;

    char_data caster {};
    char_data observer {};
    descriptor_data observer_descriptor = make_descriptor();

    initialize_player_character(&caster, "caster");
    ClearedCharacterRelease caster_release(&caster);
    initialize_player_character(&observer, "observer");
    ClearedCharacterRelease observer_release(&observer);
    observer.desc = &observer_descriptor;
    SET_BIT(PRF_FLAGS(&observer), PRF_COLOR);
    set_colornum(&observer, COLOR_MAGIC, CBBLU);

    attach_character_to_room(&observer, 0, nullptr);
    attach_character_to_room(&caster, 0, &observer);
    world[0].people = &caster;

    say_spell(&caster, SPELL_MAGIC_MISSILE);

    // say_spell() passes the global buf as the message; the colour prefix appearing once and the
    // caster's name surviving show the message was not overwritten while being coloured.
    const std::string output = observer_descriptor.output;
    const std::string magic_color = color_sequence[CBBLU];
    EXPECT_EQ(output.find(magic_color), 0u) << output;
    EXPECT_EQ(output.find(magic_color, magic_color.size()), std::string::npos) << output;
    EXPECT_NE(output.find("Caster utters a strange command, 'magic missile'"), std::string::npos)
        << output;
    EXPECT_NE(output.find(color_sequence[CNRM]), std::string::npos) << output;
}

TEST(SpellParser, MagicRoomMessageOmitsColorCodesForObserversWithoutColorEnabled)
{
    ensure_test_world_room(3002);
    ScopedRoomZeroPeople room_people;

    char_data caster {};
    char_data observer {};
    descriptor_data observer_descriptor = make_descriptor();

    initialize_player_character(&caster, "caster");
    ClearedCharacterRelease caster_release(&caster);
    initialize_player_character(&observer, "observer");
    ClearedCharacterRelease observer_release(&observer);
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

namespace {

/* An orc follower with everything can_cast_spell asks of one: a master, WIL 18, full spirit,
   and no knowledge array, so GET_KNOWLEDGE reports 80 for every spell. */
void initialize_orc_follower(char_data* pet, char_data* master)
{
    initialize_player_character(pet, "orc");
    SET_BIT(MOB_FLAGS(pet), MOB_ISNPC | MOB_PET | MOB_ORC_FRIEND);
    /* clear_char gave it a player's zeroed arrays; a loaded mob has none. */
    free(pet->skills);
    free(pet->knowledge);
    pet->skills = nullptr;
    pet->knowledge = nullptr;
    pet->master = master;
    pet->player.level = 30;
    pet->tmpabilities.wil = 18;
    pet->points.spirit = 1000;
}

} // namespace

TEST(SpellParser, NpcCannotCastTheItemOnlyResistSpells)
{
    char_data master {};
    char_data pet {};
    initialize_player_character(&master, "master");
    ClearedCharacterRelease master_release(&master);
    initialize_orc_follower(&pet, &master);
    ClearedCharacterRelease pet_release(&pet);

    for (int spell = SPELL_RESIST_FIRE; spell <= SPELL_RESIST_DARK; ++spell)
        EXPECT_FALSE(can_cast_spell(pet, spell, skills[spell])) << skills[spell].name;
}

TEST(SpellParser, OrcFollowerCanStillCastOrdinaryClericSpells)
{
    char_data master {};
    char_data pet {};
    initialize_player_character(&master, "master");
    ClearedCharacterRelease master_release(&master);
    initialize_orc_follower(&pet, &master);
    ClearedCharacterRelease pet_release(&pet);

    EXPECT_TRUE(can_cast_spell(pet, SPELL_REGENERATION, skills[SPELL_REGENERATION]));
}

ACMD(do_cast);

namespace {

// The room the casting tests' caster stands in: any slot of the test world.
constexpr int CAST_ROOM = 975;
// The caster's mana, which a refused cast leaves untouched.
constexpr int STARTING_MANA = 100;

} // namespace

TEST(SpellParser, ACastTowardADirectionPastTheLastIsRefused)
{
    room_data saved_room = world[CAST_ROOM];
    world[CAST_ROOM].room_flags = 0;
    world[CAST_ROOM].people = nullptr;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_PLYNG;
    char_data caster {};
    initialize_player_character(&caster, "caster");
    ClearedCharacterRelease caster_release(&caster);
    caster.desc = &descriptor;
    caster.in_room = CAST_ROOM;
    GET_MANA(&caster) = STARTING_MANA;

    // A cast whose wait has ended, toward the direction index one past the last direction.
    waiting_type finished_cast {};
    finished_cast.subcmd = SPELL_FREEZE;
    finished_cast.targ2.type = TARGET_DIR;
    finished_cast.targ2.choice = TAR_DIR_WAY;
    finished_cast.targ2.ch_num = NUM_OF_DIRS;
    char no_argument[] = "";
    constexpr int CAST_SUBCOMMAND = 1; /* not -1, do_cast's broken concentration */
    do_cast(&caster, no_argument, &finished_cast, CMD_CAST, CAST_SUBCOMMAND);

    EXPECT_EQ(std::string(descriptor.output), "Error in direction spell, please notify imps.\n\r");
    EXPECT_EQ(GET_MANA(&caster), STARTING_MANA);

    world[CAST_ROOM] = saved_room;
}

namespace {

/* Starts a cast of spell_number handed over the way a mob program hands one over, and returns
   the log text it wrote. The caster is left waiting if the cast began. */
std::string start_program_cast(char_data& caster, int spell_number)
{
    waiting_type program_cast {};
    program_cast.cmd = CMD_CAST;
    program_cast.subcmd = 0;
    program_cast.targ1.type = TARGET_OTHER;
    program_cast.targ1.ch_num = spell_number;
    program_cast.targ2.type = TARGET_NONE;
    char no_argument[] = "";
    testing::internal::CaptureStderr();
    do_cast(&caster, no_argument, &program_cast, CMD_CAST, 0);
    return testing::internal::GetCapturedStderr();
}

} // namespace

TEST(SpellParser, AProgramCastOfASpellNumberOutsideTheSkillsTableIsRefusedAndLogged)
{
    room_data saved_room = world[CAST_ROOM];
    world[CAST_ROOM].room_flags = 0;
    world[CAST_ROOM].people = nullptr;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_PLYNG;
    char_data caster {};
    initialize_player_character(&caster, "caster");
    ClearedCharacterRelease caster_release(&caster);
    caster.desc = &descriptor;
    caster.in_room = CAST_ROOM;
    GET_MANA(&caster) = STARTING_MANA;

    for (const int spell_number : { MAX_SKILLS, -1 }) {
        const std::string log_text = start_program_cast(caster, spell_number);

        EXPECT_NE(log_text.find("outside the skills table"), std::string::npos) << spell_number;
        EXPECT_EQ(std::string(descriptor.output), "") << spell_number;
        EXPECT_FALSE(IS_SET(caster.specials.affected_by, AFF_WAITING)) << spell_number;
        EXPECT_EQ(GET_MANA(&caster), STARTING_MANA) << spell_number;
    }

    world[CAST_ROOM] = saved_room;
}

ACMD(do_prepare);

TEST(SpellParser, AProgramPrepareOfASpellNumberOutsideTheSkillsTableIsRefusedAndLogged)
{
    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_PLYNG;
    char_data caster {};
    initialize_player_character(&caster, "caster");
    ClearedCharacterRelease caster_release(&caster);
    caster.desc = &descriptor;

    for (const int spell_number : { MAX_SKILLS, -1 }) {
        waiting_type program_prepare {};
        program_prepare.cmd = CMD_PREPARE;
        program_prepare.subcmd = 0;
        program_prepare.targ1.type = TARGET_OTHER;
        program_prepare.targ1.ch_num = spell_number;
        char no_argument[] = "";
        testing::internal::CaptureStderr();
        do_prepare(&caster, no_argument, &program_prepare, CMD_PREPARE, 0);
        const std::string log_text = testing::internal::GetCapturedStderr();

        EXPECT_NE(log_text.find("outside the skills table"), std::string::npos) << spell_number;
        EXPECT_EQ(std::string(descriptor.output), "") << spell_number;
        EXPECT_FALSE(IS_SET(caster.specials.affected_by, AFF_WAITING)) << spell_number;
    }

}

TEST(SpellParser, ACastWithNoParsedTargetsReadsTheArgumentLine)
{
    room_data saved_room = world[CAST_ROOM];
    world[CAST_ROOM].room_flags = 0;
    world[CAST_ROOM].people = nullptr;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_PLYNG;
    char_data caster {};
    initialize_player_character(&caster, "caster");
    ClearedCharacterRelease caster_release(&caster);
    caster.desc = &descriptor;
    caster.in_room = CAST_ROOM;

    char no_argument[] = "";
    do_cast(&caster, no_argument, nullptr, CMD_CAST, 0);

    EXPECT_EQ(std::string(descriptor.output), "Cast which what where?\n\r");

    world[CAST_ROOM] = saved_room;
}
