"""Native overworld and pause playtest with isolated X11 keyboard/mouse input and screenshots.
Run after ./build.sh: python3 tools/test_menu_native.py
Requires Xvfb, xdotool, Python Xlib and Pillow. Output: /tmp/hatrick-menu-preview/.
"""
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time

from PIL import Image
from Xlib import X, display

REPO = Path(__file__).resolve().parent.parent
ART = Path('/tmp/hatrick-menu-preview')
ART.mkdir(exist_ok=True)
FIELDS = ('menu', 'mapat', 'resumable', 'quitting', 'lvl', 'hx', 'hy', 'hvx', 'hvy', 'st', 'gnd', 'prevk', 'tim',
          'scoreview', 'mapto', 'nodex', 'nodey', 'pausesel')
N = len(FIELDS)

with tempfile.TemporaryDirectory(prefix='hatrick-menu-test-') as tmp:
    base = Path(tmp)
    source = (REPO / 'hatrick.c').read_text()
    for name in ('gfx.h', 'levels.h', 'sound.h', 'audio.h'):
        source = source.replace(f'#include "{name}"', f'#include "{REPO / name}"')
    source = source.replace('#include "vendor/stb_image.h"', f'#include "{REPO / "vendor" / "stb_image.h"}"')
    source = source.replace('(active ? padkeys() : 0)', '0')  # don't read the user's physical gamepad
    source = source.replace('    render();\n    XPutImage',
        '    render();\n'
        '    { int tr[] = { menu, mapat, resumable, quitting, lvl, hx, hy, hvx, hvy, st, gnd, prevk, tim, scoreview, mapto,'
        ' (node[1].x - mapcam)*SC, node[1].y*SC, pausesel }; fwrite(tr, sizeof tr, 1, stdout); fflush(stdout); }\n'
        '    XPutImage')
    (base / 'menu-test.c').write_text(source)
    binary = base / 'menu-test'
    subprocess.run(['gcc', '-O2', '-w', str(base / 'menu-test.c'), str(REPO / 'audio.o'),
                    str(REPO / 'vendor' / 'miniaudio.o'), str(REPO / 'vendor' / 'stb_image.o'), '-o', str(binary),
                    '-lX11', '-lm', '-lpthread', '-ldl'], check=True)
    (base / 'assets').symlink_to(REPO / 'assets')   # the audio engine runs, on its silent null device
    readfd, writefd = os.pipe()
    xvfb = subprocess.Popen(['Xvfb', '-displayfd', str(writefd), '-screen', '0',
                            '1024x576x24', '-nolisten', 'tcp'], pass_fds=(writefd,),
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    os.close(writefd)
    with os.fdopen(readfd) as displayfd:
        ds = ':' + displayfd.readline().strip()
    env = dict(os.environ, DISPLAY=ds, HOME=str(base))   # a fresh game: nothing cleared, no scores
    game = None
    connection = None
    try:
        with (base / 'trace').open('wb') as out:
            game = subprocess.Popen([str(binary), '--silent'], cwd=base, env=env, stdout=out,
                                    stderr=subprocess.DEVNULL)
            time.sleep(.55)
            window = subprocess.check_output(['xdotool', 'search', '--name', '^Hatrick$'],
                                             env=env, text=True).splitlines()[0]
            subprocess.run(['xdotool', 'windowfocus', window], env=env, check=True)
            connection = display.Display(ds)
            root = connection.screen().root
            def key(action, *keys):
                subprocess.run(['xdotool', action, '--delay', '0', *keys], env=env, check=True)
            def tap(k):
                key('keydown', k); time.sleep(.06); key('keyup', k); time.sleep(.04)
            def state():
                data = (base / 'trace').read_bytes()
                k = len(data) // (4*N)
                return dict(zip(FIELDS, struct.unpack(f'<{N}i', data[(k-1)*4*N:k*4*N])))
            def until(cond, limit=5, what=''):
                t0 = time.time()
                while time.time() - t0 < limit:
                    if cond(state()): return state()
                    time.sleep(.01)
                raise AssertionError(f'timed out waiting for {what}: {state()}')
            def shot(name):
                pixels = root.get_image(0, 0, 1024, 576, X.ZPixmap, 0xffffffff)
                Image.frombytes('RGB', (1024,576), pixels.data, 'raw', 'BGRX').save(ART / name)
            s = until(lambda s: s['menu'] == 1, 3, 'the map')
            assert s['resumable'] == 0 and s['mapat'] == 1 and s['tim'] == 0
            time.sleep(.3); shot('map.png')
            subprocess.run(['xdotool', 'windowfocus', str(root.id)], env=env, check=True)
            tap('Left'); tap('q'); tap('Return')
            assert game.poll() is None and state()['menu'] == 1 and state()['mapat'] == 1 and state()['mapto'] < 0
            subprocess.run(['xdotool', 'windowfocus', window], env=env, check=True)
            print('PASS: unfocused keyboard input cannot walk, enter or quit')
            # Enter level 1; the pause screen freezes it completely and resumes without losing position.
            tap('Return'); s = until(lambda s: s['menu'] == 0 and s['tim'] > 10, 3, 'level 1')
            assert s['lvl'] == 0
            tap('Escape'); s = until(lambda s: s['menu'] == 1 and s['resumable'] == 1, 2, 'pause')
            before = state(); time.sleep(.2); after = state()
            assert all(before[f] == after[f] for f in ('lvl', 'hx', 'hy', 'hvx', 'hvy', 'st', 'tim'))
            shot('paused.png')
            tap('x'); until(lambda s: s['menu'] == 0, 2, 'resume')
            print('PASS: Return enters the level; Esc pauses it completely and X resumes')
            subprocess.run(['xdotool', 'windowfocus', str(root.id)], env=env, check=True)
            until(lambda s: s['menu'] == 1 and s['resumable'] == 1, 2, 'pause on focus loss')
            before = state(); time.sleep(.1); assert state()['tim'] == before['tim']
            subprocess.run(['xdotool', 'windowfocus', window], env=env, check=True)
            print('PASS: switching away from the game pauses it')
            # Exit to the map, walk home, read the scores, walk to the playground and play it.
            tap('Down'); assert state()['pausesel'] == 1
            tap('Return'); until(lambda s: s['menu'] == 1 and s['resumable'] == 0 and s['mapat'] == 1, 2, 'back on the map')
            print('PASS: "exit to map" from the pause screen')
            tap('Left'); until(lambda s: s['mapat'] == 0 and s['mapto'] < 0, 4, 'home')
            tap('Return'); until(lambda s: s['scoreview'] == 1, 2, 'the score table')
            time.sleep(.2); shot('home-scores.png')
            tap('Escape'); until(lambda s: s['scoreview'] == 0 and s['menu'] == 1, 2, 'table closed')
            assert not state()['quitting']
            tap('Down'); until(lambda s: s['mapat'] == 6 and s['mapto'] < 0, 4, 'the playground stop')
            tap('Return'); until(lambda s: s['menu'] == 0 and s['lvl'] == 6, 2, 'the playground')
            print('PASS: walking the map: home shows the scores, the playground can be played')
            tap('Escape'); tap('Down'); tap('Return'); until(lambda s: s['menu'] == 1 and s['resumable'] == 0, 2, 'map')
            # The mouse: click the level 1 stop to walk there, click it again to enter.
            s = state()
            subprocess.run(['xdotool', 'mousemove', '--window', window, str(s['nodex']), str(s['nodey'])], env=env, check=True)
            subprocess.run(['xdotool', 'click', '1'], env=env, check=True)
            s = until(lambda s: s['mapat'] == 1 and s['mapto'] < 0, 6, 'walk to level 1 by mouse')
            time.sleep(.6); s = state()   # the camera settles; the stop is where the mouse is aimed now
            subprocess.run(['xdotool', 'mousemove', '--window', window, str(s['nodex']), str(s['nodey'])], env=env, check=True)
            subprocess.run(['xdotool', 'click', '1'], env=env, check=True)
            until(lambda s: s['menu'] == 0 and s['lvl'] == 0, 2, 'entered by mouse')
            print('PASS: a mouse click walks to a stop and a second click enters it')
            tap('Escape'); tap('Down'); tap('Return'); until(lambda s: s['menu'] == 1 and s['resumable'] == 0, 2, 'map')
            tap('q'); game.wait(timeout=3)
            assert game.returncode == 0
            print('PASS: Q quits from the map')
            Image.open(ART/'map.png').resize((768,432), Image.Resampling.NEAREST).save(ART/'map-preview.png')
            print('Screenshots:', ART)
    finally:
        if connection:
            connection.close()
        if game and game.poll() is None:
            game.terminate(); game.wait(timeout=3)
        xvfb.terminate(); xvfb.wait(timeout=3)
