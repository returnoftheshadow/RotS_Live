#include "wait_functions.h"
#include "base_utils.h"
#include "char_utils.h"
#include "structs.h"

#include "comm.h" // for send-to-char
#include "delayed_command_interpreter.h"

#include <algorithm>

// TODO(drelidan):  Put an accessor on here or something.
extern struct index_data* mob_index; // for complete-delay functionality

namespace game_types {
//============================================================================
void wait_list::wait_state(char_data* character, int wait_time)
{
    if (!character)
        return;

    int command = 0;
    int sub_command = 0;
    int priority = 50;
    int delay_flag = 0;
    int ch_num = 0;
    void* argument = NULL;

    wait_state_full(character, wait_time, command, sub_command, priority,
        delay_flag, ch_num, argument, AFF_WAITING, TARGET_IGNORE);
}

//============================================================================
void wait_list::wait_state_brief(char_data* character, int wait_time, int command, int sub_command,
    int priority, long affect_flag)
{
    if (!character)
        return;

    wait_state_full(character, wait_time, command, sub_command, priority, affect_flag,
        0, NULL, affect_flag, TARGET_IGNORE);
}

// Adding this namespace to hold on to previous code that I plan
// on overwriting as a reference.
namespace CODE_SAVER {
    void add_char_to_waitlist(char_data* character, char_data* waiting_list)
    {
        /*
         * bsantosus:  I kidnapped computer.
         *  I love ROTS! It's the best game EVERS! <3
         *  Dave and Brenda 4/14/2017! <3
         */

        // TODO(dgurley):  Parse the function below to figure out what it is.
        //                   I think it's a forward list just looking at it, but we'll see.
        // TODO(dgurley):  Figure out a better way to handle waiting.
        //
        //  It looks like it's a queue implementation.
        //  Keep track of last.
        //  This file will take over complete_delay and abort_delay.
        //  Make use of std::find to see if the character is in the waiting list.
        char_data* temp_character = waiting_list;

        if (temp_character == character) {
            temp_character = character->delay.next;
            waiting_list = temp_character;
        }

        // If there is no waiting list, the character is moved into the waiting list.
        // If there is a waiting list, ensure that the character is at the end of it.

        // All of this list wait stuff held in the character->.. ugh.
        // This will be replaced by an appropriate data structure.
        if (!temp_character) {
            waiting_list = character;
        } else {
            while (temp_character->delay.next) {
                if (temp_character->delay.next == character) {
                    temp_character->delay.next = character->delay.next;
                } else {
                    temp_character = temp_character->delay.next;
                }
            }

            if (temp_character != character) {
                temp_character->delay.next = character;
            }
        }

        character->delay.next = NULL;
    }
}

//============================================================================
void wait_list::wait_state_full(char_data* character, int wait_time, int command, int sub_command,
    int priority, int delay_flag, int ch_num, void* argument, long affect_flag,
    signed char data_type)
{
    if (!character)
        return;

    waiting_type& delay = character->delay;

    // If the character is already waiting, do a priority test.
    // If this wait is a higher priority, complete the previous
    // priority for the character.
    if (delay.wait_value != 0) {
        if (priority > delay.priority) {
            delay.subcmd = -1;
            complete_delay(character);
            abort_delay(character);
        } else {
            // The "AFF_WAITWHEEL" is typically associated with actions that
            //   the player attempted to initiate.
            if (utils::is_set(affect_flag, (long)AFF_WAITWHEEL)) {
                // The character is trying to enter a casting state while they are
                //   already waiting.  This is a bug.
                send_to_char("You're stunned!  Your body can't respond to the command you gave it!\n", character);

                log("Attempted to give a character the 'waitwheel' status while they were already waiting.  Double delay?\n");
            } else {
                // A delay is being forced on them from a skill (maybe kick, swing,
                //   rescue, etc.).  Make them wait longer.
                delay.wait_value = std::max(delay.wait_value, wait_time);
                return;
            }
        }
    }

    delay.wait_value = wait_time;
    delay.cmd = command;
    delay.subcmd = sub_command;
    delay.targ1.ptr.other = argument;

    delay.priority = priority;
    delay.flg = delay_flag;
    delay.targ1.ch_num = ch_num;

    delay.targ1.type = data_type;
    delay.targ2.type = TARGET_IGNORE;
    utils::set_bit(character->specials.affected_by, affect_flag);

    // This replicates the "add_char_to_waitlist" code, which is based on the waiting macro.

    // If the character is in the waiting list or the pending delete list, remove him.
    // Then insert the character at the end of the waiting list.
    m_pendingDeletes.remove(character);
    m_waitingList.remove(character);

    m_waitingList.push_back(character);
}

//============================================================================
void wait_list::abort_delay(char_data* character)
{
    if (!character)
        return;

    // Update the character's wait values and priority.
    utils::remove_bit(character->specials.affected_by, (long)AFF_WAITWHEEL);
    utils::remove_bit(character->specials.affected_by, (long)AFF_WAITING);
    character->delay.wait_value = 0;
    character->delay.priority = 0;

    // Move the character into our pending delete list.
    m_pendingDeletes.push_back(character);
}

//============================================================================
void wait_list::complete_delay(char_data* character)
{
    if (!character)
        return;

    character->delay.wait_value = 0;
    utils::remove_bit(character->specials.affected_by, (long)AFF_WAITWHEEL);
    utils::remove_bit(character->specials.affected_by, (long)AFF_WAITING);

    delayed_command_interpreter interpreter(character);
    interpreter.run();
}

//============================================================================
void wait_list::update()
{
    static char* wait_wheel[8] = { "\r|\r", "\r\\\r", "\r-\r", "\r/\r", "\r|\r", "\r\\\r", "\r-\r", "\r/\r" };

    // Clean up pending deletes.
    for (delete_list_iter iter = m_pendingDeletes.begin(); iter != m_pendingDeletes.end(); ++iter) {
        m_waitingList.remove(*iter);
    }
    m_pendingDeletes.clear();

    // Process commands in the wait list.
    for (wait_list_iter char_iter = m_waitingList.begin(); char_iter != m_waitingList.end(); ++char_iter) {
        char_data* character = *char_iter;
        int wait_value = character->delay.wait_value;
        if (wait_value > 0) {
            wait_value = --character->delay.wait_value;
        }

        if (wait_value > 0) {
            if (!utils::is_npc(*character) && utils::is_affected_by(*character, AFF_WAITWHEEL)) {
                if (utils::is_preference_flagged(*character, PRF_SPINNER)) {
                    // Add this function in somewhere.
                    write_to_descriptor(character->desc->descriptor, wait_wheel[wait_value % 8]);
                }
            }
        } else if (character->delay.wait_value == 0) {
            /* here is the block calling actual procedures */
            complete_delay(character);
            if (character->delay.wait_value == 0) {
                // Update flags and put the character in the delete list.
                abort_delay(character);
            }
        } else {
            // No idea how we got here.  Get the character out of the wait list.
            abort_delay(character);
        }
    }
}

//============================================================================
bool wait_list::in_waiting_list(char_data* character)
{
    return std::find(m_waitingList.begin(), m_waitingList.end(), character) != m_waitingList.end();
}

/********************************************************************
 * Singleton Implementation
 *********************************************************************/
wait_list* wait_list::m_pInstance(NULL);
bool wait_list::m_bDestroyed(false);

wait_list::wait_list()
{
}

wait_list::~wait_list()
{
}
}
