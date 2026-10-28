/* Object trigger scripts that remove their own object from the game (EXTRACT_OBJ ob1): the
 * command that fired the trigger must not touch the freed object again. */
#include "scripted_room_fixture.h"

ACMD(do_eat);
ACMD(do_drink);
ACMD(do_examine);
ACMD(do_pull);
ACMD(do_wear);
ACMD(do_wield);

using namespace scripted_room;

namespace {

class ObjectTriggerTest : public ScriptedRoomFixture {
protected:
    /* Pulls this object, as the interpreter does once it has found it. */
    void pull(obj_data* object)
    {
        waiting_type target {};
        target.targ1.type = TARGET_OBJ;
        target.targ1.ptr.obj = object;
        char empty_argument[1] = "";
        do_pull(&m_player, empty_argument, &target, 0, 0);
    }
};

} // namespace
