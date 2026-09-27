#pragma once

struct char_data;

// One registration of a character: its registry slot, its address and the serial the slot was
// stamped with. Code that keeps a character across a call that can kill it (a special, a trigger,
// a flee) captures one first, and afterwards uses the character only through resolve(). A POD,
// copied by value; no field is meaningful on its own.
struct character_identity {
    int abs_number;           // the character's registry slot at capture
    const char_data* pointer; // the character's address at capture; compared, never dereferenced
    long registration_serial; // the character's registration_serial at capture

    // The identity `character` is registered under now.
    [[nodiscard]] static character_identity capture(const char_data& character);

    // The captured character while the registry still names it under the captured serial, else
    // null: after extract_char() has unregistered and freed it, after its slot has passed to
    // another character, or after the slot was registered again, even at the same address. A
    // character that was never registered never resolves. Standing in a room is not required, so
    // a player parked at the character menu still resolves.
    [[nodiscard]] char_data* resolve() const;
};
