# Hatrick movement

Run `./build.sh` to build, then `./hatrick` to play. Restart the game to load a newly built executable. The game loads its levels, music, sounds and any replacement art from `assets/` next to the executable at startup; all of them can be edited or swapped for your own, see [MODDING.md](MODDING.md). `./hatrick --silent` runs without touching the speakers; `--dump out.wav` additionally records the mix.

The game opens on the overworld: an island with Hatrick's house, one stop per level along a path, and the movement playground below the house. Walk along the paths with the arrows or the controller stick/D-pad (hold a direction to keep walking), then Enter, Z/Space, controller A, or Start to play the stop you stand on. Clicking a stop with the mouse walks there; clicking Hatrick's stop enters it. A level's stop opens once the one before it is cleared (red: open, gold: cleared, grey: locked); the house shows the high-score table. After a level Hatrick is back on the map; the first clear opens the path to the next stop. Starting level 1 begins a new run (score and time from zero); later levels carry the run on. Esc or Q on the map quits.

During play, Esc or controller Start opens the pause screen and pauses the entire level. Switching to another window also pauses; keyboard and controller input are ignored while Hatrick is unfocused. Esc, X/C, controller B, or choosing CONTINUE resumes without reloading; EXIT TO MAP (Down, then Enter/Jump) goes back to the overworld. The mouse can pick either. Q quits from anywhere. M / controller View toggles sound.

| Action | Keyboard |
| --- | --- |
| Move | Left / Right |
| Jump | Z, Y, or Space |
| Throw cap | X or C |
| Crouch / crouch-walk | Hold Down, optionally Left / Right |
| Roll | Down + X or C on the ground |
| Boost a roll | Keep Down held and tap X or C again; boosts have a 15-frame cooldown |
| Long jump | Left/Right + Down + Jump, or Jump from a roll; crouch-walking also works |
| Backflip | Down + Jump while stationary |
| Side flip | Jump while reversing a run |
| Double / triple jump | Jump again promptly after landing; the third jump requires forward movement |
| Ground spin | Tap Up on the ground |
| Spin jump | Up + Jump, or Jump during a ground spin |
| Ground pound | Press Down in the air |
| Spinning ground pound | Press Down during a spin jump |
| Dive | Down + X in the air; holding either cap button then pressing Down also works |
| Upward throw | Up + X or C |
| Downward throw | Down + C in the air |
| Spin throw | X or C during a ground spin or spin jump, without Up / Down |
| Extend / homing throw | Tap X again while the cap is out; one extension per flight, aimed at a nearby enemy in front when available |
| Recall cap | Tap C while the cap is out |
| Ground-pound roll | Down + C in the final six frames before impact, or Down + X directly at impact |
| Ground-pound jump | Jump promptly after the ground-pound landing |
| Catch jump / air catch twirl | Jump within ten frames of catching the returning cap; jumping just before the catch also buffers the action |
| Roll cancel into cap throw | Release Down and press X or C; add Jump for a jumping roll cancel |
| Ledge grab / climb | Hold toward an exposed ledge while falling; Up or Jump climbs |
| Drop from a ledge | Down or the opposite horizontal direction; Jump + away wall-jumps |
| Slope roll | Down on a ramp rolls downhill; uphill slows the roll |
| Enter a tube | Down on top of a brass tube that opens up; walk into one that opens sideways; Up while jumping into one that opens down |
| Skip the course clear | Jump during the flag celebration (it goes straight to the next level) |
| Movement playground | F1 opens ramps, ledges, and a low tunnel; F1 again returns to the map |
| Pause / resume | Esc; X or C also resumes from the pause screen |
| Restart / mute / quit | R / M / Q |
| Volume | + / − (or keypad + / −), ten steps |

Hold Down when a dive or long jump lands to flow into a roll. A spinning ground pound starts a faster roll. Spin jumps use low gravity on both ascent and descent. An air catch twirl gives a small upward pop and recharges the next throw's stall once per airborne cycle. Ground spins last up to 90 frames and allow a moving spin throw. Ground cap vaults launch higher than airborne cap bounces. Crouching and rolling shrink the collision box, and releasing Down under a low ceiling keeps the character tucked until there is room to stand.

Levels have checkpoint flags (a death comes back to the last one touched; R restarts the whole level), brass tubes to bonus rooms and shortcuts, crumble blocks, hidden blocks found by a head bump from below, fire bars, and tube dwellers that only the cap can beat. The score counts coins, stomps, bricks and finds, plus a flag-height bonus and a bonus for every second left on the level's 500-second timer (it turns red at 100; running out is a death). After the last level a top-ten score asks for three initials (Up/Down change a letter, Left/Right or Jump/Cap move, Jump on the last letter or Start saves) and is kept in `~/.hatrick_scores`; Hatrick's house on the map shows the table. Levels can also hide up to three secret moon coins each; they count once you reach the flag with them, and show in the map's banner when Hatrick stands on the level's stop. Cleared levels and moon coins are saved in `~/.hatrick_progress`. The HUD shows coins, score and the time left; the run time and score appear at the end of the run. See [MODDING.md](MODDING.md) for how each object works in `levels.txt`.

Holding the opposite horizontal direction brakes normal jumps, long jumps, and dives. After a dive landing, horizontal input regains ground control immediately. Release movement or release Down from a roll to brake on the ground. [Movement reference and measured stopping distances](PHYSICS.md).

Long jumps use your movement direction rather than a crouch-speed threshold. Down can precede Jump by any amount while you keep moving, or arrive within the first five frames after a running jump. Recently released running input also has a short grace period. A stationary crouch-jump remains a backflip. A cap throw on the takeoff frame preserves the jump; later first air throws still have the eight-frame stall.

The cap stays out while its throw button is held. Pressing X shortly before a returning cap arrives buffers the next throw for ten frames. A roll-cancel throw travels near the floor and can reflect from one wall. Land on it or dive into it to bounce; throwing it does not consume the bounce. A grounded cap vault also leaves the airborne bounce available. Wall jumps deliberately restore cap bounce, dive, and throw stall.

On controllers, A/B jumps, X/Y throws, shoulders/triggers crouch, and the left stick or D-pad supplies directions. Start opens the pause screen; on it and on the map, A/Start selects and B goes back. Use the second cap button (Y) for the downward throw. Horizontal stick position controls walking/running speed. The same direction combinations apply to spin jumps and upward throws.

Focused movement checks: `gcc -O1 -w tools/test_movement.c -o /tmp/hatrick-movement-tests && /tmp/hatrick-movement-tests`. Recorded level routes are a separate optional `--routes` check; changing movement requires retiming those recordings.

Audio check (private Xvfb, records the mix through the silent device): `python3 tools/test_audio.py`.

Focused map and pause checks: `gcc -O1 -w tools/test_menu.c -o /tmp/hatrick-menu-tests && /tmp/hatrick-menu-tests`. Native keyboard/mouse map playtest and screenshot capture: `python3 tools/test_menu_native.py`.

Level file checks (levels.txt parsing, error reports, the map with many levels): `gcc -O1 -w tools/test_levels.c -o /tmp/hatrick-level-tests && /tmp/hatrick-level-tests`.

Level object checks (checkpoints, tubes and rooms, crumble and hidden blocks, fire bars, tube dwellers, points, the course clear, high scores): `gcc -O1 -w tools/test_features.c -o /tmp/hatrick-feature-tests && /tmp/hatrick-feature-tests`. Native playtest of the same objects on a private Xvfb with silent audio, a throwaway HOME and screenshots in `/tmp/hatrick-feature-playtest/`: `python3 tools/playtest_features.py`.

Replacement-art check (exports the templates, paints some, and looks at the real game on a private Xvfb): `python3 tools/test_gfx.py`.

Level proofs: `tas/1.tas` to `tas/5.tas` beat each level in the simulator, and `tas/1-bonus.tas` to `tas/5-bonus.tas` beat it again through its bonus room (`./sim 0 tas/1.tas` … `./sim 4 tas/5.tas`, or all at once with the movement checks' `--routes`). `python3 tools/route.py LEVEL out.tas [wp=X,Y[,ROOM] ...]` searches for a new one after level or movement changes.
