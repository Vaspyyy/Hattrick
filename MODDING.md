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
| `overworld` | Hills, Tube Town, Ramp Rally, and the overworld map |
| `underground` | Brickworks, Ember Fort |
| `athletic` | Spikes, Crumble Canyon, Sky, the movement playground |
| `finale` | Hatrick |

To add a new song, make a new folder, e.g. `assets/music/mysong/`, put the stems in it, and set `music=mysong` on the levels that should use it.

A theme is up to ten stems that loop together, all starting at the same moment:

| Stem | When it is heard |
|---|---|
| `lead` | in levels (muted on the map) |
| `bass`, `perc`, `bells`, `bah` | always |
| `fast` | fades in while Hatrick runs at full speed or chains long jumps, dives, rolls, spin jumps or triple jumps |
| `arp` | fades in after cap bounces and stomps in mid-air |
| `danger` | fades in when the timer is under 100 or a boss fight is on screen |
| `secret` | swells as Hatrick gets near a moon coin he hasn't found yet (a hint) |
| `mallet` | bonus rooms only: it replaces every other stem but `danger` and `secret` (the built-in songs play the theme on marimba and glockenspiel here) |

In water the whole mix is low-passed, so it sounds muffled.

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
| `sky2.png` | any | an optional nearer layer drawn over `sky.png` at half the camera speed; its transparent pixels show `sky.png` behind |

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
| `N` | boss (see Bosses) | `J` | mini-boss |
| `I` | ice (slippery ground) | `<` `>` | conveyor belt moving left / right |
| `=` | swing pole | `w` | water (fills down to the ground) |
| space | empty | | |

**Movement surfaces (`I` `<` `>` `=` `w`, movement.h):** ice and conveyors are ground with a surface: ice brakes at a fifth of the normal rate, a belt moves anything standing on it half a pixel per frame and its push carries into a jump. A swing pole is a bar Hatrick grabs in the air and turns around; leave it about two tiles of space all round. Water only needs its surface row: every empty cell below a `w` is water too, down to the first solid tile.

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

**Bosses (`N` `J`):** `N` marks a level's boss and `J` a mini-boss; the header picks which with `boss=haberdasher`, `bigwalker`, `cloudking` or `matriarch` and `mini=walker` or `mini=buzzer` (without the option the mark does nothing). Each one owns an arena: the 32 columns (one screen) around its mark. When Hatrick walks in, stone gates shut on the arena's two edge columns (wherever they are empty), the camera settles on it, the boss's name and health show at the top and the `danger` stem comes in; beating the boss opens the gates. A death restarts the fight; a checkpoint touched past an arena keeps that boss beaten, so put the checkpoint after a mini-boss's arena. Mini-bosses are worth 1000 points, bosses 5000, the Haberdasher 10000.

| Boss | Its mark | How it's beaten |
|---|---|---|
| `haberdasher` | the cell he stands in, on the floor | Knock his scissor boomerangs down with the cap; after two volleys he pants, and a stomp or the cap hurts him (3 times). Then his top hat flies off: hit it with the cap to wear it, and while Hatrick wears it a stomp hurts him (3 times) and a hit only knocks the hat off. |
| `bigwalker` | the seesaw's pivot, on the row of its board (15 columns wide, over a pit: lava shows wherever the area's bottom row is empty) | Ground pound the board on the side he stands on and he slides off into the lava (3 times). |
| `cloudking` | where he floats, high above the floor | Throw the cap into one of his lightning clouds: it zaps him (3 times). The clouds can be stood on; each one strikes the floor below it after flashing. |
| `matriarch` | anywhere in the arena; she uses the arena's tube mouths that open up or down (`M` or linked) | Hit her with the cap while she is out of a tube (5 times). After two hits ordinary snappers pop up as decoys. |
| `mini=walker` | the cell it stands in | Stomp it 3 times; the cap only stuns it. |
| `mini=buzzer` | the middle of its flight, in the air | Stomp it or cap it, 3 times. It swoops and drops stingers. |

**Timer:** every level has 500 seconds (or its `time=`), shown in the HUD; it turns red with a warning sound at 100 and running out is a death. It starts over with the level (and with R); a checkpoint keeps the time that was left, unless it ran out.

**Points:** coin 100, stomp or cap knockout 200, brick 50, hidden block 1100, tube dweller 500, a bonus room found 2000. At the flag, the height where Hatrick grabs the pole is worth 100 to 5000 (the very top), then every second left on the timer adds 50. A death takes the score back to the level's start, or to the last checkpoint.

To test a level without playing it, `./sim LEVEL file.tas` replays scripted input on it (levels are numbered from 0, campaign first, then labs). Add `trace` to see every frame (position, state, room and score), and set `SIM_RESPAWN=1` to keep going after deaths. `python3 tools/route.py LEVEL out.tas` searches for a route to the flag and writes it as a replay. `python3 view.py LEVEL file.tas out.png [room=N]` draws a level, or one of its bonus rooms, with the replay's path.

`./build.sh` also builds a copy of `assets/levels.txt` into the game, which is used only when the file is missing or has no playable level, and runs `./sim --check` to report mistakes.

## Cap objects (cap.h)

| Char | Tile |
|---|---|
| `H` | cap post: a brass ring in the air, not solid |
| `X` | cap switch block (solid) |
| `R` | red block: solid while red is on (the start) |
| `U` | blue block: solid while blue is on |

**Cap posts (`H`):** a thrown cap that reaches one sticks to it for 2 seconds and is a small platform Hatrick can land on. Pressing a cap button while it's stuck pulls Hatrick to it like a grapple, then the cap comes home. Put them over gaps, about 3 tiles out from where Hatrick throws.

**Cap switches (`X`):** only the cap can hit one (head bumps and stomps do nothing). Each hit swaps red and blue: every `R` and `U` in the level (all areas) turns solid or into a dotted outline. A checkpoint remembers the state; a restart puts red back on.

**Hat swap:** when the cap knocks out a walker (`g`), Hatrick wears its power: his ground pound breaks stone (`S`) as well as bricks. A buzzer (`b`, `h`) gives a flutter: press Jump again while falling and hold it to hover for a moment, once per jump. A spitter (`m`) gives seeds: every throw also lobs a seed that beats enemies and tube dwellers and smashes bricks. A hit then takes the power away instead of a life (Hatrick blinks for 1.5 s); pits and the timer still count. A death or a restart ends the power.

**Coins and the cap:** coins the cap flies through ride home on it and count when it's caught (or when Hatrick goes into a tube). A death before then loses them.

**Dark areas:** `bg=dark` on a bonus room's `+` line, or on a level's header line for its main area, makes it a dark cave. Only a small circle around Hatrick is lit; the cap is a lantern with a wider glow, and the spots it passes stay lit for 3 seconds.

**Checkpoints** (`K`) are drawn as hat racks; touching one hangs a spare cap on its hook.

The movement playground has two bonus rooms for all of these: the tube just behind its start (to the left) leads to the cap garden, and the garden's second tube to a dark lantern cave.

Cap checks: `gcc -O1 -w tools/test_cap.c -o /tmp/hatrick-cap-tests && /tmp/hatrick-cap-tests`.

## Enemies (enemies.h)

| Char | Enemy | Char | Enemy |
|---|---|---|---|
| `u` | shy-walker | `k` | shell walker |
| `v` | cap thief (a bird; place it where it perches) | `L` | cloud rider |
| `x` | chomp stake (on the ground) | `p` | puffer buzzer |
| `Q` | stone thwomp (top-left of a 2x2 space) | `Y` | beat platform (a run of them is one platform) |
| `Z` | beat piston (solid) | `$` `&` | beat fire bar, clockwise / anticlockwise (arms `~`) |

**Shy-walker (`u`):** walks like a walker, but it turns to face a cap flying at it and its mask bounces the cap away. Stomp it, catch it with the cap from behind before it turns, or bump into its back (300 points). Touching its front hurts.

**Shell walker (`k`):** a stomp (or the cap) turns it into a shell. Touching the shell, or stomping it again, kicks it away from Hatrick; rolling or diving into it kicks it faster. A sliding shell knocks over enemies, breaks bricks and bounces off walls, and hurts Hatrick from the side; a stomp stops it. A resting shell walks back out after 7 seconds.

**Cap thief (`v`):** perches until a thrown cap passes within about 8 tiles, then swoops and snatches it. Until Hatrick touches the bird, the cap can't be thrown. It flies off just out of reach and waits when it gets far ahead. Touching it gives the cap back; stomping it does too (400 points). It never hurts.

**Cloud rider (`L`):** wakes when Hatrick is within 25 tiles, follows him overhead and drops a walker every 2.5 seconds, never more than 3 of its own at once. Stomp it or cap it (800 points). Put it in the upper part of a sky level.

**Chomp (`x`):** the ball sits beside its stake and lunges up to 6 tiles at Hatrick when he comes near, then pulls back. It can't be beaten; a stomp bounces off and the cap is knocked away. Ground pound the stake three times and it breaks loose, bounding the way Hatrick faces and smashing every brick in its path (1000 points) until it hits a solid wall.

**Puffer buzzer (`p`):** bobs like a buzzer. The cap puffs it up for 4 seconds into a ball that throws Hatrick high when he lands on it (higher with Jump held). A stomp beats it while it's small.

**Thwomp (`Q`):** waits above, drops when Hatrick is underneath (fatal), sits a moment, then rises slowly. From the side it's a wall, and Hatrick can ride its top back up. Leave its 2x2 space empty.

**Stacks:** walkers (`g`), crabs (`c`), shy-walkers (`u`) and shell walkers (`k`, a resting shell too) stand on each other. Put one right above another in the level file (or let one fall onto another) and it rides along: a stack walks and turns with its bottom enemy, though a shy-walker keeps facing its own way. Stomps take them off one at a time from the top; knock out a lower one (a sliding shell, the cap) and the ones above drop down. A sliding shell never carries a rider.

**The beat (`Y` `Z` `$` `&`):** these follow the level's music, using `bpm` from its `music.txt` (104 if missing), counted from the moment the level (re)starts. A beat platform moves two tiles every fourth beat, back and forth, toward whichever side has two free tiles, and carries whoever stands on it. It flashes white during the beat before it moves. A piston shoots spikes into its first free side (up, left, right, then down) on beats 2 and 4 of each bar. Its stripes flash red just before. Beat fire bars jump an eighth of a turn on every beat.

## Collectibles (collect.h, collect_ui.h)

| Char | Object |
|------|--------|
| `G` | **Secret exit**: a second goal pole with a red pennant, in the main area or a bonus room (one per level). Touching it clears the level like `F` and opens the path to the level's secret level on the map. |
| `P` | **Postcard**: one per level. Picking it up is worth 1000 points and saves at once; it then hangs as a painted picture in Hatrick's house. |

Header options:

- `star=<seconds>`: the time to beat for the level's **star**. The star (shown by the stop on the map and on the level's trophy at home) needs every moon coin of the level brought home plus one clear inside that time. Without `star=` the time is the main area's width in pixels / 55 + 15 seconds.
- `secret=<level>`: makes this a **secret level**. It hangs off the named campaign level (its number, like `secret=1`, or its name, like `secret=hills`) and its stop stays hidden until that level's `G` exit is taken. Secret levels come after the labs in the level list and keep their own progress.

Caps: every campaign level whose moon coins are all brought home (or, if it has none, that is cleared) hangs a new cap on the rack in Hatrick's house. Cap 2 (Feather) stalls an air throw 6 frames longer; cap 4 (Magnet) pulls in coins near the thrown cap. The cap worn is kept in `~/.hatrick_progress` as a `*HAT` line. Progress bits there: 1/2/4 moon coins, 8 cleared, 16 star time beaten, 32 secret exit taken, 64 postcard found.

Hatrick's house grows a room per level cleared, each with a trophy (with a star once earned), the level's postcard once found, and a framed portrait of the boss for castle levels; on the map the house grows annexes too.

Checks: `gcc -O1 -w tools/test_collect.c -o /tmp/hatrick-collect-tests && (cd tools && /tmp/hatrick-collect-tests)`.

## New worlds (worlds.h, worlds.c, `assets/worlds.txt`)

Tidepool Bay, Frostfall Peak, Crayon Woods and Clocktower live in `assets/worlds.txt`, in the same format as `levels.txt`. The game reads it right after `levels.txt`, as if it followed that file, and errors name `worlds.txt` and its own line. `mklevels.py` builds both into the game.

| Char | Tile | Char | Tile |
|---|---|---|---|
| `c` | crab: walks its platform fast and raises its claws now and then; a stomp then hurts (a ground pound or the cap still wins) | `s` | snow: solid ground that slows walking to about half and bogs down rolls |
| `q` | quicksand: Hatrick sinks slowly and walks at a crawl, a jump gets him out, and he's lost once his head goes under. Make pits at least two deep and flush with the ground | `j` | jelly block: solid; landing from a fall bounces Hatrick back up, holding Jump builds the bounce higher each time, a ground pound gives the biggest |
| `f` | flower: 1000 points; the last of a level's flowers is worth 10000. The HUD counts them in levels that have any | `[` `]` | clock blocks: the blue `[` and red `]` blocks swap between solid and outline every two beats of the level's music, blinking for the last half beat. A block never closes on Hatrick |

Header options:

- `theme=beach|frost|crayon|clock|desert`: recolours the built-in background and the ground and grass (`none` by default). A `sky.png` still wins.
- `avalanche=<px/s>`: a wall of snow rolls in from the left at that speed (Hatrick runs at about 94 px/s). Touching it is a death; it waits while Hatrick is in a bonus room, stops short of the flag, and starts well behind him again at a checkpoint.
- `before=<level name>`: puts this campaign level just before the named one on the map (spaces as `_`). The new worlds use `before=hatrick`.

Map cards: `beach`, `peak`, `woods`, `clock`.

## Level gimmicks (gimmicks.h)

| Char | Gimmick | Char | Gimmick |
|---|---|---|---|
| `O` | hat flip flower (the cell stays empty) | `A` | gold block (solid) |
| `V` | seesaw pivot; its plank is the `_` cells either side | `{` `}` | scale lift halves |
| `D` | door that turns its bonus room (the cell stays empty) | `E` | cannon (solid) |
| `W` | propeller fan (solid) | | |

**Hat flip flower (`O`):** touching it starts 30 seconds of something odd, set per level with `flip=`: `gravity` (the default) turns the bonus room it's in upside down, so Hatrick falls toward the ceiling; `speed` runs the whole game at double speed with the `fast` stem on (the timer still counts at normal speed); `swap` turns every brick into a coin and every coin into a brick, and back when time is up. A flower in the main area never turns gravity over (the flag must stay put): there it doubles the speed instead. Build a gravity room with a solid ceiling and at least 32 rows, since its top becomes its floor. Worth 1000 points.

**Gold block (`A`):** bump it from below and it rides on Hatrick's head for 20 seconds, spilling a coin every 6 px he travels (100 at most): run, roll and dive to milk it.

**Seesaw (`V` and `_`):** write the plank as a row of `_` with the `V` among them, e.g. `___V___`; the pivot is drawn below the `V`. It leans toward Hatrick's weight; a ground pound slams it right down. Like lifts, it's solid from above only.

**Scale lifts (`{` `}`):** a run of `{` and the next run of `}` to its right on the same row hang from one rope over a pulley 5 tiles above. Standing on one lowers it and raises the other; they stop below the pulley and on the ground.

**Turning door (`D`):** in a bonus room, Up in front of a door turns the whole room a quarter turn clockwise; Hatrick steps out of a door with floor under it (the same one if it has some). Leaving the room puts it back as the file has it. A turning room can be at most 320 columns wide; slopes don't turn well, and other threads' special tiles (ice, conveyors, water, cap posts) don't turn with it, so keep turning and gravity rooms to plain tiles, coins, walkers, buzzers, crumble blocks, tubes and fire bars.

**Cannon (`E`) and propeller fan (`W`):** a cannon fires a cannonball toward Hatrick every 2⅓ seconds while he's within about 27 tiles. Cannonballs can be stomped or hit with the cap (200 points). A fan blows Hatrick upward anywhere in the 10 tiles above it, up to the first solid tile.

**Header options:**

| Option | Meaning |
|---|---|
| `rise=lava` or `rise=water`, optionally `:<px per s>` (default 10) | a kill line rising from the bottom of the main area, after 2 seconds. A checkpoint lowers it to 40 px under Hatrick |
| `scroll=<px per s>` | the main area scrolls by itself, airship style; Hatrick can't leave the screen, and being pushed into a wall by its left edge is a death |
| `sky=night` | night: a starry sky and a darker palette, and the walkers (`g`) fly while the bobbing buzzers (`b`) walk |
| `copy=<level name>` | the level uses another level's map and bonus rooms (write spaces in the name as `_`); its own rows, if any, add bonus rooms. With `sky=night` it's the day/night pair from one map |
| `flip=gravity`, `speed` or `swap` | what the level's hat flip flowers do |

**Room options:** `bg=8bit` draws the room in chunky 2x2 pixels in the NES palette, with the theme's chiptune version (`assets/music/<theme>-8bit/`, made by `audio/chip.py`); `bg=night` is the night look for one room.

**Coin rush:** F3 on the map plays three levels in a row (cleared ones, once three are cleared), 100 seconds each, counting only the coins collected. A death ends the rush. The best total is kept in `~/.hatrick_rush`.

**Gimmick gallery:** F2 plays the lab levels other than the playground, one after another: `lab gimmicks lava tower`, `toybox` (seesaws, lifts, gold blocks, and tubes to a gravity flip room, a turning room and an 8-bit room), `airship` and `hills at night`.

Tests: `gcc -O1 -w tools/test_gimmicks.c -o /tmp/hatrick-gimmick-tests && /tmp/hatrick-gimmick-tests` from the hatrick folder.
