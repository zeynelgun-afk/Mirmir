"""Import one new AI-generated fur-detail bitmap and rebuild owned coat shaders.

No imported cat model/material/texture is rewritten. No bitmap painting or image
editing is performed: power-of-two sizing, mips and mirror addressing belong to
the new Unreal texture asset's normal build settings.
"""
from pathlib import Path
import hashlib
import importlib.util
import json
import unreal

ROOT=Path(__file__).resolve().parents[3]
DEST="/Game/Mirmir/Appearance"
NAME="T_FineFurDetail"
OWNER="mirmir-generated-fur-v1"


def module(name,filename):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(filename))
    value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    source=ROOT/"assets/generated/fur-detail-v1.png"
    provenance=json.loads((ROOT/"assets/generated/fur-detail-v1.json").read_text())
    source_sha=digest(source)
    if source_sha!=provenance["sha256"]:raise RuntimeError("Generated fur source SHA256 mismatch")
    imported=ROOT/"unreal/Mirmir/Content/Mirmir/Shared/Cat/cat-walk-loop"
    before={str(p.relative_to(ROOT)):digest(p) for p in imported.rglob("*.uasset")}
    subsystem=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    path=DEST+"/"+NAME
    if subsystem.does_asset_exist(path):
        texture=unreal.load_asset(path)
        if subsystem.get_metadata_tag(texture,"MirmirGeneratedFurOwner")!=OWNER:
            raise RuntimeError("Unowned generated texture left untouched")
        if subsystem.get_metadata_tag(texture,"MirmirGeneratedFurSourceSHA")!=source_sha:
            raise RuntimeError("Existing generated texture has a different source SHA; create a sibling revision")
    else:
        task=unreal.AssetImportTask()
        for name,value in {"filename":str(source),"destination_path":DEST,"destination_name":NAME,
                           "automated":True,"async_":False,"replace_existing":False,"save":False}.items():
            task.set_editor_property(name,value)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        textures=[value for value in task.get_objects() if isinstance(value,unreal.Texture2D)]
        if len(textures)!=1:raise RuntimeError("Generated image did not import as one texture")
        texture=textures[0]
        if texture.get_path_name().split(".")[0]!=path:
            if not subsystem.rename_asset(texture.get_path_name(),path):
                raise RuntimeError("Could not name the owned generated texture")
    if not isinstance(texture,unreal.Texture2D):raise RuntimeError("Generated texture import failed")
    texture.set_editor_property("srgb",False)
    texture.set_editor_property("compression_settings",unreal.TextureCompressionSettings.TC_DEFAULT)
    texture.set_editor_property("power_of_two_mode",unreal.TexturePowerOfTwoSetting.STRETCH_TO_POWER_OF_TWO)
    texture.set_editor_property("max_texture_size",1024)
    texture.set_editor_property("mip_gen_settings",unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE)
    texture.set_editor_property("filter",unreal.TextureFilter.TF_TRILINEAR)
    texture.set_editor_property("address_x",unreal.TextureAddress.TA_MIRROR)
    texture.set_editor_property("address_y",unreal.TextureAddress.TA_MIRROR)
    subsystem.set_metadata_tag(texture,"MirmirGeneratedFurOwner",OWNER)
    subsystem.set_metadata_tag(texture,"MirmirGeneratedFurSourceSHA",source_sha)
    if not subsystem.save_loaded_asset(texture,False):raise RuntimeError("Generated texture save failed")
    appearance=module("mirmir_fine_fur_appearance","create-appearance.py")
    body=unreal.load_asset(appearance.SOURCE+"/Textures/cat-walk-loop_texture_0")
    coat=appearance.make_material(subsystem,body,False)
    fur=module("mirmir_fine_fur_shell","create-fur-material.py")
    fur.main()
    after={str(p.relative_to(ROOT)):digest(p) for p in imported.rglob("*.uasset")}
    if before!=after or digest(source)!=source_sha:raise RuntimeError("A protected source changed unexpectedly")
    record={"schema_version":1,"actual_editor_python_executed":True,"generated_image":str(source.relative_to(ROOT)),
            "generated_image_sha256":source_sha,"texture":texture.get_path_name(),"coat":coat,
            "shell_material":"/Game/Mirmir/Appearance/M_CatFur.M_CatFur","original_eye_material_modified":False,
            "texture_build_settings":{"srgb":False,"maximum_size_px":1024,"power_of_two_mode":"StretchToPowerOfTwo","mips":"SimpleAverage","filter":"Trilinear","address_x":"Mirror","address_y":"Mirror"},
            "shader_defaults":{"FineFurTiling":3.0,"uv_aspect_multiplier":[1,2],"FineFurAlbedoStrength":.45,"FineFurNormalStrength":1.4},
            "ab_test":"Set FineFurAlbedoStrength and FineFurNormalStrength to 0 for baseline; no native code change required.",
            "preservation":{"source_bitmap_byte_identical":True,"imported_cat_assets_byte_identical":before==after,"OriginalCoatWeight_1":"Original source albedo and original normal path retained; generated modulation disabled","nose_mouth_iris":"Original atlas protection and separate iris material retained"},
            "runtime_visual_acceptance":False,"limits":["The generated source image is not an exactly periodic bitmap; mirror addressing removes abrupt wrap seams at the shader sampler.","Frequency is estimated in the source atlas UVs; the source model has nonuniform UV distortion and there is no per-fiber anatomical growth calibration.","This adds filtered surface fiber detail and shallow normal variation, not strand Groom, simulation or additional silhouette geometry.","Actual 512px movement and A/B review must judge shimmer, repeated patterns and realism before acceptance."]}
    (ROOT/".local/appearance/fine-fur.json").write_text(json.dumps(record,indent=2)+"\n")
    unreal.log("MIRMIR_FINE_FUR_CREATED "+json.dumps(record))


if __name__=="__main__":main()
