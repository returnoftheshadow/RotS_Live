#pragma once

#include <cstddef>

struct extra_descr_data;
struct obj_data;

/* Text an object prototype no longer shows that copies made before the change still borrow.
 * implement_object hands over what it replaces, and each piece is freed exactly once, when no
 * object on object_list holds it any more: at once if none does, otherwise when free_obj frees
 * the last copy holding it. A copy that stops showing retired text while it lives (a drink
 * container filled, a restyle) leaves that text to the next implement, which checks again. */

/* Takes ownership of `text`, a name or description a prototype has replaced. A null `text` is
 * ignored, and text handed over already is logged and kept once. */
void retire_prototype_text(char* text);

/* Takes ownership of `list`, an extra-description list a prototype has replaced, with its
 * keywords and descriptions. An object holds it while its own list reaches any node of it. A
 * null `list` is ignored, and a list handed over already is logged and kept once. */
void retire_prototype_extra_descriptions(extra_descr_data* list);

/* free_obj calls this for a prototype's copy it is about to free: frees the retired text and
 * lists `leaving` holds that no other object on object_list holds. `leaving` may still be on
 * object_list; it is never counted as a holder. */
void release_retired_text_left_by(const obj_data& leaving);

/* How many retired texts and extra-description lists are still kept for the objects holding
 * them. */
std::size_t retired_prototype_text_count();
