# Modding Hatrick

Everything you can mod lives in `assets/` next to the game:

| Path | What |
|---|---|
| `levels.txt` | every level and bonus room, its name, music, menu card and par time |
| `sfx/` | sound effects |
| `music/` | jingles, and one folder per music theme |

The game reads these files when it starts, so there is no rebuild: edit, then restart the game. Mistakes never stop the game. A broken level, an unknown option or a missing sound is reported on the terminal with the file and line, and the rest still works. Run `./hatrick` from a terminal to see these reports, or `./sim --check` to check `levels.txt` alone.

The high-score table is saved separately, in `~/.hatrick_scores`.

## Sounds and music

To replace a sound, drop in a file with the same name. Music is read each time a theme starts.

- Formats: `.ogg`, `.wav`, `.flac` or `.mp3`, with any sample rate, mono or stereo. If several formats exist for one name, the game uses the first in that order. So to replace `jump.ogg` with `jump.wav`, delete or rename the OGG.
- A missing or unreadable file is reported on the terminal and stays silent. The game keeps running.
- Re-running `audio/build.py` regenerates the original files and overwrites your replacements, so keep copies of your mods.

### Sound effects: `assets/sfx/`

| File | Plays when |
|---|---|
| `coin` | collecting a coin |
| `jump`, `jump2`, `jump3` | first, second and third jump of a triple jump (also: `jump2` for the cap-catch jump, `jump3` for the ground-pound jump) |
| `flip` | backflip, side flip |
| `longjump` | long jump |
| `spin` | spin jump, ground spin, mid-air twirl |
| `roll` | starting or boosting a roll |
| `dive` | dive |
| `cap_throw`, `cap_catch`, `cap_bounce` | throwing the cap, catching it, bouncing off it |
| `stomp` | stomping an enemy |
| `brick` | smashing a brick |
| `gp_spin`, `gp_land` | ground pound start, ground pound impact |
| `spring` | spring launch |
| `wall_jump` | wall jump |
| `land` | landing from a big fall |
| `skid` | skidding while turning around |
| `ledge` | grabbing a ledge |
| `menu_move`, `menu_ok`, `menu_back`, `pause` | menu navigation, choosing a destination, resuming, opening the pause menu |
| `checkpoint` | touching a checkpoint flag |
| `tube` | going into a tube, and coming out of the other end |
| `crumble` | a crumble block starting to shake |
| `reveal` | finding a hidden block, or a bonus room for the first time |
| `emerge`, `spit` | a tube dweller coming out nearby; a spitter lobbing a seed |
| `tick`, `bonus` | each step of the course-clear score tally; the end of the tally |

Sounds play once at full length, overlapping freely (up to 32 at a time). Sounds are panned slightly toward where Hatrick is on screen.

### Jingles: `assets/music/`

`jingle_clear` plays when you reach the flag, over the slide, the pose and the score tally (about 3 to 5 s); skipping the celebration with Jump cuts it off. `jingle_death` plays after a death; the level music comes back after about 1.3 s.

### Music: `assets/music/<theme>/`

Each level picks its theme folder with `music=` in `levels.txt` (see below). The title menu plays a calm mix of the first level's theme. The stock themes are:

| Theme folder | Used by |
|---|---|
| `overworld` | Hills, and the title menu |
| `underground` | Brickworks |
| `athletic` | Spikes, Sky, the movement playground |
| `finale` | Hatrick |

To add a new song, make a new folder, e.g. `assets/music/mysong/`, put the stems in it, and set `music=mysong` on the levels that should use it.

A theme is up to seven stems that loop together, all starting at the same moment:

| Stem | When it is heard |
|---|---|
| `lead` | in levels (muted on the title menu) |
| `bass`, `perc`, `bells`, `bah` | always |
| `fast` | fades in while Hatrick runs at full speed or chains long jumps, dives, rolls, spin jumps or triple jumps |
| `arp` | fades in after cap bounces and stomps in mid-air |

All stems are optional: a missing stem is just silent. To use a single finished song, save it as `bass.ogg` (heard everywhere, including the menu), or as `lead.ogg` if it should be silent on the title menu. Then delete the other stems.

The loop length is the length of the longest stem. Shorter stems go silent until the loop restarts. For a seamless loop, make every stem exactly the same length. The music fades out on pause (it ducks), death and the flag, and restarts from the top when a level starts.

`music.txt` (optional) tells the game where the enemy hops go:

```
# lines starting with # are ignored
bpm 104
bah 7.5 15.5 23.5 28
```

`bah` lists the beats, counted from 0 at the start of the loop, where the enemies hop. Without the file, or without a `bah` line, the music plays normally and the enemies don't hop.

## Levels: `assets/levels.txt`

Levels are ASCII maps. Each one starts with a header line, followed by its rows, and may have up to three bonus rooms after it:

```
= 2 brickworks   music=underground   card=bricks   par=75
; lines starting with ; are comments
            o o o
@      BBB         g   11   K   F
#######################||#######
+ cellar
 o o o o o
11
||
#########
```

**Header:** `=`, then the level's name. A leading number is just for your own ordering; it isn't shown. The name appears on the destination menu in capitals (letters, digits and `- / : . ?`). Options:

| Option | Meaning | Default |
|---|---|---|
| `music=<folder>` | theme folder in `assets/music/` | `overworld` |
| `card=<style>` | menu card picture: `hills`, `bricks`, `spikes`, `sky`, `castle` or `playground` | `hills` |
| `par=<seconds>` | par time: every whole second under it is worth 50 points at the flag | `100` |

**Bonus rooms:** a line starting with `+` begins a bonus room of the level above it, with its own map (same size rules). The rest of the line is a name for your own use, plus an optional `bg=cave` (the default: a dim cave) or `bg=sky`. Rooms are only reachable through tubes. The start and the flag belong in the level's main area. The first time Hatrick enters a bonus room is worth 2000 points.

**Order:** campaign levels are played in file order, and you can have as many as you like (the menu pages through them six at a time). Finishing the last one ends the run. Levels whose name starts with `lab` are not part of the campaign. The last `lab` level is the movement playground (F1, and its card on the menu). Without any lab level there is no playground. The last card on the menu is always the high-score table.

**Size:** an area is at most 32 rows tall and 256 columns wide. Rows sit at the bottom of the 32-row map, so a short level is just floor and sky. Empty lines inside a map count as rows. Every level needs exactly one `@` and one `F`. A level holds at most 48 enemies, 40 tube mouths, 24 fire bars, 24 tube dwellers, 96 crumble blocks and 16 checkpoints.

| Char | Tile | Char | Tile |
|---|---|---|---|
| `@` | start | `F` | flag (goal) |
| `#` | ground | `B` | brick (breakable) |
| `S` | stone | `T` | spring |
| `^` | spikes | `o` | coin |
| `/` `\` | slopes | `g` | walker enemy |
| `b` | buzzer, flies up and down | `h` | buzzer, flies left and right |
| `K` | checkpoint flag | `C` | crumble block |
| `?` | hidden block | `\|` `-` | tube body (upright / sideways) |
| `0`–`9` | tube mouth, linked by number | `M` | tube mouth that goes nowhere |
| `*` `%` | fire bar, turning clockwise / anticlockwise | `~` `:` `!` | fire bar arm: normal, slow, fast |
| `n` | tube dweller (snapper) | `m` | tube dweller that spits seeds |
| space | empty | | |

**Checkpoints (`K`):** put it in the empty cell standing on the ground. Touching it raises its banner and saves the level as it is at that moment: coins, score, broken bricks, beaten enemies. A death comes back there instead of the start; R still restarts the whole level. The run timer never stops, and the level's time at the flag is measured from the level's start.

**Tubes:** a tube is two cells wide. Its mouth is two digit cells side by side with `|` body cells below it (opening up) or above it (opening down). A sideways tube is two digit cells stacked, with `-` body cells beside them; it opens away from the body. A mouth can stand alone without body cells. The two mouths with the same digit in a level (main area or bonus rooms) lead to each other:

```
 22                 2-        -2
 ||       or        2-   or   -2
```

Hatrick goes in by pressing Down on top of a mouth that opens up, by walking into one that opens sideways (standing on the floor at its lower cell), or by jumping into one that opens down while holding Up. He comes out of the other mouth. A digit used only once can't be entered (a warning says so), and an `M` mouth never can: use `M` for decoration and for dwellers' homes. Each digit links one pair, so a level has at most ten links.

**Crumble blocks (`C`):** solid until Hatrick stands on one. It shakes, falls after about half a second if he stays on (carrying him along, and he can still jump off), and comes back a few seconds later.

**Hidden blocks (`?`):** invisible, and Hatrick passes through them from above or the side. Hitting one with his head from below turns it into a solid block and pops out a coin (1100 points).

**Fire bars (`*` `%`):** the cell becomes an iron block with a line of embers turning around it, passing through everything. Draw the arm next to it in its starting direction (right, down, left or up): its length sets the bar's length, and its character the speed (`:` a turn in 6 s, `~` in 4 s, `!` in 2.5 s). Without an arm the bar is 5 long, normal speed, starting to the right. The arm cells stay empty.

```
*~~~~        %::        !
                        !
                        *
```

**Tube dwellers (`n` `m`):** put one right above a mouth that opens up (or right below one that opens down), over either of its two cells. It hides, rises out, snaps, and sinks back on a timer, but stays in while Hatrick is right next to or on its tube. Touching one is fatal, stomping included. Only the cap beats it (500 points). The `m` kind also lobs slow arcing seeds at Hatrick; the cap knocks those out of the air too. Usually you'll give dwellers an `M` mouth, but they can live in linked tubes as well: they stay in while Hatrick travels through.

**Points:** coin 100, stomp or cap knockout 200, brick 50, hidden block 1100, tube dweller 500, a bonus room found 2000. At the flag, the height where Hatrick grabs the pole is worth 100 to 5000 (the very top), then every second under par adds 50. A death takes the score back to the level's start, or to the last checkpoint.

To test a level without playing it, `./sim LEVEL file.tas` replays scripted input on it (levels are numbered from 0, campaign first, then labs). Add `trace` to see every frame (position, state, room and score), and set `SIM_RESPAWN=1` to keep going after deaths. `python3 tools/route.py LEVEL out.tas` searches for a route to the flag and writes it as a replay. `python3 view.py LEVEL file.tas out.png [room=N]` draws a level, or one of its bonus rooms, with the replay's path.

`./build.sh` also builds a copy of `assets/levels.txt` into the game, which is used only when the file is missing or has no playable level, and runs `./sim --check` to report mistakes.
