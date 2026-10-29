# Zone 11: Harness arena

The first and general-purpose zone: the start and idle rooms the game needs for every race, the
arena where most scenarios meet, and the plain mobs and objects many groups share. Files:
`wld/11.wld`, `mob/11.mob`, `obj/11.obj`, `scr/11.scr`, `mdl/11.mdl`, `zon/11.zon`. Map square
(0, 0); vnums 1100 to 1199 (top 1199). Resets: the target orc (mob 1130) into Arena East, 1132,
and the harness snake (mob 1131) into Corridor One, 1134.

Add to zone 11 only a fixture its existing scenarios need; a new scenario area gets a zone of its
own (the scenario reference, "The test world").

`python -m rots_harness.world_catalogue --zone 11` (from `tests/integration`) lists every fixture
here with its decoded header. These notes hold what the numbers do not show.

## Rooms

Every room is sector 0 (`SECT_INSIDE`), level 0, and without the `INDOORS` flag, so `OUTSIDE()` is
true in all of them but none is ever sunlit.

| Vnum | Name | Constant (in `rots_harness/world.py` unless a scenario file is named) | Notes |
| --- | --- | --- | --- |
| 1101 | Immortal Start | `world.ROOM_IMMORTAL_START` | where implementors arrive |
| 1102 | Immortal Idle | | |
| 1110 | Frozen Room | | |
| 1129 | Olog-hai Start | | |
| 1130 | Arena West | `world.ROOM_ARENA_WEST` | the lit western end of the arena; east 1131, down 1136 |
| 1131 | Arena Centre | `world.ROOM_ARENA_CENTRE` | the middle of the arena; `custom_mage()` starts here |
| 1132 | Arena East | `world.ROOM_ARENA_EAST` | east of the centre; the zone resets the target orc (1130) here |
| 1133 | Dark Cell | `world.ROOM_DARK_CELL` | the arena's one `DARK` room, north of the centre |
| 1134 | Corridor One | | the zone resets the harness snake (1131) here; south to 1135 |
| 1135 | Corridor Two | | north to 1134 |
| 1136 | Crevice Floor | | up to 1130 |
| 1137 | Gallows Pit | `ROOM_GALLOWS_PIT` in `test_followers_known_bugs.py` | a `DEATH` room with no exits |
| 1138 | Gallows Approach | `world.ROOM_GALLOWS_APPROACH` | its one exit leads east into the Gallows Pit |
| 1151 | Retirement Home | | |
| 1152 | Lost and Found | | where a character with a bad load room arrives |
| 1160 | Human Start | | |
| 1170 | Wood-elf Start | | |
| 1180 | Signpost Room | `world.ROOM_SIGNPOST` | two extra descriptions, below |
| 1184 | Beorning Start | | |
| 1190 | Uruk Idle | `ROOM_URUK_IDLE` in `test_account_menu.py` | the Uruk-hai idle room (`mortal_idle_room`, `consts.cpp`) |
| 1191 | Orc Idle | | |

- 1133 and 1137 are the zone's only rooms with a flag. `IS_DARK` is true in 1133 unless something
  there gives light, so a character there sees only with infravision, holylight, or moonvision
  while the moon is up (the room counts as outside, and moonlight follows the uncontrolled game
  hour).
- Whoever walks into 1137 dies there (`do_move`, `act_move.cpp`, calls `raw_kill` with no killer);
  a mortal wakes in its race's start room with 1 hit point and no moves. `goto` and `teleport`
  into it kill nobody.
- 1180's extra descriptions are `plaque sign` ("The plaque reads FIRST.") then `sign` ("The sign
  reads SECOND."), in that file order, so both answer to "sign".

## Mobs

The zone 11 mobs speak language 1, the animal tongue, which `say_to_char` (`act_comm.cpp`)
garbles for players, except the steward (1136), which speaks the common tongue (language 0) so
its line reads exactly.

| Vnum | Keywords | Notes |
| --- | --- | --- |
| 1105 | `innkeeper harness` | the harness innkeeper, a human whose vnum `spec_ass.cpp` assigns the receptionist special (act flags `3` include `MOB_SPEC`). Filed here, but zone 16 resets it into its inn, 1601; its behaviour is in `zone-16.md` |
| 1130 | `target orc` | a plain melee target; the zone resets one into 1132 |
| 1131 | `snake harness` | its bite poisons; act flags `3` include `MOB_SPEC`; the zone resets one into 1134 |
| 1132 | `bystander orc` | involved in nothing, there to be splashed |
| 1133 | `brute orc` | hits very hard |
| 1134 | `pet orc` | carries the `MOB_PET` flag (act flags `2097154`) |
| 1135 | `target orc hardy` | a target that outlives several ticks of damage |
| 1136 | `steward` | runs script 1101 when it hears someone speak (its script number is the 5th value of its last line); speaks the common tongue (language 0); used by `test_skins.py` |
| 1160 | `zerostam mob` | a stamina-less mob: the bystander orc with no maximum moves; neither has any maximum stamina (`0 0 0 1` and `0 0 50 1` lines) |
| 1161 | `pony harness` | a harness pony: act flags `514` (`SENTINEL`, `MOB_MOUNT`), so a player can `ride` it; no moves, so it never takes a step |
| 1170 | `angel shining` | act flags `3` (`MOB_SPEC`) and program 14, the resetter (`spec_ass.cpp`): `tell angel pracreset` resets a player's practice |

Five of these answer to the keyword `orc` (1130, 1132, 1133, 1134 and 1135), as do two of the
guildmaster copies (4601 and 32200, `shared.md`), and three answer to `harness` (1105, 1131 and
1161). The skin swords (1140 to 1143) answer to `sword`, as does zone 16's plain sword (1601).
This matters for name lookups whenever one is loaded (the scenario reference, "Names, sides
and lookups").

## Objects

| Vnum | Keyword | Notes |
| --- | --- | --- |
| 1130 | `token` | harness token; its vnum is one `spec_ass.cpp` assigns a board to |
| 1136 | `bag` | leather bag, a container |
| 1137 | `cap` | leather cap, head armour; zone 13 stocks it in both store rooms |
| 1138 | `amulet` | sickly amulet, worn on the neck; its `A 28 11` line (`APPLY_BITVECTOR`, bit 11) sets `AFF_POISON` on the wearer with no poison affect behind it |
| 1139 | `bread` | tainted bread: food, fill 2, poisoned |
| 1140 | `sword plain steel` | a plain steel sword, the base of a skin family |
| 1141 | `sword engraved steel` | an engraved steel sword: a skin of 1140 that owns its cost (level line `... 1140 1`) |
| 1142 | `sword gilded steel` | a gilded steel sword: a skin of 1140 (level line `... 1140 0`) |
| 1143 | `sword orphan steel` | names base 1199, which does not exist: boot logs an `OBJ ERROR` and loads it as a plain object |
| 1160 | `ring` | ring of invisibility; its `A 28 9` line sets `AFF_INVISIBLE` on the wearer |
| 1171 | `circlet` | iron circlet, head armour with strength +2 |
| 1185 | `girdle` | stone girdle, worn on the waist, leadership +3; extra flags `WILLPOWER`, `DWARF` and `STAY_ZONE` |
| 1186 | `shirt` | chain shirt, body armour whose second value is 5 |

## Scripts

`scr/11.scr`:

| Script | Name | Rows |
| --- | --- | --- |
| 1100 | `harness stub` | none |
| 1101 | `harness skins restyle` | the steward's: on hearing someone speak it finds the speaker's plain steel sword (wherever it is carried), restyles it into 1141, tries the cap 1137 (refused and logged as a `SCRIPT ERROR`) and says `Your sword is engraved.` |
| 1180 | `ordered triggers` | three `ON_ENTER` rows numbered 1, 2, 3, with the texts `first`, `second`, `third` |
| 1181 | `out-of-order triggers` | four `ON_ENTER` rows numbered 1, 2, 4, 3, with the texts `one`, `two`, `four`, `three` |

## Other files

`mdl/11.mdl` is a stub, the only mudlle file: one record, `#1101 harness stub`.
