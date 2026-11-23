#pragma once

#include "../object_text.h"
#include "../structs.h"

/* Gives `object` a shared text holding what its char* fields show now, as boot gives each
 * prototype one. A test sets a prototype's fields one by one and then calls this, so the copies it
 * makes share their prototype's text as in the game. */
inline void make_text_shared(obj_data& object)
{
    object.set_text(create_object_text(object_text_fields_of(object)));
}
