# Hatrick movement

Run `./build.sh` to build, then `./hatrick` to play. Restart the game to load a newly built executable.

The game opens on a destination menu with the five current levels and the movement playground. Use arrows or the controller stick/D-pad to choose, then Enter, Z/Space, controller A, or Start to play. You can also hover and click a card with the mouse. Entering a destination begins a fresh run there.

During play, Esc or controller Start opens the menu and pauses the entire level. Switching to another window also pauses; keyboard and controller input are ignored while Hatrick is unfocused. Esc, X/C, or controller B resumes without reloading; selecting a destination starts it again. Q quits from either screen. M / controller View toggles sound. Hold a direction to repeat menu navigation.

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
| Movement playground | F1 opens ramps, ledges, and a low tunnel; F1 again returns to the first campaign level |
| Destination menu / resume | Esc; X or C also resumes from the menu |
| Restart / mute / quit | R / M / Q |

Hold Down when a dive or long jump lands to flow into a roll. A spinning ground pound starts a faster roll. Spin jumps use low gravity on both ascent and descent. An air catch twirl gives a small upward pop and recharges the next throw's stall once per airborne cycle. Ground spins last up to 90 frames and allow a moving spin throw. Ground cap vaults launch higher than airborne cap bounces. Crouching and rolling shrink the collision box, and releasing Down under a low ceiling keeps the character tucked until there is room to stand.

Holding the opposite horizontal direction brakes normal jumps, long jumps, and dives. After a dive landing, horizontal input regains ground control immediately. Release movement or release Down from a roll to brake on the ground. [Movement reference and measured stopping distances](PHYSICS.md).

Long jumps use your movement direction rather than a crouch-speed threshold. Down can precede Jump by any amount while you keep moving, or arrive within the first five frames after a running jump. Recently released running input also has a short grace period. A stationary crouch-jump remains a backflip. A cap throw on the takeoff frame preserves the jump; later first air throws still have the eight-frame stall.

The cap stays out while its throw button is held. Pressing X shortly before a returning cap arrives buffers the next throw for ten frames. A roll-cancel throw travels near the floor and can reflect from one wall. Land on it or dive into it to bounce; throwing it does not consume the bounce. A grounded cap vault also leaves the airborne bounce available. Wall jumps deliberately restore cap bounce, dive, and throw stall.

On controllers, A/B jumps, X/Y throws, shoulders/triggers crouch, and the left stick or D-pad supplies directions. Start opens the menu; inside it, A/Start selects and B goes back. Use the second cap button (Y) for the downward throw. Horizontal stick position controls walking/running speed. The same direction combinations apply to spin jumps and upward throws.

Focused movement checks: `gcc -O1 -w tools/test_movement.c -o /tmp/hatrick-movement-tests && /tmp/hatrick-movement-tests`. Recorded level routes are a separate optional `--routes` check; changing movement requires retiming those recordings.

Focused menu checks: `gcc -O1 -w tools/test_menu.c -o /tmp/hatrick-menu-tests && /tmp/hatrick-menu-tests`. Native keyboard/mouse menu playtest and screenshot capture: `python3 tools/test_menu_native.py`.
