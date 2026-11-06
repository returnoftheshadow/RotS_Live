/* The affects spells give start from a cleared record: a field the spell does not set, such as the
 * counter, is 0 rather than whatever was on the stack. The counter is copied onto the character and
 * saved with it. */
#include "scripted_room_fixture.h"

#include "../spells.h"

using namespace scripted_room;

namespace {

/* How much of the stack dirty_the_stack fills. */
constexpr int DIRTY_STACK_BYTES = 16384;
/* The pattern dirty_the_stack leaves; any non-zero byte would do. */
constexpr unsigned char DIRTY_BYTE = 0xA5;

/* Fills a stretch of the stack below the caller with DIRTY_BYTE, so that a local the next call
 * leaves unset is unlikely to be 0 by chance. */
void dirty_the_stack()
{
    volatile unsigned char scratch[DIRTY_STACK_BYTES];
    for (int index = 0; index < DIRTY_STACK_BYTES; ++index) {
        scratch[index] = DIRTY_BYTE;
    }
}

/* Called through a volatile pointer, so it cannot be inlined into the test's own frame. */
void (*volatile dirty_the_stack_call)() = dirty_the_stack;

class SpellAffectTest : public ScriptedRoomFixture {
protected:
    /* Casts <spell> as <caster> at <victim>, just after dirtying the stack the spell will use. */
    static void cast_on_a_dirty_stack(void (*spell)(char_data*, char*, int, char_data*, obj_data*, int, int),
        char_data* caster, char_data* victim)
    {
        static char empty[1] = "";
        dirty_the_stack_call();
        spell(caster, empty, SPELL_TYPE_SPELL, victim, nullptr, 0, 0);
    }
};

} // namespace

TEST_F(SpellAffectTest, ResistPoisonGivesAnAffectWithACounterOfZero)
{
    char_data* mob = make_mob("patient", 0);
    affected_type poison {};
    poison.type = SPELL_POISON;
    poison.duration = 10;
    poison.location = APPLY_NONE;
    affect_to_char(mob, &poison);

    cast_on_a_dirty_stack(spell_resist_poison, mob, mob);

    const affected_type* resistance = affected_by_spell(mob, SPELL_RESIST_POISON);
    ASSERT_NE(resistance, nullptr);
    EXPECT_EQ(resistance->counter, 0);
    EXPECT_EQ(resistance->duration, poison.duration);
}

TEST_F(SpellAffectTest, ShieldGivesAnAffectWithACounterOfZero)
{
    char_data* mob = make_mob("mage", 0);

    cast_on_a_dirty_stack(spell_shield, mob, mob);

    const affected_type* shield = affected_by_spell(mob, SPELL_SHIELD);
    ASSERT_NE(shield, nullptr);
    EXPECT_EQ(shield->counter, 0);
    EXPECT_EQ(shield->bitvector, AFF_SHIELD);
}
