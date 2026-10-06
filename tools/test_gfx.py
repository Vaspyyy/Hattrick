"""Native check of replacement art (assets/gfx/*.png): exports the templates, paints the ground
magenta and Hatrick's orange cyan, adds a wrong-sized walker.png, and runs the real game on a
private Xvfb (silent audio, no gamepad) to see the new colours on screen and the size warning.
Run after ./build.sh: python3 tools/test_gfx.py   (needs Xvfb, xdotool, Python Xlib, Pillow)
"""
import os, subprocess, tempfile, time
from pathlib import Path
from PIL import Image
from Xlib import X, display

REPO = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix='hatrick-gfx-test-') as tmp:
    base = Path(tmp)
    source = (REPO / 'hatrick.c').read_text()
    for name in ('gfx.h', 'levels.h', 'sound.h', 'audio.h'):
        source = source.replace(f'#include "{name}"', f'#include "{REPO / name}"')
    source = source.replace('#include "vendor/stb_image.h"', f'#include "{REPO / "vendor" / "stb_image.h"}"')
    source = source.replace('(active ? padkeys() : 0)', '0')   # never read (or rumble) the user's gamepad
    (base / 'game.c').write_text(source)
    subprocess.run(['gcc', '-O2', '-w', str(base / 'game.c'), str(REPO / 'audio.o'), str(REPO / 'vendor' / 'miniaudio.o'),
                    str(REPO / 'vendor' / 'stb_image.o'), '-o', str(base / 'game'), '-lX11', '-lm', '-lpthread', '-ldl'], check=True)
    assets = base / 'assets'; assets.mkdir()
    for d in ('sfx', 'music', 'licenses', 'levels.txt'): (assets / d).symlink_to(REPO / 'assets' / d)
    gfx = assets / 'gfx'
    subprocess.run(['python3', str(REPO / 'tools' / 'export_gfx.py'), str(gfx)], check=True, stdout=subprocess.DEVNULL)
    tiles = Image.open(gfx / 'tiles.png').convert('RGBA')
    for y in range(8):                       # cells 0 and 1: grassy ground and plain ground
        for x in range(16):
            if tiles.getpixel((x, y))[3]: tiles.putpixel((x, y), (255, 0, 255, 255))
    tiles.save(gfx / 'tiles.png')
    hero = Image.open(gfx / 'hatrick.png').convert('RGBA')
    for y in range(hero.height):
        for x in range(hero.width):
            p = hero.getpixel((x, y))
            if p[:3] == (255, 122, 28) and p[3]: hero.putpixel((x, y), (0, 255, 255, 255))   # the cap and shirt orange
    hero.save(gfx / 'hatrick.png')
    Image.new('RGBA', (10, 10), (255, 255, 0, 255)).save(gfx / 'walker.png')
    for f in gfx.iterdir():                  # only the three test files; the rest stays built in
        if f.stem not in ('tiles', 'hatrick', 'walker'): f.unlink()
    readfd, writefd = os.pipe()
    xvfb = subprocess.Popen(['Xvfb', '-displayfd', str(writefd), '-screen', '0', '1024x576x24', '-nolisten', 'tcp'],
                            pass_fds=(writefd,), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    os.close(writefd)
    with os.fdopen(readfd) as f: ds = ':' + f.readline().strip()
    env = dict(os.environ, DISPLAY=ds, HOME=str(base))
    game = conn = None
    try:
        with (base / 'err').open('w') as err:
            game = subprocess.Popen([str(base / 'game'), '--silent'], cwd=base, env=env, stdout=subprocess.DEVNULL, stderr=err)
            time.sleep(.6)
            window = subprocess.check_output(['xdotool', 'search', '--name', '^Hatrick$'], env=env, text=True).split()[0]
            subprocess.run(['xdotool', 'windowfocus', window], env=env, check=True)
            key = lambda a, k: subprocess.run(['xdotool', a, '--delay', '0', k], env=env, check=True)
            key('keydown', 'Return'); time.sleep(.07); key('keyup', 'Return'); time.sleep(1.2)
            conn = display.Display(ds); root = conn.screen().root
            px = root.get_image(0, 0, 1024, 576, X.ZPixmap, 0xffffffff)
            shot = Image.frombytes('RGB', (1024, 576), px.data, 'raw', 'BGRX')
            shot.save('/tmp/hatrick-gfx-test.png')
            colours = shot.getcolors(1 << 20)
            count = lambda c: sum(n for n, col in colours if col == c)
            magenta, cyan = count((255, 0, 255)), count((0, 255, 255))
            assert magenta > 20000, f'the ground comes from tiles.png ({magenta} magenta pixels)'
            assert cyan > 100, f'Hatrick comes from hatrick.png ({cyan} cyan pixels)'
            print(f'PASS: replacement art is drawn ({magenta} ground pixels from tiles.png, {cyan} from hatrick.png)')
            key('keydown', 'Escape'); time.sleep(.07); key('keyup', 'Escape'); time.sleep(.2)
            key('keydown', 'q'); time.sleep(.07); key('keyup', 'q')
            game.wait(timeout=3)
        errs = (base / 'err').read_text()
        assert 'walker.png is 10x10, it must be 16x8; using the built-in art' in errs, errs
        print('PASS: a wrong-sized image is reported and the built-in art is kept')
        print('Screenshot: /tmp/hatrick-gfx-test.png')
    finally:
        if conn: conn.close()
        if game and game.poll() is None: game.terminate(); game.wait(timeout=3)
        xvfb.terminate(); xvfb.wait(timeout=3)
