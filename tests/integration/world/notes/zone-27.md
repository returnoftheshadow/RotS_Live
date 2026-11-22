# Zone 27: Harness warband inn

The zone for the `follower_rent` group: an inn, a yard and a track where an orc leader's
recruits, their wolves and two led wargs follow it through rent, login, a fight and a service
stop. Files: `wld/27.wld`, `mob/27.mob`, `obj/27.obj`, `zon/27.zon`. Map square (24, 15); vnums
2700 to 2799 (top 2799); no resets, so the scenario loads what it uses with `load mob` and
`load obj`, and places the harness innkeeper (1105) in the inn with a `world_resets` marker.

`python -m rots_harness.world_catalogue --zone 27` (from `tests/integration`) lists every fixture
here. These notes hold what the numbers do not show.

## Rooms

Every room is sector 0 (`SECT_INSIDE`), level 0, and without the `INDOORS` flag, which would
stop a rider entering it (`check_simple_move`, `act_move.cpp`). An orc reads the rooms as lit
(`The intensity of light here makes it hard to see.` as it walks in); the scenario's numbers do
not depend on it.

| Vnum | Name | Notes |
| --- | --- | --- |
| 2700 | Warband Inn | east to the yard; where the innkeeper stands and the leader rents |
| 2701 | Warband Yard | west to the inn, east to the track; the leader's load room |
| 2702 | Warband Track | west to the yard; where the dummy is loaded and killed |

## Mobs

`mob/27.mob` sorts between `mob/22.mob` and `mob/guildmasters.mob` (3888 on) so that mob vnums
ascend (`shared.md`). Every recruit, wolf and warg answers to `warhost`, so `where warhost`
lists them all; nothing else does. All are male, `SENTINEL`, and set no aggression.

| Mob | Keywords | What it is |
| --- | --- | --- |
| 2700 | `recruit gashface` | a gash-faced recruit: orc (race 13), bodytype 1, level 16, alignment -500, `ORC_FRIEND`; OB 10, damage 20, strength 18 and dexterity 12 (carrying 19 items), 100 moves, language 0 |
| 2701 | `recruit squinteye` | a squint-eyed recruit, the same |
| 2702 | `recruit broadback` | a broad-backed recruit, the same |
| 2703 | `wolf dun` | a dun wolf: `MOUNT`, bodytype 2, race 0, level 5, 100 moves |
| 2704 | `wolf ashen` | an ashen wolf, the same |
| 2705 | `wolf sable` | a sable wolf, the same |
| 2706 | `warg tawny` | a tawny warg, the same as the wolves |
| 2707 | `warg brindled` | a brindled warg, the same |
| 2708 | `dummy practice` | a practice dummy: human, level 1, 75 hit points, constitution 2, dexterity 5 and parry -15, so its real dodge and parry are both 0; no resistances or script |

- Three level-16 recruits are exactly what a level-48 orc can recruit: `do_recruit`
  (`act_othe.cpp`) allows a level up to (48 * 2 + 2) / 5 = 19 and 48 levels in all.
- A walking leader takes two `MOUNT` followers with it without a roll; a third draws
  (`do_move`, `act_move.cpp`). The wolves follow their recruits, not the leader.
- A recruit's real OB is 49 once dressed (stat), and its weapon's damage 54/10, so its blow on
  the dummy always lands and deals at least 26; the leader's barehanded blow deals at most 10
  (`check_fight_margins` in `test_follower_rent_cycles.py`).

## Objects

`obj/27.obj`. Every object answers to `gearmark`, so `where gearmark` lists every copy and who
holds it. Each is rentable: no `NORENT`, rent -1, not a key, and no alignment or race flag.

| Vnum | Keyword | Notes |
| --- | --- | --- |
| 2700 to 2714 | `ring`, `ring`, `necklace`, `collar`, `hauberk`, `helm`, `greaves`, `boots`, `gauntlets`, `vambraces`, `cloak`, `belt`, `bracer`, `wristband`, `pack` | armour (type 9) for both fingers, both neck slots, body, head, legs, feet, hands, arms, about, waist, both wrists and back, values all 0 and weight 10; `wear all` fills one slot each |
| 2715 | `cleaver` | a jagged cleaver, the gash-faced recruit's: weapon, bulk 5, type 8 (cleave), level 10, weight 300 |
| 2716 | `waraxe` | a notched waraxe, the squint-eyed recruit's, the same |
| 2717 | `greataxe` | a black greataxe, the broad-backed recruit's, the same |
| 2718 | `armband` | a red armband, worn on the arms: the leader's, since rent refuses a renter with nothing to store |

- A weapon of bulk above 4 is wielded two-handed when no shield is worn (`do_wield`,
  `act_obj2.cpp`), and `twohanded` (`do_twohand`, `fight.cpp`) does it again after login.
