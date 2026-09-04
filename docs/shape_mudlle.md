# Shape Mudlle Command (ASIMA)

ASIMA (Assembler-Style Interpreter for Mobile Activity) is the in-game language
used to script “special” mobile behaviours without recompiling the server. This
file replaces the legacy `mudl_tbl` and describes how to load, edit, and assign
programs as well as the language primitives (stack, list, flow control) builders
need to write or maintain scripts.

## Prerequisites & Program Numbers

- Immortal level: only immortals may shape programs. You must also have
  permission for the zone that owns the program (`get_permission(zone, ch)`).
- Program numbering: Programs live in `world/mdl/<zone>.mdl` files. Coordinate
  with an implementor or use the `register` workflow to reserve a vnum before
  editing (program #4205 lives in `world/mdl/42.mdl`).
- Assignment target: ASIMA only runs on mobiles. Removing a mobile’s `SPECIAL`
  flag and setting its “program number” to an ASIMA vnum turns the script on.
  Details appear in “Hooking a program to a mobile” below.

## Session commands

Start or resume via `shape program <prog_vnum>`. The shaper loads the program
from `world/mdl/<zone>.mdl`, creating a blank entry if none is found.

| Command | Purpose |
|---------|---------|
| `/load <vnum>` | Re-read program text from disk. Usually invoked automatically by `shape program`. |
| `/show` | Display the current program number, its real index, and the raw ASIMA text. |
| `/edit` | Enter the line editor (type your ASIMA source, finish with a lone `@`). |
| `/save` | Write the updated program back to disk (backs up to `world/mdl/oldmdls/`). |
| `/implement` | Replaces the live program in memory (only works for existing programs; new ones require a reboot). |
| `/free` | Discard the in-memory buffer and exit shaping mode. |
| `/done` | Equivalent to `/implement`, `/save`, then `/free`. |

> **Important:** `/implement` refuses to load a brand-new program because the
> runtime allocates space only at boot. After saving a new entry, coordinate a
> reboot so `boot_mudlle()` can add it to `mobile_program[]`.

## Hooking a program to a mobile

There are three ways to attach behaviour:

1. **Hard-coded special** – reserved for stock guildmasters/quest NPCs. Avoid.
2. **Hard-coded proc selected at runtime** – use `shape mob /29` or zone
   command `A 7 <proc_id>` to pick from the limited list in `spec_pro.cpp`.
3. **ASIMA program** – clear the `SPECIAL` flag, set the mobile’s program number
   to your ASIMA vnum (via `shape mob /29`), and ensure the mob’s regen command
   (zone `M` or an `A 7`) sets `store_prog_number` appropriately. This is the
   preferred route for custom logic.

When a mobile with an ASIMA program resets, the runtime converts the text into
bytecode via `mudlle_converter` and stores it in `mobile_program[real_num]`. Use
`implement` to refresh the program of an existing mob without rebooting.

## ASIMA language overview

- **Instruction order**: arguments precede the command. For example, to add
  2 + 3, you write `2 3 +` (the stack stores both numbers and `+` consumes them).
- **Data structures**:
  - **Stack**: holds integers for arithmetic and flow control. Commands like `t`
    (duplicate), `T` (pop), and `x` (swap) manage it. Arithmetic operations `+`,
    `-`, `*`, `/`, bitwise `&`, `|`, logic `>`, `<`, `=`, `!`, `~` operate on the
    two lowest values.
  - **List**: circular buffer storing references to strings, rooms, mobiles,
    players, and objects. Commands such as `f` (fetch item into list), `l`/`L`
    (walk forward/backward), `p` (duplicate), `P` (remove), `X` (swap) manage it.
    Many commands act on the lowest list item (e.g., `s` says the string stored
    there).
- **Flow control**:
  - Use `@NNN` to mark a label and `MNNN` to push its address to the stack.
  - `g` performs an unconditional goto to the address stored on the stack.
  - `i` performs a goto if the previous stack value is non-zero.
  - `Q/q` return FALSE (with/without reset), `R/r` return TRUE.
- **Call masks**: `I` pulls a bitmask from the stack to set triggers. Bits:
  `1` = command handler, `2` = self (heartbeat), `4` = enter-room. Example:
  `7I` enables all three.
- **Strings**: start with a backtick, end with `S` to push as a string literal.
  Example: `` `Greetings, traveller.`S `` adds the text to the list, `s` says it.
- **Delays and randomness**:
  - `d` consumes a stack value and waits that many pulses.
  - `N` consumes a stack value and pushes a random number between 0 and value.

### Interaction commands

| Command | Description |
|---------|-------------|
| Movement (`mn`, `ms`, `me`, `mw`, `mu`, `md`) | Move the host mob north/south/etc. |
| `f` + letter | Fetch references into the list: `fs` self, `fa` argument text, `fi` number-from-stack as string, `fr` room, `fh` last command issuer, `fc` first char in room, `fp` first PC, `fm` first mob, `fN` next in room. |
| `v` + letter | Push stats of the lowest list item (or host) onto the stack: `vh/VH` hit/max hit, `vm/VM` mana, `vv/VV` move, `vl` level, `vc` command verb. Returns `1` on success, `0` otherwise. |
| `V` + letter | Set stats from the stack (hit/mana/move). Use with caution. |
| `s` | Say the lowest string in the list to the room. |
| `U` | Execute the command string stored in the list (acts like “force host to run command”). |
| `W` | Cast a spell: lowest list item is the spell command, the next item (if non-zero) is the target. |
| `g` / `i` | Goto unconditionally / conditionally (jump address must already be on the stack, typically via `MNNN`). |
| `_` | Interrupt (exit without resetting state). |
| `d` | Delay for N pulses (N is taken from the stack). |

### Stack helpers

- `t` – duplicate the last stack value (push a copy).
- `T` – pop the last stack value.
- `x` – swap the two lowest stack values.
- `.` – no-op; useful to separate numeric literals (`12.3` pushes 12 then 3).

### List helpers

- `l` / `L` – move forward / backward in the list.
- `p` – duplicate current item.
- `P` – remove current item.
- `c` / `C` – detect and optionally remove duplicate references.
- `=` / `!` – compare the two lowest items in the list and push 1/0 to the stack.

### Return semantics

- `R` / `r` return TRUE (reset / do not reset memory).
- `Q` / `q` return FALSE (reset / do not reset memory). Use `r`/`q` to keep the
  stack/list contents between calls when you want stateful behaviour.

## Example programs

### Simple greeter (program #4205)

```
#4205
7I                    ; handle command/self/enter_room triggers
`Greetings, traveler.`S
s                     ; say the string
r                     ; return TRUE without resetting lists/stack
```

Assign it to a mobile via `shape mob /29` (set to 4205) and clear the SPECIAL
flag. The mob will greet on heartbeat and when someone enters the room.

### Conditional healer (program #4206)

```
#4206
7I
fp                    ; put first player in room onto the list
vH.vh.=               ; compare max HP to current HP
097i                  ; push label 97, conditional goto if HP equals max
`I see you're not well.`S s
Vh                    ; set HP from stack (must push desired value first)
R                     ; return TRUE and reset
@97
`You look healthy.`S s
R
```

This script looks for the first player in the room, compares their current HP
to max HP, and either heals or compliments them. The label `@97` plus `097i`
demonstrates conditional flow: `i` jumps to label 97 if the preceding comparison
was TRUE.

### Command relay (program #4207)

```
#4207
1I                    ; command-only trigger
fa                    ; argument line (player input) to list
`say `S               ; literal "say "
X+S                   ; concatenate "say " with the argument line
U                     ; execute the combined command (host repeats the player's request)
r
```

This turns the mobile into a parrot that repeats whatever players tell it
(`tell mobdude sing`). Because the call mask is `1`, the program only runs when
players issue commands at the mob (not on heartbeat).

## Best practices

- Keep ASIMA programs small. The language was designed for lightweight behaviours
  (greeters, basic quest logic). Complex systems are still better handled via
  hard-coded specials.
- Comment externally: there is no in-language comment syntax. Maintain a note in
  the zone docs describing what each program number does.
- Back up the `.mdl` file before editing heavily. `/save` already writes a copy
  to `world/mdl/oldmdls/`, but snapshotting your source is still wise.
- Use `/implement` after minor edits to existing programs so you can test them
  immediately.
- New programs need a reboot before they can be referenced. Plan accordingly and
  avoid hooking a brand-new vnum into mobs until after the restart.

With this reference, you can retire `mudl_tbl`, edit programs entirely from the
CLI, and understand how ASIMA scripts interact with mobiles and zone regen.

