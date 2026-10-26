#include "delayed_command_interpreter.h"
#include "char_utils.h"
#include "structs.h"

#include "comm.h" // for send-to-char
#include "db.h" // for complete-delay functionality
#include "interpre.h" // for complete-delay functionality
#include "mudlle.h" // for complete-delay functionality
#include "script.h" // for complete-delay functionality

#include <algorithm>

// TODO(drelidan):  Put an accessor on here or something.
extern struct index_data* mob_index; // for complete-delay functionality

namespace game_types {
//============================================================================
delayed_command_interpreter::delayed_command_interpreter(char_data* character)
    : m_character(character)
{
}

//============================================================================
void delayed_command_interpreter::run()
{
    if (!m_character)
        return;

    /* One way to finish a delay: complete_delay's, a paused script's deferral included. */
    ::complete_delay(m_character);
}
}
