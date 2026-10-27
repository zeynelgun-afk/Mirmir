extends SceneTree

const State = preload("res://scripts/cat_state.gd")
const Persistence = preload("res://scripts/persistence.gd")
var failures: Array[String] = []
var checks := 0

func check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failures.append(message)
		printerr("FAIL: ", message)

func _initialize() -> void:
	var cat := State.new(42)
	cat.satiety = 40
	check(cat.feed(), "hungry cat accepts food")
	check(cat.satiety == 62, "food changes satiety")
	check(not cat.feed(), "food spam is limited")
	cat.advance(2)
	check(cat.pet(), "pet interaction is accepted")
	var saved_bond: float = cat.bond
	check(not cat.pet(), "pet spam cannot farm bond every frame")
	check(cat.bond == saved_bond, "rejected pet preserves bond")
	cat.energy = 5
	check(not cat.play(), "exhausted cat rests instead of playing")
	cat.energy = 50
	check(cat.play(), "rested cat accepts game")
	check(cat.total_games == 1, "accepted game is counted")
	check(not cat.play(), "repeated game action has a cooldown")
	cat.energy = 10
	cat.choose_activity()
	check(cat.activity == "sleep", "low energy selects rest")
	var before: float = cat.energy
	cat.advance(2)
	check(cat.energy > before, "rest recovers energy")
	cat.bond = 61
	cat.satiety = 48
	cat.energy = 12
	cat.recover_offline(60 * 60 * 24 * 365)
	check(cat.bond == 61 and cat.satiety == 48, "offline absence never punishes bond or food")
	check(cat.energy == 100, "offline recovery is bounded")
	var snapshot := cat.to_dict(1000)
	var restored := State.new(2)
	check(restored.restore(snapshot, 900), "clock rollback accepts valid save")
	check(restored.bond == 61, "saved bond round-trips")
	var invalid := snapshot.duplicate()
	invalid["energy"] = "secretly a string"
	check(not restored.restore(invalid), "malformed numeric field is rejected")
	check(restored.bond == 61, "bad restore does not partially mutate state")
	invalid = snapshot.duplicate()
	invalid["version"] = 999
	check(not restored.restore(invalid), "unknown save version is rejected")
	invalid = snapshot.duplicate()
	invalid["energy"] = NAN
	check(not restored.restore(invalid), "NaN is rejected")
	cat.advance(INF)
	check(is_finite(cat.energy), "infinite dt cannot poison simulation")
	for i in range(50000):
		cat.advance(2)
	check(cat.energy >= 0 and cat.energy <= 100 and cat.satiety >= 0 and cat.satiety <= 100, "long simulation keeps needs bounded")
	var storage := Persistence.new()
	var path := "user://test-mirmir-state.json"
	check(storage.save_state(cat, path), "atomic save writes successfully")
	var loaded := State.new()
	check(storage.load_state(loaded, path), "atomic save loads successfully")
	check(loaded.total_games == cat.total_games, "counters survive disk round-trip")
	var file := FileAccess.open(path, FileAccess.WRITE)
	file.store_string("{broken")
	file.close()
	check(not storage.load_state(State.new(), path), "corrupted JSON starts safely")
	check(not storage.warning.is_empty(), "corruption is reported visibly")
	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))
	for filename in DirAccess.get_files_at("user://"):
		if filename.begins_with("test-mirmir-state.json"):
			DirAccess.remove_absolute(OS.get_user_data_dir().path_join(filename))
	print("MIRMIR_TESTS ", checks - failures.size(), "/", checks, " passed")
	quit(0 if failures.is_empty() else 1)
