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

TEST(ObjectVersionRefresh, DoesNotCarryAnEnchantOver)
{
    // Even when the refreshed weapon could be enchanted again: players recast it.
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
