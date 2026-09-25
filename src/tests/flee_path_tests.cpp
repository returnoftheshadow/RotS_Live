// A character that flees or walks can be killed on the way: by a special in the room it leaves or
// enters, by an ON_ENTER script, or by a death room. extract_char() frees an NPC or a linkless
// player, so do_flee(), do_move() and damage_credited()'s wimpy tail must stop using the character
// once it no longer resolves; a player with a descriptor respawns and keeps going. The NPCs here
// are heap characters registered the way the game registers them, so the deaths run
// extract_char() and free_char() for real and AddressSanitizer sees any later use.
#include "../character_identity.h"
#include "../handler.h"
#include "../interpre.h"
#include "../protos.h"
#include "../script.h"
#include "../spells.h"
#include "../structs.h"
#include "../utils.h"
#include "raw_kill_declaration.h"
#include "scoped_character_list.h"
#include "scoped_combat_list.h"
#include "scoped_flee_world.h"
#include "scoped_mob_index.h"
#include "scoped_player_death_sandbox.h"
#include "scoped_room_occupants.h"
#include "scoped_room_special.h"
#include "scoped_waiting_list.h"
#include "test_character_support.h"
#include "test_descriptor_support.h"
#include "test_flee_support.h"
#include "test_random_utils.h"

#include <gtest/gtest.h>

#include <string>

extern room_data world;
extern struct script_head* script_table;
extern int top_of_script_table;

ACMD(do_flee);
ACMD(do_move);

namespace {

// Rooms this file owns in the shared test world: above every other suite's (the highest,
// xp_formula_tests.cpp's, is 1010) and below the 1024 rooms gtest_main.cpp allocates.
constexpr int kOriginRoom = 1015;
constexpr int kDestinationRoom = 1016;
constexpr int kFleeDirection = EAST;

// A script number no loaded script uses; the scoped table below is the only one.
constexpr int kEnterScriptNumber = 91015;

// What the ON_ENTER script of the survivor pin tells the player, so the test can see it ran.
char g_enter_script_marker[] = "The watcher sees you arrive.";

// The damage the wimpy tests deal: from kWoundedHitPoints it leaves the victim below a fifth of
// kFleeTestHitPoints, alive and awake.
constexpr int kWoundedHitPoints = 30;
constexpr int kWimpyDamage = 15;

// Kills `character` the way a lethal special does.
void kill_character(char_data* character) { raw_kill(character, nullptr, 0); }

// A stack NPC standing where a flee or a walk ends or starts, never harmed: an attacker, or a
// watcher that runs an ON_ENTER script.
void fill_bystander(char_data& out_bystander, char_prof_data& profs, char* short_description) {
    test_support::fill_sturdy_stack_npc(out_bystander, profs);
    out_bystander.player.short_descr = short_description;
    out_bystander.specials.default_pos = POSITION_STANDING;
}

// Publishes a one-script table for the scope: script kEnterScriptNumber, an ON_ENTER trigger
// followed by one command whose first parameter names the character who entered.
class ScopedEnterScript {
  public:
    ScopedEnterScript(int command_type, char* text)
        : m_previous_table(script_table), m_previous_top(top_of_script_table) {
        m_command.command_type = command_type;
        m_command.text = text;
        m_command.param[0] = SCRIPT_PARAM_CH2; // trigger_char_enter() puts the entrant in ch[1]
        m_trigger.command_type = ON_ENTER;
        m_trigger.next = &m_command;
        m_command.prev = &m_trigger;
        m_head.number = kEnterScriptNumber;
        m_head.script = &m_trigger;
        script_table = &m_head;
        top_of_script_table = 0;
    }
    ~ScopedEnterScript() {
        script_table = m_previous_table;
        top_of_script_table = m_previous_top;
    }
    ScopedEnterScript(const ScopedEnterScript&) = delete;
    ScopedEnterScript& operator=(const ScopedEnterScript&) = delete;

  private:
    script_head* m_previous_table; // script_table before the scope
    int m_previous_top;            // top_of_script_table before the scope
    script_data m_trigger{};       // the ON_ENTER trigger that opens the script
    script_data m_command{};       // the one command the trigger runs
    script_head m_head{};          // the table's only entry
};

// Frees the script state trigger_char_enter() allocates on `watcher`.
void release_script_state(char_data& watcher) { RELEASE(watcher.specials.script_info); }

} // namespace

class FleePath : public ::testing::Test {
  protected:
    FleePath() : m_flee_world(kOriginRoom, kDestinationRoom, kFleeDirection) {}

    void TearDown() override {
        clear_test_random_values();
        test_support::release_room_objects(kOriginRoom);
        test_support::release_room_objects(kDestinationRoom);
    }

    // Arguments for do_flee() and do_move(), which take a writable string.
    char m_no_argument[1] = "";

    // mob_index[0], the test NPCs' prototype.
    test_support::ScopedMobIndex m_prototype_table;
    // Keeps the tests' fights and other suites' stale fighters apart.
    test_support::ScopedCombatList m_combat_list;
    // Keeps other suites' stale entries away from extract_char()'s walk.
    test_support::ScopedWaitingList m_waiting_list;
    // The origin, the destination, one exit east between them, and zone 0.
    test_support::ScopedFleeWorld m_flee_world;
};

TEST_F(FleePath, AFleeThatNothingInterruptsEndsInTheDestination) {
    char name[] = "a fleeing test orc";
    char_data* const fleer = test_support::make_registered_test_npc(name);
    const character_identity fleer_identity = character_identity::capture(*fleer);
    test_support::ScopedCharacterList characters({fleer});
    test_support::ScopedRoomOccupants origin_occupants(kOriginRoom, {fleer});
    test_support::queue_successful_flee_rolls(kFleeDirection);

    do_flee(fleer, m_no_argument, nullptr, 0, 0);

    EXPECT_EQ(fleer_identity.resolve(), fleer);
    EXPECT_EQ(fleer->in_room, kDestinationRoom);
    EXPECT_EQ(world[kDestinationRoom].people, fleer);
    test_support::release_survivor(fleer_identity);
}

// Pins do_move()'s check after the SPECIAL_ENTER special.
TEST_F(FleePath, AnNpcKilledByTheDestinationsEntrySpecialIsNotUsedAgain) {
    char name[] = "a fleeing test orc";
    char_data* const fleer = test_support::make_registered_test_npc(name);
    const character_identity fleer_identity = character_identity::capture(*fleer);
    test_support::ScopedCharacterList characters({fleer});
    test_support::ScopedRoomOccupants origin_occupants(kOriginRoom, {fleer});
    int entries = 0;
    test_support::ScopedRoomSpecial lethal_entry(kDestinationRoom, SPECIAL_ENTER,
                                                 [&entries](char_data* character) -> void {
                                                     ++entries;
                                                     kill_character(character);
                                                 });
    test_support::queue_successful_flee_rolls(kFleeDirection);

    do_flee(fleer, m_no_argument, nullptr, 0, 0);
    // The fleer has been freed: below it is only resolved, never read.

    EXPECT_EQ(entries, 1);
    EXPECT_EQ(fleer_identity.resolve(), nullptr);
    EXPECT_EQ(world[kOriginRoom].people, nullptr);
    EXPECT_EQ(world[kDestinationRoom].people, nullptr);
    test_support::release_survivor(fleer_identity);
}

// Pins do_flee()'s check after check_simple_move(), whose SPECIAL_COMMAND call comes first.
TEST_F(FleePath, AnNpcKilledByTheOriginsCommandSpecialDuringTheMoveCheckIsNotUsedAgain) {
    char name[] = "a fleeing test orc";
    char_data* const fleer = test_support::make_registered_test_npc(name);
    const character_identity fleer_identity = character_identity::capture(*fleer);
    test_support::ScopedCharacterList characters({fleer});
    test_support::ScopedRoomOccupants origin_occupants(kOriginRoom, {fleer});
    int commands = 0;
    test_support::ScopedRoomSpecial lethal_command(kOriginRoom, SPECIAL_COMMAND,
                                                   [&commands](char_data* character) -> void {
                                                       ++commands;
                                                       kill_character(character);
                                                   });
    test_support::queue_successful_flee_rolls(kFleeDirection);

    do_flee(fleer, m_no_argument, nullptr, 0, 0);

    EXPECT_EQ(commands, 1);
    EXPECT_EQ(fleer_identity.resolve(), nullptr);
    EXPECT_EQ(world[kDestinationRoom].people, nullptr) << "the fleer must not have moved";
    test_support::release_survivor(fleer_identity);
}

// Pins do_flee()'s check after its own special() call.
TEST_F(FleePath, AnNpcKilledByTheOriginsCommandSpecialOnTheFleeItselfIsNotUsedAgain) {
    char name[] = "a fleeing test orc";
    char_data* const fleer = test_support::make_registered_test_npc(name);
    const character_identity fleer_identity = character_identity::capture(*fleer);
    test_support::ScopedCharacterList characters({fleer});
    test_support::ScopedRoomOccupants origin_occupants(kOriginRoom, {fleer});
    int commands = 0;
    // The first call, from check_simple_move(), only claims the command; the second, do_flee()'s
    // own, kills.
    test_support::ScopedRoomSpecial lethal_second_command(
        kOriginRoom, SPECIAL_COMMAND, [&commands](char_data* character) -> void {
            ++commands;
            if (commands == 2) {
                kill_character(character);
            }
        });
    test_support::queue_successful_flee_rolls(kFleeDirection);

    do_flee(fleer, m_no_argument, nullptr, 0, 0);

    EXPECT_EQ(commands, 2);
    EXPECT_EQ(fleer_identity.resolve(), nullptr);
    EXPECT_EQ(world[kDestinationRoom].people, nullptr) << "the fleer must not have moved";
    test_support::release_survivor(fleer_identity);
}

// Pins damage_credited()'s check after a MOB_WIMPY NPC's flee.
TEST_F(FleePath, AWimpyNpcKilledDuringTheFleeItsWoundsSetOffMakesDamageReturnOne) {
    char attacker_name[] = "a test attacker";
    char_data attacker{};
    char_prof_data attacker_profs{};
    fill_bystander(attacker, attacker_profs, attacker_name);
    char victim_name[] = "a wimpy test orc";
    char_data* const victim = test_support::make_registered_test_npc(victim_name);
    SET_BIT(victim->specials2.act, MOB_WIMPY);
    victim->tmpabilities.hit = kWoundedHitPoints;
    const character_identity victim_identity = character_identity::capture(*victim);
    test_support::ScopedCharacterList characters({victim, &attacker});
    test_support::ScopedRoomOccupants origin_occupants(kOriginRoom, {&attacker, victim});
    // Already fighting each other, so damage() skips its SPECIAL_DAMAGE probe.
    set_fighting(&attacker, victim);
    set_fighting(victim, &attacker);
    int entries = 0;
    test_support::ScopedRoomSpecial lethal_entry(kDestinationRoom, SPECIAL_ENTER,
                                                 [&entries](char_data* character) -> void {
                                                     ++entries;
                                                     kill_character(character);
                                                 });
    test_support::queue_east_everywhere_rolls();

    const int result = damage(&attacker, victim, kWimpyDamage, TYPE_HIT, 0);

    EXPECT_EQ(entries, 1) << "the victim must die on entry, not from the blow";
    EXPECT_EQ(result, 1) << "a victim freed during its own flee must read as gone";
    EXPECT_EQ(victim_identity.resolve(), nullptr);
    EXPECT_EQ(attacker.specials.fighting, nullptr);
    test_support::release_survivor(victim_identity);
}

// damage_credited()'s check after a wimpy player's flee. The player has no link, so the special
// that extracts it frees it (raw_kill() is not used: for a linkless player it reads the player
// after extract_char() has freed it, a use-after-free of its own).
TEST_F(FleePath, AWimpyPlayerExtractedDuringItsFleeMakesDamageReturnOne) {
    char attacker_name[] = "a test attacker";
    char_data attacker{};
    char_prof_data attacker_profs{};
    fill_bystander(attacker, attacker_profs, attacker_name);
    descriptor_data unused_descriptor{};
    char_data* const victim = test_support::make_linked_test_player(unused_descriptor, "Wimp");
    victim->desc = nullptr; // linkless
    unused_descriptor.character = nullptr;
    victim->specials2.wimp_level = kWoundedHitPoints; // wimps out once the blow lands
    victim->tmpabilities.hit = kWoundedHitPoints;
    const character_identity victim_identity = character_identity::capture(*victim);
    test_support::ScopedCharacterList characters({victim, &attacker});
    test_support::ScopedRoomOccupants origin_occupants(kOriginRoom, {&attacker, victim});
    set_fighting(&attacker, victim);
    set_fighting(victim, &attacker);
    int extractions = 0;
    test_support::ScopedRoomSpecial extracting_entry(kDestinationRoom, SPECIAL_ENTER,
                                                     [&extractions](char_data* character) -> void {
                                                         ++extractions;
                                                         extract_char(character);
                                                     });
    test_support::queue_east_everywhere_rolls();

    const int result = damage(&attacker, victim, kWimpyDamage, TYPE_HIT, 0);

    EXPECT_EQ(extractions, 1);
    EXPECT_EQ(result, 1);
    EXPECT_EQ(victim_identity.resolve(), nullptr);
    test_support::release_survivor(victim_identity);
}

// damage_credited()'s check after a linkless player's flee: no wimpy level, so the linkless flee
// after the switch is the one that runs.
TEST_F(FleePath, ALinklessPlayerExtractedDuringItsFleeMakesDamageReturnOne) {
    char attacker_name[] = "a test attacker";
    char_data attacker{};
    char_prof_data attacker_profs{};
    fill_bystander(attacker, attacker_profs, attacker_name);
    descriptor_data unused_descriptor{};
    char_data* const victim = test_support::make_linked_test_player(unused_descriptor, "Linkless");
    victim->desc = nullptr;
    unused_descriptor.character = nullptr;
    const character_identity victim_identity = character_identity::capture(*victim);
    test_support::ScopedCharacterList characters({victim, &attacker});
    test_support::ScopedRoomOccupants origin_occupants(kOriginRoom, {&attacker, victim});
    set_fighting(&attacker, victim);
    set_fighting(victim, &attacker);
    int extractions = 0;
    test_support::ScopedRoomSpecial extracting_entry(kDestinationRoom, SPECIAL_ENTER,
                                                     [&extractions](char_data* character) -> void {
                                                         ++extractions;
                                                         extract_char(character);
                                                     });
    test_support::queue_east_everywhere_rolls();

    const int result = damage(&attacker, victim, kWimpyDamage, TYPE_HIT, 0);

    EXPECT_EQ(extractions, 1);
    EXPECT_EQ(result, 1);
    EXPECT_EQ(victim_identity.resolve(), nullptr);
    test_support::release_survivor(victim_identity);
}

// A player with a descriptor killed on entry respawns, keeps its registration, and its
// move goes on past every check: the ON_ENTER trigger do_move() fires after the entry special
// runs in the room it respawned in, where a watcher's script greets it.
TEST_F(FleePath, APlayerKilledOnEntryRespawnsAndItsMoveGoesOn) {
    test_support::ScopedPlayerDeathSandbox sandbox(RACE_HUMAN, kOriginRoom);
    char watcher_name[] = "a test watcher";
    char_data watcher{};
    char_prof_data watcher_profs{};
    fill_bystander(watcher, watcher_profs, watcher_name);
    watcher.specials.script_number = kEnterScriptNumber;
    ScopedEnterScript greeting(SCRIPT_SEND_TO_CHAR, g_enter_script_marker);
    descriptor_data descriptor{};
    char_data* const player = test_support::make_linked_test_player(descriptor, "Fleer");
    const character_identity player_identity = character_identity::capture(*player);
    test_support::ScopedCharacterList characters({player, &watcher});
    test_support::ScopedRoomOccupants origin_occupants(kOriginRoom, {&watcher, player});
    int entries = 0;
    test_support::ScopedRoomSpecial lethal_entry(kDestinationRoom, SPECIAL_ENTER,
                                                 [&entries](char_data* character) -> void {
                                                     ++entries;
                                                     kill_character(character);
                                                 });
    test_support::queue_successful_flee_rolls(kFleeDirection);

    do_flee(player, m_no_argument, nullptr, 0, 0);

    const std::string output = descriptor.output;
    EXPECT_EQ(entries, 1);
    const char_data* const survivor = player_identity.resolve();
    EXPECT_EQ(survivor, player) << "a respawned player keeps its registration";
    if (survivor != nullptr) {
        EXPECT_EQ(survivor->in_room, kOriginRoom) << "the player respawns in its start room";
    }
    EXPECT_NE(output.find("Your spirit found a new body to wear."), std::string::npos) << output;
    EXPECT_NE(output.find(g_enter_script_marker), std::string::npos)
        << "the move must go on to the ON_ENTER trigger after the entry special; output was: "
        << output;

    test_support::release_survivor(player_identity);
    release_script_state(watcher);
    test_support::release_large_output(descriptor);
}

// Pins do_move()'s check after the death room's raw_kill().
TEST_F(FleePath, AnNpcThatWalksIntoADeathRoomIsNotUsedAgain) {
    char name[] = "a walking test orc";
    char_data* const walker = test_support::make_registered_test_npc(name);
    const character_identity walker_identity = character_identity::capture(*walker);
    test_support::ScopedCharacterList characters({walker});
    test_support::ScopedRoomOccupants origin_occupants(kOriginRoom, {walker});
    world[kDestinationRoom].room_flags = DEATH;
    test_support::queue_east_everywhere_rolls();

    do_move(walker, m_no_argument, nullptr, kFleeDirection + 1, SCMD_MOVING);
    // The walker has been freed: below it is only resolved, never read.

    EXPECT_EQ(walker_identity.resolve(), nullptr);
    EXPECT_EQ(world[kOriginRoom].people, nullptr);
    EXPECT_EQ(world[kDestinationRoom].people, nullptr);
    test_support::release_survivor(walker_identity);
}

// Pins do_move()'s check after call_trigger(ON_ENTER): a watcher's script kills the entrant.
TEST_F(FleePath, AnNpcKilledByAnOnEnterScriptIsNotUsedAgain) {
    char watcher_name[] = "a test watcher";
    char_data watcher{};
    char_prof_data watcher_profs{};
    fill_bystander(watcher, watcher_profs, watcher_name);
    watcher.specials.script_number = kEnterScriptNumber;
    ScopedEnterScript lethal_script(SCRIPT_RAW_KILL, nullptr);
    char name[] = "a walking test orc";
    char_data* const walker = test_support::make_registered_test_npc(name);
    const character_identity walker_identity = character_identity::capture(*walker);
    test_support::ScopedCharacterList characters({walker, &watcher});
    test_support::ScopedRoomOccupants origin_occupants(kOriginRoom, {walker});
    test_support::ScopedRoomOccupants destination_occupants(kDestinationRoom, {&watcher});
    test_support::queue_east_everywhere_rolls();

    do_move(walker, m_no_argument, nullptr, kFleeDirection + 1, SCMD_MOVING);

    EXPECT_EQ(walker_identity.resolve(), nullptr);
    EXPECT_EQ(world[kDestinationRoom].people, &watcher);
    EXPECT_EQ(watcher.next_in_room, nullptr);
    EXPECT_NE(world[kDestinationRoom].contents, nullptr) << "raw_kill() leaves a corpse";
    test_support::release_survivor(walker_identity);
    release_script_state(watcher);
}

// The same check when the script extracts the entrant instead of killing it.
TEST_F(FleePath, AnNpcExtractedByAnOnEnterScriptIsNotUsedAgain) {
    char watcher_name[] = "a test watcher";
    char_data watcher{};
    char_prof_data watcher_profs{};
    fill_bystander(watcher, watcher_profs, watcher_name);
    watcher.specials.script_number = kEnterScriptNumber;
    ScopedEnterScript extracting_script(SCRIPT_EXTRACT_CHAR, nullptr);
    char name[] = "a walking test orc";
    char_data* const walker = test_support::make_registered_test_npc(name);
    const character_identity walker_identity = character_identity::capture(*walker);
    test_support::ScopedCharacterList characters({walker, &watcher});
    test_support::ScopedRoomOccupants origin_occupants(kOriginRoom, {walker});
    test_support::ScopedRoomOccupants destination_occupants(kDestinationRoom, {&watcher});
    test_support::queue_east_everywhere_rolls();

    do_move(walker, m_no_argument, nullptr, kFleeDirection + 1, SCMD_MOVING);

    EXPECT_EQ(walker_identity.resolve(), nullptr);
    EXPECT_EQ(world[kDestinationRoom].people, &watcher);
    EXPECT_EQ(world[kDestinationRoom].contents, nullptr) << "extraction leaves no corpse";
    test_support::release_survivor(walker_identity);
    release_script_state(watcher);
}
