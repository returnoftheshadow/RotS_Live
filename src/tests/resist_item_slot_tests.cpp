/* Ownership of an element's resist slot when more than one item grants it.
 *
 * Each element has exactly one affect slot. Between worn items the strongest holds it, in
 * whatever order they went on: a weaker item put on beside a stronger one changes nothing and
 * prints nothing. Login re-equips by wear slot rather than in the order the player dressed, so
 * this is what keeps a character's value the same across a relog or reboot.
 *
 * Taking an item off only clears the slot when that item was the one holding it, and the
 * strongest item still worn then takes it back without a message - the player never lost the
 * resistance.
 *
 * Before 2026-09-22 the last item worn owned the slot and any unequip on the element cleared
 * it; the maintainer asked for this rule instead. The rule sits under equip_char/unequip_char,
 * far from anything obviously about resistances, so these tests pin it: if one fails, decide
 * whether the behaviour was meant to change and record that - do not relax it to go green.
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
#include <string>

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
    descriptor_data descriptor {};

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

        descriptor.output = descriptor.small_outbuf;
        descriptor.bufspace = SMALL_BUFSIZE - 1;
        character.desc = &descriptor;
        clear_output();
    }

    /* What the character has been told since the last clear. */
    std::string output() const { return descriptor.output; }
    void clear_output()
    {
        descriptor.small_outbuf[0] = '\0';
        descriptor.bufptr = 0;
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
        character.desc = nullptr;
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

int count_affects_in_slot(const char_data* character, int spell)
{
    int found = 0;
    int count = 0;
    for (affected_type* aff = character->affected; aff && count < MAX_AFFECT; aff = aff->next, count++) {
        if (aff->type == spell)
            ++found;
    }
    return found;
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

TEST(ItemResistSlot, TheStrongestWornItemHoldsTheSlotWhicheverGoesOnFirst)
{
    {
        ItemResistBench bench;
        equip_char(&bench.character, bench.spawn(kStrongFireRingVnum), WEAR_FINGER_R);
        equip_char(&bench.character, bench.spawn(kWeakFireRingVnum), WEAR_FINGER_L);

        EXPECT_EQ(granted_percent(&bench.character, SPELL_RESIST_FIRE), kStrongFirePercent)
            << "a weaker item put on second must not downgrade the stronger one";
        EXPECT_EQ(count_affects_in_slot(&bench.character, SPELL_RESIST_FIRE), 1);
    }
    {
        ItemResistBench bench;
        equip_char(&bench.character, bench.spawn(kWeakFireRingVnum), WEAR_FINGER_R);
        equip_char(&bench.character, bench.spawn(kStrongFireRingVnum), WEAR_FINGER_L);

        EXPECT_EQ(granted_percent(&bench.character, SPELL_RESIST_FIRE), kStrongFirePercent)
            << "a stronger item put on second takes the slot";
        EXPECT_EQ(count_affects_in_slot(&bench.character, SPELL_RESIST_FIRE), 1);
    }
}

TEST(ItemResistSlot, AWeakerItemGoingOnSaysNothingAndAStrongerOneAnnouncesItself)
{
    ItemResistBench bench;
    equip_char(&bench.character, bench.spawn(kWeakFireRingVnum), WEAR_FINGER_R);
    ASSERT_NE(bench.output().find("You feel resistant to fire!"), std::string::npos)
        << bench.output();

    bench.clear_output();
    equip_char(&bench.character, bench.spawn(kStrongFireRingVnum), WEAR_FINGER_L);
    EXPECT_NE(bench.output().find("You feel resistant to fire!"), std::string::npos)
        << "the stronger item actually changed what the player has: " << bench.output();

    obj_data* weak = unequip_char(&bench.character, WEAR_FINGER_R);
    ASSERT_NE(weak, nullptr);
    bench.clear_output();
    equip_char(&bench.character, weak, WEAR_FINGER_R);
    EXPECT_EQ(bench.output().find("resistant"), std::string::npos)
        << "a weaker item going on beside a stronger one changes nothing: " << bench.output();
}

TEST(ItemResistSlot, RemovingTheWeakerItemLeavesTheStrongerOneInPlace)
{
    ItemResistBench bench;
    equip_char(&bench.character, bench.spawn(kStrongFireRingVnum), WEAR_FINGER_R);
    equip_char(&bench.character, bench.spawn(kWeakFireRingVnum), WEAR_FINGER_L);

    bench.clear_output();
    unequip_char(&bench.character, WEAR_FINGER_L);

    EXPECT_EQ(granted_percent(&bench.character, SPELL_RESIST_FIRE), kStrongFirePercent)
        << "the item coming off never held the slot, so the slot is untouched";
    EXPECT_TRUE(bench.character.specials.resistance & (1 << RESIST_FIRE));
    EXPECT_EQ(bench.output().find("resistant"), std::string::npos) << bench.output();
}

TEST(ItemResistSlot, RemovingTheStrongerItemFallsBackToTheWeakerOneQuietly)
{
    ItemResistBench bench;
    equip_char(&bench.character, bench.spawn(kStrongFireRingVnum), WEAR_FINGER_R);
    equip_char(&bench.character, bench.spawn(kWeakFireRingVnum), WEAR_FINGER_L);

    bench.clear_output();
    unequip_char(&bench.character, WEAR_FINGER_R);

    ASSERT_NE(bench.character.equipment[WEAR_FINGER_L], nullptr);
    EXPECT_EQ(granted_percent(&bench.character, SPELL_RESIST_FIRE), kWeakFirePercent)
        << "the strongest item still worn takes the slot back";
    EXPECT_EQ(count_affects_in_slot(&bench.character, SPELL_RESIST_FIRE), 1);
    EXPECT_TRUE(bench.character.specials.resistance & (1 << RESIST_FIRE));
    EXPECT_EQ(bench.output().find("resistant"), std::string::npos)
        << "the player never lost the resistance, so nothing is announced: " << bench.output();
}

TEST(ItemResistSlot, RemovingTheLastItemClearsTheSlot)
{
    ItemResistBench bench;
    equip_char(&bench.character, bench.spawn(kStrongFireRingVnum), WEAR_FINGER_R);
    equip_char(&bench.character, bench.spawn(kWeakFireRingVnum), WEAR_FINGER_L);

    unequip_char(&bench.character, WEAR_FINGER_R);
    unequip_char(&bench.character, WEAR_FINGER_L);

    EXPECT_EQ(granted_percent(&bench.character, SPELL_RESIST_FIRE), -1);
    EXPECT_FALSE(bench.character.specials.resistance & (1 << RESIST_FIRE));
}

TEST(ItemResistSlot, AnItemHeldWhereItCannotBeHeldIsNotCountedOnFallback)
{
    // equip_char() applies no affects for an item in HOLD that is not holdable, so the fallback
    // after an unequip must not hand its strength out either.
    ItemResistBench bench;
    obj_data* strong_ring = bench.spawn(kStrongFireRingVnum);
    obj_data* weak_ring = bench.spawn(kWeakFireRingVnum);
    REMOVE_BIT(weak_ring->obj_flags.wear_flags, ITEM_HOLD);

    equip_char(&bench.character, strong_ring, WEAR_FINGER_R);
    equip_char(&bench.character, weak_ring, HOLD);
    ASSERT_EQ(bench.character.equipment[HOLD], weak_ring);

    unequip_char(&bench.character, WEAR_FINGER_R);

    EXPECT_EQ(granted_percent(&bench.character, SPELL_RESIST_FIRE), -1)
        << "the held item never granted anything";
}
