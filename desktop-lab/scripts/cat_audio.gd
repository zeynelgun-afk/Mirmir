class_name CatAudio
extends Node
## Real recorded CC0 cat sounds; see ASSET-LICENSES.md.

var voice := AudioStreamPlayer.new()
var purr := AudioStreamPlayer.new()
var muted := false
var volume := 0.45
var _meows: Array[AudioStream] = []
var _purr_time := 0.0
var _voice_cooldown := 0.0
var _shutting_down := false

func _ready() -> void:
	add_child(voice)
	add_child(purr)
	for path in ["res://assets/audio/meow-1.ogg", "res://assets/audio/meow-2.ogg"]:
		if ResourceLoader.exists(path):
			_meows.append(load(path))
	if ResourceLoader.exists("res://assets/audio/purr.ogg"):
		purr.stream = load("res://assets/audio/purr.ogg")
		purr.finished.connect(_continue_purr)

func _process(delta: float) -> void:
	_purr_time = maxf(0, _purr_time - delta)
	_voice_cooldown = maxf(0, _voice_cooldown - delta)
	voice.volume_db = linear_to_db(volume * 0.7) if not muted and volume > 0 else -80.0
	purr.volume_db = linear_to_db(volume * 0.55) if not muted and volume > 0 else -80.0
	if _purr_time <= 0 or muted:
		purr.stop()

func meow() -> void:
	if _shutting_down or muted or _meows.is_empty() or _voice_cooldown > 0:
		return
	_voice_cooldown = 15
	voice.stream = _meows.pick_random()
	voice.pitch_scale = randf_range(0.97, 1.03)
	voice.play()

func start_purring(seconds: float = 8.0) -> void:
	if _shutting_down:
		return
	_purr_time = seconds
	if not muted and not purr.playing and purr.stream != null:
		purr.play()

func _continue_purr() -> void:
	if not _shutting_down and _purr_time > 0 and not muted:
		purr.play()

func shutdown() -> void:
	if _shutting_down:
		return
	_shutting_down = true
	set_process(false)
	_purr_time = 0
	# Stop the loop before releasing the streams. Immediate pet -> quit used to
	# retain a Vorbis playback and packet sequence on the audio mix thread.
	if purr.finished.is_connected(_continue_purr):
		purr.finished.disconnect(_continue_purr)
	voice.stop()
	purr.stop()
	voice.stream = null
	purr.stream = null
	_meows.clear()
