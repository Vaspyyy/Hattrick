#!/usr/bin/env bash
# Runs the 2.5D remake. Uses Godot 4.7 from $GODOT, or godot / godot4 on the PATH when it's 4.7
# or newer; otherwise downloads the official Godot 4.7.2 build once (about 60 MB) into
# remake/.godot-bin/, checking it against the published SHA-512 sum before running it.
#   ./play.sh                 title screen
#   ./play.sh --level 0       straight into the first level
# Anything after the script name goes to the game (see the top of src/main.gd).
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ver="4.7.2"

ok_version() {   # is "$1 --version" 4.7 or newer?
	local v
	v="$("$1" --version 2>/dev/null | head -n1)" || return 1
	[[ "$v" =~ ^4\.([0-9]+) ]] && (( BASH_REMATCH[1] >= 7 ))
}

godot="${GODOT:-}"
if [[ -z "$godot" ]]; then
	for c in godot4 godot; do
		if command -v "$c" >/dev/null && ok_version "$c"; then godot="$c"; break; fi
	done
fi
if [[ -z "$godot" ]]; then
	case "$(uname -s)-$(uname -m)" in
		Linux-x86_64)
			file="Godot_v${ver}-stable_linux.x86_64.zip"; bin="Godot_v${ver}-stable_linux.x86_64"
			sum="9aa00f7a605200940bce3027a567b782f49bd8e940dd06ae9e987bd65aee1b1467edd56ed84fcdcbdd44354bf613bdbb4e5d2913e925850368e150c59ed54c65";;
		Linux-aarch64|Linux-arm64)
			file="Godot_v${ver}-stable_linux.arm64.zip"; bin="Godot_v${ver}-stable_linux.arm64"
			sum="dd59918da086bd49bde2f5450b5e567ff8650cbde9abbd7b8f4ca1197ff8c609baa38834666d032deafb47099078d7822279e2a0e06e5665745468f26533e7e2";;
		Darwin-*)
			file="Godot_v${ver}-stable_macos.universal.zip"; bin="Godot.app/Contents/MacOS/Godot"
			sum="38aa16e5bba2083941fc5b3e54be0089bd4cc35e32415f5b9fd9a8a6a7b9818255d44532ea8ef94b5aef56c4b407c2d634fa4f657e4ebe681ebbf59b7bac69ca";;
		*) echo "Install Godot $ver (https://godotengine.org/download) and run: GODOT=/path/to/godot $0" >&2; exit 1;;
	esac
	dir="$here/.godot-bin"
	godot="$dir/$bin"
	if [[ ! -x "$godot" ]]; then
		echo "Downloading Godot $ver (once, into $dir)..."
		mkdir -p "$dir"
		tmp="$(mktemp -d)"
		trap 'rm -rf "$tmp"' EXIT
		curl -fL --progress-bar -o "$tmp/$file" "https://github.com/godotengine/godot/releases/download/${ver}-stable/$file"
		got="$( (sha512sum "$tmp/$file" 2>/dev/null || shasum -a 512 "$tmp/$file") | cut -d' ' -f1)"
		if [[ "$got" != "$sum" ]]; then
			echo "The download's SHA-512 doesn't match the official sum; not running it." >&2
			exit 1
		fi
		(cd "$dir" && unzip -q -o "$tmp/$file")
		chmod +x "$godot"
	fi
fi
exec "$godot" --path "$here" -- "$@"
