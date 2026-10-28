/* Mob trigger scripts that remove a character from the game (EXTRACT_CHAR) while the command or
 * fight that set them off still uses it, and the variables a running script keeps.
 *
 * A removed mob is freed at once. Whatever set its trigger off stops touching it: a room's other
 * mobs still hear, see or are entered on; a give or wear of everything stops; a death or a blow
 * that lost its victim or attacker is over. A script that keeps everyone changes nothing. */
#include "scripted_room_fixture.h"

#include "../big_brother.h"
#include "../comm.h"
#include "../spells.h"

ACMD(do_say);
ACMD(do_give);
ACMD(do_wear);
ACMD(do_cast);
void one_mobile_activity(char_data* mob);
void die(char_data* dead_man, char_data* killer, int attack_type);
void raw_kill(char_data* dead_man, const char_data* killer, int attack_type);

extern struct index_data* mob_index;
extern struct char_data* mob_proto;
extern int top_of_mobt;

using namespace scripted_room;

namespace {

/* What the test scripts send ch2 just before they remove their mob. */
constexpr const char* VANISH_MESSAGE = "The mob vanishes.";
/* What a damage test deals. */
constexpr int BLOW_DAMAGE = 30;

class CharacterTriggerTest : public ScriptedRoomFixture {
protected:
    /* Makes script SCRIPT_VNUM one block that tells ch2 the mob vanishes and then removes the
     * mob (ch1) from the game, ending without RETURN_FALSE. */
    void install_self_removing_mob_script(int trigger_type)
    {
        install_script(trigger_type,
            { { SCRIPT_SEND_TO_CHAR, VANISH_MESSAGE, { SCRIPT_PARAM_CH2 } },
                { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } });
    }

    /* Makes script SCRIPT_VNUM one block that only tells ch2 it ran. */
    void install_harmless_mob_script(int trigger_type)
    {
        install_script(trigger_type, { { SCRIPT_SEND_TO_CHAR, SCRIPT_RAN_MESSAGE, { SCRIPT_PARAM_CH2 } } });
    }

    /* Hands this object to the player. */
    obj_data* give_player(obj_data* object)
    {
        obj_to_char(object, &m_player);
        return object;
    }
};

} // namespace
