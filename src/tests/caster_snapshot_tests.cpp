#include "../caster_snapshot.h"
#include "../char_utils.h"
#include "../handler.h"
#include "../spells.h"
#include "../structs.h"
#include "../utils.h"
#include "../warrior_spec_handlers.h"
#include "test_random_utils.h"
#include <cstring>
#include <gtest/gtest.h>

// TASK-021 port, Task 5: caster_snapshot, the cast-time copy of the formula
// inputs. Originally this file only exercised what Task 5 shipped in THIS
// depot -- the snapshot type itself, max_race_prof_level()/the race_is_*
// helpers, the battle_mage_handler static bonus forms, other_side(), and
// saves_poison(). Task 6 added the mystic.cpp/spell_pa.cpp equivalence tests
// below (get_mystic_caster_level()/get_saving_throw_dc()/new_saves_spell());
// the mage.cpp helpers' own equivalence tests
// (get_mage_caster_level()/get_magic_power()/should_apply_spell_penetration()/
// get_spell_pen_value()/get_victim_saving_throw()/get_save_bonus()/
// is_friendly_taget()) live in mage_tests.cpp instead, alongside the rest of
// mage.cpp's coverage. The room_affect_caster() registry is still out of
// scope here (Tasks 7-12).

namespace {

// A minimally-populated mage caster, matching the modern suite's make_mage()
// helper field-for-field so the two depots' expectations stay comparable.
struct CasterSnapshotTestContext {
    char_data character {};
    char_prof_data profs {};
    char name_storage[16] = "test-mage";

    CasterSnapshotTestContext()
    {
        character.profs = &profs;
        character.player.name = name_storage;
        profs.prof_level[PROF_MAGE] = 25;
        profs.prof_level[PROF_CLERIC] = 3;
        profs.specialization = static_cast<int>(game_types::PS_Fire);
        character.player.level = 30;
        character.player.race = RACE_HUMAN;
        character.tmpabilities.intel = 21;
        character.tmpabilities.wil = 17;
        character.points.spell_power = 4;
        character.points.spell_pen = 2;
        character.points.willpower = 9;
    }
};

} // namespace

TEST(CasterSnapshot, CaptureCopiesEveryFormulaInput)
{
    CasterSnapshotTestContext context;
    context.character.abs_number = 4242;

    const caster_snapshot snap = caster_snapshot::capture(context.character);

    EXPECT_EQ(snap.abs_number, 4242);
    EXPECT_EQ(snap.identity_ptr, &context.character);
    EXPECT_EQ(snap.mage_prof_level, 25);
    EXPECT_EQ(snap.cleric_prof_level, 3);
    EXPECT_EQ(snap.intel, 21);
    EXPECT_EQ(snap.wil, 17);
    EXPECT_EQ(snap.spell_power, 4);
    EXPECT_EQ(snap.spell_pen, 2);
    EXPECT_EQ(snap.willpower, 9);
    EXPECT_EQ(snap.specialization, game_types::PS_Fire);
    EXPECT_EQ(snap.race, RACE_HUMAN);
    EXPECT_FALSE(snap.is_npc);
    EXPECT_FALSE(snap.is_none());
    EXPECT_STREQ(snap.name, "test-mage");
}

TEST(CasterSnapshot, LaterStatChangesDoNotReachTheSnapshot)
{
    CasterSnapshotTestContext context;
    const caster_snapshot snap = caster_snapshot::capture(context.character);

    context.character.tmpabilities.intel = 3; // the death penalty shape
    context.profs.prof_level[PROF_MAGE] = 1;

    EXPECT_EQ(snap.intel, 21);
    EXPECT_EQ(snap.mage_prof_level, 25);
}

TEST(CasterSnapshot, ResolveRequiresTheSameRegisteredCharacter)
{
    CasterSnapshotTestContext context;
    const int slot = MAX_CHARACTERS - 401;
    remove_char_exists(slot); // defensive: ensure the slot starts clean
    context.character.abs_number = slot;

    set_char_exists(slot, &context.character);
    const caster_snapshot snap = caster_snapshot::capture(context.character);
    EXPECT_EQ(snap.resolve(), &context.character);

    remove_char_exists(slot);
    EXPECT_EQ(snap.resolve(), nullptr) << "an extracted caster resolves to nobody";

    // The slot is recycled: a DIFFERENT character is registered under the
    // same abs_number. resolve() must recognize the mismatch without ever
    // dereferencing snap's own (now-stale) identity_ptr -- it only compares
    // that pointer value against char_by_abs_number()'s report of the
    // CURRENT owner, so this stays safe even when the old pointer is
    // dangling (a freed char_data, in real usage).
    CasterSnapshotTestContext other_context;
    other_context.character.abs_number = slot;
    set_char_exists(slot, &other_context.character);
    EXPECT_EQ(snap.resolve(), nullptr);

    remove_char_exists(slot); // restore: leave the slot unregistered
}

TEST(CasterSnapshot, SameCharacterAsRequiresPointerAndNumberToMatch)
{
    CasterSnapshotTestContext context;
    context.character.abs_number = 4243;
    const caster_snapshot snap = caster_snapshot::capture(context.character);
    EXPECT_TRUE(snap.same_character_as(context.character));

    CasterSnapshotTestContext other_context;
    other_context.character.abs_number = 4243; // a different char_data at the same abs_number
    EXPECT_FALSE(snap.same_character_as(other_context.character));

    context.character.abs_number = 4244; // same pointer, but abs_number changed since capture
    EXPECT_FALSE(snap.same_character_as(context.character));

    const caster_snapshot none = caster_snapshot::none();
    EXPECT_FALSE(none.same_character_as(context.character));
}

TEST(CasterSnapshot, NoneIsNeverResolvable)
{
    const caster_snapshot none = caster_snapshot::none();
    EXPECT_TRUE(none.is_none());
    EXPECT_EQ(none.resolve(), nullptr);
    EXPECT_STREQ(none.name, "nobody");
}

// The Task-1 deferred coverage gap the final whole-branch review reopened
// (m-3) in the modern port: capture()'s charmed-orc-friend spell-penetration
// derivation needs its own test rather than trusting a hand-set snapshot.
// mage.cpp's should_apply_spell_penetration()/get_spell_pen_value() (Task 6
// in this port; see mage_tests.cpp for their own equivalence coverage) are
// the CONSUMERS of is_pc_for_spell_pen and master_mage_prof_level; this test
// only proves capture() itself derives them correctly.
namespace {

// Builds the charmed-orc-friend NPC pet the arm exists for; each of the four
// bool flags below independently disables one conjunct of capture()'s
// is_pc_for_spell_pen arm. Field-by-field construction (never a whole-struct
// char_data copy) avoids invoking char_data's implicit copy constructor,
// which the specialization_data member's user-provided destructor
// deprecates.
char_data make_orc_friend_pet(char_prof_data& pet_profs, char_data* master, bool is_orc_friend, bool is_charmed)
{
    char_data pet {};
    pet.profs = &pet_profs;
    pet.player.level = 10;
    pet.player.race = RACE_ORC;
    pet.specials2.act = is_orc_friend ? (MOB_ISNPC | MOB_ORC_FRIEND) : MOB_ISNPC;
    pet.specials.affected_by = is_charmed ? AFF_CHARM : 0;
    pet.master = master;
    return pet;
}

} // namespace

TEST(CasterSnapshot, CaptureDerivesTheCharmedOrcFriendSpellPenetrationPair)
{
    char_data master {};
    char_prof_data master_profs {};
    master.profs = &master_profs;
    master_profs.prof_level[PROF_MAGE] = 30; // a PC, PROF_MAGE 30

    char_prof_data pet_profs {};
    const char_data pet = make_orc_friend_pet(pet_profs, &master, /*is_orc_friend=*/true, /*is_charmed=*/true);

    const caster_snapshot charmed = caster_snapshot::capture(pet);
    EXPECT_TRUE(charmed.is_npc);
    EXPECT_TRUE(charmed.is_charmed);
    EXPECT_TRUE(charmed.is_pc_for_spell_pen)
        << "a charmed orc friend with a PC master penetrates on its master's account";
    EXPECT_EQ(charmed.master_mage_prof_level, 30)
        << "and carries the MASTER's mage level, not its own";
    // utils::get_prof_level()'s NPC arm returns the mob's own player.level, so
    // the pet's captured mage_prof_level is 10.
    EXPECT_EQ(charmed.mage_prof_level, 10);

    // Each of the arm's four conjuncts, dropped one at a time.
    char_prof_data not_orc_friend_profs {};
    const char_data not_orc_friend = make_orc_friend_pet(not_orc_friend_profs, &master, /*is_orc_friend=*/false, /*is_charmed=*/true);
    EXPECT_FALSE(caster_snapshot::capture(not_orc_friend).is_pc_for_spell_pen)
        << "a charmed NPC that is not an orc friend does not qualify";

    char_prof_data uncharmed_profs {};
    const char_data uncharmed = make_orc_friend_pet(uncharmed_profs, &master, /*is_orc_friend=*/true, /*is_charmed=*/false);
    const caster_snapshot uncharmed_snap = caster_snapshot::capture(uncharmed);
    EXPECT_FALSE(uncharmed_snap.is_pc_for_spell_pen) << "nor an uncharmed orc friend";
    EXPECT_EQ(uncharmed_snap.master_mage_prof_level, 0)
        << "and an uncharmed follower carries no master level either";

    char_prof_data masterless_profs {};
    const char_data masterless = make_orc_friend_pet(masterless_profs, nullptr, /*is_orc_friend=*/true, /*is_charmed=*/true);
    const caster_snapshot masterless_snap = caster_snapshot::capture(masterless);
    EXPECT_FALSE(masterless_snap.is_pc_for_spell_pen) << "nor one with no master at all";
    EXPECT_EQ(masterless_snap.master_mage_prof_level, 0);

    char_data npc_master {};
    char_prof_data npc_master_profs {};
    npc_master.profs = &npc_master_profs;
    npc_master_profs.prof_level[PROF_MAGE] = 30; // ignored: get_prof_level()'s NPC arm
    npc_master.specials2.act = MOB_ISNPC;
    npc_master.player.level = 12;
    char_prof_data mob_led_profs {};
    const char_data mob_led = make_orc_friend_pet(mob_led_profs, &npc_master, /*is_orc_friend=*/true, /*is_charmed=*/true);
    const caster_snapshot mob_led_snap = caster_snapshot::capture(mob_led);
    EXPECT_FALSE(mob_led_snap.is_pc_for_spell_pen)
        << "an NPC master does not lend its PC-ness";
    EXPECT_EQ(mob_led_snap.master_mage_prof_level, 12)
        << "but the master's mage level is captured whoever the master is -- and for an NPC "
           "master that level is its own player.level, not its prof table";
}

// TASK-021 fix round 1, finding 1: GET_NAME() returns player.short_descr for
// an NPC, routinely well past MAX_NAME_LENGTH (12) -- capture() must
// truncate rather than overflow the fixed kNameCapacity buffer.
TEST(CasterSnapshot, NpcNameCapacityTruncatesLongShortDescriptorsSafely)
{
    char_data npc {};
    char_prof_data profs {};
    npc.profs = &profs;
    npc.specials2.act = MOB_ISNPC;
    npc.player.race = RACE_ORC;

    char long_short_descr[100];
    std::memset(long_short_descr, 'x', sizeof(long_short_descr) - 1);
    long_short_descr[sizeof(long_short_descr) - 1] = '\0';
    npc.player.short_descr = long_short_descr;

    const caster_snapshot snap = caster_snapshot::capture(npc);

    EXPECT_EQ(std::strlen(snap.name), static_cast<size_t>(caster_snapshot::kNameCapacity - 1))
        << "expected a name far longer than the 64-byte buffer to be truncated, not overflowed";
    for (int i = 0; i < caster_snapshot::kNameCapacity - 1; ++i) {
        ASSERT_EQ(snap.name[i], 'x') << "expected the truncated name to keep the source's own prefix";
    }
}

// max_race_prof_level()'s table (structs.h) and the GET_MAX_RACE_PROF_LEVEL
// macro (utils.h) that forwards onto it must agree exactly, for every race,
// with the inline table the old macro used to carry directly.
TEST(CasterSnapshot, MaxRaceProfLevelMatchesTheOldRaceKeyedTable)
{
    EXPECT_EQ(max_race_prof_level(PROF_MAGE, RACE_ORC), 20);
    EXPECT_EQ(max_race_prof_level(PROF_WARRIOR, RACE_ORC), 20);
    EXPECT_EQ(max_race_prof_level(PROF_MAGE, RACE_URUK), 27);
    EXPECT_EQ(max_race_prof_level(PROF_WARRIOR, RACE_URUK), 30);
    EXPECT_EQ(max_race_prof_level(PROF_CLERIC, RACE_URUK), 30);
    EXPECT_EQ(max_race_prof_level(PROF_MAGE, RACE_HUMAN), 30);
    EXPECT_EQ(max_race_prof_level(PROF_CLERIC, RACE_WOOD), 30);

    char_data character {};
    character.player.race = RACE_ORC;
    EXPECT_EQ(GET_MAX_RACE_PROF_LEVEL(PROF_MAGE, &character), max_race_prof_level(PROF_MAGE, RACE_ORC));
    character.player.race = RACE_URUK;
    EXPECT_EQ(GET_MAX_RACE_PROF_LEVEL(PROF_MAGE, &character), max_race_prof_level(PROF_MAGE, RACE_URUK));
    character.player.race = RACE_HUMAN;
    EXPECT_EQ(GET_MAX_RACE_PROF_LEVEL(PROF_WARRIOR, &character), max_race_prof_level(PROF_WARRIOR, RACE_HUMAN));
}

// battle_mage_handler's static bonus forms (warrior_spec_handlers.h) own the
// formula; the member forms are thin adapters onto them. Prove the two agree
// for both a battle mage and a non-specialist, so a stored caster_snapshot's
// eventual (Task 6+) use of the static forms replays the exact live bonus.
TEST(CasterSnapshot, BattleMageStaticBonusFormsAgreeWithMemberForms)
{
    char_data character {};
    char_prof_data profs {};
    character.profs = &profs;
    character.specials.tactics = TACTICS_AGGRESSIVE;
    profs.specialization = static_cast<int>(game_types::PS_BattleMage);
    profs.prof_level[PROF_MAGE] = 24;
    profs.prof_level[PROF_WARRIOR] = 18;

    player_spec::battle_mage_handler battle_handler(&character);
    EXPECT_EQ(player_spec::battle_mage_handler::get_bonus_spell_pen(
                  game_types::PS_BattleMage, TACTICS_AGGRESSIVE, 24, 50),
        battle_handler.get_bonus_spell_pen(50));
    EXPECT_EQ(player_spec::battle_mage_handler::get_bonus_spell_power(
                  game_types::PS_BattleMage, TACTICS_AGGRESSIVE, 24, 60),
        battle_handler.get_bonus_spell_power(60));
    EXPECT_EQ(battle_handler.get_bonus_spell_pen(50), 50 + TACTICS_AGGRESSIVE / 2 + 24 / 12);

    char_data non_spec_character {};
    char_prof_data non_spec_profs {};
    non_spec_character.profs = &non_spec_profs;
    non_spec_character.specials.tactics = TACTICS_NORMAL;
    non_spec_profs.specialization = static_cast<int>(game_types::PS_None);
    non_spec_profs.prof_level[PROF_MAGE] = 24;

    player_spec::battle_mage_handler non_spec_handler(&non_spec_character);
    EXPECT_EQ(player_spec::battle_mage_handler::get_bonus_spell_pen(
                  game_types::PS_None, TACTICS_NORMAL, 24, 50),
        non_spec_handler.get_bonus_spell_pen(50));
    EXPECT_EQ(non_spec_handler.get_bonus_spell_pen(50), 50)
        << "a non-battle-mage's static form must pass the value straight through";
}

// TASK-021: other_side() has a caster_snapshot overload sharing the live
// form's other_side_impl() body (handler.cpp); the two forms must agree for
// every race-war side combination the live form distinguishes.
TEST(CasterSnapshot, OtherSideAgreesBetweenLiveAndSnapshotFormsAcrossTheRaceMatrix)
{
    const int races[] = { RACE_GOD, RACE_HUMAN, RACE_DWARF, RACE_WOOD, RACE_HIGH,
        RACE_URUK, RACE_ORC, RACE_MAGUS, RACE_HARADRIM, RACE_EASTERLING };

    for (int character_race : races) {
        for (int other_race : races) {
            char_data character {};
            char_prof_data character_profs {};
            character.profs = &character_profs;
            character.player.race = character_race;

            char_data other {};
            char_prof_data other_profs {};
            other.profs = &other_profs;
            other.player.race = other_race;

            const caster_snapshot snap = caster_snapshot::capture(character);
            EXPECT_EQ(other_side(snap, &other), other_side(&character, &other))
                << "character_race=" << character_race << " other_race=" << other_race;
        }
    }
}

TEST(CasterSnapshot, OtherSideReflectsTheCapturedRaceNpcAndCharmFlags)
{
    char_data character {};
    char_prof_data profs {};
    character.profs = &profs;
    character.player.race = RACE_HUMAN;

    char_data orc {};
    char_prof_data orc_profs {};
    orc.profs = &orc_profs;
    orc.player.race = RACE_URUK;

    const caster_snapshot snap = caster_snapshot::capture(character);
    EXPECT_EQ(other_side(snap, &orc), 1);
    EXPECT_EQ(other_side(snap, &orc), other_side(&character, &orc));

    caster_snapshot evil = snap;
    evil.race = RACE_URUK;
    EXPECT_EQ(other_side(evil, &orc), 0)
        << "the race-war side must come from the captured race, not the live character's";

    caster_snapshot uncharmed_npc = snap;
    uncharmed_npc.is_npc = true;
    uncharmed_npc.is_charmed = false;
    EXPECT_EQ(other_side(uncharmed_npc, &orc), 0)
        << "an uncharmed NPC is on nobody's side";
}

// TASK-021: saves_poison()'s caster_snapshot form (spell_pa.cpp) reads
// caster.willpower/caster.perception, captured from exactly the
// GET_WILLPOWER()/GET_PERCEPTION() calls the live form reads directly -- the
// two must agree under the same RNG draws.
TEST(CasterSnapshot, SavesPoisonAgreesBetweenLiveAndSnapshotFormsUnderPinnedRng)
{
    char_data caster {};
    char_prof_data caster_profs {};
    caster.profs = &caster_profs;
    caster.points.willpower = 9;
    caster.specials2.perception = 50;

    char_data victim {};
    char_prof_data victim_profs {};
    victim.profs = &victim_profs;
    victim.tmpabilities.con = 10;
    victim.points.willpower = 5;
    victim.player.race = RACE_HUMAN;

    const caster_snapshot snap = caster_snapshot::capture(caster);

    // saves_poison() draws exactly two number(a, b) rolls (the offence and
    // defense draws); both calls below are given the same value for each of
    // their two draws, so any platform-rounding wobble lands identically on
    // both sides and the comparison stays a pure equivalence check.
    push_test_random_value(0.5);
    push_test_random_value(0.5);
    const char live = saves_poison(&victim, &caster);
    push_test_random_value(0.5);
    push_test_random_value(0.5);
    EXPECT_EQ(saves_poison(&victim, snap), live);
    clear_test_random_values();
}

TEST(CasterSnapshot, SavesPoisonOffenceReadsTheCapturedWillpowerAndPerception)
{
    char_data victim {};
    char_prof_data victim_profs {};
    victim.profs = &victim_profs;
    victim.tmpabilities.con = 10;
    victim.points.willpower = 9; // defense = 10*5 + 9*3 = 77

    char_data caster {};
    char_prof_data caster_profs {};
    caster.profs = &caster_profs;
    caster.points.willpower = 9;
    caster.specials2.perception = 50;
    const caster_snapshot snap = caster_snapshot::capture(caster);

    caster_snapshot blind = snap;
    blind.perception = 0; // offence 0 -> the victim always saves
    push_test_random_value(0.5);
    push_test_random_value(0.5);
    EXPECT_NE(saves_poison(&victim, blind), 0);
    clear_test_random_values();

    caster_snapshot potent = snap;
    potent.perception = 100;
    potent.willpower = 100; // offence 800 -> the victim (almost) never saves
    push_test_random_value(0.5);
    push_test_random_value(0.5);
    EXPECT_EQ(saves_poison(&victim, potent), 0)
        << "the poison offence term must scale with the captured perception";
    clear_test_random_values();

    caster_snapshot spineless = potent;
    spineless.willpower = 0; // offence 0 again -> the victim saves
    push_test_random_value(0.5);
    push_test_random_value(0.5);
    EXPECT_NE(saves_poison(&victim, spineless), 0)
        << "the poison offence term must scale with the captured willpower";
    clear_test_random_values();
}

// ---------------------------------------------------------------------------
// TASK-021 Task 6: get_mystic_caster_level() (mystic.cpp), get_saving_throw_dc()
// and new_saves_spell() (spell_pa.cpp) each gain a caster_snapshot overload
// that owns the body; the live const char_data* form is a one-line forwarder
// onto it. Ported here rather than mage_tests.cpp -- these three formulas
// live in mystic.cpp/spell_pa.cpp, not mage.cpp.
// ---------------------------------------------------------------------------

TEST(CasterSnapshot, MysticCasterLevelSnapshotFormMatchesLiveFormUnderPinnedRng)
{
    CasterSnapshotTestContext context; // PROF_CLERIC 3, wil 17 -> will_factor 3, remainder 2 (a real draw)
    const caster_snapshot snap = caster_snapshot::capture(context.character);

    push_test_random_value(0.5);
    const int live_level = get_mystic_caster_level(&context.character);
    push_test_random_value(0.5);
    EXPECT_EQ(get_mystic_caster_level(snap), live_level)
        << "Expected the snapshot form to reproduce the live form's mystic caster level under the "
           "same pinned remainder roll.";
    clear_test_random_values();
}

TEST(CasterSnapshot, SavingThrowDcSnapshotFormMatchesLiveForm)
{
    CasterSnapshotTestContext context; // PROF_MAGE 25, intel 21, spell_pen 2
    const caster_snapshot snap = caster_snapshot::capture(context.character);

    EXPECT_EQ(get_saving_throw_dc(snap), get_saving_throw_dc(&context.character))
        << "Expected the snapshot form to reproduce the live form's saving throw DC.";

    context.character.specials.tactics = TACTICS_AGGRESSIVE;
    context.profs.specialization = static_cast<int>(game_types::PS_BattleMage);
    const caster_snapshot battle_snap = caster_snapshot::capture(context.character);
    EXPECT_EQ(get_saving_throw_dc(battle_snap), get_saving_throw_dc(&context.character))
        << "Expected the battle-mage spell-pen bonus to agree between the live and snapshot forms.";
}

TEST(CasterSnapshot, NewSavesSpellSnapshotFormMatchesLiveFormUnderPinnedRng)
{
    CasterSnapshotTestContext context;
    char_data victim {};
    char_prof_data victim_profs {};
    victim.profs = &victim_profs;
    victim.player.race = RACE_HUMAN;
    victim.tmpabilities.intel = 18;

    const caster_snapshot snap = caster_snapshot::capture(context.character);

    push_test_random_value(0.5);
    const bool live_saved = new_saves_spell(&context.character, &victim, 3);
    push_test_random_value(0.5);
    EXPECT_EQ(new_saves_spell(snap, &victim, 3), live_saved)
        << "Expected the snapshot form to reproduce the live form's save result under the same "
           "pinned roll.";
    clear_test_random_values();
}
