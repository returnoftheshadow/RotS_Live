/* Ownership of an element's resist slot when more than one item grants it.
 *
 * Each element has exactly one affect slot, and an item always seizes it: do_resist_spell's
 * is_object branch removes whatever holds the element before installing its own. That rule is
 * inherited from spell_evasion and predates per-element magnitudes; while every item merely set
 * the same flag it was invisible, and it only became observable once items carried differing
 * strengths.
 *
 * Two of the tests below therefore lock in behaviour that is KNOWN TO BE WRONG and has been
 * accepted as-is rather than fixed:
 *
 *   - the last item worn owns the element, not the strongest, so a weaker item silently
 *     downgrades a stronger one;
 *   - removing the newer of two items on the same element leaves the older one worn and
 *     granting nothing at all, because its affect was destroyed rather than suppressed.
 *
 * They are pinned deliberately. The slot rule sits under equip_char/unequip_char, far from
 * anything obviously about resistances, so a change here would otherwise land silently and
 * shift live damage numbers with nothing to catch it. If a test here fails, decide whether the
 * behaviour was meant to change and update the test with that decision recorded - do not relax
 * it to make a build green.
 *
 * The harness drives the real equip_char/unequip_char path rather than calling do_resist_spell,
 * so it covers the APPLY_SPELL decode and the affect_total pass as well as the slot rule.
 */

#include "gtest/gtest.h"

#include "db.h"
#include "handler.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"

#include <cstring>

extern struct index_data* obj_index;
extern struct obj_data* obj_proto;
extern int top_of_objt;
extern struct obj_data* object_list;

namespace {

constexpr int kStrongFireRingVnum = 9001;
constexpr int kColdRingVnum = 9002;
constexpr int kWeakFireRingVnum = 9003;

constexpr int kStrongFirePercent = 40;
constexpr int kColdPercent = 25;
constexpr int kWeakFirePercent = 10;

/* An item grants a resistance through APPLY_SPELL, whose modifier packs the spell number in
   the low byte and the strength above it - the same "A 27 <pct*256 + spell>" a builder writes. */
int resist_payload(int percent, int spell) { return percent * 256 + spell; }

/* Three wearable prototypes and a character to put them on. Object state is global, so the
   previous prototype table is saved and restored around each test. */
struct ItemResistBench {
    index_data* previous_obj_index;
    obj_data* previous_obj_proto;
    int previous_top_of_objt;
    obj_data* previous_object_list;

    char_data character {};
    char character_name[16] = "slotbench";

    ItemResistBench()
        : previous_obj_index(obj_index)
        , previous_obj_proto(obj_proto)
        , previous_top_of_objt(top_of_objt)
        , previous_object_list(object_list)
    {
        obj_index = new index_data[3] {};
        obj_proto = new obj_data[3] {};
        top_of_objt = 2;
        object_list = nullptr;

        define_prototype(0, kStrongFireRingVnum, "ring fire", "a heavy fire ring",
            resist_payload(kStrongFirePercent, SPELL_RESIST_FIRE));
        define_prototype(1, kColdRingVnum, "ring cold", "a pale cold ring",
            resist_payload(kColdPercent, SPELL_RESIST_COLD));
        define_prototype(2, kWeakFireRingVnum, "amulet fire", "a thin fire amulet",
            resist_payload(kWeakFirePercent, SPELL_RESIST_FIRE));

        character.specials2.act = MOB_ISNPC;
        character.player.short_descr = character_name;
        character.player.race = RACE_HUMAN;
        character.player.level = 20;
        character.in_room = NOWHERE;
    }

    void define_prototype(int slot, int vnum, const char* name, const char* short_description,
        int apply_spell_modifier)
    {
        obj_index[slot].virt = vnum;
        obj_index[slot].number = 0;
        obj_index[slot].func = 0;

        clear_object(&obj_proto[slot]);
        obj_proto[slot].name = strdup(name);
        obj_proto[slot].short_description = strdup(short_description);
        obj_proto[slot].description = strdup("It lies here.");
        obj_proto[slot].item_number = slot;
        obj_proto[slot].obj_flags.type_flag = ITEM_TREASURE;
        obj_proto[slot].obj_flags.wear_flags = ITEM_TAKE | ITEM_WEAR_FINGER | ITEM_HOLD;
        obj_proto[slot].obj_flags.weight = 0;
        obj_proto[slot].affected[0].location = APPLY_SPELL;
        obj_proto[slot].affected[0].modifier = apply_spell_modifier;
    }

    obj_data* spawn(int vnum) { return read_object(vnum, VIRT); }

    ~ItemResistBench()
    {
        while (character.affected)
            affect_remove(&character, character.affected);

        while (object_list != nullptr) {
            obj_data* next = object_list->next;
            delete object_list;
            object_list = next;
        }

        for (int i = 0; i < 3; ++i) {
            free(obj_proto[i].name);
            free(obj_proto[i].short_description);
            free(obj_proto[i].description);
        }
        delete[] obj_proto;
        delete[] obj_index;

        obj_proto = previous_obj_proto;
        obj_index = previous_obj_index;
        top_of_objt = previous_top_of_objt;
        object_list = previous_object_list;
    }
};

/* The strength the character currently has against an element, or -1 when no affect holds it. */
int granted_percent(char_data* character, int spell)
{
    affected_type* affect = affected_by_spell(character, spell);
    return affect ? affect->effect_modifier : -1;
}

} // namespace

TEST(ItemResistSlot, EachElementHasItsOwnSlotSoUnrelatedItemsDoNotInterfere)
{
    ItemResistBench bench;

    obj_data* fire_ring = bench.spawn(kStrongFireRingVnum);
    obj_data* cold_ring = bench.spawn(kColdRingVnum);
    ASSERT_NE(fire_ring, nullptr);
    ASSERT_NE(cold_ring, nullptr);

    equip_char(&bench.character, fire_ring, WEAR_FINGER_R);
    equip_char(&bench.character, cold_ring, WEAR_FINGER_L);

    ASSERT_EQ(granted_percent(&bench.character, SPELL_RESIST_FIRE), kStrongFirePercent);
    ASSERT_EQ(granted_percent(&bench.character, SPELL_RESIST_COLD), kColdPercent);

    unequip_char(&bench.character, WEAR_FINGER_R);

    EXPECT_EQ(granted_percent(&bench.character, SPELL_RESIST_FIRE), -1);
    EXPECT_FALSE(bench.character.specials.resistance & (1 << RESIST_FIRE));

    EXPECT_EQ(granted_percent(&bench.character, SPELL_RESIST_COLD), kColdPercent)
        << "removing an item must not disturb an element it does not grant";
    EXPECT_TRUE(bench.character.specials.resistance & (1 << RESIST_COLD))
        << "affect_total rebuilds both masks on every equip change; the surviving affect "
           "has to put its own bit back";
}

TEST(ItemResistSlot, TheLastItemWornOwnsTheElementEvenIfItIsWeaker)
{
    // KNOWN DEFECT, pinned on purpose. Largest-wins governs a cast sitting alongside an item,
    // but not two items: the second one removes the first instead of joining it, so a 10%
    // amulet worn over a 40% ring downgrades the wearer rather than being ignored.
    ItemResistBench bench;

    obj_data* strong_ring = bench.spawn(kStrongFireRingVnum);
    obj_data* weak_amulet = bench.spawn(kWeakFireRingVnum);
    ASSERT_NE(strong_ring, nullptr);
    ASSERT_NE(weak_amulet, nullptr);

    equip_char(&bench.character, strong_ring, WEAR_FINGER_R);
    ASSERT_EQ(granted_percent(&bench.character, SPELL_RESIST_FIRE), kStrongFirePercent);

    equip_char(&bench.character, weak_amulet, WEAR_FINGER_L);

    EXPECT_EQ(granted_percent(&bench.character, SPELL_RESIST_FIRE), kWeakFirePercent)
        << "the stronger item's affect was destroyed, not merely outranked";
}

TEST(ItemResistSlot, RemovingTheNewerItemLeavesTheOlderOneGrantingNothing)
{
    // KNOWN DEFECT, pinned on purpose. The older item is still worn, but the affect that
    // represented it was removed when the newer item seized the slot, and nothing replays it:
    // affect_total skips APPLY_SPELL gear affects, so only a fresh equip re-fires one.
    ItemResistBench bench;

    obj_data* strong_ring = bench.spawn(kStrongFireRingVnum);
    obj_data* weak_amulet = bench.spawn(kWeakFireRingVnum);

    equip_char(&bench.character, strong_ring, WEAR_FINGER_R);
    equip_char(&bench.character, weak_amulet, WEAR_FINGER_L);
    ASSERT_EQ(granted_percent(&bench.character, SPELL_RESIST_FIRE), kWeakFirePercent);

    unequip_char(&bench.character, WEAR_FINGER_L);

    ASSERT_NE(bench.character.equipment[WEAR_FINGER_R], nullptr)
        << "the stronger ring is still worn";
    EXPECT_EQ(granted_percent(&bench.character, SPELL_RESIST_FIRE), -1)
        << "a worn resist item is granting nothing";
    EXPECT_FALSE(bench.character.specials.resistance & (1 << RESIST_FIRE))
        << "and the element's bit is clear despite the item being worn";
}

TEST(ItemResistSlot, ReEquippingTheOlderItemRestoresIt)
{
    // The escape hatch, and the reason the defect above has never been reported: logging in
    // re-equips every worn item, and each equip re-fires APPLY_SPELL. Any relog or reboot
    // therefore repairs the lost resistance on its own.
    ItemResistBench bench;

    obj_data* strong_ring = bench.spawn(kStrongFireRingVnum);
    obj_data* weak_amulet = bench.spawn(kWeakFireRingVnum);

    equip_char(&bench.character, strong_ring, WEAR_FINGER_R);
    equip_char(&bench.character, weak_amulet, WEAR_FINGER_L);
    unequip_char(&bench.character, WEAR_FINGER_L);
    ASSERT_EQ(granted_percent(&bench.character, SPELL_RESIST_FIRE), -1);

    obj_data* removed = unequip_char(&bench.character, WEAR_FINGER_R);
    ASSERT_NE(removed, nullptr);
    equip_char(&bench.character, removed, WEAR_FINGER_R);

    EXPECT_EQ(granted_percent(&bench.character, SPELL_RESIST_FIRE), kStrongFirePercent);
    EXPECT_TRUE(bench.character.specials.resistance & (1 << RESIST_FIRE));
}
