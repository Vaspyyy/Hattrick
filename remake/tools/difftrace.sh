#!/usr/bin/env bash
# Replays a route through the C game (tools/ctrace.c) and through the remake's port (xtrace.gd)
# and shows where they first differ. Silent with exit 0 when every frame matches.
#   remake/tools/difftrace.sh LEVEL FILE.tas      (paths relative to where you run it)
#   SIM_RESPAWN=1 / SIM_BOSS=N are passed on to both.
# Needs gcc for the C side (built once into remake/.ctrace) and Godot (see check.sh) for the port.
set -uo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
root="$(cd "$here/.." && pwd)"
lvl="$1"; tas="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
g="${GODOT:-}"
if [[ -z "$g" ]]; then
	for c in godot4 godot "$here/.godot-bin/Godot_v4.7.2-stable_linux.x86_64"; do
		if command -v "$c" >/dev/null 2>&1; then g="$c"; break; fi
	done
fi
bin="$here/.ctrace/ctrace"
if [[ ! -x "$bin" || "$here/tools/ctrace.c" -nt "$bin" ]]; then
	mkdir -p "$here/.ctrace"
	ln -sfn "$root/assets" "$here/.ctrace/assets"
	gcc -O1 -w "$here/tools/ctrace.c" -o "$bin" -lm || exit 3
fi
tmp="$(mktemp -d)"
"$bin" "$lvl" "$tas" > "$tmp/c.txt"
timeout 900 "$g" --headless --path "$here" --script res://tools/xtrace.gd -- "$lvl" "$tas" 2>"$tmp/err.txt" | grep -E '^[0-9]+ |^(GOAL|DIED|END) ' > "$tmp/g.txt"
if cmp -s "$tmp/c.txt" "$tmp/g.txt"; then
	echo "level $lvl $(basename "$tas"): $(wc -l < "$tmp/c.txt") lines match ($(tail -n 1 "$tmp/c.txt"))"
	rm -rf "$tmp"
	exit 0
fi
echo "level $lvl $(basename "$tas"): differs (C: $(tail -n 1 "$tmp/c.txt"), port: $(tail -n 1 "$tmp/g.txt"))"
grep -E 'SCRIPT ERROR|ERROR' "$tmp/err.txt" | head -5
diff "$tmp/c.txt" "$tmp/g.txt" | head -n "${DIFFLINES:-6}" | cut -c1-600
rm -rf "$tmp"
exit 1
