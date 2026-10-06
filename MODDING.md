# Modding Hatrick

## Sounds and music

The game reads every sound from `assets/` next to the binary when it starts. Music is read each time a theme starts. To replace a sound, drop in a file with the same name. No rebuild is needed; just restart the game.

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

Each level uses one theme folder:

| Theme folder | Used by |
|---|---|
| `overworld` | Hills, and the title menu |
| `underground` | Brickworks |
| `athletic` | Spikes, Sky, the movement playground |
| `finale` | Hatrick |

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

## Levels: `levels.txt`

Levels are ASCII maps that are built into the game, so run `./build.sh` after editing (it takes a second). Each level begins with a line like `= 1 hills`; the name after the number appears on the destination menu. A level named `lab ...` is a test map rather than a campaign level. Lines starting with `;` are comments.

A level is at most 32 rows tall and 256 columns wide; rows sit at the bottom of the 32-row map. Legend:

| Char | Tile | Char | Tile |
|---|---|---|---|
| `@` | start | `F` | flag (goal) |
| `#` | ground | `B` | brick (breakable) |
| `S` | stone | `T` | spring |
| `^` | spikes | `o` | coin |
| `/` `\` | slopes | `g` | walker enemy |
| `b` | buzzer, flies up and down | `h` | buzzer, flies left and right |

The music theme for each level slot is set in `themeof()` in `hatrick.c`. Levels past the fifth use `athletic`.
