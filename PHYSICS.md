# Movement reference and braking

The public [OdysseyDecomp movement constants](https://github.com/MonsterDruide1/OdysseyDecomp/blob/696db2ff5ed558b053c753de420504b8682f24b0/src/Player/PlayerConst.cpp) provide a concrete reference. The [long-jump state](https://github.com/MonsterDruide1/OdysseyDecomp/blob/696db2ff5ed558b053c753de420504b8682f24b0/src/Player/PlayerStateLongJump.cpp) and [dive state](https://github.com/MonsterDruide1/OdysseyDecomp/blob/696db2ff5ed558b053c753de420504b8682f24b0/src/Player/PlayerStateHeadSliding.cpp) pass separate acceleration and braking values into their movement functions.

Hatrick uses 1/256 pixel fixed-point velocities at 60 Hz. For horizontal tuning, scale Odyssey's normal maximum speed of 14 units/frame to Hatrick's 400 fixed-point units/frame (1.5625 pixels/frame). That conversion is 400/14; rounded integer acceleration values are used below.

| Parameter | Odyssey default | Hatrick adaptation |
| --- | --- | --- |
| Normal run speed | 14 units/frame | 400 fixed-point units/frame, without acceleration overshoot |
| Ground braking | `NormalBrakeFrame = 10` (also 10 for Odyssey's 2D mode) | 40 fixed-point units/frame², reaching zero in 10 frames from normal running speed |
| Normal air acceleration forward | 0.5 units/frame² | 14 fixed-point units/frame² |
| Normal air braking backward | 1.0 units/frame² | 28 fixed-point units/frame² |
| Long-jump and dive braking | 0.5 units/frame² | 14 fixed-point units/frame² when holding opposite horizontal input |
| Long-jump and dive minimum speed | 2.5 units/frame | 71 fixed-point units/frame |

These are the decompilation's default constants; its loader can override them with game parameters. Hatrick adapts the horizontal control values to its own size and collision system. Its jump powers, cap range, stall, and animation timings are calibrated for its small 2D levels; this is not a complete reproduction of Odyssey's movement equations.

Dive landings in Hatrick now use ground braking and allow directional input to leave the slide immediately. The former 14-unit slide decay ignored left/right input and forced nearly 0.8 seconds of sliding. Holding Down still chains the landing into a roll. Intentional rolls retain their gentle drag while Down is held; releasing Down begins normal braking immediately.

Measured on the flat simulation lab:

| Scenario | Before | After |
| --- | --- | --- |
| Run, release input | 17 frames / 12.38 px | 10 frames / 7.03 px |
| Dive landing, release input (speed 760) | 48 frames / 77.84 px | 17 frames / 26.25 px |
| Extra speed 1040, release input | 44 frames / 86.00 px | 26 frames / 50.78 px |

The native X11 keyboard check also measured a 10-frame stop over 7.03 pixels and confirmed ground counter-steering. Focused behavior checks cover braking in both directions, slide recovery, trick braking, momentum preservation, and the existing movement features. Full level replay recordings were not retimed for this change.

## Remaining movement adaptation

The same pinned [PlayerConst.cpp](https://github.com/MonsterDruide1/OdysseyDecomp/blob/696db2ff5ed558b053c753de420504b8682f24b0/src/Player/PlayerConst.cpp) supplies the ratios and frame windows below. Fixed-point vertical acceleration uses 48 for Odyssey's normal 1.5 gravity, hence 32 per reference gravity unit. Jump powers are separately calibrated to preserve the existing platform scale and height ordering; copying the original 3D speeds directly would radically change the levels.

| Behavior | Odyssey default | Hatrick |
| --- | --- | --- |
| Triple / backflip / sideflip gravity | 1.0 | 32 throughout the arc; powers 1220 / 1060 / 1020 |
| Spin-jump gravity | 0.4 | 13 throughout the arc; power 560 |
| Cap leapfrog gravity | 1.0 | 32; air power 768, ground power 945 (32/26 ratio) |
| Ground catch jump / air catch pop gravity | 1.3 / 0.8 | 42 / 26; powers 900 / 320 |
| Roll start / maximum speed | 20 / 35, against normal 14 | 571 / 1000, against normal 400 |
| Held roll drag / boost interval | 0.998 / 15 frames | Same; rounded fixed-point multiplication |
| Ground spin duration / speed / drag | 90 / 8 / 0.95 | 90 frames / 228 / 0.95, preserving incoming bonus momentum |
| Cap pre-input / catch action / chained jumps | 10 / 10 / 10 frames | Ten-frame windows |
| Ground-pound jump recovery window | Frames 5 through 30 | Early input buffers to frame 5; directional recovery after frame 24 |

Roll boost amount (171), slope acceleration (18), ledge geometry, cap targeting, and the Up spin shortcut are 2D adaptations. Normal release braking remains 40, including exiting rolls and dive slides. Crouching on a ramp starts a downhill roll, which gains speed downhill and loses it uphill. The triangular ramp pixels are shared by hero, cap, enemy collision, and rendering. `/` and `\` in level maps encode the two slope directions using a backwards-compatible extended tile record. F1 opens a separate movement playground with both ramps and ledges.

The [ground-pound state](https://github.com/MonsterDruide1/OdysseyDecomp/blob/696db2ff5ed558b053c753de420504b8682f24b0/src/Player/PlayerStateHipDrop.cpp) permits a dive during both windup and descent. Hatrick allows both; an input directly before impact still buffers the ground-pound roll. Ground spin follows the duration and drag in [PlayerStateGroundSpin.cpp](https://github.com/MonsterDruide1/OdysseyDecomp/blob/696db2ff5ed558b053c753de420504b8682f24b0/src/Player/PlayerStateGroundSpin.cpp).

The long-jump misfire came from testing speed **after** crouch deceleration: a running character slowed below 150 in seven frames, selecting a backflip. Move selection now uses horizontal intent, ten frames of recent running intent, and a five-frame allowance for pressing Down after a running takeoff. Stationary backflips and later airborne ground pounds remain distinct. Jump + throw preserves the takeoff instead of applying a stall at floor height.

Cap action uses C for recall and X for one append throw, with a nearby forward enemy steering that extension. Ordinary throw range stays at its previously tuned short distance. A cancelled roll can also throw along the floor and reflect once. These controls translate Odyssey's motion-input actions into keyboard/gamepad actions; they are not exact reproductions of its 3D cap trajectories.

Validation uses actual-source focused simulation checks, including 132 long-jump timing combinations, collision checks across an entire ramp, ledge grab/climb/drop, resource refresh, and cap combinations. Native X11 keyboard checks cover the delayed crouch jump, jumping roll cancel, spins, cap extension, walking both slope directions, and ledge grab/climb. Campaign TAS routes were not rerun or retimed.
