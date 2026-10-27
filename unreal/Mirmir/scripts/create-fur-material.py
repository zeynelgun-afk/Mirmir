"""Create an optional masked short-fur shell shader; no runtime activation."""
from pathlib import Path
import importlib.util
import json
import unreal

ROOT=Path(__file__).resolve().parents[3]
spec=importlib.util.spec_from_file_location("mirmir_appearance",Path(__file__).with_name("create-appearance.py"))
appearance=importlib.util.module_from_spec(spec);spec.loader.exec_module(appearance)
LIB=unreal.MaterialEditingLibrary

SHELL_MASK=r"""
float2 scale=float2(700,1400);
float2 cell=floor(UV*scale);
float n=frac(sin(dot(cell,float2(127.1,311.7)))*43758.5453);
float2 centered=frac(UV*scale)-.5;
float radius=lerp(.46,.12,saturate(ShellIndex));
float strand=1-smoothstep(radius*.65,radius,length(centered));
float density=step(n,saturate(FurDensity));
float3 original=Texture2DSample(OriginalCoatTexture,OriginalCoatTextureSampler,UV).rgb;
float skin=smoothstep(.018,.10,original.r-original.g)*smoothstep(.012,.07,original.r-original.b);
float2 muzzle=(frac(UV)-float2(.21,.881))/float2(.060,.028);
float bareMuzzle=1-smoothstep(.7,1.1,dot(muzzle,muzzle));
return strand*density*(1-saturate(max(skin,bareMuzzle)));
"""


def main():
    audit=json.loads((ROOT/".local/appearance/materials.json").read_text())
    texture=unreal.load_asset(audit["original_texture"])
    subsystem=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    record=appearance.make_material(subsystem,texture,False,"M_CatFur")
    material=unreal.load_asset(record["path"])
    material.set_editor_property("blend_mode",unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("opacity_mask_clip_value",.33)
    material.set_editor_property("two_sided",True)
    expressions=LIB.get_material_expressions(material)
    tex=next(x for x in expressions if isinstance(x,unreal.MaterialExpressionTextureObjectParameter)
             and str(x.get_editor_property("parameter_name"))=="OriginalCoatTexture")
    uv=next(x for x in expressions if isinstance(x,unreal.MaterialExpressionTextureCoordinate))
    length=appearance.scalar(material,"FurLengthCm",.18,-900,950)
    density=appearance.scalar(material,"FurDensity",.65,-900,1080)
    index=appearance.scalar(material,"ShellIndex",1,-900,1210)
    normal=appearance.expression(material,unreal.MaterialExpressionVertexNormalWS,-900,1340)
    opacity=appearance.custom(material,SHELL_MASK,{"UV":uv,"OriginalCoatTexture":tex,"FurDensity":density,"ShellIndex":index},-400,950,unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    offset=appearance.custom(material,"return NormalWS * max(FurLengthCm,0) * saturate(ShellIndex);",{"NormalWS":normal,"FurLengthCm":length,"ShellIndex":index},-400,1250)
    appearance.connect(opacity,unreal.MaterialProperty.MP_OPACITY_MASK)
    appearance.connect(offset,unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    errors=list(LIB.recompile_material(material))
    if errors:raise RuntimeError("Fur shader errors: "+str(errors))
    subsystem.set_metadata_tag(material,"MirmirAppearanceStatus","compiled_optional_unreviewed_shell")
    if not subsystem.save_loaded_asset(material,False):raise RuntimeError("Fur material save failed")
    record.update({"actual_editor_python_executed":True,"shader_compile_errors":errors,"scalar_parameters":[str(x) for x in LIB.get_scalar_parameter_names(material)],"activation":"opt-in only; no runtime component, Blueprint or map modified by this script","shell_contract":{"FurLengthCm":"world centimetres along VertexNormalWS; default 0.18","FurDensity":"fraction [0,1], default 0.65; not a strand count","ShellIndex":"fraction [0,1], e.g. (i+1)/6 for six body-only leader-pose shells"},"technique":"masked procedural tapered UV-cell shell layers, PBR coat color/roughness and world-space normal offset","limitations":["This is actual offset layered geometry when the native renderer instantiates shells, not strand-based Groom.","Subpixel alpha coverage may shimmer or look too smooth; actual 512px motion and performance review is required before enabling.","No per-strand self-shadowing, combing, simulation or breed-specific fur growth data.","Use no shadow/collision on shell components; imported geometry and source textures remain unchanged."],"source_model_license":audit["source_model_license"],"runtime_visual_acceptance":False,"script":"unreal/Mirmir/scripts/create-fur-material.py"})
    (ROOT/".local/appearance/fur-material.json").write_text(json.dumps(record,indent=2)+"\n")
    unreal.log("MIRMIR_FUR_MATERIAL_CREATED "+json.dumps(record))


if __name__=="__main__":main()
