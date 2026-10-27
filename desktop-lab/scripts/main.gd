extends Node3D

const State = preload("res://scripts/cat_state.gd")
const Persistence = preload("res://scripts/persistence.gd")
const Audio = preload("res://scripts/cat_audio.gd")
const Character = preload("res://scripts/cat_visual.gd")
const WINDOW_SIZE := Vector2i(880, 700)
const CAT_REGION := Rect2(100, 145, 680, 445)
const LABELS := {"idle": "Keyfi yerinde", "walk": "Küçük bir gezinti", "curious": "Etrafı merak ediyor", "pet": "Mırlıyor…", "eat": "Mama molası", "play": "Oyun zamanı", "sleep": "Dinleniyor"}

var state := State.new()
var storage := Persistence.new()
var audio := Audio.new()
var cat: Node3D
var camera: Camera3D
var ui := CanvasLayer.new()
var panel: PanelContainer
var status_label: Label
var toast_label: Label
var need_labels: Dictionary = {}
var mute_button: Button
var ball := MeshInstance3D.new()
var bowl := Node3D.new()
var _ball_velocity := Vector3(1.2, 1.4, 0)
var _direction := -1.0
var _window_x := 0.0
var _save_timer := 0.0
var _ipc_timer := 0.0
var _ui_timer := 0.0
var _greeting_timer := 2.0
var _toast_left := 0.0
var _last_activity := ""
var _dragging := false
var _drag_origin := Vector2i.ZERO
var _drag_window_origin := Vector2i.ZERO
var _press_position := Vector2.ZERO
var _pet_distance := 0.0
var _ipc_dir: String
var _capture_frame := 0
var _capture_path := ""
var _test_mode := false
var _idle_fps := 30
var _game_hits := 0
var _shape_timer := 0.0
var _shape_updating := false
var _quitting := false

func _ready() -> void:
	Engine.max_fps = 30
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	get_tree().auto_accept_quit = false
	storage.load_state(state)
	add_child(audio)
	_build_scene()
	_build_ui()
	_ipc_dir = OS.get_user_data_dir().path_join("commands")
	DirAccess.make_dir_recursive_absolute(_ipc_dir)
	get_window().size = WINDOW_SIZE
	_place_window()
	_update_input_region()
	_refresh_input_shape.call_deferred()
	_apply_audio()
	_parse_arguments()
	if storage.warning:
		_toast(storage.warning, 12)
	elif state.total_pets == 0:
		_toast("Üzerimi okşa · Sağ tıkla menümü aç · Shift + sürükle ile taşı", 16)
	else:
		_toast("Yeniden birlikteyiz. Hoş geldin!", 6)
	print("MIRMIR_READY ", JSON.stringify({"display": DisplayServer.get_name(), "size": get_window().size, "position": get_window().position, "save_dir": OS.get_user_data_dir()}))

func _build_scene() -> void:
	var world_environment := WorldEnvironment.new()
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0, 0, 0, 0)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.84, 0.89, 1.0)
	environment.ambient_light_energy = 0.65
	environment.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	world_environment.environment = environment
	add_child(world_environment)
	var key := DirectionalLight3D.new()
	key.rotation_degrees = Vector3(-40, -28, 0)
	key.light_color = Color(1.0, 0.91, 0.8)
	key.light_energy = 1.45
	add_child(key)
	var rim := DirectionalLight3D.new()
	rim.rotation_degrees = Vector3(-35, 145, 0)
	rim.light_color = Color(0.82, 0.9, 1.0)
	rim.light_energy = 0.55
	add_child(rim)
	camera = Camera3D.new()
	camera.position = Vector3(1.6, 0.95, 2.8)
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.size = 2.05
	add_child(camera)
	camera.look_at(Vector3(0, 0.38, 0))
	camera.current = true
	cat = Character.new()
	add_child(cat)
	_create_shadow()
	var sphere := SphereMesh.new()
	sphere.radius = 0.07
	sphere.height = 0.14
	ball.mesh = sphere
	var material := StandardMaterial3D.new()
	material.albedo_color = Color("63b8ab")
	material.roughness = 0.55
	ball.material_override = material
	ball.position = Vector3(0.7, 0.07, 0.25)
	ball.visible = false
	add_child(ball)
	add_child(bowl)
	var dish := MeshInstance3D.new()
	var cylinder := CylinderMesh.new()
	cylinder.top_radius = 0.13
	cylinder.bottom_radius = 0.10
	cylinder.height = 0.06
	dish.mesh = cylinder
	var ceramic := StandardMaterial3D.new()
	ceramic.albedo_color = Color("bd8c69")
	ceramic.roughness = 0.36
	dish.material_override = ceramic
	bowl.add_child(dish)
	bowl.position = Vector3(0.55, 0.03, 0.30)
	bowl.visible = false

func _create_shadow() -> void:
	var gradient := Gradient.new()
	gradient.set_color(0, Color(0.03, 0.025, 0.02, 0.22))
	gradient.set_color(1, Color(0.03, 0.025, 0.02, 0))
	var texture := GradientTexture2D.new()
	texture.gradient = gradient
	texture.fill = GradientTexture2D.FILL_RADIAL
	texture.fill_from = Vector2(0.5, 0.5)
	texture.fill_to = Vector2(0.5, 1.0)
	var material := StandardMaterial3D.new()
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_texture = texture
	material.no_depth_test = true
	var shadow := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(1.6, 0.75)
	shadow.mesh = plane
	shadow.material_override = material
	shadow.position.y = -0.005
	add_child(shadow)

func _build_ui() -> void:
	add_child(ui)
	var base_theme := Theme.new()
	base_theme.default_font_size = 17
	var button_style := _style(Color("284742"), 12)
	button_style.content_margin_left = 15
	button_style.content_margin_right = 15
	button_style.content_margin_top = 10
	button_style.content_margin_bottom = 10
	base_theme.set_stylebox("normal", "Button", button_style)
	base_theme.set_stylebox("hover", "Button", _style(Color("39665b"), 12))
	base_theme.set_stylebox("pressed", "Button", _style(Color("23483b"), 12))
	base_theme.set_color("font_color", "Button", Color("f4efe4"))
	base_theme.set_color("font_color", "Label", Color("f4efe4"))
	var dock := PanelContainer.new()
	dock.position = Vector2(210, 593)
	dock.size = Vector2(460, 60)
	dock.theme = base_theme
	var dock_style := _style(Color(0.055, 0.12, 0.115, 0.94), 24)
	dock_style.content_margin_top = 8
	dock_style.content_margin_bottom = 8
	dock.add_theme_stylebox_override("panel", dock_style)
	ui.add_child(dock)
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 12)
	dock.add_child(row)
	var name_label := Label.new()
	name_label.text = "Mırmır"
	name_label.add_theme_font_size_override("font_size", 21)
	row.add_child(name_label)
	status_label = Label.new()
	status_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	status_label.add_theme_color_override("font_color", Color("b9c8bc"))
	status_label.add_theme_font_size_override("font_size", 14)
	row.add_child(status_label)
	var menu := Button.new()
	menu.text = "···"
	menu.tooltip_text = "Mırmır'ın menüsü"
	menu.pressed.connect(_toggle_panel)
	row.add_child(menu)
	panel = PanelContainer.new()
	panel.theme = base_theme
	panel.position = Vector2(200, 225)
	panel.size = Vector2(480, 350)
	panel.add_theme_stylebox_override("panel", _style(Color(0.055, 0.12, 0.115, 0.98), 24))
	panel.visible = false
	ui.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 12)
	panel.add_child(column)
	var heading := Label.new()
	heading.text = "Birlikte küçük bir mola"
	heading.add_theme_font_size_override("font_size", 24)
	column.add_child(heading)
	var instructions := Label.new()
	instructions.text = "Üzerimi okşa. Taşımak için Shift ile sürükle."
	instructions.add_theme_font_size_override("font_size", 14)
	instructions.add_theme_color_override("font_color", Color("b9c8bc"))
	column.add_child(instructions)
	var needs := HBoxContainer.new()
	needs.add_theme_constant_override("separation", 28)
	column.add_child(needs)
	for key in ["satiety", "energy", "bond"]:
		var label := Label.new()
		label.add_theme_font_size_override("font_size", 15)
		needs.add_child(label)
		need_labels[key] = label
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 9)
	column.add_child(actions)
	_add_button(actions, "Mama ver", func(): _command("feed"))
	_add_button(actions, "Top oyna", func(): _command("play"))
	_add_button(actions, "Dinlen", func(): _command("rest"))
	var settings := HBoxContainer.new()
	settings.add_theme_constant_override("separation", 9)
	column.add_child(settings)
	mute_button = _add_button(settings, "", func(): _command("mute"))
	_add_button(settings, "Yanıma gel", func(): _command("home"))
	var volume_row := HBoxContainer.new()
	column.add_child(volume_row)
	var volume_label := Label.new()
	volume_label.text = "Ses"
	volume_row.add_child(volume_label)
	var slider := HSlider.new()
	slider.min_value = 0
	slider.max_value = 1
	slider.step = 0.05
	slider.value = state.volume
	slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	slider.custom_minimum_size.x = 260
	slider.value_changed.connect(func(value: float): state.volume = value; _apply_audio())
	volume_row.add_child(slider)
	var footer := HBoxContainer.new()
	footer.add_theme_constant_override("separation", 9)
	column.add_child(footer)
	_add_button(footer, "Menüyü kapat", _toggle_panel)
	_add_button(footer, "Kaydet ve çık", _quit)
	toast_label = Label.new()
	toast_label.position = Vector2(75, 660)
	toast_label.size = Vector2(730, 36)
	toast_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	toast_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	toast_label.add_theme_color_override("font_color", Color("fff8eb"))
	toast_label.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.85))
	toast_label.add_theme_constant_override("shadow_offset_x", 1)
	toast_label.add_theme_constant_override("shadow_offset_y", 2)
	toast_label.add_theme_font_size_override("font_size", 15)
	toast_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	ui.add_child(toast_label)
	_update_ui()

func _style(color: Color, radius: int) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = color
	style.set_corner_radius_all(radius)
	style.content_margin_left = 20
	style.content_margin_right = 20
	style.content_margin_top = 15
	style.content_margin_bottom = 15
	return style

func _add_button(parent: Control, title: String, action: Callable) -> Button:
	var button := Button.new()
	button.text = title
	button.pressed.connect(action)
	button.mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
	button.focus_mode = Control.FOCUS_NONE
	parent.add_child(button)
	return button

func _process(delta: float) -> void:
	state.advance(delta)
	if state.activity != _last_activity:
		cat.set_activity(state.activity)
		_last_activity = state.activity
		ball.visible = state.activity == "play"
		bowl.visible = state.activity == "eat"
		if state.activity == "play":
			_ball_velocity = Vector3(1.2, 1.0, 0)
	var speed := 0.0
	if state.activity == "walk" and not panel.visible and not _dragging:
		speed = _direction * 34
		_move_window(speed, delta)
	cat.set_motion(Vector2(speed / 40.0, 0))
	var local_mouse := get_viewport().get_mouse_position()
	cat.set_attention((local_mouse - Vector2(440, 350)) / Vector2(440, 350))
	if state.activity == "play":
		_update_ball(delta)
	else:
		cat.position.x = lerpf(cat.position.x, 0, minf(1, delta * 3))
	_greeting_timer -= delta
	if _greeting_timer <= 0 and _greeting_timer > -1:
		audio.meow()
		_greeting_timer = -2
	_toast_left = maxf(0, _toast_left - delta)
	toast_label.visible = _toast_left > 0
	_save_timer += delta
	_ipc_timer += delta
	_ui_timer += delta
	_shape_timer += delta
	if _shape_timer >= 1.25 and not _quitting:
		_shape_timer = 0
		_refresh_input_shape()
	if _ipc_timer >= 0.3:
		_ipc_timer = 0
		_poll_commands()
	if _ui_timer >= 0.5:
		_ui_timer = 0
		_update_ui()
	if _save_timer >= 10:
		_save_timer = 0
		if not _test_mode:
			storage.save_state(state)
		_write_status()
	if _capture_frame > 0:
		_capture_frame -= 1
		if _capture_frame == 0:
			_capture.call_deferred()

func _update_ball(delta: float) -> void:
	_ball_velocity.y -= 2.2 * delta
	ball.position += _ball_velocity * delta
	ball.rotate_z(-_ball_velocity.x * delta * 8)
	if ball.position.y < 0.07:
		ball.position.y = 0.07
		_ball_velocity.y = absf(_ball_velocity.y) * 0.68
		_ball_velocity.x *= 0.985
	if absf(ball.position.x) > 0.95:
		ball.position.x = clampf(ball.position.x, -0.95, 0.95)
		_ball_velocity.x *= -1
	cat.position.x = move_toward(cat.position.x, clampf(ball.position.x - 0.35, -0.45, 0.4), delta * 0.18)
	cat.set_motion(Vector2(signf(ball.position.x - cat.position.x) * 0.3, 0))
	if absf(_ball_velocity.x) < 0.2 and ball.position.y < 0.1:
		_ball_velocity = Vector3(-signf(ball.position.x + 0.001) * 0.8, 0.7, 0)

func _input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_RIGHT and event.pressed:
			_toggle_panel()
			return
		if event.button_index == MOUSE_BUTTON_LEFT:
			if event.pressed and not _over_ui(event.position) and CAT_REGION.has_point(event.position):
				_press_position = event.position
				if event.shift_pressed:
					_dragging = true
					_drag_origin = DisplayServer.mouse_get_position()
					_drag_window_origin = get_window().position
					state.set_activity("curious", 5)
				elif state.activity == "play" and event.position.distance_to(camera.unproject_position(ball.global_position)) < 65:
					_ball_velocity = Vector3(randf_range(-1.5, 1.5), randf_range(0.8, 1.4), 0)
					_game_hits += 1
					state.bond = minf(100, state.bond + 0.15)
					_toast("Güzel atış!  ·  " + str(_game_hits), 2)
				else:
					_pet()
			elif not event.pressed:
				_dragging = false
				_pet_distance = 0
	elif event is InputEventMouseMotion:
		if _dragging:
			get_window().position = _clamp_position(_drag_window_origin + DisplayServer.mouse_get_position() - _drag_origin)
			_window_x = get_window().position.x
		elif event.button_mask & MOUSE_BUTTON_MASK_LEFT and not _over_ui(event.position) and CAT_REGION.has_point(event.position):
			_pet_distance += event.relative.length()
			if _pet_distance > 22:
				_pet_distance = 0
				_pet()

func _pet() -> void:
	if state.pet():
		cat.react_to_pet()
		audio.start_purring(7)
		_toast("Biraz daha…", 2.5)

func _over_ui(point: Vector2) -> bool:
	return point.y > 585 or (panel.visible and Rect2(panel.position, panel.size).has_point(point))

func _command(command: String) -> void:
	match command:
		"feed":
			if state.feed():
				_toast("Mama hazır. Afiyet olsun Mırmır!", 4)
				audio.meow()
			else:
				_toast("Şimdilik tokum; biraz sonra yine bakarız.", 4)
		"play":
			if state.play():
				_game_hits = 0
				_toast("Topa dokun, birlikte oynayalım!", 7)
			else:
				_toast("Kısa bir mola iyi gelir.", 4)
		"rest":
			state.rest()
			_toast("Sakin bir mola…", 4)
		"pet":
			_pet()
		"mute":
			state.muted = not state.muted
			_apply_audio()
		"home":
			_place_window()
			state.set_activity("curious", 8)
		"menu":
			_toggle_panel()
		"quit":
			_quit()
	if command in ["feed", "play", "rest"]:
		panel.hide()
		_update_input_region()
	_update_ui()
	if not _test_mode:
		storage.save_state(state)
	_write_status()

func _apply_audio() -> void:
	audio.muted = state.muted
	audio.volume = state.volume

func _toggle_panel() -> void:
	panel.visible = not panel.visible
	_update_input_region()
	_refresh_input_shape.call_deferred()

func _update_ui() -> void:
	status_label.text = LABELS.get(state.activity, "Keyfi yerinde")
	need_labels["satiety"].text = "Tokluk  " + str(roundi(state.satiety))
	need_labels["energy"].text = "Enerji  " + str(roundi(state.energy))
	need_labels["bond"].text = "Bağımız  " + str(roundi(state.bond))
	mute_button.text = "Sesi aç" if state.muted else "Sessize al"

func _update_input_region() -> void:
	# Safe first-frame fallback; after drawing, alpha-derived shape replaces it.
	var points := PackedVector2Array([
		Vector2(100, 145), Vector2(780, 145), Vector2(780, 590),
		Vector2(680, 590), Vector2(680, 654), Vector2(200, 654),
		Vector2(200, 590), Vector2(100, 590),
	])
	get_window().mouse_passthrough_polygon = points

func _refresh_input_shape() -> void:
	if _shape_updating or _quitting:
		return
	_shape_updating = true
	await RenderingServer.frame_post_draw
	if not is_inside_tree():
		return
	var rendered := get_viewport().get_texture().get_image()
	rendered.resize(220, 175, Image.INTERPOLATE_NEAREST)
	var left_edge := PackedVector2Array()
	var right_edge := PackedVector2Array()
	var started := false
	var pending_rows: Array[int] = []
	# Read only our own rendered pixels. Soft shadow and help text are excluded.
	# 4px sampling plus 8px margin is an approximation, not per-hair hit testing.
	for row in range(164):
		var left := 220
		var right := -1
		for col in range(220):
			if rendered.get_pixel(col, row).a > 0.35:
				left = mini(left, col)
				right = maxi(right, col)
		if right < 0:
			if started:
				pending_rows.append(row)
			continue
		if not pending_rows.is_empty():
			# One narrow bridge joins the cat and dock into a single X11 polygon.
			for gap_row in pending_rows:
				left_edge.append(Vector2(436, gap_row * 4))
				right_edge.append(Vector2(444, gap_row * 4 + 4))
			pending_rows.clear()
		started = true
		left_edge.append(Vector2(maxi(0, left * 4 - 8), maxi(0, row * 4 - 2)))
		right_edge.append(Vector2(mini(880, (right + 1) * 4 + 8), mini(658, row * 4 + 6)))
	if left_edge.size() >= 3:
		right_edge.reverse()
		left_edge.append_array(right_edge)
		get_window().mouse_passthrough_polygon = left_edge
	_shape_updating = false

func _place_window() -> void:
	var usable := DisplayServer.screen_get_usable_rect()
	var position := Vector2i(usable.position.x + usable.size.x - WINDOW_SIZE.x - 35, usable.end.y - WINDOW_SIZE.y - 30)
	get_window().position = _clamp_position(position)
	_window_x = get_window().position.x

func _clamp_position(position: Vector2i) -> Vector2i:
	var usable := DisplayServer.screen_get_usable_rect()
	return Vector2i(clampi(position.x, usable.position.x, maxi(usable.position.x, usable.end.x - WINDOW_SIZE.x)), clampi(position.y, usable.position.y, maxi(usable.position.y, usable.end.y - WINDOW_SIZE.y)))

func _move_window(speed: float, delta: float) -> void:
	_window_x += speed * delta
	var proposed := _clamp_position(Vector2i(roundi(_window_x), get_window().position.y))
	if absf(proposed.x - _window_x) > 2:
		_direction *= -1
		_window_x = proposed.x
	get_window().position = proposed

func _toast(message: String, seconds: float) -> void:
	toast_label.text = message
	_toast_left = seconds

func _poll_commands() -> void:
	var directory := DirAccess.open(_ipc_dir)
	if directory == null:
		return
	for filename in directory.get_files():
		if not filename.ends_with(".json"):
			continue
		var path := _ipc_dir.path_join(filename)
		var file := FileAccess.open(path, FileAccess.READ)
		if file == null:
			continue
		if file.get_length() > 1024:
			file.close()
			DirAccess.remove_absolute(path)
			continue
		var data: Variant = JSON.parse_string(file.get_as_text())
		file.close()
		DirAccess.remove_absolute(path)
		if data is Dictionary and data.get("command") is String:
			_command(data["command"])

func _write_status() -> void:
	var file := FileAccess.open("user://runtime-status.json.tmp", FileAccess.WRITE)
	if file == null:
		return
	var data := state.to_dict()
	data["activity"] = state.activity
	data["fps"] = Engine.get_frames_per_second()
	data["position"] = [get_window().position.x, get_window().position.y]
	data["memory_bytes"] = OS.get_static_memory_usage()
	data["renderer"] = RenderingServer.get_video_adapter_name()
	data["implementation"] = "godot-desktop-lab"
	file.store_string(JSON.stringify(data))
	file.close()
	DirAccess.rename_absolute(OS.get_user_data_dir().path_join("runtime-status.json.tmp"), OS.get_user_data_dir().path_join("runtime-status.json"))

func _parse_arguments() -> void:
	var arguments := OS.get_cmdline_user_args()
	for i in range(arguments.size()):
		if arguments[i] == "--capture" and i + 1 < arguments.size():
			_capture_path = arguments[i + 1]
			_capture_frame = 45
		if arguments[i] == "--test-mode":
			_test_mode = true
		if arguments[i] == "--open-menu":
			_toggle_panel()

func _capture() -> void:
	await RenderingServer.frame_post_draw
	var result := get_viewport().get_texture().get_image().save_png(_capture_path)
	print("MIRMIR_CAPTURE ", result, " ", _capture_path)
	_write_status()

func _notification(what: int) -> void:
	if what == NOTIFICATION_WM_CLOSE_REQUEST:
		_quit()

func _quit() -> void:
	if _quitting:
		return
	_quitting = true
	if not _test_mode:
		storage.save_state(state)
	_write_status()
	audio.shutdown()
	# Audio playback removal is queued to the mix thread. Give it a short mix
	# interval before destroying the scene, including an immediate pet -> quit.
	await get_tree().create_timer(maxf(0.08, AudioServer.get_output_latency() * 2.0)).timeout
	# Drain a pending post-draw silhouette readback before destroying the scene.
	# Otherwise the suspended coroutine retains the scene and reports leaked objects.
	if _shape_updating:
		await RenderingServer.frame_post_draw
		await get_tree().process_frame
	get_tree().quit()
