"""Native menu playtest with isolated X11 keyboard/mouse input and screenshots.
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

with tempfile.TemporaryDirectory(prefix='hatrick-menu-test-') as tmp:
    base = Path(tmp)
    source = (REPO / 'hatrick.c').read_text()
    for name in ('gfx.h', 'levels.h', 'sound.h', 'audio.h'):
        source = source.replace(f'#include "{name}"', f'#include "{REPO / name}"')
    source = source.replace('#include "vendor/stb_image.h"', f'#include "{REPO / "vendor" / "stb_image.h"}"')
    source = source.replace('(active ? padkeys() : 0)', '0')  # don't read the user's physical gamepad
    source = source.replace('    render();\n    XPutImage',
        '    { int tr[] = { menu, menusel, resumable, quitting, lvl, hx, hy, hvx, hvy, st, gnd, prevk, tim }; fwrite(tr, sizeof tr, 1, stdout); fflush(stdout); }\n    render();\n    XPutImage')
    (base / 'menu-test.c').write_text(source)
    binary = base / 'menu-test'
    subprocess.run(['gcc', '-O2', '-w', str(base / 'menu-test.c'), str(REPO / 'audio.o'),
                    str(REPO / 'vendor' / 'miniaudio.o'), str(REPO / 'vendor' / 'stb_image.o'), '-o', str(binary), '-lX11', '-lm', '-lpthread', '-ldl'],
                   check=True)
    (base / 'assets').symlink_to(REPO / 'assets')   # the audio engine runs, on its silent null device
    readfd, writefd = os.pipe()
    xvfb = subprocess.Popen(['Xvfb', '-displayfd', str(writefd), '-screen', '0',
                            '1024x576x24', '-nolisten', 'tcp'], pass_fds=(writefd,),
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    os.close(writefd)
    with os.fdopen(readfd) as displayfd:
        ds = ':' + displayfd.readline().strip()
    env = dict(os.environ, DISPLAY=ds)
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
            def records():
                data = (base / 'trace').read_bytes()
                return [struct.unpack('<13i', data[i:i+52]) for i in range(0, len(data)//52*52, 52)]
            def state():
                return records()[-1]
            def shot(name):
                pixels = root.get_image(0, 0, 1024, 576, X.ZPixmap, 0xffffffff)
                Image.frombytes('RGB', (1024,576), pixels.data, 'raw', 'BGRX').save(ART / name)
            assert state()[0] == 1 and state()[1] == 0 and state()[12] == 0
            shot('main-menu.png')
            subprocess.run(['xdotool', 'windowfocus', str(root.id)], env=env, check=True)
            tap('Right'); tap('q'); tap('Return')
            assert game.poll() is None and state()[0] == 1 and state()[1] == 0
            subprocess.run(['xdotool', 'windowfocus', window], env=env, check=True)
            print('PASS: unfocused keyboard input cannot activate or quit the menu')
            # Each destination can be reached and entered with real keyboard input.
            for i in range(6):
                if i:
                    tap('Escape'); assert state()[0] == 1
                    tap('Right')
                assert state()[1] == i
                if i == 3:
                    shot('sky-selected.png')
                tap('Return'); time.sleep(.4)
                assert state()[0] == 0 and state()[4] == (6 if i == 5 else i)
                assert state()[12] > 0
                print(f'PASS: keyboard enters destination {i+1}')
            # The active world pauses completely and resumes without losing position.
            tap('Escape'); assert state()[0] == 1 and state()[2] == 1
            before = state()
            time.sleep(.2); after = state()
            assert before[4:11] == after[4:11] and before[12] == after[12]
            shot('paused-menu.png')
            tap('x'); assert state()[0] == 0 and state()[4] == 6
            print('PASS: pause freezes the world and X resumes')
            subprocess.run(['xdotool', 'windowfocus', str(root.id)], env=env, check=True)
            time.sleep(.1); assert state()[0] == 1
            before = state(); time.sleep(.1); assert state()[12] == before[12]
            subprocess.run(['xdotool', 'windowfocus', window], env=env, check=True)
            print('PASS: switching away from the game pauses it')
            # A real mouse hover and click selects the first card.
            subprocess.run(['xdotool', 'mousemove', '--window', window, '220', '300'], env=env, check=True)
            time.sleep(.08); assert state()[1] == 0
            subprocess.run(['xdotool', 'click', '1'], env=env, check=True)
            time.sleep(.4); assert state()[0] == 0 and state()[4] == 0
            print('PASS: mouse selects and enters a destination')
            tap('Escape'); tap('q'); game.wait(timeout=3)
            assert game.returncode == 0
            print('PASS: Q quits from the menu')
            Image.open(ART/'main-menu.png').resize((768,432), Image.Resampling.NEAREST).save(ART/'main-menu-preview.png')
            print('Screenshots:', ART)
    finally:
        if connection:
            connection.close()
        if game and game.poll() is None:
            game.terminate(); game.wait(timeout=3)
        xvfb.terminate(); xvfb.wait(timeout=3)
