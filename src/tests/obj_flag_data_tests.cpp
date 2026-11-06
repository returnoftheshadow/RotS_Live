#include "../structs.h"
#include "ObjFlagDataBuilder.h"
#include <gtest/gtest.h>

namespace {

const char *material_name(signed char material) {
    switch (material) {
        case 3:
            return "chain";
        case 4:
            return "metal";
        default:
            return "unknown";
    }
}

} // namespace

TEST(ObjectFlagData, ReturnsObCoefConfiguredByBuilder) {
    builders::ObjFlagDataBuilder builder;
    builder.setObCoef(10);

    obj_flag_data objFlagData = builder.build();

    EXPECT_EQ(objFlagData.get_ob_coef(), 10)
        << "Expected get_ob_coef() to return the value assigned by ObjFlagDataBuilder.";
}

TEST(ObjectFlagData, ReportsChainMaterialAsChain) {
    obj_flag_data objFlagData{};
    objFlagData.material = 3;

    EXPECT_TRUE(objFlagData.is_chain())
        << "Expected material " << static_cast<int>(objFlagData.material)
        << " (" << material_name(objFlagData.material) << ") to be recognized as chain.";
}

TEST(ObjectFlagData, ReportsMetalMaterialAsMetal) {
    obj_flag_data objFlagData{};
    objFlagData.material = 4;

    EXPECT_TRUE(objFlagData.is_metal())
        << "Expected material " << static_cast<int>(objFlagData.material)
        << " (" << material_name(objFlagData.material) << ") to be recognized as metal.";
}

TEST(ObjectFlagData, ReturnsConfiguredWeaponType) {
    builders::ObjFlagDataBuilder builder;
    builder.setWeaponType(game_types::weapon_type::WT_SLASHING);
    obj_flag_data objFlagData = builder.build();

    EXPECT_EQ(objFlagData.get_weapon_type(), game_types::weapon_type::WT_SLASHING)
        << "Expected get_weapon_type() to return WT_SLASHING.";
}
