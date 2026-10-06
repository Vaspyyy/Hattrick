"""Native audio check: plays a scripted session on a private Xvfb with the game writing its real
mix to a WAV through the silent null device (nothing reaches the speakers), then checks the
sound/theme events and the mix itself.
Run after ./build.sh: python3 tools/test_audio.py   (needs Xvfb, xdotool, numpy)
"""
import os, re, shutil, struct, subprocess, tempfile, time, wave
from pathlib import Path
import numpy as np

REPO = Path(__file__).resolve().parent.parent

with tempfile.TemporaryDirectory(prefix='hatrick-audio-test-') as tmp:
    base = Path(tmp)
    source = (REPO / 'hatrick.c').read_text()
    for name in ('gfx.h', 'levels.h', 'sound.h', 'audio.h'):
        source = source.replace(f'#include "{name}"', f'#include "{REPO / name}"')
    source = source.replace('(active ? padkeys() : 0)', '0')   # never read (or rumble) the user's gamepad
    source = source.replace('static void die(void) { if (st < TUBE)', 'static void die(void) { if (0)')   # the scripted run must not die on the level's hazards
    assert 'if (0)' in source
    (base / 'audio-test.c').write_text(source)
    binary = base / 'audio-test'
    subprocess.run(['gcc', '-O2', '-w', str(base / 'audio-test.c'), str(REPO / 'audio.o'), str(REPO / 'vendor' / 'miniaudio.o'),
                    '-o', str(binary), '-lX11', '-lm', '-lpthread', '-ldl'], check=True)
    (base / 'assets').symlink_to(REPO / 'assets')
    readfd, writefd = os.pipe()
    xvfb = subprocess.Popen(['Xvfb', '-displayfd', str(writefd), '-screen', '0', '1024x576x24', '-nolisten', 'tcp'],
                            pass_fds=(writefd,), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    os.close(writefd)
    with os.fdopen(readfd) as displayfd:
        env = dict(os.environ, DISPLAY=':' + displayfd.readline().strip(), HATRICK_AUDIO_LOG='1')
    wav = base / 'mix.wav'
    game = None
    try:
        with (base / 'log').open('w') as log:
            game = subprocess.Popen([str(binary), '--dump', str(wav)], cwd=base, env=env, stderr=log, stdout=subprocess.DEVNULL)
            t0 = time.monotonic()
            time.sleep(.6)
            window = subprocess.check_output(['xdotool', 'search', '--name', '^Hatrick$'], env=env, text=True).split()[0]
            subprocess.run(['xdotool', 'windowfocus', window], env=env, check=True)
            key = lambda a, k: subprocess.run(['xdotool', a, '--delay', '0', k], env=env, check=True)
            def tap(k): key('keydown', k); time.sleep(.06); key('keyup', k)
            marks = {}
            time.sleep(1.4); marks['menu'] = time.monotonic() - t0       # calm title music
            tap('Return'); time.sleep(1.5)                                  # enter Hills
            key('keydown', 'Right'); time.sleep(.5)
            for _ in range(3): tap('z'); time.sleep(.5)                     # run and jump
            key('keyup', 'Right'); time.sleep(.5); marks['play'] = time.monotonic() - t0
            tap('Escape'); time.sleep(1.3); marks['pause'] = time.monotonic() - t0
            tap('x'); time.sleep(1.0); marks['resume'] = time.monotonic() - t0
            tap('Escape'); time.sleep(.2); tap('q')
            game.wait(timeout=5)
        events = [(float(t), kind, name) for t, kind, name in re.findall(r'^([\d.]+) (play|theme) (\S+)$', (base / 'log').read_text(), re.M)]
        names = [n for _, _, n in events]
        assert game.returncode == 0, 'game did not quit cleanly'
        assert names[0] == 'overworld', 'the title menu starts the overworld theme'
        for want in ('menu_ok', 'jump', 'pause', 'menu_back'):
            assert want in names, f'missing sound: {want} ({names})'
        assert names.count('overworld') >= 2, 'entering Hills restarts its theme from the top'
        print('PASS: sound and theme events', ' '.join(names))
        # the mix: 32-bit float stereo WAV written by the engine
        data = wav.read_bytes()
        assert data[:4] == b'RIFF' and struct.unpack('<H', data[20:22])[0] == 3
        x = np.frombuffer(data[44:], np.float32).reshape(-1, 2)
        assert np.isfinite(x).all() and np.abs(x).max() <= 1.0, 'mix is finite and never exceeds full scale'
        def rms(a, b):   # dB over a window of seconds
            seg = x[int(a * 48000):int(b * 48000)]
            return 20 * np.log10(np.sqrt((seg ** 2).mean()) + 1e-9)
        menu, play, pause = rms(marks['menu'] - 1.0, marks['menu']), rms(marks['play'] - 2.0, marks['play']), rms(marks['pause'] - 0.8, marks['pause'])
        resume = rms(marks['resume'] - 0.5, marks['resume'])
        print(f'PASS: mix levels  menu {menu:.1f} dB, playing {play:.1f} dB, paused {pause:.1f} dB, resumed {resume:.1f} dB')
        assert menu > -40 and play > -40, 'music is audible on the menu and in the level'
        assert pause < play - 6 and pause < resume - 6, 'pausing ducks the music'
        print(f'PASS: {len(x) / 48000:.1f} s of mix, peak {np.abs(x).max():.2f}, no clipping')

        # drop-in replacements: a WAV in place of an OGG, a missing sound, and a levels.txt that gives
        # Hills a new theme folder holding a single 44.1 kHz mono FLAC stem, without music.txt
        mod = base / 'mod'; (mod / 'assets').mkdir(parents=True)
        shutil.copy(binary, mod / 'audio-test')
        shutil.copytree(REPO / 'assets' / 'sfx', mod / 'assets' / 'sfx')
        shutil.copytree(REPO / 'assets' / 'music', mod / 'assets' / 'music')
        stock = (REPO / 'assets' / 'levels.txt').read_text()
        (mod / 'assets' / 'levels.txt').write_text(stock.replace('music=overworld', 'music=mysong', 1))
        (mod / 'assets' / 'sfx' / 'jump.ogg').unlink()
        (mod / 'assets' / 'sfx' / 'menu_ok.ogg').unlink()
        with wave.open(str(mod / 'assets' / 'sfx' / 'menu_ok.wav'), 'wb') as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(22050)
            w.writeframes((np.sin(np.arange(4410) * 2 * np.pi * 880 / 22050) * 12000).astype('<i2').tobytes())
        (mod / 'assets' / 'music' / 'mysong').mkdir()
        subprocess.run(['ffmpeg', '-loglevel', 'error', '-f', 'lavfi', '-i', 'sine=frequency=330:sample_rate=44100:duration=2.5',
                        '-ac', '1', str(mod / 'assets' / 'music' / 'mysong' / 'bass.flac')], check=True)
        wav = mod / 'mix.wav'
        with (mod / 'log').open('w') as log:
            game = subprocess.Popen([str(mod / 'audio-test'), '--dump', str(wav)], cwd=mod, env=env, stderr=log, stdout=subprocess.DEVNULL)
            t0 = time.monotonic(); time.sleep(.6)
            window = subprocess.check_output(['xdotool', 'search', '--name', '^Hatrick$'], env=env, text=True).split()[0]
            subprocess.run(['xdotool', 'windowfocus', window], env=env, check=True)
            time.sleep(3.0); menu_end = time.monotonic() - t0          # longer than the 2.5 s loop
            tap('Return'); time.sleep(1.5); tap('z'); time.sleep(.5)
            tap('Escape'); time.sleep(.2); tap('q')
            game.wait(timeout=5)
        text = (mod / 'log').read_text()
        names = re.findall(r'^[\d.]+ (?:play|theme) (\S+)$', text, re.M)
        assert game.returncode == 0, 'modded game did not quit cleanly'
        assert 'no sound file for sfx/jump' in text and 'jump' not in names, 'a missing sound is reported and stays silent'
        assert 'menu_ok' in names and 'cannot decode' not in text, 'the WAV replacement plays'
        assert names[0] == 'mysong' and 'overworld' not in names, 'levels.txt picks the theme folder'
        x = np.frombuffer(wav.read_bytes()[44:], np.float32).reshape(-1, 2)
        seg = x[int((menu_end - 1.0) * 48000):int(menu_end * 48000), 0]
        peak = np.argmax(np.abs(np.fft.rfft(seg))) * 48000 / len(seg)
        print(f'PASS: drop-in assets  events {" ".join(names)}; menu music {20 * np.log10(np.sqrt((seg ** 2).mean())):.1f} dB at {peak:.0f} Hz')
        assert abs(peak - 330) < 5, 'the replacement stem is what plays, at the right pitch after resampling'
        assert np.isfinite(x).all() and np.abs(x).max() <= 1.0
    finally:
        if game and game.poll() is None: game.terminate(); game.wait(timeout=3)
        xvfb.terminate(); xvfb.wait(timeout=3)
