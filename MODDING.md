# Modding Hatrick

Everything you can mod lives in `assets/` next to the game:

| Path | What |
|---|---|
| `levels.txt` | every level and bonus room, its name, music and map landmark |
| `sfx/` | sound effects |
| `music/` | jingles, and one folder per music theme |
| `gfx/` | replacement art: tiles, Hatrick, enemies, backgrounds (PNG) |

The game reads these files when it starts, so there is no rebuild: edit, then restart the game. Mistakes never stop the game. A broken level, an unknown option or a missing sound is reported on the terminal with the file and line, and the rest still works. Run `./hatrick` from a terminal to see these reports, or `./sim --check` to check `levels.txt` alone.

The high-score table and the progress (levels cleared, moon coins found) are saved separately, in `~/.hatrick_scores` and `~/.hatrick_progress`.

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
| `menu_move`, `menu_ok`, `menu_back`, `pause` | a step on the map or a choice moving, entering a level, resuming, opening the pause screen |
| `checkpoint` | touching a checkpoint flag |
| `tube` | going into a tube, and coming out of the other end |
| `crumble` | a crumble block starting to shake |
| `reveal` | finding a hidden block, or a bonus room for the first time |
| `emerge`, `spit` | a tube dweller coming out nearby; a spitter lobbing a seed |
| `tick`, `bonus` | each step of the course-clear score tally; the end of the tally |
| `hurry` | 100 seconds left on the level timer |
| `moon` | picking up a moon coin |

Sounds play once at full length, overlapping freely (up to 32 at a time). Sounds are panned slightly toward where Hatrick is on screen.

### Jingles: `assets/music/`

`jingle_clear` plays when you reach the flag, over the slide, the pose and the score tally (about 3 to 5 s); skipping the celebration with Jump cuts it off. `jingle_death` plays after a death; the level music comes back after about 1.3 s.

### Music: `assets/music/<theme>/`

Each level picks its theme folder with `music=` in `levels.txt` (see below). The overworld map plays a calm mix of the first level's theme. The stock themes are:

| Theme folder | Used by |
|---|---|
| `overworld` | Hills, and the overworld map |
| `underground` | Brickworks |
| `athletic` | Spikes, Sky, the movement playground |
| `finale` | Hatrick |

To add a new song, make a new folder, e.g. `assets/music/mysong/`, put the stems in it, and set `music=mysong` on the levels that should use it.

A theme is up to seven stems that loop together, all starting at the same moment:

| Stem | When it is heard |
|---|---|
| `lead` | in levels (muted on the map) |
| `bass`, `perc`, `bells`, `bah` | always |
| `fast` | fades in while Hatrick runs at full speed or chains long jumps, dives, rolls, spin jumps or triple jumps |
| `arp` | fades in after cap bounces and stomps in mid-air |

All stems are optional: a missing stem is just silent. To use a single finished song, save it as `bass.ogg` (heard everywhere, including the map), or as `lead.ogg` if it should be silent on the map. Then delete the other stems.

The loop length is the length of the longest stem. Shorter stems go silent until the loop restarts. For a seamless loop, make every stem exactly the same length. The music fades out on pause (it ducks), death and the flag, and restarts from the top when a level starts.

`music.txt` (optional) tells the game where the enemy hops go:

```
# lines starting with # are ignored
bpm 104
bah 7.5 15.5 23.5 28
```

`bah` lists the beats, counted from 0 at the start of the loop, where the enemies hop. Without the file, or without a `bah` line, the music plays normally and the enemies don't hop.

## Graphics: `assets/gfx/`

The art is built into the game, and any part of it can be replaced by a PNG in `assets/gfx/`. A file that isn't there keeps the built-in art, so a texture pack can be as small as one file. Start from the real thing:

```
python3 tools/export_gfx.py
```

writes every file below with today's art to `gfx-template/` (after `./build.sh`). Copy the ones you change into `assets/gfx/` and restart the game.

- PNG, any colour, with transparency: a pixel is drawn if its alpha is at least 128, and is fully transparent below that. There's no blending.
- Sizes are fixed (except the backgrounds), because collisions don't change with the art. A file of the wrong size is reported on the terminal and the built-in art is used.
- 1 pixel is one game pixel (the window shows each one 4x4).

| File | Size | What |
|---|---|---|
| `tiles.png` | 64x32 | every map tile: eight 8x8 cells per row, in the order below |
| `hatrick.png` | 80x12 or 80x24 | Hatrick: ten 8x12 frames side by side; an optional second row is the same frames while his cap is thrown |
| `walker.png` | 16x8 | the walker enemy (`g`): two 8x8 walking frames |
| `buzzer.png` | 16x8 | the buzzers (`b` `h`): two wing frames |
| `dweller.png` | 32x16 | tube dwellers: snapper open, snapper shut, spitter open, spitter shut (8x16 each, head up) |
| `cap.png` | 8x4 | the thrown cap |
| `moon.png` | 13x13 | a moon coin; one already brought home is drawn from every other pixel, pale |
| `sky.png` | any | the background of levels and `bg=sky` rooms: scrolls sideways at a quarter of the camera speed, repeating, stretched to the screen's height |
| `cave.png` | any | the background of `bg=cave` rooms, the same way |

**`tiles.png` cells**, left to right, top to bottom (row 1 is cells 0–7):

| Cells | Tile |
|---|---|
| 0, 1 | ground (`#`): with nothing above it (grass), and with ground above |
| 2 | brick (`B`) |
| 3, 4 | spikes (`^`): pointing up, and hanging from a ceiling |
| 5 | stone (`S`) |
| 6 | spring (`T`) |
| 7, 8 | slopes `/` and `\` |
| 9 | a found hidden block |
| 10 | crumble block (`C`) |
| 11 | fire bar pivot (`*` `%`) |
| 12–15 | coin (`o`): four frames of its spin |
| 16, 17 | upright tube body (`\|`): left half, right half |
| 18, 19 | sideways tube body (`-`): top half, bottom half |
| 20–27 | tube mouths, in pairs (two cells of one mouth): opening up (left, right), opening down (left, right), opening left (top, bottom), opening right (top, bottom) |
| 28 | one ember of a fire bar, drawn centred on it |
| 29 | a spitter's seed, drawn centred on it |

Cells 30 and 31 are unused. Each tile cell is drawn at the same spot whatever its neighbours: the built-in art's speckles and tube bands that vary along a run of tiles become one fixed pattern.

**`hatrick.png` frames**, left to right: standing, running (2 frames), jumping and falling, throwing, hanging from a ledge, crouching, spinning, the course-clear pose, rolling. The rolling frame is 8x8, sitting at the bottom of its 8x12 cell; the other frames face right and stand on the bottom row (the game mirrors them for left).

The overworld map, the HUD, the flag, checkpoints and particles are still drawn by the game itself.

## Levels: `assets/levels.txt`

Levels are ASCII maps. Each one starts with a header line, followed by its rows, and may have up to three bonus rooms after it:

```
= 2 brickworks   music=underground   card=bricks
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

**Header:** `=`, then the level's name. A leading number is just for your own ordering; it isn't shown. The name appears in the overworld map's banner in capitals (letters, digits and `- / : . ?`). Options:

| Option | Meaning | Default |
|---|---|---|
| `music=<folder>` | theme folder in `assets/music/` | `overworld` |
| `time=<seconds>` | the level timer, 1 to 9999 | `500` |
| `card=<style>` | the landmark beside its stop on the map: `hills`, `bricks`, `spikes`, `sky`, `castle` or `playground` | `hills` |

**Bonus rooms:** a line starting with `+` begins a bonus room of the level above it, with its own map (same size rules). The rest of the line is a name for your own use, plus an optional `bg=cave` (the default: a dim cave) or `bg=sky`. Rooms are only reachable through tubes. The start and the flag belong in the level's main area. The first time Hatrick enters a bonus room is worth 2000 points.

**Order and the map:** campaign levels are played in file order, and you can have as many as you like: the overworld lays out one stop per level along its path (the map scrolls as far as it needs), each opening once the level before it is cleared. Finishing the last one ends the run. Levels whose name starts with `lab` are not part of the campaign. The last `lab` level is the movement playground (F1, and its stop below Hatrick's house). Without any lab level there is no playground. Progress is kept by level name: renaming a level makes it count as not cleared.

**Size:** an area (the main area or a bonus room) is up to 320 rows tall and 2560 columns wide. It is as tall as its rows, but at least 32: rows sit at the bottom, so a short level is just floor and sky above it. Falling off the bottom is a death; above the top there's only sky. For long levels, give the level more time with `time=`. Empty lines inside a map count as rows. Every level needs exactly one `@` and one `F`. A level holds at most 3 moon coins, 480 enemies, 128 tube mouths, 240 fire bars, 240 tube dwellers, 960 crumble blocks and 64 checkpoints.

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
| `(` | moon coin (secret, at most 3 per level) | | |
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

**Moon coins (`(`):** each level can hide up to three, in its main area or its bonus rooms. They're the game's secrets, so put them somewhere that takes exploring or skill to reach: behind a tube, above a hidden-block staircase, at the end of a hard detour. Picking one up is worth 2000 points, but it only counts once Hatrick reaches the flag; a death before the next checkpoint loses it again. Coins brought home are saved in `~/.hatrick_progress` by level name ("`<bits> <LEVEL NAME>`" per line, bit 1 for the level's first moon coin in reading order, 2 for the second, 4 for the third; 8 means the level is cleared). They show in the map's banner on the level's stop, and as faint outlines in the level (they can still be picked up for points). Renaming a level forgets its moon coins.

**Timer:** every level has 500 seconds (or its `time=`), shown in the HUD; it turns red with a warning sound at 100 and running out is a death. It starts over with the level (and with R); a checkpoint keeps the time that was left, unless it ran out.

**Points:** coin 100, stomp or cap knockout 200, brick 50, hidden block 1100, tube dweller 500, a bonus room found 2000. At the flag, the height where Hatrick grabs the pole is worth 100 to 5000 (the very top), then every second left on the timer adds 50. A death takes the score back to the level's start, or to the last checkpoint.

To test a level without playing it, `./sim LEVEL file.tas` replays scripted input on it (levels are numbered from 0, campaign first, then labs). Add `trace` to see every frame (position, state, room and score), and set `SIM_RESPAWN=1` to keep going after deaths. `python3 tools/route.py LEVEL out.tas` searches for a route to the flag and writes it as a replay. `python3 view.py LEVEL file.tas out.png [room=N]` draws a level, or one of its bonus rooms, with the replay's path.

`./build.sh` also builds a copy of `assets/levels.txt` into the game, which is used only when the file is missing or has no playable level, and runs `./sim --check` to report mistakes.
