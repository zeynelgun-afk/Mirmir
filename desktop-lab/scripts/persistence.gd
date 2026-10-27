class_name CatPersistence
extends RefCounted

const SAVE_PATH := "user://cat-state.json"
var warning := ""

func load_state(state: CatState, path: String = SAVE_PATH) -> bool:
	warning = ""
	if not FileAccess.file_exists(path):
		return false
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() > 65536:
		warning = "Kayıt okunamadı; Mırmır güvenli başlangıçla açıldı."
		return false
	var parser := JSON.new()
	var parse_error := parser.parse(file.get_as_text())
	file.close()
	var parsed: Variant = parser.data
	if parse_error != OK or not parsed is Dictionary or not state.restore(parsed):
		warning = "Eski kayıt bozuk veya uyumsuz; yeni bir başlangıç yapıldı."
		var backup := path + ".invalid-" + str(int(Time.get_unix_time_from_system()))
		DirAccess.copy_absolute(ProjectSettings.globalize_path(path), ProjectSettings.globalize_path(backup))
		return false
	return true

func save_state(state: CatState, path: String = SAVE_PATH) -> bool:
	var temporary := path + ".tmp"
	var file := FileAccess.open(temporary, FileAccess.WRITE)
	if file == null:
		warning = "Mırmır'ın kaydı yazılamadı."
		return false
	file.store_string(JSON.stringify(state.to_dict(), "\t"))
	file.flush()
	var error := file.get_error()
	file.close()
	if error != OK:
		warning = "Kayıt diske yazılamadı."
		return false
	if DirAccess.rename_absolute(ProjectSettings.globalize_path(temporary), ProjectSettings.globalize_path(path)) != OK:
		warning = "Kayıt güvenli biçimde tamamlanamadı."
		return false
	return true
