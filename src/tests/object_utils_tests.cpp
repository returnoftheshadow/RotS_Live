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
