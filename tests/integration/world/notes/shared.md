# Fixtures outside every zone's own vnums

Files whose fixtures lie outside every zone's own vnums (n * 100 to n * 100 + 99): `mob/14.mob`,
`mob/15.mob`, `mob/guildmasters.mob` and `obj/14.obj`. No zone resets them except the pen boar,
so a scenario loads them with `load mob` or `load obj`. The catalogue lists them
(`python -m rots_harness.world_catalogue --category mobs`, from `tests/integration`).

## Tame animals: `mob/14.mob`

For the `tame` group; zone 18 (`zone-18.md`) resets the pen boar into its pen.

| Mob | Keywords | What it is |
| --- | --- | --- |
| 1450 | `boar pen` | an animal (bodytype 2, perception 0, level 1, `SENTINEL`) a ranger can tame; zone 18 resets one into 1801. In `mob/14.mob`, listed before `guildmasters.mob` so vnums ascend |
| 1451 | `bear grizzly` | a level-14 animal (perception 0), one level past a level-30 ranger's tame cap at full tame knowledge |
| 1452 | `wolf grey` | a level-12 animal (perception 0), within that cap only while the ranger has no other tame |
| 1453 | `owl watchful` | an animal with perception 10, which tame refuses |
| 1454 | `scarecrow straw` | perception 0 but bodytype 1, not an animal, which tame refuses |

1451 to 1454 are level 1 unless the table says otherwise, `SENTINEL`, and follow 1450 in
`mob/14.mob`; nothing resets them, so a scenario loads them with `load mob`.

## Guild masters: `mob/15.mob` and `mob/guildmasters.mob`

For the `practice` group. Seven real guildmasters are copied at their real vnums (1503, 2043, 4601,
10003, 10054, 13600, 32200) so `spec_ass.cpp` binds the guild special to them; each one's program
number is its row of `guildmasters[]` (`consts.cpp`), such as 7 for the traveller 1503, 16 for
Scorther 10003 and 28 for Aroden 10054. Four more guild-special mobs sit at vnums `spec_ass.cpp`
binds the guild special to, with programs chosen for a scenario rather than the real mob's: 1509
`mystic` (program 61, the last row), 3888 `prisoner` (program 0) and 8500 `enchanter` (program
62), the last two naming no row, and 4608 `trainer` (program 9, the URUK WARRIOR row, speaking
human language) for the reject-message bug pin.

1503 and 1509 are in `mob/15.mob`, so that the mobs of `mob/17.mob` keep the vnums ascending; the
rest are in `mob/guildmasters.mob`.

Each teaches the races in its last field, the `will_teach` mask (`WILL_TEACH`, `utils.h`): 63 (god,
human, dwarf, wood elf, hobbit and high elf, not the beorning) for 1503, 1509, 3888, 8500, 10003
and 10054; 2048 (uruk-hai) for 4601 and 4608; 10240 (uruk-hai and orc) for 2043; 8192 (orc) for
32200; 32768 (uruk-lhuth) for 13600.

Two of the copies, 4601 and 32200, answer to the keyword `orc`, as do five zone 11 mobs
(`zone-11.md`); this matters for name lookups whenever one is loaded.

## Fire objects: `obj/14.obj`

For the `resistances` group: worn items that cast, resist or are vulnerable.

| Vnum | Keyword | Notes |
| --- | --- | --- |
| 1400 | `circlet` | fire circlet, head armour, `A 27 5281` (`APPLY_SPELL`) |
| 1401 | `pendant` | fire pendant, worn on the neck, `A 27 10401` (`APPLY_SPELL`) |
| 1402 | `ring` | ember ring, worn on a finger, `A 30 1` (`APPLY_RESIST`, bit 1) |
| 1403 | `band` | tinder band, worn on a finger, `A 31 1` (`APPLY_VULN`, bit 1) |
| 1404 | `bracelet` | kindling bracelet, worn on a wrist, `A 27 161` (`APPLY_SPELL`) |
