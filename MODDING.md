# Modding Hatrick

Everything you can mod lives in `assets/` next to the game:

| Path | What |
|---|---|
| `levels.txt` | every level, its name, music and menu card |
| `sfx/` | sound effects |
| `music/` | jingles, and one folder per music theme |

The game reads these files when it starts, so there is no rebuild: edit, then restart the game. Mistakes never stop the game. A broken level, an unknown option or a missing sound is reported on the terminal with the file and line, and the rest still works. Run `./hatrick` from a terminal to see these reports.

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

Sounds play once at full length, overlapping freely (up to 32 at a time). Sounds are panned slightly toward where Hatrick is on screen.

### Jingles: `assets/music/`

`jingle_clear` plays when you reach the flag. The game waits 4 s on the flag, so keep it about that long. `jingle_death` plays after a death; the level music comes back after about 1.3 s.

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

Levels are ASCII maps. Each one starts with a header line, followed by its rows:

```
= 2 brickworks   music=underground   card=bricks
; lines starting with ; are comments
            o o o
@      BBB         g        F
############################
```

**Header:** `=`, then the level's name. A leading number is just for your own ordering; it isn't shown. The name appears on the destination menu in capitals (letters, digits and `- / : . ?`). Options:

| Option | Meaning | Default |
|---|---|---|
| `music=<folder>` | theme folder in `assets/music/` | `overworld` |
| `card=<style>` | menu card picture: `hills`, `bricks`, `spikes`, `sky`, `castle` or `playground` | `hills` |

**Order:** campaign levels are played in file order, and you can have as many as you like (the menu pages through them six at a time). Finishing the last one ends the run. Levels whose name starts with `lab` are not part of the campaign. The last `lab` level is the movement playground (F1, and the last card on the menu). Without any lab level there is no playground.

**Size:** a level is at most 32 rows tall and 256 columns wide, with at most 48 enemies. Rows sit at the bottom of the 32-row map, so a short level is just floor and sky. Empty lines inside a map count as rows. Every level needs exactly one `@` and one `F`.

| Char | Tile | Char | Tile |
|---|---|---|---|
| `@` | start | `F` | flag (goal) |
| `#` | ground | `B` | brick (breakable) |
| `S` | stone | `T` | spring |
| `^` | spikes | `o` | coin |
| `/` `\` | slopes | `g` | walker enemy |
| `b` | buzzer, flies up and down | `h` | buzzer, flies left and right |
| space | empty | | |

To test a level without playing it, `./sim LEVEL file.tas` replays scripted input on it (levels are numbered from 0, campaign first, then labs). `python3 view.py LEVEL file.tas out.png` draws the level with the replay's path.

`./build.sh` also builds a copy of `assets/levels.txt` into the game, which is used only when the file is missing or has no playable level.
