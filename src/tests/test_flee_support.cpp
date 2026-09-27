#include "test_flee_support.h"

#include "../character_identity.h"
#include "../handler.h"
#include "../structs.h"
#include "../utils.h"
#include "test_character_support.h"
#include "test_descriptor_support.h"

#include <gtest/gtest.h>

#include <initializer_list>

extern room_data world;

namespace test_support {

namespace {

// Every stat the builders give, in all three sets: moving divides by the current strength
// (room_move_cost()) and by the base strength, dexterity and constitution
// (has_critical_stat_damage()), and a player's recalc_abilities() rebuilds the base set from the
// constant one (after a flee's experience loss, for one), so no set may leave them 0.
constexpr int kFleeTestStat = 18;

// The level both builders give: mortal, so a player respawns in its race's mortal start room.
constexpr int kFleeTestLevel = 10;

// Stands in for a connected socket: only its being nonzero is read, and nothing writes to it.
constexpr int kUnusedSocketNumber = -1;

// The name a builder gives when the test passed none.
char g_placeholder_name[] = "unnamed";

// The stats both builders give.
void give_flee_test_stats(char_data& character) {
    character.player.race = RACE_HUMAN;
    for (char_ability_data* stats :
         {&character.constabilities, &character.abilities, &character.tmpabilities}) {
        stats->str = kFleeTestStat;
        stats->lea = kFleeTestStat;
        stats->intel = kFleeTestStat;
        stats->wil = kFleeTestStat;
        stats->dex = kFleeTestStat;
        stats->con = kFleeTestStat;
        stats->hit = kFleeTestHitPoints;
        stats->move = kFleeTestMovePoints;
    }
    character.specials.position = POSITION_STANDING;
    character.specials.fighting = nullptr;
}

} // namespace

char_data* make_registered_test_npc(char* short_description) {
    if (short_description == nullptr) {
        ADD_FAILURE() << "make_registered_test_npc: null name";
        short_description = g_placeholder_name;
    }
    char_data* npc = allocate_test_character(MOB_ISNPC);
    npc->specials2.act = MOB_ISNPC;
    npc->nr = 0; // prototype slot 0 of a ScopedMobIndex
    npc->player.short_descr = short_description;
    npc->player.level = kFleeTestLevel;
    give_flee_test_stats(*npc);
    register_npc_char(npc);
    return npc;
}

char_data* make_linked_test_player(descriptor_data& descriptor, const char* name) {
    if (name == nullptr) {
        ADD_FAILURE() << "make_linked_test_player: null name";
        name = g_placeholder_name;
    }
    char_data* player = allocate_test_character(MOB_VOID);
    player->player.name = str_dup(name); // free_char() releases a player's name
    player->player.level = kFleeTestLevel;
    give_flee_test_stats(*player);
    prepare_capture_descriptor(descriptor);
    descriptor.descriptor = kUnusedSocketNumber;
    descriptor.character = player;
    player->desc = &descriptor;
    register_pc_char(player);
    return player;
}

void release_room_objects(int room_number) {
    while (world[room_number].contents != nullptr) {
        extract_obj(world[room_number].contents);
    }
}

void release_survivor(const character_identity& identity) {
    char_data* const survivor = identity.resolve();
    if (survivor == nullptr) {
        return;
    }
    if (survivor->in_room != NOWHERE) {
        char_from_room(survivor);
    }
    release_test_character(survivor);
}

} // namespace test_support
