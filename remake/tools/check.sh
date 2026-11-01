#!/usr/bin/env bash
# Parses every script and shader-using script in the project and prints the errors (none: silent).
#   GODOT=/path/to/godot tools/check.sh
cd "$(dirname "$0")/.."
g="${GODOT:-godot}"
for f in $(find src tools -name '*.gd'); do
	out="$("$g" --headless --path . --check-only --script "res://$f" 2>&1 | grep -E 'SCRIPT ERROR|Parse Error|Compile Error' || true)"
	[[ -n "$out" ]] && echo "== $f" && echo "$out"
done
exit 0
