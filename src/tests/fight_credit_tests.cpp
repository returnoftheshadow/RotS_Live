// TASK-021/026 port, Task 8: damage_credited() -- separating who ENGAGES the
// victim (`attacker`) from who is CREDITED with the kill (`credited_killer`).
// damage() becomes a thin forwarder onto damage_credited() with
// credited_killer == attacker, so every historical call site keeps its exact
// behavior; the two limits.cpp poison-tick sites (Task 8) and the future
// room-affect tick (Task 12) pass a credited_killer that can be null or
// remote instead.
//
// This depot has no extract_char test seam (see the test adaptation policy in
// global-constraints.md), so the death-path pins below drive the real death
// pipeline -- heap-allocated, register_npc_char()'d NPCs linked into
// character_list and a real room, so raw_kill()/extract_char()/free_char()
// run for real -- following the precedent at
// src/tests/interpre_account_menu_tests.cpp:3779 and the heap-NPC/
// ScopedMobIndex/RoomExitGuard idiom mage_tests.cpp's fireball suite already
// established for this exact "no extract_char seam" problem.
//
// Observation strategy (documented per pin below, since this depot cannot
// observe "who die() was told" directly without Task 12's kill-contributor
// records):
//   (a) the pet-master redirect is observed through make_physical_corpse()'s
//       `attack_type == SPELL_POISON || !IS_NPC(killer)` gear-move branch: a
//       non-NPC killer pulls wearables out of any container in the corpse,
//       an NPC killer leaves them nested. Since the pet itself is always an
//       NPC, "the gear moved" can only happen if the redirect substituted its
//       (non-NPC) master.
//   (b) no death is needed at all -- engagement is fully local to
//       damage_credited()'s own body, checked by reading
//       credited_killer->specials.fighting and victim->specials.fighting
//       directly after a non-lethal call.
//   (c)/(d) the same gear-move signal, applied to the TASK-026 fallback: an
//       NPC engaged opponent leaves gear nested (proving the fallback armed),
//       while no opponent at all leaves the corpse crediting nobody, which
//       make_physical_corpse() also treats as a non-NPC killer and moves the
//       gear. The two pins are a deliberately contrasting pair.
//   (e) SPELL_POISON forces the gear-move branch on regardless of killer (see
//       the `attack_type == SPELL_POISON ||` clause above), so the gear
//       signal cannot distinguish killers for a poison death. This pin
//       instead drives limits.cpp's exact call shape
//       (`damage_credited(i, i, resolve_poisoner(*i), 5, SPELL_POISON, 0)`)
//       and observes that the credited poisoner's specials.fighting is never
//       touched -- proving the credited character is never engaged even
//       though it took the kill, which is Task 8's central claim.
//
// All death-path fixtures below deliberately pre-arm
// `victim->specials.fighting == attacker` (or drive the self-tick shape
// `attacker == victim` that limits.cpp itself uses) rather than leaving
// combatants unengaged at call time: damage_credited()'s pre-existing (not
// Task 8's) `special(attacker, 0, "", SPECIAL_DAMAGE, &tmpwtl)` call is only
// skipped when the two already agree, and this suite has no reason to
// exercise that unrelated call path.
#include "../db.h"
#include "../handler.h"
#include "../kill_contributors.h"
#include "../pkill.h"
#include "../spells.h"
#include "../structs.h"
#include "../utils.h"
#include "test_random_utils.h"
#include <algorithm>
#include <gtest/gtest.h>

// damage()/damage_credited()/resolve_poisoner()/record_poison_origin()/
// set_char_exists()/char_by_abs_number() are all declared by handler.h,
// included above.

extern room_data world;
extern int top_of_world;
extern char_data* character_list;
extern index_data* mob_index;
extern obj_data* object_list;
extern char_data* combat_list;

// TASK-026 port, Task 12: pkill_weight()/pkill_opponents()/pkill_valid_killer()
// are pkill.cpp internals with no header declaration (only pkill_create() is
// exposed via pkill.h) -- matching damage_tests.cpp's precedent of declaring
// an internal production function `extern` in the test TU rather than adding
// a header just for tests. Signatures copied verbatim from pkill.cpp.
extern int pkill_weight(struct char_data* victim, const kill_contributor_list& contributors);
extern int pkill_opponents(struct char_data* victim, const kill_contributor_list& contributors);
extern int pkill_valid_killer(struct char_data* killer, struct char_data* victim);

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

// Room numbers this file claims within the shared test-binary `world[]` --
// high, out-of-band values distinct from other suites' rooms (damage_tests.cpp:
// room 1; mage_tests.cpp: rooms up to 32), matching poison_origin_tests.cpp's
// convention of claiming an unclaimed slot rather than reusing a shared one.
constexpr int kRedirectRoom = 900;
constexpr int kFallbackRoom = 901;
constexpr int kNobodyRoom = 902;
constexpr int kPoisonRoom = 903;

// abs_number slot this file's poison pin hands to its NPC poisoner -- an
// out-of-band slot distinct from poison_origin_tests.cpp's kPoisonerSlot
// (MAX_CHARACTERS - 601) and the other suites listed in that file's own
// comment.
constexpr int kPoisonerSlot = MAX_CHARACTERS - 801;

// One-entry mob prototype table: raw_kill()'s SPECIAL_DEATH probe
// (activate_char_special) and make_physical_corpse() both read
// mob_index[character->nr] unconditionally for any IS_NPC() character.
// Mirrors mage_tests.cpp's ScopedFireballMobIndex.
class ScopedMobIndex {
public:
    ScopedMobIndex()
        : m_previous(mob_index)
    {
        m_entry = index_data {};
        m_entry.virt = 1;
        mob_index = &m_entry;
    }
    ~ScopedMobIndex() { mob_index = m_previous; }
    ScopedMobIndex(const ScopedMobIndex&) = delete;
    ScopedMobIndex& operator=(const ScopedMobIndex&) = delete;

private:
    index_data* m_previous; // whatever this suite found installed (normally null)
    index_data m_entry {}; // the single prototype slot the victim's nr = 0 names
};

// Saves/restores a room's occupant and content lists for one death-path test.
struct RoomGuard {
    int room_number;
    char_data* original_people;
    obj_data* original_contents;

    explicit RoomGuard(int room)
        : room_number(room)
    {
        ensure_test_world(room);
        original_people = world[room].people;
        original_contents = world[room].contents;
    }

    ~RoomGuard()
    {
        world[room_number].people = original_people;
        world[room_number].contents = original_contents;
    }
};

// make_corpse() CREATE()s a heap corpse and pushes it onto world[].contents
// and object_list; take both back out so a death-path test leaves no residue
// for later tests in this binary. Mirrors mage_tests.cpp's
// release_fireball_corpse().
void release_corpse(int room_number, obj_data* previous_object_list)
{
    obj_data* corpse = world[room_number].contents;
    if (corpse == nullptr)
        return;
    obj_from_room(corpse);
    if (object_list == corpse)
        object_list = corpse->next;
    RELEASE(corpse->name);
    RELEASE(corpse->short_description);
    RELEASE(corpse->description);
    RELEASE(corpse);
    object_list = previous_object_list;
}

// Builds the heap-allocated, registered NPC victim the death pipeline needs
// (see the file comment above): clear_char() + register_npc_char() the way
// the game constructs an NPC, with nr = 0 naming the ScopedMobIndex slot
// above. `hit_points` is both the current and max hit total, so any nonzero
// damage is lethal.
char_data* make_npc_victim(int room, char* short_descr, int hit_points)
{
    char_data* victim = new char_data {};
    clear_char(victim, MOB_ISNPC);
    victim->specials2.act = MOB_ISNPC;
    victim->nr = 0; // prototype slot 0 of the scoped one-entry mob_index above
    victim->player.race = RACE_HUMAN;
    victim->player.short_descr = short_descr; // GET_NAME()/make_physical_corpse() read this for an NPC
    victim->player.level = 5;
    victim->abilities.hit = std::max(hit_points, 1);
    victim->tmpabilities.hit = hit_points;
    victim->specials.position = POSITION_STANDING;
    victim->in_room = room;
    register_npc_char(victim);
    return victim;
}

// A one-item container carried (not worn) by `victim`: make_physical_corpse()
// always moves `victim`'s carried objects into the corpse's top level intact,
// but only recurses into containers -- pulling the wearable item out to sit
// directly in the corpse -- when `move_wearables_to_corpse()` runs, which is
// exactly the `!IS_NPC(killer)` (or SPELL_POISON) branch. `container`/`item`
// are stack objects; they are never RELEASE()'d because make_corpse() never
// allocated them, only linked them.
struct CarriedGear {
    obj_data container {};
    obj_data item {};

    void attach_to(char_data& victim)
    {
        container.obj_flags.type_flag = ITEM_CONTAINER;
        item.obj_flags.type_flag = ITEM_ARMOR; // wearable, per obj_flag_data::is_wearable()
        container.contains = &item;
        item.in_obj = &container;
        victim.carrying = &container;
    }
};

} // namespace

// Pin (b): a remote credited_killer is never engaged by damage_credited(),
// regardless of whether the hit is lethal. No death is needed for this pin --
// engagement is entirely local to the function body, so it is asserted
// directly on the character structures after one non-lethal call.
TEST(FightCredit, RemoteCreditedKillerIsNeverEngaged)
{
    // damage_credited() never dereferences world[] on this (non-lethal, no
    // death branch) path, but growing the shared test-binary world[] up
    // front costs nothing and removes any doubt.
    ensure_test_world(kFallbackRoom);

    char_data attacker {};
    char_data victim {};
    char_data remote_killer {};
    char attacker_name[] = "test_attacker";
    char victim_name[] = "test_victim";
    char remote_name[] = "test_remote_killer";

    attacker.specials2.act = MOB_ISNPC;
    victim.specials2.act = MOB_ISNPC;
    remote_killer.specials2.act = MOB_ISNPC;
    attacker.player.short_descr = attacker_name;
    victim.player.short_descr = victim_name;
    remote_killer.player.short_descr = remote_name;
    attacker.player.race = RACE_HUMAN;
    victim.player.race = RACE_HUMAN;
    remote_killer.player.race = RACE_HUMAN;
    attacker.player.level = 20;
    victim.player.level = 20;
    remote_killer.player.level = 20;

    attacker.abilities.hit = 500;
    attacker.tmpabilities.hit = 500;
    victim.abilities.hit = 500;
    victim.tmpabilities.hit = 500; // well above the 10-point non-lethal hit below
    remote_killer.abilities.hit = 500;
    remote_killer.tmpabilities.hit = 500;

    attacker.specials.position = POSITION_FIGHTING;
    victim.specials.position = POSITION_FIGHTING;
    remote_killer.specials.position = POSITION_STANDING;

    // Pre-armed so damage_credited()'s pre-existing
    // `victim->specials.fighting != attacker` SPECIAL_DAMAGE probe (unrelated
    // to Task 8) is skipped -- see the file comment above.
    attacker.specials.fighting = &victim;
    victim.specials.fighting = &attacker;
    remote_killer.specials.fighting = nullptr;

    attacker.in_room = kRedirectRoom; // not linked into world[].people -- no room list is read for a non-lethal hit
    victim.in_room = kRedirectRoom;
    remote_killer.in_room = kFallbackRoom; // deliberately a different room: engagement must never reach across it

    int result = damage_credited(&attacker, &victim, &remote_killer, 10, TYPE_HIT, 0);

    EXPECT_EQ(result, 0) << "10 damage against a 500-hit victim must not be lethal";
    EXPECT_EQ(remote_killer.specials.fighting, nullptr)
        << "a remote credited_killer must never be engaged by damage_credited()";
    EXPECT_EQ(victim.specials.fighting, &attacker)
        << "the victim must still be engaged with the character that actually struck it, not the "
           "remote credited_killer";
    EXPECT_EQ(attacker.specials.fighting, &victim);
}

// Pin (a): damage() forwards with credit == attacker (an ordinary lethal hit
// credits the attacker, byte-identical to the pre-Task-8 shape), and that
// credit still survives the pet-master redirect. See the file comment above
// for the gear-move observation strategy this pin relies on; the master is
// pinned at LEVEL_IMMORT so group_gain()'s per-character loop `continue`s
// past the exp/spirit/alignment machinery (which needs a populated
// zone_table this test harness never boots) without needing to stub it --
// this pin only needs group_gain() to have been HANDED the master, not to
// finish crediting them.
TEST(FightCredit, DamageForwardsCreditAndAppliesThePetMasterRedirectOnDeath)
{
    ScopedMobIndex prototype_table;
    RoomGuard room_guard(kRedirectRoom);

    char victim_short_descr[] = "a testing credit victim";
    char_data* victim = make_npc_victim(kRedirectRoom, victim_short_descr, 1);
    character_list = victim;
    victim->next = nullptr;

    char pet_short_descr[] = "a testing pet";
    char_data pet {};
    pet.specials2.act = MOB_ISNPC | MOB_PET;
    pet.player.race = RACE_HUMAN;
    pet.player.short_descr = pet_short_descr;
    pet.player.level = 20;
    pet.abilities.hit = 500;
    pet.tmpabilities.hit = 500;
    pet.specials.position = POSITION_STANDING;
    pet.in_room = kRedirectRoom;
    pet.nr = 0; // shares the scoped prototype slot; special()'s func-check reads it but never calls through it

    char master_name[] = "a testing pet master";
    char_data master {};
    // Left without MOB_ISNPC: a "PC" for utils::is_pc()'s purposes, matching
    // a player-owned pet's real master.
    master.player.race = RACE_HUMAN;
    master.player.name = master_name;
    master.player.level = LEVEL_IMMORT;
    master.abilities.hit = 500;
    master.tmpabilities.hit = 500;
    master.specials.position = POSITION_STANDING;
    master.in_room = kRedirectRoom;
    master.specials.fighting = nullptr;
    pet.master = &master;

    // Pre-armed so the pre-existing SPECIAL_DAMAGE probe is skipped -- see
    // the file comment above.
    victim->specials.fighting = &pet;
    pet.specials.fighting = victim;

    CarriedGear gear;
    gear.attach_to(*victim);

    world[kRedirectRoom].people = victim;
    victim->next_in_room = nullptr;

    obj_data* const previous_object_list = object_list;

    int result = damage(&pet, victim, 5, TYPE_HIT, 0);

    EXPECT_EQ(result, 1) << "1-hit-point victim must die to any nonzero hit";
    EXPECT_EQ(character_list, nullptr)
        << "extract_char()'s NPC arm must have unlinked the dead victim from character_list";
    EXPECT_EQ(world[kRedirectRoom].people, nullptr)
        << "extract_char()'s NPC arm must have unlinked the dead victim from the room's occupant list";

    obj_data* const corpse = world[kRedirectRoom].contents;
    ASSERT_NE(corpse, nullptr) << "raw_kill() must have created a corpse in the death room";
    EXPECT_EQ(gear.container.contains, nullptr)
        << "move_wearables_to_corpse() must have pulled the wearable item out of its container -- "
           "this only happens for a non-NPC killer (!IS_NPC(killer)), so this is the pet-master "
           "redirect's observable signature: had die() still been credited to the (NPC) pet, the "
           "item would still be sitting inside its container.";
    EXPECT_EQ(gear.item.in_obj, corpse)
        << "the wearable item must have landed directly in the corpse, proving the redirect "
           "credited the non-NPC master rather than the NPC pet.";

    release_corpse(kRedirectRoom, previous_object_list);
}

// Pin (c): a null credited_killer with the victim engaged with an opponent
// falls back to crediting that opponent (TASK-026), not the character that
// actually delivered the blow. Drives the production self-tick shape
// (`attacker == victim`, matching both limits.cpp call sites) with the
// engaged opponent left as an NPC, so make_physical_corpse()'s gear-move
// branch stays off if (and only if) the fallback correctly names the
// opponent rather than falling through to nobody -- see the file comment
// above and the companion pin below for the contrasting case.
TEST(FightCredit, NullCreditFallsBackToTheEngagedOpponent)
{
    ScopedMobIndex prototype_table;
    RoomGuard room_guard(kFallbackRoom);

    char victim_short_descr[] = "a testing tick victim";
    char_data* victim = make_npc_victim(kFallbackRoom, victim_short_descr, 1);
    character_list = victim;
    victim->next = nullptr;
    world[kFallbackRoom].people = victim;
    victim->next_in_room = nullptr;

    char opponent_short_descr[] = "a testing melee opponent";
    char_data opponent {};
    opponent.specials2.act = MOB_ISNPC;
    opponent.player.race = RACE_HUMAN;
    opponent.player.short_descr = opponent_short_descr;
    opponent.player.level = 10;
    opponent.abilities.hit = 500;
    opponent.tmpabilities.hit = 500;
    opponent.specials.position = POSITION_STANDING;
    opponent.in_room = kFallbackRoom;
    opponent.specials.fighting = victim;

    // The victim is mid-melee with `opponent` when a self-inflicted tick (the
    // production shape: attacker == victim) lands the killing blow with no
    // credited_killer of its own -- exactly resolve_poisoner() answering
    // nullptr once the recorded poisoner is gone.
    victim->specials.fighting = &opponent;

    CarriedGear gear;
    gear.attach_to(*victim);

    obj_data* const previous_object_list = object_list;

    int result = damage_credited(victim, victim, nullptr, 5, TYPE_HIT, 0);

    EXPECT_EQ(result, 1) << "1-hit-point victim must die to any nonzero hit";
    EXPECT_EQ(character_list, nullptr);

    obj_data* const corpse = world[kFallbackRoom].contents;
    ASSERT_NE(corpse, nullptr) << "raw_kill() must have created a corpse in the death room";
    EXPECT_EQ(gear.container.contains, &gear.item)
        << "the item must still be nested in its container: the fallback must have credited the "
           "engaged opponent (an NPC), not fallen through to crediting nobody -- a null killer is "
           "treated as non-NPC by make_physical_corpse() and would have moved it (see the companion "
           "pin below, where that is exactly what happens with no opponent to fall back to).";

    release_corpse(kFallbackRoom, previous_object_list);
}

// Pin (d): a null credited_killer with the victim fighting nobody credits
// nobody -- the fallback never invents a killer. Companion to the pin above:
// same self-tick shape, no engaged opponent, so make_physical_corpse() must
// take the gear-move branch (a null killer reads as non-NPC).
TEST(FightCredit, NullCreditWithNoEngagedOpponentCreditsNobody)
{
    ScopedMobIndex prototype_table;
    RoomGuard room_guard(kNobodyRoom);

    char victim_short_descr[] = "a testing unengaged tick victim";
    char_data* victim = make_npc_victim(kNobodyRoom, victim_short_descr, 1);
    character_list = victim;
    victim->next = nullptr;
    world[kNobodyRoom].people = victim;
    victim->next_in_room = nullptr;
    victim->specials.fighting = nullptr; // fighting nobody at the instant of the tick

    CarriedGear gear;
    gear.attach_to(*victim);

    obj_data* const previous_object_list = object_list;

    int result = damage_credited(victim, victim, nullptr, 5, TYPE_HIT, 0);

    EXPECT_EQ(result, 1) << "1-hit-point victim must die to any nonzero hit";
    EXPECT_EQ(character_list, nullptr);

    obj_data* const corpse = world[kNobodyRoom].contents;
    ASSERT_NE(corpse, nullptr) << "raw_kill() must have created a corpse in the death room";
    EXPECT_EQ(gear.container.contains, nullptr)
        << "credited_killer null with no engaged opponent must credit nobody: "
           "make_physical_corpse()'s `!IS_NPC(killer)` treats a null killer as non-NPC, so wearables "
           "move out. A fallback that had invented a killer here (rather than leaving it null) would "
           "have left the item nested in its container instead.";
    EXPECT_EQ(gear.item.in_obj, corpse);

    release_corpse(kNobodyRoom, previous_object_list);
}

// Pin (e): the poison DoT via resolve_poisoner() credits the recorded
// poisoner, and -- Task 8's central claim -- never engages it. Drives
// limits.cpp's exact call shape from both the point_update() gear-poison arm
// and affect_update_person()'s ordinary poison arm:
// `damage_credited(i, i, resolve_poisoner(*i), 5, SPELL_POISON, 0)`.
// SPELL_POISON forces make_physical_corpse()'s gear-move branch on
// regardless of killer (see the file comment above), so this pin cannot use
// the gear-move oracle; it instead observes the poisoner's own
// specials.fighting, which damage_credited() must never touch since the
// poisoner is `credited_killer`, never `attacker`.
TEST(FightCredit, PoisonTickCreditsTheResolvedPoisonerWithoutEngagingIt)
{
    ScopedMobIndex prototype_table;
    RoomGuard room_guard(kPoisonRoom);

    char poisoner_short_descr[] = "a testing poisoner, long gone";
    char_data poisoner {};
    poisoner.specials2.act = MOB_ISNPC;
    poisoner.player.race = RACE_HUMAN;
    poisoner.player.short_descr = poisoner_short_descr;
    poisoner.player.level = 10;
    poisoner.abilities.hit = 500;
    poisoner.tmpabilities.hit = 500;
    poisoner.specials.position = POSITION_STANDING;
    poisoner.in_room = NOWHERE; // physically elsewhere (or already gone) by the time the DoT lands
    poisoner.specials.fighting = nullptr;
    poisoner.abs_number = kPoisonerSlot;
    set_char_exists(kPoisonerSlot, &poisoner);

    char victim_short_descr[] = "a testing poison victim";
    char_data* victim = make_npc_victim(kPoisonRoom, victim_short_descr, 1);
    character_list = victim;
    victim->next = nullptr;
    world[kPoisonRoom].people = victim;
    victim->next_in_room = nullptr;
    victim->specials.fighting = nullptr; // not currently in melee -- limits.cpp's own tick shape

    record_poison_origin(victim, &poisoner);
    ASSERT_EQ(resolve_poisoner(*victim), &poisoner) << "the poisoner must resolve before this pin can exercise it";

    obj_data* const previous_object_list = object_list;

    // Matches both limits.cpp call sites exactly.
    int result = damage_credited(victim, victim, resolve_poisoner(*victim), 5, SPELL_POISON, 0);

    EXPECT_EQ(result, 1) << "the poison tick must be lethal against a 1-hit-point victim";
    EXPECT_EQ(character_list, nullptr)
        << "extract_char()'s NPC arm must have unlinked the dead victim from character_list";
    EXPECT_EQ(poisoner.specials.fighting, nullptr)
        << "the credited poisoner must never be engaged by the tick that kills it -- "
           "damage_credited() only ever engages `attacker` (the self-ticking victim), never "
           "`credited_killer`";

    release_corpse(kPoisonRoom, previous_object_list);
    remove_char_exists(kPoisonerSlot);
}

// ---------------------------------------------------------------------------
// TASK-026 port, Task 12: kill_contributor_list / kill_contributors() /
// pkill_weight() / pkill_opponents(). See this file's header comment for the
// death-path observation-strategy convention this suite follows; the notes
// below explain the ONE place this group departs from it.
//
// die()'s own contributor-union code (fight.cpp's `else` arm reached only for
// a non-NPC dead_man) is NOT exercised end-to-end here. Driving it for real
// needs a heap PC victim that survives raw_kill()'s full non-NPC branch --
// save_char()/Crash_crashsave()/extract_char(dead_man, room) -- which this
// depot's test harness has never exercised for a non-NPC character (every
// existing death-path test, including this file's own and mage_tests.cpp's
// fireball-suicide suite, builds an NPC victim). Two concrete blockers found
// during a real attempt at a PC pin: (1) free_char()'s non-NPC arm
// unconditionally RELEASE()s player.name/title/short_descr/long_descr/
// description/profs, so every one of those fields must be a real str_dup()
// allocation rather than a stack/literal string (the NPC fixtures above rely
// on nr==0 skipping that RELEASE arm, which does not apply to a PC); (2)
// add_exploit_record()'s EXPLOIT_POISON/EXPLOIT_PK/EXPLOIT_DEATH calls funnel
// through write_exploits(), which -- for a desc-less character -- performs a
// REAL file write via write_exploit_record_for_character(".", ...) relative
// to the test binary's working directory, a side effect no existing test in
// this suite (or pkill.cpp itself) is set up to sandbox or clean up.
//
// Per the task's test-adaptation policy, the fallback is to pin the same
// claims one layer down, where they are directly and safely observable:
//   - kill_contributors()'s union/exclusion rules (this is exactly what
//     die()'s `const kill_contributor_list contributors =
//     kill_contributors(dead_man, killer);` calls) -- pins below.
//   - the `if (contributors.count > 0)` guard that stands in for the old
//     `attack_type == SPELL_POISON && !fighting` early-out -- pinned by
//     KillContributorsEmptyWhenNoPoisonerAndNoFighters below, which is the
//     exact shape of "a poison death with no resolvable poisoner and nobody
//     fighting": die() would see contributors.count == 0 and skip both
//     pkill_create() and the EXPLOIT_PK record.
//   - pkill_weight()/pkill_opponents() (and, through it, pkill_valid_killer())
//     consuming a hand-built kill_contributor_list exactly as
//     pkill_create() does, without pkill_create()'s own file-writing tail
//     (pkill_update_file()/PKILL_FILE) or player_table dependency.
//   - raw_kill()'s `died_to_player` no longer special-casing
//     `attack_type == SPELL_POISON`: this is a one-line boolean-expression
//     diff (`killer != NULL && !IS_NPC(killer)`, the `attack_type ==
//     SPELL_POISON ||` term removed) with no branching of its own left to
//     mis-port; PoisonTickCreditsTheResolvedPoisonerWithoutEngagingIt above
//     already pins that a poison death's `killer` argument is exactly
//     resolve_poisoner()'s answer (nullptr once the poisoner is gone, the
//     live poisoner otherwise), which is the only fact `died_to_player`'s new
//     form depends on. A test that also drove raw_kill()'s non-NPC branch
//     would only be re-checking C++ operator semantics on that answer, not
//     new logic -- it is not attempted here for that reason, independent of
//     the PC-fixture blockers above.
//   - Task 8's FightCredit.NullCreditFallsBackToTheEngagedOpponent (this
//     file, above) already pins "a fighting victim's sourceless death credits
//     the engaged opponent" at the damage_credited() layer that feeds
//     die()'s `killer` argument; not duplicated here.
namespace {

// Saves/restores the global combat_list around a kill_contributors() pin, so
// one test's fighters never leak into another's (or into an unrelated
// suite's) walk of the list. Mirrors damage_tests.cpp's
// `combat_list = nullptr;` TearDown reset, scoped per-test via RAII instead.
struct CombatListGuard {
    char_data* previous;
    CombatListGuard()
        : previous(combat_list)
    {
        combat_list = nullptr;
    }
    ~CombatListGuard() { combat_list = previous; }
};

} // namespace

// Pin: kill_contributor_list::add() refuses a null candidate and a duplicate
// without growing count, and contains() answers accordingly. No death or
// global state involved -- pure list-method pins.
TEST(KillContributorList, AddRefusesNullAndDuplicate)
{
    kill_contributor_list contributors;
    char_data candidate {};

    EXPECT_FALSE(contributors.add(nullptr)) << "a null candidate must never be added";
    EXPECT_EQ(contributors.count, 0);

    EXPECT_TRUE(contributors.add(&candidate)) << "a fresh, non-null candidate must be added";
    EXPECT_EQ(contributors.count, 1);
    EXPECT_TRUE(contributors.contains(&candidate));

    EXPECT_FALSE(contributors.add(&candidate)) << "a duplicate candidate must be refused";
    EXPECT_EQ(contributors.count, 1) << "count must not grow for a refused duplicate";
    EXPECT_FALSE(contributors.contains(nullptr)) << "null is never a member";
}

// Pin: the 33rd distinct candidate (one past kCapacity == 32) is refused and
// overflow_logged latches true; a 34th distinct candidate is refused too and
// overflow_logged stays true (the "logs once" guarantee -- observed through
// the latch itself, since this depot has no vmudlog capture seam; the
// `if (!overflow_logged)` guard in kill_contributor_list::add() is what a
// double-log would have to bypass, so the latch not re-arming is the
// behavioral proxy for "logs once").
TEST(KillContributorList, AddRefusesPastCapacityAndLatchesOverflowOnce)
{
    kill_contributor_list contributors;
    char_data candidates[kill_contributor_list::kCapacity + 2] {};

    for (int i = 0; i < kill_contributor_list::kCapacity; ++i) {
        EXPECT_TRUE(contributors.add(&candidates[i]))
            << "candidate " << i << " is within capacity and must be added";
    }
    EXPECT_EQ(contributors.count, kill_contributor_list::kCapacity);
    EXPECT_FALSE(contributors.overflow_logged) << "overflow must not latch before capacity is exceeded";

    EXPECT_FALSE(contributors.add(&candidates[kill_contributor_list::kCapacity]))
        << "the 33rd distinct candidate must be refused";
    EXPECT_EQ(contributors.count, kill_contributor_list::kCapacity) << "count must not grow past capacity";
    EXPECT_TRUE(contributors.overflow_logged) << "the first refusal must latch the overflow flag";

    EXPECT_FALSE(contributors.add(&candidates[kill_contributor_list::kCapacity + 1]))
        << "a 34th distinct candidate must also be refused";
    EXPECT_EQ(contributors.count, kill_contributor_list::kCapacity);
    EXPECT_TRUE(contributors.overflow_logged) << "overflow_logged must remain latched, not re-armed";
}

// Pin: kill_contributors()'s pet redirect -- an NPC pet fighting the victim,
// whose master shares its room, contributes as the MASTER, never the pet
// itself. Exercises offer_kill_contributor() indirectly (it has no external
// linkage), via the combat_list walk kill_contributors() performs.
TEST(KillContributors, PetFightingTheVictimIsRedirectedToItsSameRoomMaster)
{
    CombatListGuard combat_list_guard;

    char_data victim {};
    victim.specials2.act = 0; // a PC victim -- NPC-ness is irrelevant to this pin
    victim.player.level = 20;

    char_data master {};
    master.player.level = 25;
    master.in_room = 5;
    master.specials.fighting = nullptr;

    char_data pet {};
    pet.specials2.act = MOB_ISNPC | MOB_PET;
    pet.player.level = 10;
    pet.in_room = 5; // same room as master
    pet.master = &master;
    pet.specials.fighting = &victim;

    combat_list = &pet;
    pet.next_fighting = nullptr;

    kill_contributor_list contributors = kill_contributors(&victim, nullptr);

    EXPECT_EQ(contributors.count, 1) << "the pet must be redirected to its master, not added itself";
    ASSERT_GE(contributors.count, 1);
    EXPECT_EQ(contributors.entries[0], &master);
    EXPECT_FALSE(contributors.contains(&pet)) << "the pet itself must never land in the contributor list";
}

// Companion to the redirect pin above: when the pet's master is NOT in the
// pet's room, damage_credited()'s own redirect condition fails, so the pet
// contributes as itself.
TEST(KillContributors, PetFightingTheVictimContributesAsItselfWhenMasterIsElsewhere)
{
    CombatListGuard combat_list_guard;

    char_data victim {};
    victim.player.level = 20;

    char_data master {};
    master.player.level = 25;
    master.in_room = 9; // a different room from the pet below

    char_data pet {};
    pet.specials2.act = MOB_ISNPC | MOB_PET;
    pet.player.level = 10;
    pet.in_room = 5;
    pet.master = &master;
    pet.specials.fighting = &victim;

    combat_list = &pet;
    pet.next_fighting = nullptr;

    kill_contributor_list contributors = kill_contributors(&victim, nullptr);

    EXPECT_EQ(contributors.count, 1);
    ASSERT_GE(contributors.count, 1);
    EXPECT_EQ(contributors.entries[0], &pet) << "with the master elsewhere, the pet contributes as itself";
}

// Pin: an immortal candidate is never a contributor, regardless of how it
// reaches offer_kill_contributor() (here: a plain combat_list fighter).
TEST(KillContributors, ImmortalCandidateIsExcluded)
{
    CombatListGuard combat_list_guard;

    char_data victim {};
    victim.player.level = 20;

    char_data immortal_fighter {};
    immortal_fighter.player.level = LEVEL_IMMORT;
    immortal_fighter.specials.fighting = &victim;

    combat_list = &immortal_fighter;
    immortal_fighter.next_fighting = nullptr;

    kill_contributor_list contributors = kill_contributors(&victim, nullptr);

    EXPECT_EQ(contributors.count, 0) << "an immortal must never be added as a contributor";
}

// Pin: the victim itself is never a contributor, even when handed in
// directly as `primary` (die() never does this, but the exclusion rule in
// offer_kill_contributor() applies to every candidate uniformly).
TEST(KillContributors, VictimItselfIsNeverAContributor)
{
    CombatListGuard combat_list_guard;

    char_data victim {};
    victim.player.level = 20;

    kill_contributor_list contributors = kill_contributors(&victim, &victim);

    EXPECT_EQ(contributors.count, 0) << "the victim must never be added as its own contributor";
}

// Pin: kill_contributors() unions all three sources -- combat_list fighters,
// the resolved poisoner, and the primary killer -- without duplicating a
// candidate that appears in more than one source (here: `fighter_one` is
// both a combat_list fighter AND the `primary` argument).
TEST(KillContributors, UnionsFightersPoisonerAndPrimaryWithoutDuplicates)
{
    CombatListGuard combat_list_guard;
    // Distinct from every other abs_number slot this test binary's suites
    // claim (see this file's own kPoisonerSlot comment above and
    // poison_origin_tests.cpp's list); kPoisonerSlot (-801) is already used
    // earlier in this file, so this pin claims -802.
    constexpr int kContributorsPoisonerSlot = MAX_CHARACTERS - 802;

    char_data victim {};
    victim.player.level = 20;

    char_data fighter_one {};
    fighter_one.player.level = 15;
    fighter_one.specials.fighting = &victim;

    char_data fighter_two {};
    fighter_two.player.level = 30;
    fighter_two.specials.fighting = &victim;

    fighter_one.next_fighting = &fighter_two;
    fighter_two.next_fighting = nullptr;
    combat_list = &fighter_one;

    char_data poisoner {};
    poisoner.player.level = 40;
    poisoner.abs_number = kContributorsPoisonerSlot;
    set_char_exists(kContributorsPoisonerSlot, &poisoner);
    record_poison_origin(&victim, &poisoner);
    ASSERT_EQ(resolve_poisoner(victim), &poisoner) << "the poisoner must resolve before this pin can exercise it";

    // `primary` duplicates `fighter_one` -- must not double-add it.
    kill_contributor_list contributors = kill_contributors(&victim, &fighter_one);

    EXPECT_EQ(contributors.count, 3) << "fighter_one, fighter_two and the poisoner, with no duplicate for "
                                         "fighter_one appearing as both a fighter and `primary`";
    EXPECT_TRUE(contributors.contains(&fighter_one));
    EXPECT_TRUE(contributors.contains(&fighter_two));
    EXPECT_TRUE(contributors.contains(&poisoner));

    record_poison_origin(&victim, nullptr);
    remove_char_exists(kContributorsPoisonerSlot);
}

// Pin: "a poison death with no resolvable poisoner and nobody fighting
// records nothing" -- the exact condition die()'s
// `const kill_contributor_list contributors = kill_contributors(dead_man,
// killer);` followed by `if (contributors.count > 0)` guards. A fresh
// char_data{} already has no recorded poisoner (poisoned_by defaults to
// nullptr, which resolve_poisoner() treats as "nobody" regardless of
// poisoned_by_abs_number) and no combat_list entry, so this needs no extra
// setup beyond an empty combat_list and a null primary -- exactly a poison
// tick whose resolved poisoner is gone and whose victim is not otherwise
// fighting anyone (limits.cpp passes `resolve_poisoner(*i)` as the primary
// killer argument for the poison DoT, so a gone poisoner surfaces here as
// `primary == nullptr`).
TEST(KillContributors, EmptyWhenNoPoisonerAndNoFightersRecordsNothing)
{
    CombatListGuard combat_list_guard;

    char_data victim {};
    victim.player.level = 20;

    kill_contributor_list contributors = kill_contributors(&victim, nullptr);

    EXPECT_EQ(contributors.count, 0)
        << "with no fighters, no resolvable poisoner and no primary killer, die()'s "
           "`if (contributors.count > 0)` guard must skip pkill_create() and the EXPLOIT_PK record";
}

// Pin: pkill_weight() sums GET_LEVEL() over every contributor, NPCs included
// -- the breadth pkill.cpp's original combat_list walk had, now read from a
// hand-built kill_contributor_list instead. victim.specials.attacked_level is
// left at its zero default so pkill_weight()'s `MAX(attacked_level,
// total_levels)` picks total_levels.
TEST(PkillWeightAndOpponents, WeightSumsContributorLevelsIncludingNpcLevels)
{
    char_data victim {};
    victim.player.level = 50;
    victim.specials.attacked_level = 0;

    char_data npc_contributor {};
    npc_contributor.specials2.act = MOB_ISNPC;
    npc_contributor.player.level = 20;

    char_data pc_contributor {};
    pc_contributor.player.level = 30;

    kill_contributor_list contributors;
    contributors.add(&npc_contributor);
    contributors.add(&pc_contributor);

    // total_levels = 20 + 30 = 50; weight = GET_LEVEL(victim) * 1000 / (50 * 50).
    EXPECT_EQ(pkill_weight(&victim, contributors), 50 * 1000 / (50 * 50))
        << "an NPC contributor's level must count toward the weight exactly like a PC's";
}

// Pin: pkill_opponents() counts only the contributors pkill_valid_killer()
// accepts, exercising every one of its gates: a valid PC; an NPC lacking one
// of the required MOB_PET/MOB_ORC_FRIEND flags (invalid, per
// pkill_valid_killer()'s literal `!MOB_FLAGGED(..., ORC_FRIEND) ||
// !MOB_FLAGGED(..., PET)` condition -- both flags are required); an immortal
// PC (invalid); a valid orc-friend pet whose master is fighting someone
// else; and an otherwise-valid orc-friend pet whose master is ALSO fighting
// the victim (invalid -- pkill_valid_killer()'s "don't double-count the
// leader's own engagement" rule).
TEST(PkillWeightAndOpponents, OpponentsCountsOnlyValidKillers)
{
    char_data victim {};
    victim.player.level = 20;

    char_data valid_pc {};
    valid_pc.player.level = 20;

    char_data npc_missing_flags {};
    npc_missing_flags.specials2.act = MOB_ISNPC;
    npc_missing_flags.player.level = 20;

    char_data immortal_pc {};
    immortal_pc.player.level = LEVEL_IMMORT + 4;

    char_data uninvolved_leader {};
    uninvolved_leader.player.level = 25;
    uninvolved_leader.specials.fighting = nullptr; // fighting someone else (or nobody)

    char_data valid_orc_pet {};
    valid_orc_pet.specials2.act = MOB_ISNPC | MOB_PET | MOB_ORC_FRIEND;
    valid_orc_pet.player.level = 15;
    valid_orc_pet.master = &uninvolved_leader;

    char_data engaged_leader {};
    engaged_leader.player.level = 25;
    engaged_leader.specials.fighting = &victim; // the leader is ALSO fighting the victim

    char_data invalid_orc_pet {};
    invalid_orc_pet.specials2.act = MOB_ISNPC | MOB_PET | MOB_ORC_FRIEND;
    invalid_orc_pet.player.level = 15;
    invalid_orc_pet.master = &engaged_leader;

    ASSERT_TRUE(pkill_valid_killer(&valid_pc, &victim));
    ASSERT_FALSE(pkill_valid_killer(&npc_missing_flags, &victim));
    ASSERT_FALSE(pkill_valid_killer(&immortal_pc, &victim));
    ASSERT_TRUE(pkill_valid_killer(&valid_orc_pet, &victim));
    ASSERT_FALSE(pkill_valid_killer(&invalid_orc_pet, &victim));

    kill_contributor_list contributors;
    contributors.add(&valid_pc);
    contributors.add(&npc_missing_flags);
    contributors.add(&immortal_pc);
    contributors.add(&valid_orc_pet);
    contributors.add(&invalid_orc_pet);

    EXPECT_EQ(pkill_opponents(&victim, contributors), 2)
        << "only valid_pc and valid_orc_pet must be counted as opponents";
}
