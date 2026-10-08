/* room_lists.h

   Named ranges over the lists a room holds, for range-for and the standard algorithms:

       const room_contents_range contents = contents_of(world[room]);
       auto lit = std::count_if(std::begin(contents), std::end(contents), is_lit);

   The ranges follow intrusive::each(): the list must not change while one is being walked. */

#ifndef ROOM_LISTS_H
#define ROOM_LISTS_H

#include "intrusive_list.h"
#include "structs.h"

// The characters in a room, linked through next_in_room.
using room_people_range = intrusive::list_range<intrusive::list_iterator<&char_data::next_in_room>>;

// The objects on a room's floor, linked through next_content.
using room_contents_range = intrusive::list_range<intrusive::list_iterator<&obj_data::next_content>>;

// The characters in `room`, first to last.
inline room_people_range people_in(const room_data& room)
{
    return intrusive::each<&char_data::next_in_room>(room.people);
}

// The objects on `room`'s floor, first to last. Objects inside containers are not included.
inline room_contents_range contents_of(const room_data& room)
{
    return intrusive::each<&obj_data::next_content>(room.contents);
}

#endif /* ROOM_LISTS_H */
