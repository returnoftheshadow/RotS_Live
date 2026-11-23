# Zone 24: Harness specials hall

The zone for the `knocking`, `text_editor`, `object_text`, `herald` and `divination` groups: a
gatehouse whose keeper answers a knock, a post office, two heralds' halls, a yard for corpses and
filled flasks, and a seer's room. Files: `wld/24.wld`, `mob/24.mob`, `obj/24.obj`, `zon/24.zon`.
Map square (21, 18); vnums 2400 to 2499 (top 2499); no resets, so scenarios place what they use
with `world_resets` or `load`.

`python -m rots_harness.world_catalogue --zone 24` (from `tests/integration`) lists every fixture
here. These notes hold what the numbers do not show.

## Rooms

Every room is sector 0 (`SECT_INSIDE`, which divination names "Floor"), level 0, and without
flags. Each scenario file keeps the constants of the rooms it uses.

| Vnum | Name | Used by | Notes |
| --- | --- | --- | --- |
| 2401 | Outside the Gate | `test_knocking.py` | north door, keyword `door` (flags 3: door, closed; key 0) |
| 2402 | Gatehouse | `test_knocking.py` | the same door south, keyword `gate`, so a knock is heard "on the gate" |
| 2403 | Post Office | `test_text_editor.py` | where every editor scenario starts; the postmaster is placed here |
| 2404 | Herald's Hall | `test_herald.py` | north of the antechamber |
| 2405 | Antechamber | `test_herald.py` | between the two heralds' halls (north 2404, south 2406) |
| 2406 | Odd Herald's Hall | `test_herald.py` | south of the antechamber |
| 2407 | Dummy Yard | `test_object_text.py` | no exits; the cask of ale is placed here |
| 2408 | Seer's Room | `test_divination.py` | an open exit north to 2409 and a closed door east (keyword `hatch`, key 0) to 2410, so divination's report has both kinds of exit line |
| 2409 | Cluttered Room | `test_divination.py` | the floor the implementor crowds with caps |
| 2410 | Store Cupboard | `test_divination.py` | behind the hatch |

- The door between 2401 and 2402 starts closed and unlocked. The gatekeeper's `do_unlock` and
  `do_lock` find no key 0 on him and say so only to him, so the door is never locked.

## Mobs

Every mob is race 1 with no preference bits and no `rp_flag`, so the gatekeeper never calls a
human an enemy (`IS_AGGR_TO`) or refuses one (`RP_RACE_CHECK`); each speaks no language
(language 0), so players read their lines exactly.

| Mob | Keywords | What it is |
| --- | --- | --- |
| 2400 | `gatekeeper stern` | `MOB_SPEC`, program 2 (`gatekeeper`, `spec_pro.cpp`): a knock on the far side of his door, or a say starting "open" beside him, has him open it after `number(2, 4)` pulses and close it 15 pulses later. His day-and-night routine runs only on mob turns, which `affect-hours` never runs. A say that does not start with "open" draws `number(1, 10)` for his loitering line |
| 2401 | `postmaster clerk` | `MOB_SPEC`, program 29 (`postmaster`, `mail.cpp`): answers `mail`, `check` and `receive`. The harness lib has no `misc/plrmail`; `scan_file` creates it at boot |
| 2402 | `herald proud` | `MOB_SPEC`, program 28 (`herald`, `spec_pro.cpp`), a new-style (`N`) mob whose second death cry, `Make way for %s, %s!`, is his announcement: the first `%s` is the player's name, the second the title |
| 2403 | `herald odd` | as 2402, announcing `Hail, %s %s! Tolls are 5%% today and 100%d tomorrow.`; the `%d` is the text a builder might write by mistake |
| 2404 | `dummy practice` | level 1, bodytype 1 (humanoid), no gold, no special: slain, its corpse has a head to take and holds no coins |

## Objects

| Vnum | Keyword | Notes |
| --- | --- | --- |
| 2400 | `flask` | an empty drink container holding 20; pouring into it puts the liquid's keyword in front of its name |
| 2401 | `cask oak` | a fountain (type 23) of 1000 drinks of ale (liquid 3), not takeable. Its keywords leave out `ale`, so `drink ale` in the yard finds only a flask, never the cask on the floor. Ale's `drink_aff` has a drunk effect, so `do_drink` takes a fixed amount; water would draw `number(3, 10)` |
