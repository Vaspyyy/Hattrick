# Music and sound, from the same assets/ as the C game: each theme folder holds one OGG per
# stem, all looping together, and the director fades stems in and out from the game state the
# way hatrick.c's director() does (fast while running flat out, arp after cap bounces and air
# stomps, danger when time is short, secret near a moon coin, mallet alone in bonus rooms).
# music.txt gives the tempo and the beats where the enemies hop ("bah").
extends Node

const Sim := preload("res://src/sim/sim.gd")
const STEMS := ["lead", "bass", "perc", "bells", "fast", "arp", "bah", "danger", "secret", "mallet"]
const MUSIC := 0.75
const VOICES := 14
const EXT := ["ogg", "wav", "mp3"]   # audio.c tries ogg, wav, flac, mp3; Godot can't play FLAC
# audio.c's SOUND_FILE: the sounds the game plays, by their names under assets/
const SOUND_FILE := [
	"sfx/coin", "sfx/jump", "sfx/jump2", "sfx/jump3", "sfx/flip", "sfx/longjump", "sfx/spin", "sfx/roll", "sfx/dive",
	"sfx/cap_throw", "sfx/cap_catch", "sfx/cap_bounce", "sfx/stomp", "sfx/brick", "sfx/gp_spin", "sfx/gp_land",
	"sfx/spring", "sfx/wall_jump", "sfx/land", "sfx/skid", "sfx/ledge", "sfx/menu_move", "sfx/menu_ok",
	"sfx/menu_back", "sfx/pause", "sfx/checkpoint", "sfx/tube", "sfx/crumble", "sfx/reveal", "sfx/spit", "sfx/emerge",
	"sfx/tick", "sfx/bonus", "sfx/hurry", "sfx/moon", "music/jingle_clear", "music/jingle_death",
]

var assets := ""
var theme := ""
var players := {}         # stem -> AudioStreamPlayer
var gain := {}            # stem -> current linear gain
var bpm := 0.0
var bahs := PackedFloat32Array()
var loop_len := 0.0
var sfx := {}             # name -> AudioStream
var voices: Array = []
var voice_i := 0
var jingle: AudioStreamPlayer
var fastt := 0
var arpt := 0
var quiet := 0
var dead := false
var seen := -1
var muted := false
var volume := 1.0
var moon_home := 0        # moon coins already brought home: no hint for those


func setup(dir: String) -> void:
	assets = dir
	for i in VOICES:
		var p := AudioStreamPlayer.new()
		add_child(p)
		voices.append(p)
	jingle = AudioStreamPlayer.new()
	add_child(jingle)
	for n in SOUND_FILE:
		var st := load_sound(n)
		if st:
			sfx[n.get_file()] = st
		else:
			printerr("hatrick: no sound file for %s (assets/%s.ogg, .wav or .mp3)" % [n, n])


# assets/<name>.<ext> for the first extension that exists, like audio.c's find(); "" if none.
func find(name: String) -> String:
	for e in EXT:
		var path := assets.path_join(name + "." + e)
		if FileAccess.file_exists(path):
			return path
	return ""


# A sound or music stem by name. The file's contents say what it is, not its extension: a WAV or
# MP3 saved as .ogg still plays, and a file Godot can't play (Ogg Opus, FLAC, a broken file) is
# named in one warning instead of the engine's decoder error.
func load_sound(name: String) -> AudioStream:
	var path := find(name)
	return load_path(path) if path != "" else null


static func load_path(path: String) -> AudioStream:
	var b := FileAccess.get_file_as_bytes(path)
	var why := ""
	if b.size() < 64:
		why = "it is empty or cut short"
	elif b.slice(0, 4).get_string_from_ascii() == "OggS":
		var p: int = 27 + b[26]   # the first packet: after the page header and its segment table
		var id := b.slice(p + 1, p + 7).get_string_from_ascii() if b.size() > p + 7 else ""
		if b[p] == 1 and id == "vorbis":
			var st := AudioStreamOggVorbis.load_from_buffer(b)
			if st: return st
			why = "its Ogg Vorbis data doesn't decode"
		elif b.slice(p, p + 8).get_string_from_ascii() == "OpusHead":
			why = "it is Ogg Opus, which Godot can't play (save it as Ogg Vorbis)"
		else:
			why = "it is an Ogg file without Vorbis audio"
	elif b.slice(0, 4).get_string_from_ascii() == "RIFF" and b.slice(8, 12).get_string_from_ascii() == "WAVE":
		var st := AudioStreamWAV.load_from_buffer(b)
		if st: return st
		why = "its WAV data doesn't load"
	elif b.slice(0, 3).get_string_from_ascii() == "ID3" or (b[0] == 0xFF and (b[1] & 0xE0) == 0xE0):
		var st := AudioStreamMP3.load_from_buffer(b)
		if st: return st
		why = "its MP3 data doesn't decode"
	elif b.slice(0, 4).get_string_from_ascii() == "fLaC":
		why = "it is FLAC, which Godot can't play (save it as Ogg Vorbis)"
	else:
		why = "it isn't Ogg Vorbis, WAV or MP3"
	printerr("hatrick: can't play %s: %s" % [path, why])
	return null


# Music stems loop as a whole.
static func set_loop(st: AudioStream) -> void:
	if st is AudioStreamWAV:
		var w: AudioStreamWAV = st
		w.loop_mode = AudioStreamWAV.LOOP_FORWARD
		w.loop_begin = 0
		w.loop_end = int(w.get_length() * w.mix_rate)
	elif "loop" in st:
		st.set("loop", true)


func set_theme(name: String, restart := true) -> void:
	if name == theme and not restart:
		return
	if name != theme:
		for p in players.values():
			p.queue_free()
		players.clear()
		gain.clear()
		theme = name
		bpm = 0.0
		bahs = PackedFloat32Array()
		loop_len = 0.0
		var dir := assets.path_join("music").path_join(name)
		for s in STEMS:
			if find("music/%s/%s" % [name, s]) == "":
				continue
			var st := load_sound("music/%s/%s" % [name, s])
			if st == null:
				continue
			set_loop(st)
			loop_len = maxf(loop_len, st.get_length())
			var p := AudioStreamPlayer.new()
			p.stream = st
			p.volume_db = -80.0
			add_child(p)
			players[s] = p
			gain[s] = 0.0
		var mt := dir.path_join("music.txt")
		if FileAccess.file_exists(mt):
			for line in FileAccess.get_file_as_string(mt).split("\n"):
				var w := line.strip_edges().split(" ", false)
				if w.size() < 2 or w[0].begins_with("#"):
					continue
				if w[0] == "bpm":
					bpm = w[1].to_float()
				elif w[0] == "bah":
					for i in range(1, w.size()):
						bahs.append(w[i].to_float())
	for p in players.values():   # all stems start on the same mix step, so they stay in sync
		p.play(0.0)


func play(name: String) -> void:
	if muted:
		return
	if name == "clear" or name == "death":
		var j := "jingle_" + name
		if sfx.has(j):
			jingle.stream = sfx[j]
			jingle.volume_db = linear_to_db(volume * 0.9)
			jingle.play()
		return
	if not sfx.has(name):
		return
	var p: AudioStreamPlayer = voices[voice_i]
	voice_i = (voice_i + 1) % VOICES
	p.stream = sfx[name]
	p.volume_db = linear_to_db(volume * 0.85)
	p.play()


func stop_jingle() -> void:
	jingle.stop()


# Seconds since the last "bah" beat (enemies hop for a quarter second after it), or -1.
func since_bah() -> float:
	var p: AudioStreamPlayer = players.get("bass", players.get("perc"))
	if p == null or not p.playing or bpm <= 0.0 or bahs.is_empty():
		return -1.0
	var spb := 60.0 / bpm
	var sec := p.get_playback_position() + AudioServer.get_time_since_last_mix() - AudioServer.get_output_latency() - 0.03
	var last := -1e9
	for b in bahs:
		var t := b * spb
		if t > sec:
			t -= loop_len
		last = maxf(last, t)
	return sec - last


func hop() -> float:   # 0..1 through a hop, 0 when not hopping
	var t := since_bah()
	return t / 0.25 if t >= 0.0 and t < 0.25 else 0.0


# Once per step, after the sim: the sounds of this step and the stem levels.
func director(sim, events: Array, mode: String, dt: float) -> void:
	for e in events:
		if e[0] == "sfx":
			if e[1] == "cap_bounce" or (e[1] == "stomp" and not sim.gnd):
				arpt = 150
			play(e[1])
		elif e[0] == "skipclear":
			stop_jingle()
	var title := mode == "title"
	var menu := mode == "pause"
	var want: String = sim.L.music if sim.L else "overworld"
	if sim.st == Sim.DEAD:
		dead = true
	if want != theme:
		set_theme(want)
		seen = sim.loads
		dead = false
	elif sim.loads != seen and mode == "play":
		set_theme(theme)
		seen = sim.loads
		if dead: quiet = 80
		dead = false
	if quiet: quiet -= 1
	var st: int = sim.st
	var fast: bool = (sim.gnd and absi(sim.hvx) >= 380) or st == Sim.LONGJ or st == Sim.DIVE or st == Sim.SLIDE \
		or st == Sim.ROLL or st == Sim.SPINJ or (sim.spin and sim.jn == 2)
	if fast and not menu: fastt = 90
	elif fastt: fastt -= 1
	if arpt: arpt -= 1
	var g := {"lead": 1.0, "bass": 1.0, "perc": 1.0, "bells": 1.0, "fast": 1.0 if fastt else 0.0,
		"arp": 1.0 if arpt else 0.0, "bah": 1.0, "danger": 0.0, "secret": 0.0, "mallet": 0.0}
	var rate := 1.0 / 0.6
	if not title:
		if sim.left < 100 * 60 and st != Sim.WIN:
			g.danger = 1.0
		for i in sim.L.moons.size():
			var m: Dictionary = sim.L.moons[i]
			if m.room == sim.room and not ((sim.wd.moongot >> i) & 1) and not ((moon_home >> i) & 1):
				var dx: int = m.x * 8 + 4 - (sim.hx >> 8) - 3
				var dy: int = m.y * 8 + 4 - (sim.hy >> 8) - 6
				var d := sqrt(float(dx * dx + dy * dy))
				g.secret = maxf(g.secret, clampf((140.0 - d) / 100.0, 0.0, 1.0))
		if sim.room:
			for s in STEMS:
				if s != "danger" and s != "secret":
					g[s] = 0.0
			g.mallet = 1.0
	if title:
		g.lead = 0.0; g.perc = 0.45; g.bass = 0.8; g.fast = 0.0; g.arp = 0.0
	elif menu:
		for s in STEMS: g[s] *= 0.3
		rate = 5.0
	if not title and (st == Sim.DEAD or st == Sim.WIN or quiet):
		for s in STEMS: g[s] = 0.0
		rate = 3.3 if st == Sim.WIN else 6.6
	for s in players:
		var target: float = g[s] * MUSIC * volume * (0.0 if muted else 1.0)
		var r := rate
		if s == "fast" and g[s] > 0.0: r = 2.5
		elif s == "arp" and g[s] > 0.0: r = 6.6
		elif (s == "danger" or s == "secret" or s == "mallet") and not menu: r = 1.1
		elif (s == "fast" or s == "arp") and g[s] == 0.0 and not menu: r = 0.66
		var cur: float = gain[s]
		cur = move_toward(cur, target, r * dt)
		gain[s] = cur
		players[s].volume_db = linear_to_db(maxf(cur, 0.00001))
	for p in players.values():
		p.stream_paused = menu
