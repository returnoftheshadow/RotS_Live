#pragma once

struct char_data;

// The identity of the character recorded as the source of a victim's poison. No field is
// meaningful alone; only resolve_poisoner() turns it back into a character. Not persisted.
struct poison_origin {
    int abs_number; // the poisoner's abs_number, -1 when nothing is recorded
    char_data* identity; // the poisoner's address when recorded; compared, never dereferenced
    long registration_serial; // the poisoner's registration_serial when recorded
};

// The live character recorded as the source of `victim`'s poison, or null when the record no
// longer names a live character in the game.
char_data* resolve_poisoner(const char_data& victim);
// Records `poisoner` as the source of `victim`'s poison. A null `poisoner` clears the record.
// `poisoner` is not retained beyond its identity and is never dereferenced later.
void record_poison_origin(char_data* victim, char_data* poisoner);
// Clears `victim`'s poison record.
void clear_poison_origin(char_data* victim);
