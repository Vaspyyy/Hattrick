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
	var d := DirAccess.open(assets.path_join("sfx"))
	if d:
		for f in d.get_files():
			if f.ends_with(".ogg"):
				var s := AudioStreamOggVorbis.load_from_file(assets.path_join("sfx").path_join(f))
				if s:
					sfx[f.get_basename()] = s
	for j in ["jingle_clear", "jingle_death"]:
		var path := assets.path_join("music").path_join(j + ".ogg")
		if FileAccess.file_exists(path):
			sfx[j] = AudioStreamOggVorbis.load_from_file(path)


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
			var path := dir.path_join(s + ".ogg")
			if not FileAccess.file_exists(path):
				continue
			var st := AudioStreamOggVorbis.load_from_file(path)
			if st == null:
				continue
			st.loop = true
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
