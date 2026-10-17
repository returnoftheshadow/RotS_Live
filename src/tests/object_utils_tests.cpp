#include "../structs.h"
#include "ObjFlagDataBuilder.h"
#include <gtest/gtest.h>

TEST(ObjectFlagData, ClampsWeightToAtLeastOne) {
    obj_flag_data objFlagData{};
    objFlagData.weight = 0;

    EXPECT_EQ(objFlagData.get_weight(), 1)
        << "Expected get_weight() to clamp zero weight to the minimum supported value.";
}

TEST(ObjectFlagData, ReturnsStoredWeightWhenPositive) {
    obj_flag_data objFlagData{};
    objFlagData.weight = 37;

    EXPECT_EQ(objFlagData.get_weight(), 37)
        << "Expected get_weight() to return the stored positive item weight.";
}

TEST(ObjectFlagData, ReportsWeaponTypeAsWearable) {
    obj_flag_data objFlagData{};
    objFlagData.type_flag = ITEM_WEAPON;

    EXPECT_TRUE(objFlagData.is_wearable())
        << "Expected weapons to be considered wearable equipment.";
}

TEST(ObjectFlagData, ReportsTrashAsNotWearable) {
    obj_flag_data objFlagData{};
    objFlagData.type_flag = ITEM_TRASH;

    EXPECT_FALSE(objFlagData.is_wearable())
        << "Expected non-equipment item types like trash to not be wearable.";
}

TEST(ObjectData, ReportsContainerNamedQuiverAsQuiver) {
    obj_data object{};
    object.obj_flags.type_flag = ITEM_CONTAINER;
    char quiver_name[] = "small leather quiver";
    object.name = quiver_name;

    EXPECT_TRUE(object.is_quiver())
        << "Expected a container with 'quiver' in its keywords to be recognized as a quiver.";
}

TEST(ObjectData, ReportsNonContainerAsNotQuiver) {
    obj_data object{};
    object.obj_flags.type_flag = ITEM_WEAPON;
    char quiver_name[] = "small leather quiver";
    object.name = quiver_name;

    EXPECT_FALSE(object.is_quiver())
        << "Expected only containers to be recognized as quivers.";
}

TEST(ObjectData, ReportsBowWeaponAsRangedWeapon) {
    obj_data object{};
    object.obj_flags = builders::ObjFlagDataBuilder()
                           .setWeaponType(game_types::weapon_type::WT_BOW)
                           .build();
    object.obj_flags.type_flag = ITEM_WEAPON;

    EXPECT_TRUE(object.is_ranged_weapon())
        << "Expected bows to be recognized as ranged weapons.";
}

TEST(ObjectData, ReportsMeleeWeaponAsNotRangedWeapon) {
    obj_data object{};
    object.obj_flags = builders::ObjFlagDataBuilder()
                           .setWeaponType(game_types::weapon_type::WT_SLASHING)
                           .build();
    object.obj_flags.type_flag = ITEM_WEAPON;

    EXPECT_FALSE(object.is_ranged_weapon())
        << "Expected slashing weapons to not be recognized as ranged weapons.";
}

#include "../spells.h"
#include "../utils.h"

namespace {

obj_data make_versioned_object(int type_flag, int version)
{
    obj_data obj {};
    obj.obj_flags.type_flag = type_flag;
    obj.obj_flags.version = version;
    for (int i = 0; i < MAX_OBJ_AFFECT; i++)
        obj.affected[i].location = APPLY_NONE;
    return obj;
}

} // namespace

TEST(ObjectVersionRefresh, NeverTouchesACopyWhilePrototypeVersionIsBlankOrOff)
{
    for (int proto_version : { 0, -3 }) {
        obj_data proto = make_versioned_object(ITEM_ARMOR, proto_version);
        proto.affected[0] = { APPLY_OB, 4 };

        obj_data copy = make_versioned_object(ITEM_ARMOR, 2);
        copy.affected[0] = { APPLY_OB, 3 };
        copy.obj_flags.extra_flags = ITEM_MAGIC;

        EXPECT_FALSE(refresh_object_to_prototype_version(&copy, &proto));
        EXPECT_EQ(copy.affected[0].modifier, 3);
        EXPECT_EQ(copy.obj_flags.extra_flags, ITEM_MAGIC);
        EXPECT_EQ(copy.obj_flags.version, 2);
    }
}

TEST(ObjectVersionRefresh, LeavesACopyAlreadyAtThePrototypeVersion)
{
    obj_data proto = make_versioned_object(ITEM_ARMOR, 5);
    proto.affected[0] = { APPLY_OB, 4 };
    obj_data copy = make_versioned_object(ITEM_ARMOR, 5);
    copy.affected[0] = { APPLY_OB, 9 };

    EXPECT_FALSE(refresh_object_to_prototype_version(&copy, &proto));
    EXPECT_EQ(copy.affected[0].modifier, 9);
}

TEST(ObjectVersionRefresh, TakesAffectsFlagsAndBitvectorFromThePrototypeAndKeepsPlayState)
{
    // Blank (0) and any other differing number both refresh; the number only has to differ.
    for (int copy_version : { 0, 3, 7, -4 }) {
        obj_data proto = make_versioned_object(ITEM_DRINKCON, 4);
        proto.affected[0] = { APPLY_MOVE, 10 };
        proto.obj_flags.extra_flags = ITEM_ANTI_GOOD;
        proto.obj_flags.bitvector = 8;
        proto.obj_flags.value[1] = 20;

        obj_data copy = make_versioned_object(ITEM_DRINKCON, copy_version);
        copy.affected[0] = { APPLY_OB, 2 };
        copy.affected[1] = { APPLY_STR, 1 };
        copy.obj_flags.extra_flags = ITEM_MAGIC;
        copy.obj_flags.bitvector = 1;
        copy.obj_flags.value[1] = 3; // partly drunk
        copy.obj_flags.timer = -1;
        copy.loaded_by = 77;

        EXPECT_TRUE(refresh_object_to_prototype_version(&copy, &proto));
        EXPECT_EQ(copy.affected[0].location, APPLY_MOVE);
        EXPECT_EQ(copy.affected[0].modifier, 10);
        EXPECT_EQ(copy.affected[1].location, APPLY_NONE);
        EXPECT_EQ(copy.obj_flags.extra_flags, ITEM_ANTI_GOOD);
        EXPECT_EQ(copy.obj_flags.bitvector, 8);
        EXPECT_EQ(copy.obj_flags.version, 4);
        EXPECT_EQ(copy.obj_flags.value[1], 3) << "drink contents are play state, not design";
        EXPECT_EQ(copy.obj_flags.timer, -1);
        EXPECT_EQ(copy.loaded_by, 77);
    }
}

namespace {

// What enchant weapon leaves on a plain weapon: the affect, the flags and the record.
obj_data make_enchanted_weapon(int version, int align)
{
    obj_data copy = make_versioned_object(ITEM_WEAPON, version);
    const obj_enchant_type enchant = { ENCHANT_OB, (byte)align, 6 };
    apply_object_enchant(&copy, enchant);
    record_object_enchant(&copy, enchant);
    return copy;
}

} // namespace

TEST(ObjectEnchant, ApplyAndRecordAreWhatTheSpellLeaves)
{
    obj_data good = make_enchanted_weapon(0, ENCHANT_ALIGN_GOOD);
    EXPECT_EQ(good.obj_flags.extra_flags, ITEM_MAGIC | ITEM_ANTI_EVIL);
    EXPECT_EQ(good.affected[0].location, APPLY_OB);
    EXPECT_EQ(good.affected[0].modifier, 6);
    EXPECT_EQ(count_object_enchants(&good), 1);
    EXPECT_EQ(object_enchants_text(&good), "ob +6 (good)");

    EXPECT_EQ(make_enchanted_weapon(0, ENCHANT_ALIGN_EVIL).obj_flags.extra_flags, ITEM_MAGIC | ITEM_ANTI_GOOD);
    EXPECT_EQ(make_enchanted_weapon(0, ENCHANT_ALIGN_NEUTRAL).obj_flags.extra_flags, ITEM_MAGIC);

    obj_data plain = make_versioned_object(ITEM_WEAPON, 0);
    EXPECT_EQ(count_object_enchants(&plain), 0);
    EXPECT_EQ(object_enchants_text(&plain), "none");
}

ASPELL(spell_enchant_weapon);

TEST(ObjectEnchant, TheSpellRecordsTheCastersAlignmentAndRefusesAMagicWeapon)
{
    const struct {
        int alignment;
        int align;
        long flags;
    } cases[] = {
        { 500, ENCHANT_ALIGN_GOOD, ITEM_MAGIC | ITEM_ANTI_EVIL },
        { -500, ENCHANT_ALIGN_EVIL, ITEM_MAGIC | ITEM_ANTI_GOOD },
        { 0, ENCHANT_ALIGN_NEUTRAL, ITEM_MAGIC },
    };
    for (const auto& expected : cases) {
        char_data caster {};
        GET_ALIGNMENT(&caster) = expected.alignment;
        obj_data sword = make_versioned_object(ITEM_WEAPON, 0);

        spell_enchant_weapon(&caster, nullptr, 0, nullptr, &sword, 0, 0);
        EXPECT_EQ(sword.obj_flags.extra_flags, expected.flags) << expected.alignment;
        EXPECT_EQ(sword.affected[0].location, APPLY_OB);
        EXPECT_EQ(sword.affected[0].modifier, 6);
        ASSERT_EQ(count_object_enchants(&sword), 1);
        EXPECT_EQ(sword.enchants[0].type, ENCHANT_OB);
        EXPECT_EQ(sword.enchants[0].align, expected.align);

        // A second cast finds it magic and leaves it as it is.
        spell_enchant_weapon(&caster, nullptr, 0, nullptr, &sword, 0, 0);
        EXPECT_EQ(sword.affected[0].modifier, 6);
        EXPECT_EQ(count_object_enchants(&sword), 1);
    }

    // A weapon the builder gave an affect holds too much magic already.
    char_data caster {};
    obj_data built = make_versioned_object(ITEM_WEAPON, 0);
    built.affected[1] = { APPLY_DODGE, 2 };
    spell_enchant_weapon(&caster, nullptr, 0, nullptr, &built, 0, 0);
    EXPECT_EQ(built.obj_flags.extra_flags, 0);
    EXPECT_EQ(built.affected[0].location, APPLY_NONE);
    EXPECT_EQ(count_object_enchants(&built), 0);

    // Armour is not a weapon.
    obj_data helm = make_versioned_object(ITEM_ARMOR, 0);
    spell_enchant_weapon(&caster, nullptr, 0, nullptr, &helm, 0, 0);
    EXPECT_EQ(helm.obj_flags.extra_flags, 0);
    EXPECT_EQ(count_object_enchants(&helm), 0);
}

TEST(ObjectEnchant, TakesAnEnchantOnlyWhereTheSpellWould)
{
    const obj_enchant_type enchant = { ENCHANT_OB, ENCHANT_ALIGN_NEUTRAL, 6 };

    obj_data plain = make_versioned_object(ITEM_WEAPON, 0);
    EXPECT_TRUE(object_takes_enchant(&plain, enchant));

    obj_data armor = make_versioned_object(ITEM_ARMOR, 0);
    EXPECT_FALSE(object_takes_enchant(&armor, enchant));

    obj_data magic = make_versioned_object(ITEM_WEAPON, 0);
    magic.obj_flags.extra_flags = ITEM_MAGIC;
    EXPECT_FALSE(object_takes_enchant(&magic, enchant));

    obj_data with_affect = make_versioned_object(ITEM_WEAPON, 0);
    with_affect.affected[1] = { APPLY_STR, 1 };
    EXPECT_FALSE(object_takes_enchant(&with_affect, enchant));

    const obj_enchant_type unknown = { 99, ENCHANT_ALIGN_NEUTRAL, 6 };
    EXPECT_FALSE(object_takes_enchant(&plain, unknown));
}

TEST(ObjectEnchant, RecordsUpToMaxObjEnchant)
{
    obj_data obj = make_versioned_object(ITEM_WEAPON, 0);
    const obj_enchant_type enchant = { ENCHANT_OB, ENCHANT_ALIGN_NEUTRAL, 6 };
    for (int i = 0; i < MAX_OBJ_ENCHANT; i++)
        EXPECT_TRUE(record_object_enchant(&obj, enchant));
    EXPECT_FALSE(record_object_enchant(&obj, enchant));
    EXPECT_EQ(count_object_enchants(&obj), MAX_OBJ_ENCHANT);
}

TEST(ObjectVersionRefresh, PutsARecordedEnchantBackWhenThePrototypeStillTakesIt)
{
    // The builder changed something unrelated: the weapon now glows.
    obj_data proto = make_versioned_object(ITEM_WEAPON, 2);
    proto.obj_flags.extra_flags = ITEM_GLOW;

    for (int align : { ENCHANT_ALIGN_GOOD, ENCHANT_ALIGN_EVIL, ENCHANT_ALIGN_NEUTRAL }) {
        obj_data copy = make_enchanted_weapon(1, align);
        const int cast_flags = copy.obj_flags.extra_flags;

        EXPECT_TRUE(refresh_object_to_prototype_version(&copy, &proto));
        EXPECT_EQ(copy.obj_flags.version, 2);
        EXPECT_EQ(copy.affected[0].location, APPLY_OB);
        EXPECT_EQ(copy.affected[0].modifier, 6);
        EXPECT_EQ(copy.obj_flags.extra_flags, cast_flags | ITEM_GLOW);
        EXPECT_EQ(count_object_enchants(&copy), 1);
    }
}

TEST(ObjectVersionRefresh, ForgetsARecordedEnchantThePrototypeNoLongerTakes)
{
    // Each prototype now fails the spell's test in its own way.
    obj_data with_affect = make_versioned_object(ITEM_WEAPON, 2);
    with_affect.affected[0] = { APPLY_OB, 3 };
    obj_data magic = make_versioned_object(ITEM_WEAPON, 2);
    magic.obj_flags.extra_flags = ITEM_MAGIC;
    obj_data not_a_weapon = make_versioned_object(ITEM_ARMOR, 2);

    for (const obj_data* proto : { &with_affect, &magic, &not_a_weapon }) {
        obj_data copy = make_enchanted_weapon(1, ENCHANT_ALIGN_GOOD);
        copy.obj_flags.type_flag = proto->obj_flags.type_flag; // a copy is made from its prototype

        EXPECT_TRUE(refresh_object_to_prototype_version(&copy, proto));
        EXPECT_EQ(copy.affected[0].location, proto->affected[0].location);
        EXPECT_EQ(copy.affected[0].modifier, proto->affected[0].modifier);
        EXPECT_EQ(copy.obj_flags.extra_flags, proto->obj_flags.extra_flags) << "the caster's ANTI_EVIL went with it";
        EXPECT_EQ(count_object_enchants(&copy), 0) << "a later refresh must not bring it back";
    }
}

TEST(ObjectVersionRefresh, DoesNotGuessAtAnEnchantThatIsNotRecorded)
{
    // A weapon built +6 OB and magic looks exactly like this. Saves from before enchants were
    // recorded get their records when they load (record_old_object_enchants), not here.
    obj_data proto = make_versioned_object(ITEM_WEAPON, 2);
    proto.obj_flags.extra_flags = ITEM_GLOW;

    obj_data copy = make_versioned_object(ITEM_WEAPON, 1);
    copy.obj_flags.extra_flags = ITEM_MAGIC | ITEM_ANTI_EVIL;
    copy.affected[0] = { APPLY_OB, 6 };

    EXPECT_TRUE(refresh_object_to_prototype_version(&copy, &proto));
    EXPECT_EQ(copy.affected[0].location, APPLY_NONE);
    EXPECT_EQ(copy.affected[0].modifier, 0);
    EXPECT_EQ(copy.obj_flags.extra_flags, ITEM_GLOW);
}

TEST(ObjectVersionRefresh, AnEnchantSurvivesAFileRestoredToAnOlderVersion)
{
    // "Differs", not "newer": a world file put back at 3 moves a copy at 5 down with it.
    obj_data proto = make_versioned_object(ITEM_WEAPON, 3);
    obj_data copy = make_enchanted_weapon(5, ENCHANT_ALIGN_EVIL);

    EXPECT_TRUE(refresh_object_to_prototype_version(&copy, &proto));
    EXPECT_EQ(copy.obj_flags.version, 3);
    EXPECT_EQ(copy.affected[0].modifier, 6);
    EXPECT_EQ(copy.obj_flags.extra_flags, ITEM_MAGIC | ITEM_ANTI_GOOD);
}

TEST(ObjectVersionRefresh, ABrokenKeyStaysBroken)
{
    obj_data proto = make_versioned_object(ITEM_KEY, 2);
    proto.obj_flags.extra_flags = ITEM_BREAKABLE;

    obj_data broken = make_versioned_object(ITEM_KEY, 1);
    broken.obj_flags.extra_flags = ITEM_BREAKABLE | ITEM_BROKEN | ITEM_GLOW;
    EXPECT_TRUE(refresh_object_to_prototype_version(&broken, &proto));
    EXPECT_EQ(broken.obj_flags.extra_flags, ITEM_BREAKABLE | ITEM_BROKEN);

    obj_data whole = make_versioned_object(ITEM_KEY, 1);
    whole.obj_flags.extra_flags = ITEM_BREAKABLE;
    EXPECT_TRUE(refresh_object_to_prototype_version(&whole, &proto));
    EXPECT_EQ(whole.obj_flags.extra_flags, ITEM_BREAKABLE) << "a refresh never breaks a key";
}

TEST(ObjectVersionRefresh, VersionTextShowsOnOffAndNone)
{
    EXPECT_EQ(object_version_text(0), "none");
    EXPECT_EQ(object_version_text(3), "3 (on)");
    EXPECT_EQ(object_version_text(-3), "3 (off)");
}
