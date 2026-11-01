# Fredoka (SIL Open Font License, fonts/Fredoka-OFL.txt), loaded straight from the woff2 files so
# the project needs no import step.
extends RefCounted

static var _bold: FontFile
static var _medium: FontFile


static func _load(file: String) -> FontFile:
	var path := ProjectSettings.globalize_path("res://fonts/" + file)
	var f := FontFile.new()
	if f.load_dynamic_font(path) != OK:
		return null
	f.antialiasing = TextServer.FONT_ANTIALIASING_GRAY
	f.generate_mipmaps = true
	return f


static func bold() -> FontFile:
	if _bold == null:
		_bold = _load("fredoka-latin-700-normal.woff2")
	return _bold


static func medium() -> FontFile:
	if _medium == null:
		_medium = _load("fredoka-latin-500-normal.woff2")
	return _medium
