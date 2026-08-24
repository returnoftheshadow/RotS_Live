#pragma once
// TASK-026 port: the set of characters that took part in one death, built by
// kill_contributors() (fight.cpp) and carried into pkill_create(). It exists
// because pkill.cpp's three record-building walks derived their own answer
// straight from combat_list, and such a walk cannot see two contributors this
// branch credits: the recorded poisoner (who may be rooms away) and the caster
// a room affect names when its tick lands the killing blow. Fixed capacity,
// no heap: built on the death path, consumed immediately, never outlives it.

struct char_data;

struct kill_contributor_list {
    // Ceiling on the distinct characters one death can credit. Comfortably
    // above any real fight -- add() refuses past it rather than overrunning.
    static constexpr int kCapacity = 32;

    // The contributors, in the order kill_contributors() found them: everyone
    // fighting the victim (combat_list order), then the recorded poisoner,
    // then the primary killer. Only the first `count` entries are live, and
    // no entry is ever null or repeated.
    char_data* entries[kCapacity] {};

    // How many of `entries` are live.
    int count = 0;

    // Latches the first refusal for want of room, so one very crowded death
    // logs a single overflow line instead of one per dropped contributor.
    bool overflow_logged = false;

    // True when `candidate` is already an entry. Null is never an entry.
    bool contains(const char_data* candidate) const;

    // Appends `candidate`, and answers whether it actually landed: false for
    // null, for a duplicate, and for a full list (which also logs, once).
    bool add(char_data* candidate);
};

// Builds that set for `victim`'s death. `primary` is the character die() was
// told did it -- it may be null, and may be standing anywhere. Membership
// rules, applied to every candidate: an NPC pet or orc-friend whose master
// stands in the same room contributes as its MASTER; the victim itself and
// immortals are never contributors.
kill_contributor_list kill_contributors(struct char_data* victim, struct char_data* primary);
