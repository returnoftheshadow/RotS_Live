#pragma once

#include "../structs.h"

namespace test_support {

// A wearable item inside a container that a character carries (not wears). make_physical_corpse()
// moves carried objects into the corpse intact, and pulls the wearable out of the container to
// sit directly in the corpse only when death_strips_corpse_containers() says so. Where the item
// ends up therefore shows which rule the death took. The objects are this struct's own and are
// never RELEASE()d: make_corpse() only links them.
struct CarriedGear {
    CarriedGear() = default;
    CarriedGear(const CarriedGear&) = delete;
    CarriedGear& operator=(const CarriedGear&) = delete;

    // Makes the container the only object `owner` carries, with the item inside it.
    void attach_to(char_data& owner) {
        container.obj_flags.type_flag = ITEM_CONTAINER;
        item.obj_flags.type_flag = ITEM_ARMOR; // wearable, per obj_flag_data::is_wearable()
        container.contains = &item;
        item.in_obj = &container;
        owner.carrying = &container;
    }

    obj_data container{}; // the carried container
    obj_data item{};      // the wearable inside the container
};

} // namespace test_support
