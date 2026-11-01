#!/usr/bin/env bash
# Parses every script in the project and prints the errors. Silent and exit 0 when all parse;
# exit 1 when any script fails, exit 2 when there's no Godot to run.
#   tools/check.sh                    uses $GODOT, godot4 / godot on the PATH, or play.sh's download
#   GODOT=/path/to/godot tools/check.sh
set -uo pipefail
cd "$(dirname "$0")/.."
g="${GODOT:-}"
if [[ -z "$g" ]]; then
	for c in godot4 godot .godot-bin/Godot_v4.7.2-stable_linux.x86_64 .godot-bin/Godot_v4.7.2-stable_linux.arm64 .godot-bin/Godot.app/Contents/MacOS/Godot; do
		if command -v "$c" >/dev/null 2>&1; then g="$c"; break; fi
	done
fi
if [[ -z "$g" ]] || ! "$g" --version >/dev/null 2>&1; then
	echo "check.sh: no working Godot (set GODOT=/path/to/godot, or run ./play.sh once to download it)" >&2
	exit 2
fi
failed=0
while IFS= read -r f; do
	out="$("$g" --headless --path . --check-only --script "res://$f" 2>&1)"
	status=$?
	errs="$(grep -E 'SCRIPT ERROR|Parse Error|Compile Error' <<<"$out")"
	if (( status != 0 )) || [[ -n "$errs" ]]; then
		failed=1
		echo "== $f (exit $status)"
		echo "${errs:-$(tail -n 5 <<<"$out")}"
	fi
done < <(find src tools -name '*.gd' | sort)
exit "$failed"
