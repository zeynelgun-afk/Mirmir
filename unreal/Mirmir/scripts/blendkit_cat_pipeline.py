#!/usr/bin/env python3
"""Audit and export the locally licensed Blendkit domestic cat.

Run this script through Blender, not system Python. It never saves the source
blend. The reviewed source and every derived model remain in the ignored
Content/Animals boundary; only this reproducible adapter is public.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sys

import bpy


ASSET_NAME = "Domestic Cat, Rigged"
ASSET_BASE_ID = "6b3bbcc7-db99-4424-9436-8e6678ee9354"
ASSET_VERSION_ID = "731d9ed6-cbe6-4ab5-b9cc-105f021457ba"
EXPECTED_SOURCE_SHA256 = "5559921e22fee4af399e2ae640d7b77269c794e2a5aa14ab9ad5bf05b743d709"
LISTING_URL = (
    "https://www.blendkit.com/asset-gallery-detail/"
    "6b3bbcc7-db99-4424-9436-8e6678ee9354/"
)
EXPECTED_ARMATURE = "Armature.001"
EXPECTED_BODY = "sculpt.001"
EXPECTED_EYES = "Eye1"
EXPECTED_HAIR = {"BODY", "HEAD/PAWS", "whiskers", "FACE", "UNDERFUR"}

# The source file was authored with 30-100 interpolated children per parent
# hair, which is several million curves.  Parent-only export is too sparse and
# makes the strands look like ribbons in Unreal.  These caps keep enough of the
# authored interpolation for a continuous coat while staying appropriate for a
# desktop companion that is always running.
GROOM_GROUPS = {
    "BODY": {"id": 0, "children": 5, "width_cm": 0.018, "length_scale": 0.35},
    "HEAD_PAWS": {"id": 1, "children": 4, "width_cm": 0.014, "length_scale": 1.0},
    "FACE": {"id": 2, "children": 4, "width_cm": 0.014, "length_scale": 0.75},
    "UNDERFUR": {"id": 3, "children": 3, "width_cm": 0.012, "length_scale": 0.75},
    "WHISKERS": {"id": 4, "children": 2, "width_cm": 0.025, "length_scale": 1.0},
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def arguments() -> argparse.Namespace:
    values = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--groom-output", type=Path)
    return parser.parse_args(values)


def mesh_summary(mesh_object: bpy.types.Object) -> dict[str, object]:
    mesh = mesh_object.data
    triangles = sum(max(0, len(polygon.vertices) - 2) for polygon in mesh.polygons)
    return {
        "name": mesh_object.name,
        "vertices": len(mesh.vertices),
        "polygons": len(mesh.polygons),
        "triangles_before_modifiers": triangles,
        "vertex_groups": len(mesh_object.vertex_groups),
        "armature_modifiers": [
            modifier.object.name if modifier.object else None
            for modifier in mesh_object.modifiers
            if modifier.type == "ARMATURE"
        ],
    }


def audit(source: Path) -> dict[str, object]:
    source_digest = sha256(source)
    armature = bpy.data.objects.get(EXPECTED_ARMATURE)
    body = bpy.data.objects.get(EXPECTED_BODY)
    eyes = bpy.data.objects.get(EXPECTED_EYES)
    problems: list[str] = []
    if source_digest != EXPECTED_SOURCE_SHA256:
        problems.append("source SHA-256 differs from the reviewed free asset")
    if not armature or armature.type != "ARMATURE":
        problems.append(f"missing armature {EXPECTED_ARMATURE}")
    if not body or body.type != "MESH":
        problems.append(f"missing body mesh {EXPECTED_BODY}")
    if not eyes or eyes.type != "MESH":
        problems.append(f"missing eye mesh {EXPECTED_EYES}")

    particle_systems = []
    if body and body.type == "MESH":
        particle_systems = [
            {
                "name": system.name,
                "settings": system.settings.name,
                "type": system.settings.type,
                "count": system.settings.count,
                "hair_length_m": system.settings.hair_length,
            }
            for system in body.particle_systems
        ]
        hair_names = {item["name"] for item in particle_systems}
        if hair_names != EXPECTED_HAIR:
            problems.append(
                "hair systems differ: expected "
                f"{sorted(EXPECTED_HAIR)}, found {sorted(hair_names)}"
            )
    bone_names = []
    if armature and armature.type == "ARMATURE":
        bone_names = [bone.name for bone in armature.data.bones]
        if len(bone_names) != 32:
            problems.append(f"expected 32 bones, found {len(bone_names)}")

    meshes = [
        mesh_summary(value)
        for value in (body, eyes)
        if value is not None and value.type == "MESH"
    ]
    return {
        "schema": 1,
        "asset": ASSET_NAME,
        "asset_base_id": ASSET_BASE_ID,
        "asset_version_id": ASSET_VERSION_ID,
        "listing": LISTING_URL,
        "license": "Blendkit Royalty Free",
        "license_boundary": (
            "commercial use and higher-level derivatives allowed; raw or modified "
            "model may not be resold as a model or asset pack"
        ),
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "blender_version": bpy.app.version_string,
        "source_sha256": source_digest,
        "source_matches_reviewed_asset": source_digest == EXPECTED_SOURCE_SHA256,
        "armature": armature.name if armature else None,
        "bone_count": len(bone_names),
        "bone_names": bone_names,
        "meshes": meshes,
        "hair_particle_systems": particle_systems,
        "actions": [
            {
                "name": action.name,
                "frame_start": float(action.frame_range[0]),
                "frame_end": float(action.frame_range[1]),
            }
            for action in bpy.data.actions
        ],
        "usable_behavior_animation_library": False,
        "problems": problems,
        "structure_gate_passed": not problems,
        "visual_acceptance": False,
    }


def export_fbx(output: Path) -> None:
    armature = bpy.data.objects[EXPECTED_ARMATURE]
    body = bpy.data.objects[EXPECTED_BODY]
    eyes = bpy.data.objects[EXPECTED_EYES]
    bpy.ops.object.select_all(action="DESELECT")
    for value in (armature, body, eyes):
        value.select_set(True)
    bpy.context.view_layer.objects.active = armature

    # Legacy Blender particle hair cannot be represented by FBX. Export the
    # weighted base mesh deterministically; Unreal fur is a separate gate.
    particle_visibility = []
    for modifier in body.modifiers:
        if modifier.type == "PARTICLE_SYSTEM":
            particle_visibility.append((modifier, modifier.show_viewport, modifier.show_render))
            modifier.show_viewport = False
            modifier.show_render = False
    output.parent.mkdir(parents=True, exist_ok=True)
    try:
        result = bpy.ops.export_scene.fbx(
            filepath=str(output),
            check_existing=False,
            use_selection=True,
            object_types={"ARMATURE", "MESH"},
            use_mesh_modifiers=False,
            add_leaf_bones=False,
            bake_anim=False,
            apply_unit_scale=True,
            apply_scale_options="FBX_SCALE_ALL",
            axis_forward="-Y",
            axis_up="Z",
            path_mode="COPY",
            embed_textures=True,
        )
    finally:
        for modifier, viewport, render in particle_visibility:
            modifier.show_viewport = viewport
            modifier.show_render = render
    if "FINISHED" not in result or not output.is_file():
        raise RuntimeError(f"FBX export failed: {result}")


def curve_length(curve_object: bpy.types.Object) -> float:
    """Return a sampled mean polyline length for one imported Curves object."""
    curves = curve_object.data.curves
    if not curves:
        return 0.0
    stride = max(1, len(curves) // 1000)
    sample = list(curves)[::stride]
    total = 0.0
    for curve in sample:
        start = curve.first_point_index
        end = start + curve.points_length
        total += sum(
            (
                curve_object.data.points[index].position
                - curve_object.data.points[index - 1].position
            ).length
            for index in range(start + 1, end)
        )
    return total / len(sample)


def classify_curve_groups(curve_objects: list[bpy.types.Object]) -> dict[str, bpy.types.Object]:
    """Recover semantic groups after Blender reopens its raw Alembic.

    Legacy particle-system names are not retained by Blender's Alembic exporter,
    but this audited source has unambiguous strand counts and mean lengths:
    whiskers are the tiny group, body coat is the longest dense group, head/paws
    is the shortest, and face is shorter than underfur.
    """
    metrics = [(value, len(value.data.curves), curve_length(value)) for value in curve_objects]
    whiskers = min(metrics, key=lambda item: item[1])
    dense = [item for item in metrics if item is not whiskers]
    body = max(dense, key=lambda item: item[2])
    remaining = [item for item in dense if item is not body]
    head_paws = min(remaining, key=lambda item: item[2])
    remaining = [item for item in remaining if item is not head_paws]
    face, underfur = sorted(remaining, key=lambda item: item[2])
    return {
        "BODY": body[0],
        "HEAD_PAWS": head_paws[0],
        "FACE": face[0],
        "UNDERFUR": underfur[0],
        "WHISKERS": whiskers[0],
    }


def tag_curve_group(curve_object: bpy.types.Object, name: str) -> None:
    """Write Unreal's standard per-strand group attributes and authored width."""
    config = GROOM_GROUPS[name]
    length_scale = config["length_scale"]
    if length_scale != 1.0:
        for curve in curve_object.data.curves:
            start = curve.first_point_index
            root = curve_object.data.points[start].position.copy()
            for index in range(start + 1, start + curve.points_length):
                point = curve_object.data.points[index]
                point.position = root + (point.position - root) * length_scale
    group_id = curve_object.data.attributes.get("groom_group_id")
    if group_id is None:
        group_id = curve_object.data.attributes.new("groom_group_id", "INT", "CURVE")
    for value in group_id.data:
        value.value = config["id"]

    group_name = curve_object.data.attributes.get("groom_group_name")
    if group_name is None:
        group_name = curve_object.data.attributes.new("groom_group_name", "STRING", "CURVE")
    for value in group_name.data:
        value.value = name.encode("utf-8")

    radius = curve_object.data.attributes.get("radius")
    if radius is None:
        radius = curve_object.data.attributes.new("radius", "FLOAT", "POINT")
    for value in radius.data:
        # The second-pass Alembic still carries metre-sized numeric units;
        # Unreal's Groom translator applies the metres-to-centimetres import
        # conversion.  Store the requested centimetre width in those units.
        value.value = config["width_cm"] / 100.0


def export_groom(output: Path) -> dict[str, object]:
    """Export a performance-capped version of the authored particle coat.

    Blender's full render settings would create millions of curves; exporting
    only parents creates visible gaps.  A small, explicit child count retains
    the source grooming while keeping this always-on desktop app practical.
    Blender's Alembic exporter writes legacy particle hair as ICurves directly;
    converting each modifier to a Curve first is lossy and very slow.
    """
    body = bpy.data.objects[EXPECTED_BODY]
    source_systems = [
        {
            "name": system.name,
            "settings": system.settings.name,
            "parent_hair_count": system.settings.count,
            "hair_length_m": system.settings.hair_length,
            "hair_step": system.settings.hair_step,
            "original_child_type": system.settings.child_type,
            "original_rendered_child_count": system.settings.rendered_child_count,
            "export_rendered_child_count": GROOM_GROUPS[
                "WHISKERS" if system.name == "whiskers" else system.name.replace("/", "_")
            ]["children"],
        }
        for system in body.particle_systems
    ]
    for system in body.particle_systems:
        group_name = "WHISKERS" if system.name == "whiskers" else system.name.replace("/", "_")
        system.settings.child_type = "INTERPOLATED"
        system.settings.rendered_child_count = GROOM_GROUPS[group_name]["children"]
    bpy.context.view_layer.update()
    bpy.ops.object.select_all(action="DESELECT")
    body.select_set(True)
    bpy.context.view_layer.objects.active = body
    output.parent.mkdir(parents=True, exist_ok=True)
    raw_output = output.with_name(f".{output.stem}.raw.abc")
    result = bpy.ops.wm.alembic_export(
        filepath=str(raw_output),
        check_existing=False,
        start=1,
        end=1,
        selected=True,
        visible_objects_only=False,
        flatten=False,
        curves_as_mesh=False,
        export_hair=True,
        export_particles=True,
        export_custom_properties=True,
        as_background_job=False,
        # Blender stores this cat in metres; Unreal Groom coordinates are cm.
        # Baking the conversion here also keeps roots aligned with the FBX
        # skeletal mesh, which FBX imports in centimetres.
        global_scale=100.0,
    )
    if "FINISHED" not in result or not raw_output.is_file():
        raise RuntimeError(f"Raw Alembic groom export failed: {result}")

    # Blender must select the emitter mesh to evaluate legacy particle hair,
    # which puts both PolyMesh and ICurves schemas into the first Alembic. The
    # Unreal factory then sees the mesh first and routes the file to its generic
    # static-mesh importer. Re-import locally and export only Blender's CURVES
    # objects so the final file is unambiguously a Groom source.
    try:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        result = bpy.ops.wm.alembic_import(
            filepath=str(raw_output),
            set_frame_range=False,
            validate_meshes=False,
            always_add_cache_reader=False,
            as_background_job=False,
        )
        if "FINISHED" not in result:
            raise RuntimeError(f"Could not reopen raw Alembic groom: {result}")
        curve_objects = [value for value in bpy.context.scene.objects if value.type == "CURVES"]
        if len(curve_objects) != len(source_systems):
            raise RuntimeError(
                f"Expected {len(source_systems)} Alembic curve groups, found {len(curve_objects)}"
            )
        semantic_groups = classify_curve_groups(curve_objects)
        if set(semantic_groups) != set(GROOM_GROUPS):
            raise RuntimeError(f"Could not classify Alembic groom groups: {semantic_groups}")
        for name, value in semantic_groups.items():
            tag_curve_group(value, name)

        bpy.ops.object.select_all(action="DESELECT")
        for value in curve_objects:
            value.select_set(True)
        bpy.context.view_layer.objects.active = curve_objects[0]
        result = bpy.ops.wm.alembic_export(
            filepath=str(output),
            check_existing=False,
            start=1,
            end=1,
            selected=True,
            visible_objects_only=False,
            flatten=False,
            curves_as_mesh=False,
            export_hair=True,
            export_particles=False,
            export_custom_properties=True,
            as_background_job=False,
            global_scale=1.0,
        )
        if "FINISHED" not in result or not output.is_file():
            raise RuntimeError(f"Curves-only Alembic groom export failed: {result}")
        curve_summary = [
            {
                "name": name,
                "alembic_object": value.name,
                "curve_count": len(value.data.curves),
                "point_count": len(value.data.points),
                "mean_curve_length_cm": curve_length(value) * 100.0,
                "width_cm": GROOM_GROUPS[name]["width_cm"],
                "length_scale": GROOM_GROUPS[name]["length_scale"],
            }
            for name, value in semantic_groups.items()
        ]
    finally:
        raw_output.unlink(missing_ok=True)
    return {
        "source_systems": source_systems,
        "children_omitted": False,
        "export_mode": "Blender Alembic capped particle children, grouped curves-only second pass",
        "curve_groups": curve_summary,
    }


def main() -> None:
    args = arguments()
    source = Path(bpy.data.filepath).resolve()
    if not source.is_file():
        raise RuntimeError("Open the reviewed Blendkit .blend before running this script")
    report = audit(source)
    if not report["structure_gate_passed"]:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        raise RuntimeError("Blendkit cat structure gate failed: " + "; ".join(report["problems"]))
    if args.output:
        output = args.output.resolve()
        export_fbx(output)
        report["fbx"] = str(output)
        report["fbx_sha256"] = sha256(output)
        report["fbx_bytes"] = output.stat().st_size
        provenance = {
            "schema": 1,
            "asset_base_id": ASSET_BASE_ID,
            "asset_version_id": ASSET_VERSION_ID,
            "source_blend_sha256": report["source_sha256"],
            "fbx_sha256": report["fbx_sha256"],
            "fbx_bytes": report["fbx_bytes"],
            "generated_utc": report["generated_utc"],
        }
        output.with_suffix(".provenance.json").write_text(
            json.dumps(provenance, indent=2) + "\n", encoding="utf-8"
        )
    if args.groom_output:
        groom_output = args.groom_output.resolve()
        report["groom_guides"] = export_groom(groom_output)
        report["groom_alembic"] = str(groom_output)
        report["groom_alembic_sha256"] = sha256(groom_output)
        report["groom_alembic_bytes"] = groom_output.stat().st_size
        groom_provenance = {
            "schema": 1,
            "asset_base_id": ASSET_BASE_ID,
            "asset_version_id": ASSET_VERSION_ID,
            "source_blend_sha256": report["source_sha256"],
            "groom_alembic_sha256": report["groom_alembic_sha256"],
            "groom_alembic_bytes": report["groom_alembic_bytes"],
            "children_omitted": False,
            "source_systems": report["groom_guides"]["source_systems"],
            "generated_utc": report["generated_utc"],
        }
        groom_output.with_suffix(".provenance.json").write_text(
            json.dumps(groom_provenance, indent=2) + "\n", encoding="utf-8"
        )
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(
        "MIRMIR_FREE_CAT_PIPELINE "
        + json.dumps(
            {
                "structure_gate_passed": True,
                "bone_count": report["bone_count"],
                "hair_system_count": len(report["hair_particle_systems"]),
                "fbx": report.get("fbx"),
                "groom_alembic": report.get("groom_alembic"),
                "report": str(args.report.resolve()),
            }
        )
    )


if __name__ == "__main__":
    main()
