"""Native playtest of the level objects: the real game on a private Xvfb, driven with xdotool
key presses that react to the game's state (read from a per-frame trace), silent audio, a
throwaway HOME and score file, and no gamepad access. Plays a small level with every object,
then a second one, enters initials for the high score and opens the table from the menu.
Run after ./build.sh: python3 tools/playtest_features.py   (needs Xvfb, xdotool, Python Xlib, Pillow)
Screenshots: /tmp/hatrick-feature-playtest/
"""
import os, struct, subprocess, tempfile, time
from pathlib import Path
from PIL import Image
from Xlib import X, display

REPO = Path(__file__).resolve().parent.parent
ART = Path('/tmp/hatrick-feature-playtest'); ART.mkdir(exist_ok=True)
FIELDS = ('menu', 'naming', 'scoreview', 'done', 'lvl', 'room', 'st', 'gnd', 'hx', 'hy', 'coins', 'score', 'deaths',
          'haveck', 'crumble', 'dweller', 'barangle', 'fr', 'wphase', 'tim', 'hidden')
TRACE = ('{ int tr[] = { menu, naming, scoreview, done, lvl, room, st, gnd, hx >> 8, hy >> 8, coins, score, deaths, haveck, '
         'wd.ncr > 1 ? (wd.cr[0].state == 1 || wd.cr[1].state == 1) : -1, LV[lvl].nhome ? wd.dw[0].phase*2 + wd.dw[0].a : -1, '
         'LV[lvl].nbar ? (LV[lvl].bar[0].a0*256 + LV[lvl].bar[0].speed*fr) >> 8 & 255 : -1, fr, wphase, tim, '
         'tile(11, 25) }; fwrite(tr, sizeof tr, 1, stdout); fflush(stdout); }\n')
# The test level: every object within a short walk. Hatrick walks on row y29 (stands at y 221).
L1 = [
    '',
    '',
    '',
    '           ?',
    '                                              *~~~',
    '',
    ' @   n          K                                         F',
    '#####MM#############22#######CC#####33#########################',
    '####################||#######  #####||#########################',
    '####################||##############||#########################',
]
LEVELS = ("= 1 playtest par=60\n" + "\n".join(L1) + "\n"
          "+ test room\nSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS\nS                              S\nS  o o o o o o o o o o o o     S\n"
          "S                              S\nS                              S\nSS22SSSSSSSSSSSSSSSSSSSSSSS33SSS\nSS||SSSSSSSSSSSSSSSSSSSSSSS||SSS\n"
          "= 2 second par=30\n\n @         F\n############\n"
          "= lab play card=playground\n @     F\n#########\n")

def main():
    with tempfile.TemporaryDirectory(prefix='hatrick-feature-test-') as tmp:
        base = Path(tmp)
        source = (REPO / 'hatrick.c').read_text()
        for name in ('gfx.h', 'levels.h', 'sound.h', 'audio.h'):
            source = source.replace(f'#include "{name}"', f'#include "{REPO / name}"')
        source = source.replace('(active ? padkeys() : 0)', '0')   # never read (or rumble) the user's gamepad
        assert '    render();\n    XPutImage' in source
        source = source.replace('    render();\n    XPutImage', TRACE + '    render();\n    XPutImage')
        (base / 'game.c').write_text(source)
        binary = base / 'game'
        subprocess.run(['gcc', '-O2', '-w', str(base / 'game.c'), str(REPO / 'audio.o'), str(REPO / 'vendor' / 'miniaudio.o'),
                        '-o', str(binary), '-lX11', '-lm', '-lpthread', '-ldl'], check=True)
        (base / 'assets').mkdir()
        for d in ('sfx', 'music', 'licenses'): (base / 'assets' / d).symlink_to(REPO / 'assets' / d)
        (base / 'assets' / 'levels.txt').write_text(LEVELS)
        home = base / 'home'; home.mkdir()
        scores = home / '.hatrick_scores'
        readfd, writefd = os.pipe()
        xvfb = subprocess.Popen(['Xvfb', '-displayfd', str(writefd), '-screen', '0', '1024x576x24', '-nolisten', 'tcp'],
                                pass_fds=(writefd,), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        os.close(writefd)
        with os.fdopen(readfd) as f: ds = ':' + f.readline().strip()
        env = dict(os.environ, DISPLAY=ds, HOME=str(home)); env.pop('HATRICK_SCORES', None)
        game = conn = None
        try:
            with (base / 'trace').open('wb') as out, (base / 'err').open('w') as err:
                game = subprocess.Popen([str(binary), '--silent'], cwd=base, env=env, stdout=out, stderr=err)
                time.sleep(.6)
                window = subprocess.check_output(['xdotool', 'search', '--name', '^Hatrick$'], env=env, text=True).split()[0]
                subprocess.run(['xdotool', 'windowfocus', window], env=env, check=True)
                conn = display.Display(ds); root = conn.screen().root
                n = len(FIELDS)
                def state():
                    data = (base / 'trace').read_bytes()
                    k = len(data) // (4*n)
                    return dict(zip(FIELDS, struct.unpack(f'<{n}i', data[(k-1)*4*n:k*4*n])))
                def key(action, *keys): subprocess.run(['xdotool', action, '--delay', '0', *keys], env=env, check=True)
                def tap(k): key('keydown', k); time.sleep(.07); key('keyup', k); time.sleep(.05)
                def hold(k): key('keydown', k)
                def release(*ks): key('keyup', *ks)
                def until(cond, limit=10, what=''):
                    t0 = time.time()
                    while time.time() - t0 < limit:
                        s = state()
                        if cond(s): return s
                        time.sleep(.01)
                    raise AssertionError(f'timed out waiting for {what}: {state()}')
                def shot(name):
                    px = root.get_image(0, 0, 1024, 576, X.ZPixmap, 0xffffffff)
                    Image.frombytes('RGB', (1024, 576), px.data, 'raw', 'BGRX').save(ART / name)
                def stand_at(c):   # nudge until Hatrick's centre is within 2 px of c
                    for _ in range(60):
                        d = c - (state()['hx'] + 3)
                        if abs(d) <= 2 and state()['gnd']: return
                        k = 'Right' if d > 0 else 'Left'
                        key('keydown', k); time.sleep(.02 if abs(d) < 8 else .1); key('keyup', k); time.sleep(.25)
                    raise AssertionError(f'could not stand at {c}: {state()}')
                def walk_to(x, k='Right'):
                    hold(k); until(lambda s: (s['hx'] >= x) if k == 'Right' else (s['hx'] <= x), 10, f'x {x}'); release(k)
                    time.sleep(.25)

                until(lambda s: s['menu'] == 1, 5, 'menu')
                tap('Return'); s = until(lambda s: s['menu'] == 0 and s['fr'] > 30, 5, 'level start')
                assert s['lvl'] == 0 and s['room'] == 0
                # the snapper: wait (out of its reach) for it to come out, then knock it out with the cap
                walk_to(14)
                until(lambda s: s['dweller'] == 5, 12, 'snapper out'); time.sleep(.1); shot('1-snapper-out.png')
                tap('x'); s = until(lambda s: s['dweller'] % 2 == 0, 3, 'snapper knocked out')
                assert s['score'] == 500, s
                print('PASS: the snapper rises out of its tube and only the cap knocks it out (+500)')
                time.sleep(.6)
                # hidden block: under it, jump
                walk_to(11*8 - 1); time.sleep(.2)
                c0 = state()['coins']; tap('z'); s = until(lambda s: s['coins'] == c0 + 1, 3, 'hidden block coin')
                assert s['hidden'] == 15 and s['score'] == 1600, s
                time.sleep(.15); shot('2-hidden-block.png'); print('PASS: a head bump from below reveals the hidden block and its coin (+1100)')
                until(lambda s: s['gnd'] == 1, 3, 'landing')
                # checkpoint
                walk_to(16*8 - 4); s = until(lambda s: s['haveck'] == 1, 3, 'checkpoint')
                time.sleep(.4); shot('3-checkpoint.png'); print('PASS: touching the checkpoint raises it and saves the level')
                # tube 2: hop on, Down, into the bonus room
                walk_to(20*8 - 8); stand_at(20*8 + 8)
                hold('Down'); s = until(lambda s: s['st'] == 12, 3, 'into the tube'); time.sleep(.15); shot('4-tube-in.png')
                s = until(lambda s: s['room'] == 1 and s['st'] == 0, 3, 'out in the room'); release('Down')
                assert s['score'] == 1600 + 2000, s
                time.sleep(.3); shot('5-bonus-room.png'); print('PASS: Down on the tube leads into the bonus room (+2000 for finding it)')
                # back out through the room's other tube
                walk_to(25*8); stand_at(27*8 + 8)
                hold('Down'); s = until(lambda s: s['room'] == 0 and s['st'] == 0, 5, 'back in the level'); release('Down')
                assert 36*8 <= s['hx'] <= 38*8 and s['hy'] == 29*8 - 11, s
                time.sleep(.3); shot('6-tube-out.png'); print('PASS: the linked tube brings Hatrick back out further along')
                # fire bar: walk into it on purpose, die, come back at the checkpoint with what it saved
                d0 = state()['deaths']
                walk_to(40*8); until(lambda s: 10 <= s['barangle'] <= 30, 8, 'fire bar pointing right')
                hold('Right'); s = until(lambda s: s['deaths'] == d0 + 1, 8, 'death by fire bar'); release('Right')
                time.sleep(.2); shot('7-fire-bar-death.png')
                s = until(lambda s: s['st'] == 0 and s['fr'] > 30, 5, 'respawn')
                assert 16*8 - 4 <= s['hx'] <= 16*8 + 4 and s['hy'] == 29*8 - 11 and s['room'] == 0 and s['haveck'] and s['score'] == 1600 and s['hidden'] == 15, s
                print('PASS: the fire bar is lethal; the death comes back at the checkpoint with its score and found block')
                # crumble: hop over tube 2, stand on the crumble until it falls
                walk_to(29*8 - 4); s = until(lambda s: s['gnd'], 2, 'standing'); assert s['hy'] == 29*8 - 11, s
                until(lambda s: s['crumble'] == 1, 3, 'crumble falling'); time.sleep(.1); shot('8-crumble-falls.png')
                until(lambda s: s['gnd'] == 1 and s['hy'] == 31*8 - 11, 3, 'landed in the dip')
                print('PASS: the crumble block shakes, falls with Hatrick and drops him into the dip')
                hold('Right'); tap('z'); until(lambda s: s['hx'] >= 34*8, 4, 'out of the dip')
                until(lambda s: s['hx'] >= 40*8, 6, 'near the fire bar'); release('Right')
                # through the fire bar with timing, to the flag
                until(lambda s: 150 <= s['barangle'] <= 180, 8, 'fire bar turned up')
                hold('Right')
                s = until(lambda s: s['st'] == 14 or s['deaths'] > d0 + 1, 10, 'flag')
                assert s['st'] == 14, s
                release('Right'); time.sleep(.9); shot('9-course-clear.png')
                until(lambda s: s['wphase'] >= 2, 6, 'tally'); time.sleep(.25); shot('10-tally.png')
                t0 = state()['tim']; tap('z')
                s = until(lambda s: s['lvl'] == 1 and s['st'] == 0, 2, 'skipped to level 2')
                print(f'PASS: the course clear (slide, pose, tally) plays, and Jump skips it ({s["tim"] - t0} frames)')
                # level 2: let the whole celebration run; the run ends with the initials
                hold('Right'); until(lambda s: s['st'] == 14, 8, 'flag 2'); release('Right')
                s = until(lambda s: s['naming'] == 1, 20, 'initials entry')
                score = s['score']; time.sleep(.3); shot('11-new-high-score.png')
                tap('Up'); tap('Up'); tap('Right'); tap('Down'); tap('z'); tap('Up'); tap('Return')
                s = until(lambda s: s['scoreview'] == 1 and s['menu'] == 1, 3, 'score table')
                time.sleep(.3); shot('12-score-table.png')
                saved = scores.read_text()
                assert saved.split() == ['CZB', str(score)], saved
                print(f'PASS: the run ends with initials, saved to ~/.hatrick_scores as "{saved.strip()}"')
                tap('Escape'); until(lambda s: s['scoreview'] == 0, 2, 'back to the cards')
                tap('Left'); tap('Right'); time.sleep(.2); tap('Return'); until(lambda s: s['scoreview'] == 1, 2, 'table from its card')
                time.sleep(.2); shot('13-score-card.png'); tap('x'); until(lambda s: s['scoreview'] == 0, 2, 'cards')
                print('PASS: the high-score card on the menu opens the table')
                tap('q'); game.wait(timeout=3)
                assert game.returncode == 0
            errs = (base / 'err').read_text()
            assert 'hatrick:' not in errs.replace('hatrick: playing without sound', ''), errs
            print('Screenshots:', ART)
        finally:
            if conn: conn.close()
            if game and game.poll() is None: game.terminate(); game.wait(timeout=3)
            xvfb.terminate(); xvfb.wait(timeout=3)

main()
