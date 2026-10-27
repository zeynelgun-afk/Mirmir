"""Update only the owned eye/head-detail template after the source UV audit."""
from pathlib import Path
import hashlib
import importlib.util
import json
import unreal

ROOT=Path(__file__).resolve().parents[3]


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    spec=importlib.util.spec_from_file_location("mirmir_eyes_appearance",Path(__file__).with_name("create-appearance.py"))
    helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
    imported=ROOT/"unreal/Mirmir/Content/Mirmir/Shared/Cat/cat-walk-loop"
    protected=list(imported.rglob("*.uasset"))+[ROOT/"unreal/Mirmir/Content/Mirmir/Appearance"/name for name in ["M_CatCoat.uasset","M_CatFur.uasset","T_FineFurDetail.uasset"]]
    before={str(p.relative_to(ROOT)):digest(p) for p in protected}
    subsystem=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    texture=unreal.load_asset(helper.SOURCE+"/Textures/cat-walk-loop_texture_0")
    material=helper.make_material(subsystem,texture,True)
    after={str(p.relative_to(ROOT)):digest(p) for p in protected}
    if before!=after:raise RuntimeError("A protected original/coat/fur asset changed")
    record={"schema_version":1,"actual_editor_python_executed":True,"material":material,
            "source_uv_audit":{"Object_10_surface_area_source_units_squared":160.122,"Object_7_surface_area_source_units_squared":4060.045,"Object_10_anatomy":"Head/eye globe/rim, mouth mucosa, teeth and whisker strips; not torso/chest.","mouth_mucosa_uv_box":[.78,.975,.315,.365],"white_feature_uv_box":[.72,.89,.63,.76],"eye_globe_uv_center_radii":[.858,.087,.104,.089]},
            "iris_linear_source_statistics":{"ring_median_luma":.013632,"ring_median_red_minus_green":.004892},
            "shader_changes":{"outside_identified_features":"Same coat color and source fine/coarse albedo detail formula as base coat.","source_feature_preservation":"UV-gated mouth mucosa, bright teeth/whiskers, eye globe/sclera and near-black pupil; no global warm-color skin classification.","brown_chroma_smoothstep":[.001,.010],"pupil_luma_smoothstep":[.0015,.0055],"iris_albedo_detail":"pow(luma/.04,.42), clamp[.38,1.25]","emissive_connected":False,"OriginalCoatWeight_1":"Returns source atlas*.8 unchanged."},
            "protected_asset_hashes":before,"all_protected_assets_byte_identical":before==after,
            "runtime_visual_acceptance":False,"script":"unreal/Mirmir/scripts/create-eye-material.py"}
    (ROOT/".local/appearance/eye-material-update.json").write_text(json.dumps(record,indent=2)+"\n")
    unreal.log("MIRMIR_EYE_MATERIAL_UPDATED "+json.dumps(record))


if __name__=="__main__":main()
