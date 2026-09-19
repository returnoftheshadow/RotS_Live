"""Mirror of the mob loader in src/db.cpp (load_mobiles, 'N' records).

Field order is copied from the fscanf sequence; do not reorder without
re-reading db.cpp. Strings end at '~'; numbers are a whitespace stream.
"""
from __future__ import annotations
import csv
import sys
from dataclasses import dataclass, fields
from pathlib import Path

# Values confirmed against src/structs.h on 2026-09-19; do not guess these.
MOB_SPEC = 1 << 0
MOB_AGGRESSIVE = 1 << 5
MOB_MEMORY = 1 << 11
MOB_SWITCHING = 1 << 18
MOB_FAST = 1 << 20
MOB_PET = 1 << 21
MOB_ORC_FRIEND = 1 << 23


@dataclass
class MobRecord:
    vnum: int
    zone: int
    aliases: str
    short_descr: str
    mob_flags: int
    affected_by: int
    alignment: int
    level: int
    ob: int
    parry: int
    dodge: int
    hit: int
    max_hit: int
    damage: int
    ene_regen: int
    gold: int
    exp: int
    position: int
    default_pos: int
    sex: int
    race: int
    prog: int
    spirit: int

    def _flag(self, bit: int) -> bool:
        return bool(self.mob_flags & bit)

    @property
    def is_aggressive(self) -> bool:
        return self._flag(MOB_AGGRESSIVE)

    @property
    def is_memory(self) -> bool:
        return self._flag(MOB_MEMORY)

    @property
    def is_spec(self) -> bool:
        return self._flag(MOB_SPEC)

    @property
    def is_fast(self) -> bool:
        return self._flag(MOB_FAST)

    @property
    def is_switching(self) -> bool:
        return self._flag(MOB_SWITCHING)

    @property
    def is_orc_friend(self) -> bool:
        return self._flag(MOB_ORC_FRIEND)

    @property
    def is_pet(self) -> bool:
        return self._flag(MOB_PET)


class _Cursor:
    """Sequential reader over one file's text with fread_string / fscanf semantics."""

    def __init__(self, text: str):
        self.text = text
        self.pos = 0

    def read_string(self) -> str:
        end = self.text.index("~", self.pos)
        value = self.text[self.pos:end]
        self.pos = end + 1
        return value.strip("\r\n")

    def read_token(self) -> str:
        length = len(self.text)
        while self.pos < length and self.text[self.pos].isspace():
            self.pos += 1
        start = self.pos
        while self.pos < length and not self.text[self.pos].isspace():
            self.pos += 1
        return self.text[start:self.pos]

    def read_ints(self, count: int) -> list[int]:
        # fscanf("%d") leaves the destination variable untouched and does not
        # consume input on a failed conversion; a few world records (e.g. the
        # unused "golem" template at vnum 10174 in 101.mob) run out of trailing
        # fields before the next record marker, so a non-numeric token is left
        # unconsumed here and defaulted to 0 rather than raising.
        values = []
        for _ in range(count):
            checkpoint = self.pos
            token = self.read_token()
            try:
                values.append(int(token))
            except ValueError:
                self.pos = checkpoint
                values.append(0)
        return values


def parse_mob_file(path: Path) -> list[MobRecord]:
    zone_number = int(path.stem)
    cursor = _Cursor(path.read_text(encoding="latin-1"))
    records: list[MobRecord] = []
    while True:
        marker = cursor.read_token()
        if marker in ("", "$", "$~"):
            return records
        if not marker.startswith("#"):
            raise ValueError(f"{path.name}: expected '#vnum', found {marker!r} near offset {cursor.pos}")
        vnum = int(marker[1:])
        if vnum >= 99999:  # terminator record; db.cpp stops reading fields here
            return records
        aliases = cursor.read_string()
        short_descr = cursor.read_string()
        cursor.read_string()  # long_descr
        cursor.read_string()  # description
        mob_flags = int(cursor.read_token())
        affected_by, alignment = cursor.read_ints(2)
        letter = cursor.read_token()
        if letter != "N":
            raise ValueError(f"{path.name}: vnum {vnum} uses record type {letter!r}; only 'N' is implemented")
        cursor.read_string()  # death_cry
        cursor.read_string()  # death_cry2
        level, ob, parry, dodge = cursor.read_ints(4)
        hit, max_hit = cursor.read_ints(2)
        damage, ene_regen = cursor.read_ints(2)
        gold, exp, _owner = cursor.read_ints(3)
        position, default_pos, sex, race, _pref = cursor.read_ints(5)
        _weight, _height, prog, _butcher, _corpse, _rp_flag = cursor.read_ints(6)
        _prof, _mana, _move, _bodytype = cursor.read_ints(4)
        _saving_throw = cursor.read_ints(1)[0]
        cursor.read_ints(6)  # str int wil dex con lea
        _language, _perception, _resistance, _vulnerability, _script, spirit, _will_teach = cursor.read_ints(7)
        records.append(MobRecord(vnum, zone_number, aliases, short_descr, mob_flags, affected_by,
                                 alignment, level, ob, parry, dodge, hit, max_hit, damage, ene_regen,
                                 gold, exp, position, default_pos, sex, race, prog, spirit))


def parse_all(mob_dir: Path) -> list[MobRecord]:
    # lib/world/mob/index is the file the server's boot loop actually reads
    # (src/db.cpp:1109); everything with a non-numeric stem, such as n.mob,
    # is not listed there and is not part of the live world.
    records: list[MobRecord] = []
    zone_files = [path for path in mob_dir.glob("*.mob") if path.stem.isdigit()]
    for path in sorted(zone_files, key=lambda p: int(p.stem)):
        records.extend(parse_mob_file(path))
    return records


def main(argv: list[str]) -> int:
    records = parse_all(Path(argv[1]))
    writer = csv.writer(sys.stdout)
    names = [f.name for f in fields(MobRecord)]
    flags = ["is_aggressive", "is_memory", "is_spec", "is_fast", "is_switching", "is_orc_friend", "is_pet"]
    writer.writerow(names + flags)
    for record in records:
        writer.writerow([getattr(record, n) for n in names] + [int(getattr(record, f)) for f in flags])
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
