"""Focused native audio-lifecycle check. Uses its own Xvfb and a muted test build.
Run: python3 tools/test_audio.py (requires gcc -m32, Xvfb, xdotool, aplay).
"""
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

REPO = Path(__file__).resolve().parent.parent


def processes():
    result = {}
    for entry in Path('/proc').iterdir():
        if not entry.name.isdigit():
            continue
        try:
            fields = (entry / 'stat').read_text().rsplit(')', 1)[1].split()
            result[int(entry.name)] = (int(fields[1]), fields[0])
        except (OSError, ValueError):
            pass
    return result


def wait_for(predicate):
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(.02)
    raise AssertionError('Timed out waiting for audio lifecycle transition')


def alive(pid):
    info = processes().get(pid)
    return info is not None and info[1] != 'Z'


with tempfile.TemporaryDirectory(prefix='hatrick-audio-test-') as tmp:
    base = Path(tmp)
    source = (REPO / 'hatrick.c').read_text()
    for name in ('gfx.h', 'levels.h'):
        source = source.replace(f'#include "{name}"', f'#include "{REPO / name}"')
    # Exercise the real audio function without playing test music on the user's speakers.
    source = source.replace('  audio(envp);', '  audio(envp); if (shm) shm[2] = 1;')
    (base / 'audio-test.c').write_text(source)
    binary = base / 'audio-test'
    subprocess.run([
        'gcc', '-m32', '-O2', '-fomit-frame-pointer', '-fno-tree-loop-distribute-patterns',
        '-nostartfiles', '-nostdlib', '-fno-pic', '-no-pie', '-fno-plt',
        '-fno-asynchronous-unwind-tables', '-fno-stack-protector', '-fcf-protection=none',
        '-Wl,-T,' + str(REPO / 'tiny.ld'), '-Wl,--build-id=none', '-Wl,-z,norelro',
        '-Wl,--no-warn-rwx-segments', str(base / 'audio-test.c'), '-o', str(binary),
        '-L/usr/lib32', '-lX11',
    ], check=True)
    readfd, writefd = os.pipe()
    xvfb = subprocess.Popen(['Xvfb', '-displayfd', str(writefd), '-screen', '0',
                             '1024x576x24', '-nolisten', 'tcp'], pass_fds=(writefd,),
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    os.close(writefd)
    with os.fdopen(readfd) as displayfd:
        display = ':' + displayfd.readline().strip()
    env = dict(os.environ, DISPLAY=display)
    game = None
    owned = set()
    try:
        for mode in ('escape', 'window_close', 'terminate', 'kill', 'synth_exit'):
            game = subprocess.Popen([str(binary)], cwd=base, env=env,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            def audio_ready():
                procs = processes()
                children = [pid for pid, (parent, state) in procs.items()
                            if parent == game.pid and state != 'Z']
                if len(children) != 1:
                    return False
                players = [pid for pid, (parent, state) in procs.items()
                           if parent == children[0] and state != 'Z']
                if len(players) != 1:
                    return False
                try:
                    if os.readlink(f'/proc/{players[0]}/exe') != '/usr/bin/aplay':
                        return False
                except OSError:
                    return False
                owned.update(children + players)
                return True
            wait_for(audio_ready)
            synth = next(pid for pid, (parent, state) in processes().items()
                         if parent == game.pid and state != 'Z')
            player = next(pid for pid, (parent, state) in processes().items()
                          if parent == synth and state != 'Z')
            wait_for(lambda: os.readlink(f'/proc/{player}/fd/0').startswith('pipe:'))
            pipe = os.readlink(f'/proc/{player}/fd/0')
            for fd in Path(f'/proc/{player}/fd').iterdir():
                try:
                    if os.readlink(fd) == pipe:
                        flags = (Path(f'/proc/{player}/fdinfo') / fd.name).read_text()
                        flags = next(line.split()[1] for line in flags.splitlines()
                                     if line.startswith('flags:'))
                        assert int(flags, 8) & 3 == 0, 'aplay retained a writer to its own input'
                except FileNotFoundError:
                    pass
            if mode in ('escape', 'window_close'):
                window = subprocess.check_output(['xdotool', 'search', '--name', '^Hatrick$'],
                                                 env=env, text=True).splitlines()[0]
                if mode == 'window_close':
                    subprocess.run(['xdotool', 'windowclose', window], env=env, check=True)
                else:
                    subprocess.run(['xdotool', 'windowfocus', window, 'keydown', 'Escape'],
                                   env=env, check=True)
                    time.sleep(.06)
                    subprocess.run(['xdotool', 'keyup', 'Escape'], env=env, check=True)
            elif mode == 'synth_exit':
                os.kill(synth, signal.SIGTERM)
            else:
                game.send_signal(signal.SIGTERM if mode == 'terminate' else signal.SIGKILL)
            wait_for(lambda: not alive(player) and not alive(synth))
            if mode == 'synth_exit':
                assert game.poll() is None, 'audio exit killed the game'
                game.terminate()
            game.wait(timeout=3)
            game = None
            print(f'PASS: {mode} leaves no live audio processes')
        # Include exits during setup: the post-prctl parent check closes this fork race.
        for delay in (0, .001, .005, .02) * 3:
            game = subprocess.Popen([str(binary)], cwd=base, env=env,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            time.sleep(delay)
            game.kill()
            game.wait(timeout=3)
            game = None
        time.sleep(.3)
        for pid, (_, state) in processes().items():
            if state == 'Z':
                continue
            try:
                assert os.readlink(f'/proc/{pid}/cwd') != str(base), f'Leaked test process {pid}'
            except (FileNotFoundError, PermissionError):
                pass
        print('PASS: 12 rapid startup/shutdown cycles leave no audio processes')
    finally:
        if game and game.poll() is None:
            game.kill()
            game.wait()
        for pid in owned:
            if alive(pid):
                os.kill(pid, signal.SIGKILL)
        xvfb.terminate()
        xvfb.wait(timeout=3)
