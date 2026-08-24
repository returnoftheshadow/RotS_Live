// TASK-025 port: summon targets by player name regardless of the dark-room
// sight arm. summon's skills[] mask (consts.cpp) is TAR_CHAR_ROOM |
// TAR_CHAR_WORLD | TAR_DARK_OK -- the `tell` precedent (interpre.cpp's
// COMMANDO(19) row) -- so target_from_word()'s TAR_CHAR_WORLD arm calls
// get_char_vis(ch, word, dark_ok=1) and CAN_SEE()'s light arm (the
// target-room-is-dark refusal) is skipped. The repro pin below records the
// code-confirmed root cause: under the OLD mask (no TAR_DARK_OK) the SAME
// setup refuses, and the refusing arm is the dark TARGET room -- not
// caster-blind, not hiding, not invisibility, all of which are absent from
// this fixture and deliberately still refuse under this port's scope.
//
// Mirrors RotS_Live_Modern's visibility_tests.cpp SummonTargeting suite,
// adapted to this depot's world fixture idiom: this depot has no
// ScopedTestWorld/set_location/room_by_id_total helpers, so this file reuses
// the established ensure_test_world()/direct-world[]-index pattern (matching
// fight_credit_tests.cpp/room_affect_caster_tests.cpp) instead.

#include "../db.h"
#include "../handler.h"
#include "../spells.h"
#include "../structs.h"

#include <gtest/gtest.h>

// consts.cpp's global skill table -- not declared in any header (matching
// this depot's other suites' local-extern convention for undeclared
// consts.cpp/interpre.cpp globals, e.g. spell_pa_tests.cpp).
extern struct skill_data skills[MAX_SKILLS];
// The process-global character list get_char_vis() walks.
extern struct char_data* character_list;
extern struct room_data world;
extern int top_of_world;

// interpre.cpp's target-string parser and delayed-cast re-validation gate --
// neither is declared in any header (checked interpre.h); same local-extern
// convention as skills[] above.
char* target_from_word(struct char_data* ch, char* argument, int mask, struct target_data* t1);
int target_check_one(struct char_data* ch, int mask, struct target_data* t1);

namespace {

void ensure_test_world(int minimum_room_number)
{
    if (!room_data::BASE_WORLD) {
        world.create_bulk(minimum_room_number + 2);
        top_of_world = minimum_room_number + 1;
    } else if (top_of_world < minimum_room_number) {
        top_of_world = minimum_room_number;
    }
}

// Real world[] array indices this suite claims within the shared
// test-binary world[] -- high, out-of-band values above the other suites'
// documented high-water mark (affect_update_tests.cpp: 27/28; mage_tests.cpp:
// up to 32; fight_credit_tests.cpp: 900-903; room_affect_caster_tests.cpp:
// 950-953; room_affect_tick_tests.cpp: 960-990; interpre_account_menu_tests.cpp/
// spell_pa_tests.cpp/db_loader_tests.cpp: array slot 0, stamped .number
// 1200/3001/3002).
constexpr int kCasterRoom = 1000;
constexpr int kDarkRoom = 1001;

// abs_number slots this suite registers. Kept as small positive literals
// rather than this file's siblings' MAX_CHARACTERS-relative offsets
// (affect_update_tests.cpp: -201/-202; caster_snapshot_tests.cpp: -401;
// char_utils_tests.cpp: -17/-18; poison_origin_tests.cpp: -601;
// fight_credit_tests.cpp: -801; room_affect_tick_tests.cpp: -1001/-1002):
// TargetCheckOneUnderTheSummonMaskAcceptsADarkRoomWorldTarget round-trips
// this value through target_data::ch_num, a 16-bit `sh_int` -- a
// MAX_CHARACTERS-relative offset (~62000+) would silently truncate there.
constexpr int kCasterAbsNumber = 4301;
constexpr int kVictimAbsNumber = 4302;

// Caster in room kCasterRoom, player victim in room kDarkRoom with the DARK
// room flag set and no light source -- the exact "target cannot see" shape
// TASK-025 fixes. Construction publishes the victim at the head of
// character_list (get_char_vis() walks that list, not a room occupant
// chain) and darkens the victim's room; destruction restores both. The
// victim is deliberately NOT linked into either room's people list:
// get_char_vis()/get_char_room_vis() read character_list and ch->in_room
// directly, and CAN_SEE() only reads world[ch->in_room] on either side, so
// setting in_room is the honest placement here.
struct DarkRoomSummonContext {
    char_data caster {};
    char_data victim {};
    char victim_name[8] = "frodo";

    long saved_dark_room_flags = 0;
    int saved_dark_room_light = 0;
    int saved_dark_room_number = -1;
    int saved_caster_room_number = -1;
    char_data* saved_character_list_head = nullptr;

    DarkRoomSummonContext()
    {
        ensure_test_world(kDarkRoom);

        caster.player.race = RACE_HUMAN;
        caster.player.level = 30;
        caster.specials.position = POSITION_STANDING;
        caster.abs_number = kCasterAbsNumber;
        caster.in_room = kCasterRoom;

        victim.player.race = RACE_HUMAN;
        victim.player.level = 30;
        victim.player.name = victim_name;
        victim.specials.position = POSITION_STANDING;
        victim.abs_number = kVictimAbsNumber;
        victim.in_room = kDarkRoom;

        saved_caster_room_number = world[kCasterRoom].number;
        world[kCasterRoom].number = kCasterRoom;
        world[kCasterRoom].people = nullptr;

        saved_dark_room_flags = world[kDarkRoom].room_flags;
        saved_dark_room_light = world[kDarkRoom].light;
        saved_dark_room_number = world[kDarkRoom].number;
        world[kDarkRoom].number = kDarkRoom;
        world[kDarkRoom].room_flags |= DARK;
        world[kDarkRoom].light = 0;
        world[kDarkRoom].people = nullptr;

        saved_character_list_head = character_list;
        victim.next = character_list;
        character_list = &victim;
    }

    ~DarkRoomSummonContext()
    {
        character_list = saved_character_list_head;
        world[kDarkRoom].room_flags = saved_dark_room_flags;
        world[kDarkRoom].light = saved_dark_room_light;
        world[kDarkRoom].number = saved_dark_room_number;
        world[kCasterRoom].number = saved_caster_room_number;
    }
};

} // namespace

TEST(SummonTargeting, SummonsTargetMaskCarriesDarkOkAlongsideRoomAndWorld)
{
    EXPECT_EQ(skills[SPELL_SUMMON].targets, TAR_CHAR_ROOM | TAR_CHAR_WORLD | TAR_DARK_OK)
        << "Expected summon's consts.cpp mask to match tell's TAR_DARK_OK precedent so a "
           "name-targeted world spell is not refused by the dark-room sight arm.";
}

TEST(SummonTargeting, TargetFromWordUnderTheSummonMaskFindsAPlayerStandingInADarkRoom)
{
    DarkRoomSummonContext ctx;
    target_data target {};
    char argument[] = "frodo";

    char* remainder = target_from_word(&ctx.caster, argument, skills[SPELL_SUMMON].targets, &target);

    ASSERT_NE(remainder, nullptr)
        << "Expected the summon mask's TAR_DARK_OK to let get_char_vis() resolve a player "
           "standing in a dark room.";
    EXPECT_EQ(target.type, TARGET_CHAR);
    EXPECT_EQ(target.ptr.ch, &ctx.victim);
    EXPECT_EQ(target.choice, TAR_CHAR_WORLD);
}

// Repro pin: the pre-fix mask (TAR_CHAR_ROOM | TAR_CHAR_WORLD, the literal 6
// consts.cpp carried) refuses the identical setup, and the only CAN_SEE arm
// this fixture can trip is the dark-target-room one. This test passes before
// AND after the fix -- it documents which arm fired, and pins that
// hiding/invisible/blind refusals (absent here) are not what this port lifts.
TEST(SummonTargeting, TargetFromWordWithoutDarkOkStillRefusesADarkRoomTarget)
{
    DarkRoomSummonContext ctx;
    target_data target {};
    char argument[] = "frodo";

    char* remainder = target_from_word(&ctx.caster, argument, TAR_CHAR_ROOM | TAR_CHAR_WORLD, &target);

    EXPECT_EQ(remainder, nullptr)
        << "Expected the old dark_ok-less mask to keep refusing a dark-room target -- if this "
           "resolves, the repro no longer exercises the dark arm.";
    EXPECT_EQ(target.type, TARGET_NONE);
}

TEST(SummonTargeting, TargetCheckOneUnderTheSummonMaskAcceptsADarkRoomWorldTarget)
{
    DarkRoomSummonContext ctx;
    target_data target {};
    target.type = TARGET_CHAR;
    target.ptr.ch = &ctx.victim;
    target.ch_num = ctx.victim.abs_number;
    set_char_exists(ctx.victim.abs_number);

    int accepted = target_check_one(&ctx.caster, skills[SPELL_SUMMON].targets, &target);

    remove_char_exists(ctx.victim.abs_number);
    EXPECT_EQ(accepted, TAR_CHAR_WORLD)
        << "Expected the delayed-cast re-validation gate to accept a dark-room world target "
           "under summon's TAR_DARK_OK mask.";
}
