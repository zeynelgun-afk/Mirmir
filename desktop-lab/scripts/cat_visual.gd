class_name CatVisual
extends Node3D
## Real imported feline mesh with conservative procedural animation.
## Fripouille has no authored clips or separate ear/eyelid bones. All motion here
## is a rest-pose-preserving laboratory approximation, not production animation.

const MODEL_PATH := "res://assets/cat/fripouille.glb"
const MODEL_LENGTH := 1.45
const TAIL_NAMES := ["Bone.005_014", "Bone.006_015", "Bone.007_016", "Bone.008_017", "Bone.009_018", "Bone.010_019"]
const LEG_CHAINS := [
	["Bone.015_06", "Bone.016_07", "Bone.017_08", "Bone.018_09"],
	["Bone.019_010", "Bone.020_011", "Bone.021_012", "Bone.022_013"],
	["Bone.011_020", "Bone.012_021", "Bone.013_022", "Bone.014_023"],
	["Bone.023_024", "Bone.024_00", "Bone.025_025", "Bone.026_026"],
]

var _heading: Node3D
var _body: Node3D
var _model: Node3D
var _skeleton: Skeleton3D
var _meshes: Array[MeshInstance3D] = []
var _rest_rotations: Array[Quaternion] = []
var _rest_scales: Array[Vector3] = []
var _bone_ids: Dictionary = {}
var _activity := "idle"
var _velocity := Vector2.ZERO
var _attention := Vector2.ZERO
var _attention_smoothed := Vector2.ZERO
var _time := 0.0
var _gait_phase := 0.0
var _walk_weight := 0.0
var _pet_strength := 0.0
var _facing_angle := 0.0
var _neutral_bounds := AABB(Vector3(-0.75, 0.0, -0.2), Vector3(1.5, 0.8, 0.4))


func _ready() -> void:
	_heading = Node3D.new()
	_heading.name = "Facing"
	add_child(_heading)
	_body = Node3D.new()
	_body.name = "BreathAndWeight"
	_heading.add_child(_body)
	# Runtime GLTF import also works in a fresh checkout before .godot import data
	# exists. No third-party scripts or generated executables are loaded.
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	var error := document.append_from_file(MODEL_PATH, state)
	if error != OK:
		push_error("Mırmır: lisanslı kedi modeli yüklenemedi: %s (hata %s)" % [MODEL_PATH, error])
		return
	_model = document.generate_scene(state) as Node3D
	if _model == null:
		push_error("Mırmır: model bir 3B sahne içermiyor.")
		return
	_body.add_child(_model)
	_collect_nodes(_model)
	if _skeleton != null:
		_cache_rest_pose()
		# Import has a 25.66 degree armature yaw. Align anatomical forward +X.
		var relative := global_transform.affine_inverse() * _skeleton.global_transform
		var forward := relative.basis.x.normalized()
		_model.rotate_y(atan2(forward.z, forward.x))
		# The asset's skinned bind geometry is inverted relative to the armature
		# axes. Verified by rendering: half-turn makes paws down and nose +X.
		_model.rotate_z(PI)
	_normalize_geometry()
	_tune_materials()


func set_activity(activity: String) -> void:
	_activity = activity.to_lower()


func set_motion(velocity: Vector2) -> void:
	_velocity = velocity
	if absf(velocity.x) > 0.01:
		_facing_angle = 0.0 if velocity.x >= 0.0 else PI


func set_attention(target: Vector2) -> void:
	# Relative/normalized screen target; callers can use (mouse-cat)/200.
	_attention = target.limit_length(1.0)


func react_to_pet() -> void:
	_pet_strength = minf(_pet_strength + 0.7, 1.0)


func get_render_bounds() -> AABB:
	# Conservative local visual bounds, including room for procedural tail motion.
	return _heading.transform * _neutral_bounds.grow(0.045) if _heading != null else _neutral_bounds


func _process(delta: float) -> void:
	if _skeleton == null:
		return
	_time += delta
	_pet_strength = move_toward(_pet_strength, 0.0, delta * 0.22)
	_attention_smoothed = _attention_smoothed.lerp(_attention, 1.0 - exp(-delta * 3.5))
	_heading.rotation.y = lerp_angle(_heading.rotation.y, _facing_angle, 1.0 - exp(-delta * 6.0))
	var walking := _velocity.length() > 0.01 and _activity not in ["sleep", "sleeping", "rest", "eating", "eat"]
	_walk_weight = move_toward(_walk_weight, 1.0 if walking else 0.0, delta * 3.0)
	_gait_phase += delta * (5.2 + minf(_velocity.length(), 1.0) * 1.6)
	var sleepy := _activity in ["sleep", "sleeping", "rest"]
	var playful := _activity in ["play", "playing", "chase"]
	var breath := sin(_time * (1.45 if sleepy else 2.2))
	_body.position.y = (breath + 1.0) * 0.0018 + absf(sin(_gait_phase)) * 0.003 * _walk_weight
	# Always rebuild from imported rest pose; never accumulate rotations.
	for bone in _skeleton.get_bone_count():
		_skeleton.set_bone_pose_rotation(bone, _rest_rotations[bone])
		_skeleton.set_bone_pose_scale(bone, _rest_scales[bone])
	var chest := _id("Bone.002_03")
	if chest >= 0:
		_skeleton.set_bone_pose_scale(chest, _rest_scales[chest] * Vector3(1.0 + breath * 0.0035, 1.0, 1.0 + breath * 0.0025))
	var facing_sign := 1.0 if cos(_heading.rotation.y) >= 0.0 else -1.0
	var head_yaw := _attention_smoothed.x * 0.12 * facing_sign
	var head_pitch := _attention_smoothed.y * 0.08
	if sleepy:
		head_pitch += 0.13
	if _activity in ["eat", "eating"]:
		head_pitch += 0.12 + sin(_time * 3.0) * 0.025
	head_pitch -= _pet_strength * 0.07
	_rotate_bone("Bone.003_04", Vector3(0, 0, 1), head_yaw * 0.45)
	_rotate_bone("Bone.004_05", Vector3(0, 0, 1), head_yaw * 0.55)
	_rotate_bone("Bone.004_05", Vector3(0, 1, 0), head_pitch)
	_rotate_bone("Bone.004_05", Vector3(1, 0, 0), sin(_time * 1.1) * 0.012 + _pet_strength * sin(_time * 2.0) * 0.035)
	for segment in TAIL_NAMES.size():
		var tail_speed := 2.3 if playful else 1.1
		var amplitude := 0.055 if playful else 0.025
		if sleepy:
			amplitude *= 0.35
		_rotate_bone(TAIL_NAMES[segment], Vector3(0, 0, 1), sin(_time * tail_speed - segment * 0.48) * amplitude)
		_rotate_bone(TAIL_NAMES[segment], Vector3(0, 1, 0), _pet_strength * 0.035 * sin(_time * 1.3 - segment * 0.22))
	# Small diagonal gait. A future authored gait + IK must replace this layer;
	# modest amplitudes avoid the severe limb stretching common in fake demos.
	var phases := [0.0, PI, PI, 0.0]
	for leg in LEG_CHAINS.size():
		var phase: float = _gait_phase + phases[leg]
		var swing := sin(phase) * _walk_weight
		var lift := maxf(0.0, cos(phase)) * _walk_weight
		_rotate_bone(LEG_CHAINS[leg][0], Vector3(0, 1, 0), swing * 0.085)
		_rotate_bone(LEG_CHAINS[leg][1], Vector3(0, 1, 0), -swing * 0.045 + lift * 0.055)
		_rotate_bone(LEG_CHAINS[leg][2], Vector3(0, 1, 0), -lift * 0.06)
		_rotate_bone(LEG_CHAINS[leg][3], Vector3(0, 1, 0), -swing * 0.04 + lift * 0.005)


func _collect_nodes(node: Node) -> void:
	if node is Skeleton3D:
		_skeleton = node as Skeleton3D
	if node is MeshInstance3D:
		_meshes.append(node as MeshInstance3D)
	for child in node.get_children():
		_collect_nodes(child)


func _cache_rest_pose() -> void:
	for bone in _skeleton.get_bone_count():
		_bone_ids[_skeleton.get_bone_name(bone)] = bone
		_rest_rotations.append(_skeleton.get_bone_pose_rotation(bone))
		_rest_scales.append(_skeleton.get_bone_pose_scale(bone))


func _id(bone_name: String) -> int:
	return int(_bone_ids.get(bone_name, -1))


func _rotate_bone(bone_name: String, skeleton_axis: Vector3, angle: float) -> void:
	var bone := _id(bone_name)
	if bone < 0:
		return
	var local_axis := (_skeleton.get_bone_global_rest(bone).basis.inverse() * skeleton_axis).normalized()
	var current := _skeleton.get_bone_pose_rotation(bone)
	_skeleton.set_bone_pose_rotation(bone, (current * Quaternion(local_axis, angle)).normalized())


func _normalize_geometry() -> void:
	# Raw mesh AABBs in this asset include FBX pre-bind coordinates (hundreds of
	# units). Compute the actual rest-skinned bounds once, not those raw bounds.
	var bounds := _rest_skinned_bounds()
	if bounds.size.x < 0.001:
		push_error("Mırmır: kedi geometrisinin sınırları bulunamadı.")
		return
	var factor := MODEL_LENGTH / bounds.size.x
	_model.scale *= factor
	bounds = _rest_skinned_bounds()
	_model.position -= Vector3(bounds.get_center().x, bounds.position.y, bounds.get_center().z)
	_neutral_bounds = _rest_skinned_bounds()
	# Skinning can move vertices outside the unposed mesh's unusual import AABB.
	# Conservative custom AABBs prevent frustum culling while the model animates.
	for mesh in _meshes:
		mesh.extra_cull_margin = 2000.0
	print("CAT_VISUAL_READY length=%.3f height=%.3f bones=%s authored_clips=0 procedural=true" % [_neutral_bounds.size.x, _neutral_bounds.size.y, _skeleton.get_bone_count() if _skeleton else 0])


func _rest_skinned_bounds() -> AABB:
	var low := Vector3(INF, INF, INF)
	var high := Vector3(-INF, -INF, -INF)
	var inv := global_transform.affine_inverse()
	for instance in _meshes:
		var mesh := instance.mesh
		if mesh == null:
			continue
		var skeleton := instance.get_node_or_null(instance.skeleton) as Skeleton3D
		var skin := instance.skin
		var bind_transforms: Array[Transform3D] = []
		if skeleton != null and skin != null:
			for binding in skin.get_bind_count():
				var bone := skin.get_bind_bone(binding)
				if bone < 0:
					bone = skeleton.find_bone(skin.get_bind_name(binding))
				bind_transforms.append(skeleton.get_bone_global_rest(bone) * skin.get_bind_pose(binding) if bone >= 0 else Transform3D.IDENTITY)
		for surface in mesh.get_surface_count():
			var arrays := mesh.surface_get_arrays(surface)
			var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
			var bones: PackedInt32Array = arrays[Mesh.ARRAY_BONES] if arrays[Mesh.ARRAY_BONES] != null else PackedInt32Array()
			var weights: PackedFloat32Array = arrays[Mesh.ARRAY_WEIGHTS] if arrays[Mesh.ARRAY_WEIGHTS] != null else PackedFloat32Array()
			var influences := bones.size() / maxi(vertices.size(), 1)
			for vertex in vertices.size():
				var point := vertices[vertex]
				var output := Vector3.ZERO
				var total := 0.0
				if not bind_transforms.is_empty() and influences > 0:
					for influence in influences:
						var slot := vertex * influences + influence
						var weight: float = weights[slot]
						if weight > 0.0 and bones[slot] < bind_transforms.size():
							output += (bind_transforms[bones[slot]] * point) * weight
							total += weight
				if total > 0.0:
					output = inv * skeleton.global_transform * (output / total)
				else:
					output = inv * instance.global_transform * point
				low = low.min(output)
				high = high.max(output)
	return AABB(low, high - low) if is_finite(low.x) else AABB()


func _tune_materials() -> void:
	for instance in _meshes:
		for surface in instance.mesh.get_surface_count():
			var imported := instance.get_active_material(surface)
			if imported is StandardMaterial3D:
				var material := imported.duplicate() as StandardMaterial3D
				# Preserve the artist's texture maps; avoid metallic-looking fur.
				material.metallic = 0.0
				if "eye" not in material.resource_name.to_lower():
					material.roughness = maxf(material.roughness, 0.65)
				instance.set_surface_override_material(surface, material)
