# Hattrick 2.5D

A remake of Hattrick in Godot 4: real 3D characters and worlds, a side-on camera, and the same
tuned movement as the C game. It reads the same `assets/` (levels, worlds, music and sounds), so
level edits and music mods show up in both.

```sh
cd remake
./play.sh              # title screen
./play.sh --level 0    # straight into level 1
```

`play.sh` uses Godot 4.7 if you have it (`$GODOT`, or `godot` / `godot4` on the PATH). Otherwise it
downloads the official Godot 4.7.2 build once into `remake/.godot-bin/` (about 60 MB), checks it
against the published SHA-512 sum, and runs it. No editor or import step is needed: every model is
built in code and the assets load at run time.

## Controls

The same as the C game ([CONTROLS.md](../CONTROLS.md)): arrows move, Z / Y / Space jump, X throws
the cap, C is the second cap button (recall, downward throw), R restarts the level, Esc pauses,
M mutes, + / − change the volume, F11 toggles fullscreen. On a controller: A / B jump, Y throws,
X is the second cap button, shoulders and triggers crouch, the stick (analog speed) or D-pad
moves, Start pauses. Controllers rumble.

## What's in it so far

This is the first slice: level 1 plays from start to flag in 3D.

- **Movement:** all of it, ported line by line from `hatrick.c`, `movement.h` and the core of
  `cap.h`: runs, the triple jump, long jumps, flips, rolls, dives, ground pounds, wall kicks,
  ledges, the cap (throws, bounces, vaults, catches, recalls), swing poles, water, ice, conveyors
  and slopes. It runs at 60 steps a second in the same fixed point maths, and recorded routes
  (`tas/`) give the same result frame for frame as the C simulator.
- **Hatrick:** a 3D model built in code with procedural animation for every state: run cycle
  by distance, the flips and spins timed from the sim, squash and stretch, throw and catch arms,
  a face that blinks and reacts.
- **Levels:** built from `levels.txt`: grassy ground slabs with rounded edges, stone blocks,
  bricks, coins, springs, spikes, hidden and crumble blocks, brass tubes into bonus rooms,
  checkpoints, moon coins and the flag. Sky areas get hills, woods and clouds behind them; caves
  a rock wall, stalactites and crystals with a lantern light on Hatrick.
- **Enemies:** walkers (stacking, hopping on the music's "bah") and buzzers.
- **The rest:** HUD with hearts and moon coin slots, level card, course clear tally, iris wipes,
  adaptive music (the same stems and rules as the C game's director), sound effects, title
  screen, level select and pause screen. Cleared levels and moon coins are saved in Godot's
  user folder (`~/.local/share/godot/app_userdata/Hattrick/progress.json` on Linux).

Not yet: the later enemies (shell walkers, tube dwellers, fire bars, the bosses), the gimmick
levels and worlds, items and the shop, the overworld map. Later levels can be played from the
level select, but whatever isn't ported is missing from them.

## How it fits together

| Path | What |
|---|---|
| `src/sim/levels.gd` | reads `levels.txt` and `worlds.txt` |
| `src/sim/sim.gd` | the game logic, ported from the C game; draws nothing |
| `src/main.gd` | runs the sim at 60 Hz, feeds the views, title / pause / level flow |
| `src/view/level_view.gd` | the level's 3D world, updated from the sim's changed cells |
| `src/view/hatrick.gd` | Hatrick's model and animation |
| `src/view/actors.gd` | enemies and the thrown cap |
| `src/view/fx.gd` | particles and score pop-ups |
| `src/view/backdrop.gd`, `lighting.gd`, `camera_rig.gd` | scenery, sky and light, the camera |
| `src/ui/` | HUD and menus |
| `src/audio/audio.gd` | music stems and sound effects |
| `shaders/` | toon shading and outlines, the ground, blocks, coins, cloth |

The sim keeps the C names (`hx`, `hvy`, `st`, `cst`...) so the two read side by side. The views
only read its state and its events (`ev`: sounds, particles, kills; `dirty`: changed cells).

## Tools

Run from the repository root. Godot works inside `remake/`, so relative file arguments start
there (`../tas/1.tas`). The screenshot tools need a display; `xvfb-run -a` gives them one.

```sh
# check that every script parses (silent when they do; exit 1 when one doesn't)
remake/tools/check.sh
# replay a recorded route through the port; diff with the C simulator's `./sim 0 tas/1.tas trace`
godot --headless --path remake --script res://tools/sim_trace.gd -- 0 ../tas/1.tas trace
# pose Hatrick in every state and save screenshots
godot --path remake --script res://tools/booth.gd -- /tmp/booth
# play a recorded route in the full game and save screenshots after some steps
# (--skip 250 runs the first 250 steps without drawing them, much faster on a slow GPU)
godot --path remake -- --replay ../tas/1.tas --shots 60,300 --out /tmp/shots
```

Fonts: Fredoka (SIL Open Font License, `fonts/Fredoka-OFL.txt`).
