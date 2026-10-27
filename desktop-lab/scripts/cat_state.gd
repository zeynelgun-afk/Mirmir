class_name CatState
extends RefCounted
## Render-independent needs, activity selection, interaction cooldowns and offline recovery.

const SCHEMA_VERSION := 1
const NEED_KEYS := ["satiety", "energy", "bond", "curiosity"]
const ACTIVITIES := ["idle", "walk", "curious", "pet", "eat", "play", "sleep"]
const MAX_OFFLINE_SECONDS := 8.0 * 3600.0
var satiety := 78.0
var energy := 82.0
var bond := 12.0
var curiosity := 65.0
var activity := "idle"
var activity_left := 8.0
var age_seconds := 0.0
var total_pets := 0
var total_games := 0
var muted := false
var volume := 0.45
var rng := RandomNumberGenerator.new()
var _pet_cooldown := 0.0
var _feed_cooldown := 0.0
var _play_cooldown := 0.0

func _init(seed_value: int = 0) -> void:
	if seed_value:
		rng.seed = seed_value
	else:
		rng.randomize()

func advance(delta: float) -> void:
	if not is_finite(delta) or delta <= 0:
		return
	# A suspended computer should never cause one huge simulation frame.
	var dt := minf(delta, 2.0)
	age_seconds += dt
	_pet_cooldown = maxf(0, _pet_cooldown - dt)
	_feed_cooldown = maxf(0, _feed_cooldown - dt)
	_play_cooldown = maxf(0, _play_cooldown - dt)
	satiety -= dt * 0.012
	curiosity += dt * 0.015
	if activity == "sleep":
		energy += dt * 0.16
	elif activity == "play":
		energy -= dt * 0.13
		curiosity -= dt * 0.18
	elif activity == "walk":
		energy -= dt * 0.025
	else:
		energy -= dt * 0.008
	_clamp_needs()
	activity_left -= dt
	if activity_left <= 0:
		choose_activity()

func choose_activity() -> String:
	if energy < 25:
		set_activity("sleep", rng.randf_range(40, 75))
	elif activity == "sleep" and energy < 72:
		set_activity("sleep", 35)
	else:
		var roll := rng.randf()
		if roll < 0.25:
			set_activity("walk", rng.randf_range(6, 13))
		elif roll < 0.50:
			set_activity("curious", rng.randf_range(5, 10))
		elif roll < 0.60 and energy < 85:
			set_activity("sleep", rng.randf_range(18, 35))
		else:
			set_activity("idle", rng.randf_range(8, 18))
	return activity

func set_activity(value: String, duration: float) -> void:
	if value not in ACTIVITIES:
		return
	activity = value
	activity_left = clampf(duration, 0.5, 120)

func pet() -> bool:
	if _pet_cooldown > 0:
		return false
	_pet_cooldown = 0.65
	bond += 0.18
	energy += 0.12
	total_pets += 1
	set_activity("pet", 5)
	_clamp_needs()
	return true

func feed() -> bool:
	if _feed_cooldown > 0 or satiety >= 96:
		return false
	_feed_cooldown = 30
	satiety += 22
	bond += 0.6
	set_activity("eat", 9)
	_clamp_needs()
	return true

func play() -> bool:
	if energy < 12 or _play_cooldown > 0:
		return false
	_play_cooldown = 3
	total_games += 1
	bond += 0.3
	set_activity("play", 28)
	_clamp_needs()
	return true

func rest() -> void:
	set_activity("sleep", 60)

func recover_offline(seconds: float) -> void:
	if not is_finite(seconds) or seconds <= 0:
		return
	var elapsed := minf(seconds, MAX_OFFLINE_SECONDS)
	# Absence is rest, never a neglect/death penalty. Bond and satiety are preserved.
	energy = minf(100, energy + elapsed * 0.012)
	curiosity = minf(100, curiosity + elapsed * 0.004)
	set_activity("curious", 10)
	_clamp_needs()

func to_dict(now: float = -1.0) -> Dictionary:
	return {
		"version": SCHEMA_VERSION,
		"saved_at": Time.get_unix_time_from_system() if now < 0 else now,
		"satiety": satiety, "energy": energy, "bond": bond, "curiosity": curiosity,
		"total_pets": total_pets, "total_games": total_games,
		"age_seconds": age_seconds, "muted": muted, "volume": volume,
	}

func restore(data: Dictionary, now: float = -1.0) -> bool:
	if data.get("version") != SCHEMA_VERSION:
		return false
	for key in NEED_KEYS:
		var value: Variant = data.get(key)
		if not _valid_number(value):
			return false
	for key in ["saved_at", "age_seconds", "total_pets", "total_games", "volume"]:
		if not _valid_number(data.get(key, 0)):
			return false
	for key in NEED_KEYS:
		set(key, float(data[key]))
	total_pets = clampi(int(data.get("total_pets", 0)), 0, 100000000)
	total_games = clampi(int(data.get("total_games", 0)), 0, 100000000)
	age_seconds = clampf(float(data.get("age_seconds", 0)), 0, 1e10)
	muted = data.get("muted", false) == true
	volume = clampf(float(data.get("volume", 0.45)), 0, 1)
	_clamp_needs()
	var current := Time.get_unix_time_from_system() if now < 0 else now
	recover_offline(maxf(0, current - float(data.get("saved_at", current))))
	return true

func _clamp_needs() -> void:
	satiety = clampf(satiety, 0, 100)
	energy = clampf(energy, 0, 100)
	bond = clampf(bond, 0, 100)
	curiosity = clampf(curiosity, 0, 100)

static func _valid_number(value: Variant) -> bool:
	return (value is int or value is float) and is_finite(float(value))
