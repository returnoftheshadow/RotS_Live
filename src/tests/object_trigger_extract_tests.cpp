/* Object trigger scripts that remove their own object from the game (EXTRACT_OBJ ob1): the
 * command that fired the trigger must not touch the freed object again. */
#include "scripted_room_fixture.h"

void do_eat(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_drink(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_examine(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_pull(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_wear(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_wield(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);

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
