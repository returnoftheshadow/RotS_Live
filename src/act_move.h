/* act_move.h */

#ifndef ACT_MOVE_H
#define ACT_MOVE_H

struct char_data;

/* Leaves in <room> the stay-zone items <ch> wears or carries, as <ch> moves to another zone.
 * Returns false when taking a worn one off (unequip_char) killed or removed ch: the item then
 * lies in room, and the move must not go on. ch may then be freed, so it must not be touched
 * again. */
bool prohibit_item_stay_zone_move(char_data* ch, int room);

// Sends ch's MSDP client the room ch stands in: its name, number, terrain and the exits ch sees.
// Does nothing for an NPC, a character without a protocol-capable connection, or a room outside
// the world. Only values that changed are sent.
void msdp_room_update(const char_data* ch);

#endif /* ACT_MOVE_H */
