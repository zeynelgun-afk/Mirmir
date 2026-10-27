"""Create owned coat/iris materials; preserve all imported cat assets.

Run with UnrealEditor-Cmd -run=pythonscript -script=<this file>. These materials
are templates for runtime MIDs, not a Groom or breed-specific replacement mesh.
"""
from pathlib import Path
import hashlib
import json
import unreal

ROOT=Path(__file__).resolve().parents[3]
DEST="/Game/Mirmir/Appearance"
SOURCE="/Game/Mirmir/Shared/Cat/cat-walk-loop"
OWNER="mirmir-appearance-v1"
LIB=unreal.MaterialEditingLibrary

COAT_CODE=r"""
float2 uv=frac(UV);
float3 original=Texture2DSample(OriginalCoatTexture,OriginalCoatTextureSampler,UV).rgb;
float3 coarse=Texture2DSampleLevel(OriginalCoatTexture,OriginalCoatTextureSampler,UV,3).rgb;
float lum=dot(original,float3(.2126,.7152,.0722));
float low=dot(coarse,float3(.2126,.7152,.0722));
float fine=clamp((lum+.018)/(low+.018),.78,1.18);
float detail=lerp(1.0,fine,saturate(FurDetailStrength));
float3 recolored=CoatColor*(.91+.09*pow(saturate(low),.22))*detail;
// Keep pink nose/ear/pad skin and dark mouth/eye lining in the source atlas.
float skin=smoothstep(.018,.10,original.r-original.g)*smoothstep(.012,.07,original.r-original.b);
float2 muzzle=(uv-float2(.210,.889))/float2(.077,.022);
float mouth=(1-smoothstep(.75,1.12,dot(muzzle,muzzle)))*(1-smoothstep(.025,.075,lum));
float2 e1=(uv-float2(.156,.819))/float2(.027,.014);
float2 e2=(uv-float2(.271,.819))/float2(.027,.014);
float eyeLine=(1-smoothstep(.7,1.2,min(dot(e1,e1),dot(e2,e2))))*(1-smoothstep(.02,.06,lum));
float protectedMask=saturate(max(skin,max(mouth,eyeLine)));
float3 result=lerp(recolored,original*.8,protectedMask);
return lerp(result,original*.8,saturate(OriginalCoatWeight));
"""

COAT_FINE_CODE=COAT_CODE.replace("// Keep pink nose/ear/pad skin",r"""
// Generated fine fibers modulate only the recolored coat. Implicit gradients
// select filtered mips; mirror addressing removes hard repeat-edge jumps.
float2 fineUV=UV*float2(FineFurTiling,FineFurTiling*2);
float furHeight=dot(Texture2DSample(FineFurTexture,FineFurTextureSampler,fineUV).rgb,float3(.2126,.7152,.0722));
float fiberDetail=clamp((furHeight-.804)/.804,-.25,.20);
recolored*=1+fiberDetail*saturate(FineFurAlbedoStrength);
// Keep pink nose/ear/pad skin""")

EYE_CODE=r"""
float2 uv=frac(UV);
float3 original=Texture2DSample(OriginalCoatTexture,OriginalCoatTextureSampler,UV).rgb;
float3 coarse=Texture2DSampleLevel(OriginalCoatTexture,OriginalCoatTextureSampler,UV,3).rgb;
float lum=dot(original,float3(.2126,.7152,.0722));
float low=dot(coarse,float3(.2126,.7152,.0722));
float fine=clamp((lum+.018)/(low+.018),.78,1.18);
float3 coat=CoatColor*(.91+.09*pow(saturate(low),.22))*lerp(1.0,fine,saturate(FurDetailStrength));
// UV islands audited from Object_10: mouth mucosa (not warm dark fur),
// pale teeth/whisker strips, and the eye globe/sclera. No global red-skin gate.
float mouthIsland=step(.78,uv.x)*step(uv.x,.975)*step(.315,uv.y)*step(uv.y,.365);
float whiteFeatureIsland=step(.72,uv.x)*step(uv.x,.89)*step(.63,uv.y)*step(uv.y,.76);
float whiteFeature=whiteFeatureIsland*smoothstep(.20,.55,lum);
float2 globe=(uv-float2(.858,.087))/float2(.104,.089);
float eyeSurface=1-smoothstep(.94,1.12,dot(globe,globe));
float sourceFeature=saturate(max(eyeSurface,max(mouthIsland,whiteFeature)));
float3 base=lerp(coat,original*.8,sourceFeature);
float2 eye=(uv-float2(.858,.087))/float2(.058,.059);
float irisArea=1-smoothstep(.72,1.05,dot(eye,eye));
// Source iris-ring medians are linear luma .0136 and R-G .0049. The
// previous .005-.035 chroma gate excluded most real iris texels.
float brown=smoothstep(.001,.010,original.r-original.g);
float pupil=1-smoothstep(.0015,.0055,lum);
float mask=irisArea*brown*(1-pupil);
float3 tint=lerp(EyeRightColor,EyeLeftColor,step(0,PreSkinPosition.x));
float textureDetail=clamp(pow(max(lum,.001)/.04,.42),.38,1.25);
float3 recolored=lerp(base,tint*textureDetail,mask);
return lerp(recolored,original*.8,saturate(OriginalCoatWeight));
"""

NORMAL_CODE=r"""
float3 weights=float3(.2126,.7152,.0722);
float h=dot(Texture2DSample(OriginalCoatTexture,OriginalCoatTextureSampler,UV).rgb,weights);
float hx=dot(Texture2DSample(OriginalCoatTexture,OriginalCoatTextureSampler,UV+float2(1.0/512,0)).rgb,weights);
float hy=dot(Texture2DSample(OriginalCoatTexture,OriginalCoatTextureSampler,UV+float2(0,1.0/1024)).rgb,weights);
// Very shallow tangent perturbation, from the existing fur photo detail.
return normalize(float3((h-hx)*.16*FurDetailStrength,(h-hy)*.16*FurDetailStrength,1));
"""

NORMAL_FINE_CODE=r"""
float3 weights=float3(.2126,.7152,.0722);
float3 original=Texture2DSample(OriginalCoatTexture,OriginalCoatTextureSampler,UV).rgb;
float h=dot(original,weights);
float hx=dot(Texture2DSample(OriginalCoatTexture,OriginalCoatTextureSampler,UV+float2(1.0/512,0)).rgb,weights);
float hy=dot(Texture2DSample(OriginalCoatTexture,OriginalCoatTextureSampler,UV+float2(0,1.0/1024)).rgb,weights);
float2 fineUV=UV*float2(FineFurTiling,FineFurTiling*2);
float f=dot(Texture2DSample(FineFurTexture,FineFurTextureSampler,fineUV).rgb,weights);
float fx=dot(Texture2DSample(FineFurTexture,FineFurTextureSampler,fineUV+float2(1.0/1024,0)).rgb,weights);
float fy=dot(Texture2DSample(FineFurTexture,FineFurTextureSampler,fineUV+float2(0,1.0/1024)).rgb,weights);
float skin=smoothstep(.018,.10,original.r-original.g)*smoothstep(.012,.07,original.r-original.b);
float2 muzzle=(frac(UV)-float2(.210,.889))/float2(.077,.022);
float mouth=(1-smoothstep(.75,1.12,dot(muzzle,muzzle)))*(1-smoothstep(.025,.075,h));
float protectedMask=saturate(max(skin,mouth));
float strength=max(FineFurNormalStrength,0)*(1-saturate(OriginalCoatWeight))*(1-protectedMask);
float2 furSlope=float2(f-fx,f-fy)*strength;
return normalize(float3(float2(h-hx,h-hy)*.16*FurDetailStrength+furSlope,1));
"""

EYE_ROUGHNESS_CODE=r"""
float2 eye=(frac(UV)-float2(.858,.087))/float2(.082,.082);
float area=1-smoothstep(.9,1.15,dot(eye,eye));
return lerp(clamp(FurRoughness,.5,1),.17,area);
"""


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def expression(mat,cls,x,y,**props):
    node=LIB.create_material_expression(mat,cls,x,y)
    for name,value in props.items():node.set_editor_property(name,value)
    return node


def scalar(mat,name,value,x,y):
    return expression(mat,unreal.MaterialExpressionScalarParameter,x,y,parameter_name=name,default_value=value)


def vector(mat,name,value,x,y):
    return expression(mat,unreal.MaterialExpressionVectorParameter,x,y,parameter_name=name,default_value=unreal.LinearColor(*value,1))


def custom(mat,code,inputs,x,y,output=unreal.CustomMaterialOutputType.CMOT_FLOAT3):
    pins=[]
    for name in inputs:
        pin=unreal.CustomInput();pin.set_editor_property("input_name",name);pins.append(pin)
    node=expression(mat,unreal.MaterialExpressionCustom,x,y,code=code,output_type=output,
                    inputs=pins)
    for name,source in inputs.items():
        if not LIB.connect_material_expressions(source,"",node,name):raise RuntimeError("Material connection failed: "+name)
    return node


def connect(node,prop):
    if not LIB.connect_material_property(node,"",prop):raise RuntimeError("Could not connect material property "+str(prop))


def owned_material(subsystem,name):
    path=DEST+"/"+name
    if subsystem.does_asset_exist(path):
        mat=unreal.load_asset(path)
        if not isinstance(mat,unreal.Material) or subsystem.get_metadata_tag(mat,"MirmirAppearanceOwner")!=OWNER:
            raise RuntimeError("Unowned appearance asset left untouched: "+path)
        LIB.delete_all_material_expressions(mat)
    else:
        mat=unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,DEST,unreal.Material,unreal.MaterialFactoryNew())
    subsystem.set_metadata_tag(mat,"MirmirAppearanceOwner",OWNER)
    mat.set_editor_property("blend_mode",unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property("two_sided",False)
    LIB.set_base_material_usage(mat,unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
    return mat


def make_material(subsystem,texture,is_eye,material_name=None):
    name=material_name or ("M_CatEyes" if is_eye else "M_CatCoat");mat=owned_material(subsystem,name)
    tex=expression(mat,unreal.MaterialExpressionTextureObjectParameter,-1000,-100,parameter_name="OriginalCoatTexture",texture=texture)
    uv=expression(mat,unreal.MaterialExpressionTextureCoordinate,-1000,50)
    original_weight=scalar(mat,"OriginalCoatWeight",0,-1000,180)
    inputs={"OriginalCoatTexture":tex,"UV":uv,"OriginalCoatWeight":original_weight}
    if is_eye:
        inputs["CoatColor"]=vector(mat,"CoatColor",(.91,.92,.90),-1300,300)
        inputs["FurDetailStrength"]=scalar(mat,"FurDetailStrength",.7,-1300,430)
        fur_roughness=scalar(mat,"FurRoughness",.86,-1300,560)
        inputs["EyeLeftColor"]=vector(mat,"EyeLeftColor",(.045,.26,.66),-1000,300)
        inputs["EyeRightColor"]=vector(mat,"EyeRightColor",(.62,.27,.035),-1000,430)
        local=expression(mat,unreal.MaterialExpressionLocalPosition,-1000,600,
                         local_origin=unreal.LocalPositionOrigin.INSTANCE_PRE_SKINNING,
                         included_offsets=unreal.PositionIncludedOffsets.EXCLUDE_OFFSETS)
        interpolator=expression(mat,unreal.MaterialExpressionVertexInterpolator,-760,600)
        if not LIB.connect_material_expressions(local,"",interpolator,"VS"):raise RuntimeError("Pre-skin vertex interpolation failed")
        inputs["PreSkinPosition"]=interpolator
        color=custom(mat,EYE_CODE,inputs,-420,0)
        roughness=custom(mat,EYE_ROUGHNESS_CODE,{"UV":uv,"FurRoughness":fur_roughness},-420,350,unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    else:
        inputs["CoatColor"]=vector(mat,"CoatColor",(.91,.92,.90),-1000,300)
        detail=scalar(mat,"FurDetailStrength",.7,-1000,430);inputs["FurDetailStrength"]=detail
        normal_inputs={"OriginalCoatTexture":tex,"UV":uv,"FurDetailStrength":detail}
        fine_path=DEST+"/T_FineFurDetail"
        fine_texture=unreal.load_asset(fine_path) if subsystem.does_asset_exist(fine_path) else None
        if fine_texture:
            fine=expression(mat,unreal.MaterialExpressionTextureObjectParameter,-1300,550,
                            parameter_name="FineFurTexture",texture=fine_texture,
                            sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
            tiling=scalar(mat,"FineFurTiling",3.0,-1300,680)
            inputs.update({"FineFurTexture":fine,"FineFurTiling":tiling,
                           "FineFurAlbedoStrength":scalar(mat,"FineFurAlbedoStrength",.45,-1300,810)})
            normal_inputs.update({"FineFurTexture":fine,"FineFurTiling":tiling,"OriginalCoatWeight":original_weight,
                                  "FineFurNormalStrength":scalar(mat,"FineFurNormalStrength",1.4,-1300,940)})
        color=custom(mat,COAT_FINE_CODE if fine_texture else COAT_CODE,inputs,-420,0)
        roughness=scalar(mat,"FurRoughness",.86,-420,350)
        normal=custom(mat,NORMAL_FINE_CODE if fine_texture else NORMAL_CODE,normal_inputs,-420,500)
        connect(normal,unreal.MaterialProperty.MP_NORMAL)
    connect(color,unreal.MaterialProperty.MP_BASE_COLOR);connect(roughness,unreal.MaterialProperty.MP_ROUGHNESS)
    connect(scalar(mat,"DielectricSpecular",.5 if is_eye else .28,-420,700),unreal.MaterialProperty.MP_SPECULAR)
    connect(expression(mat,unreal.MaterialExpressionConstant,-420,800,r=0),unreal.MaterialProperty.MP_METALLIC)
    errors=list(LIB.recompile_material(mat))
    if errors:raise RuntimeError(name+" shader errors: "+str(errors))
    subsystem.set_metadata_tag(mat,"MirmirAppearanceStatus","compiled")
    if not subsystem.save_loaded_asset(mat,False):raise RuntimeError("Material save failed: "+name)
    return {"path":mat.get_path_name(),"shader_compile_errors":errors,
            "scalar_parameters":[str(x) for x in LIB.get_scalar_parameter_names(mat)],
            "vector_parameters":[str(x) for x in LIB.get_vector_parameter_names(mat)],
            "texture":texture.get_path_name()}


def main():
    imported=ROOT/"unreal/Mirmir/Content/Mirmir/Shared/Cat/cat-walk-loop"
    before={str(p.relative_to(ROOT)):sha(p) for p in imported.rglob("*.uasset")}
    subsystem=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    original=unreal.load_asset(SOURCE+"/Materials/pet_s_4cat_Black_mat")
    texture_params=original.get_editor_property("texture_parameter_values")
    candidates=[p.get_editor_property("parameter_value") for p in texture_params
                if "basecolor" in str(p.get_editor_property("parameter_info").get_editor_property("name")).lower().replace("_","")]
    if len(candidates)!=1 or not isinstance(candidates[0],unreal.Texture2D):
        raise RuntimeError("Original body BaseColor texture is absent/ambiguous: "+str(texture_params))
    texture=candidates[0]
    records=[make_material(subsystem,texture,eye) for eye in [False,True]]
    after={str(p.relative_to(ROOT)):sha(p) for p in imported.rglob("*.uasset")}
    if before!=after:raise RuntimeError("An imported cat asset changed unexpectedly")
    audit={"schema_version":1,"owner":OWNER,"actual_editor_python_executed":True,
           "materials":records,"mesh_binding":{"Object_7":"M_CatCoat","Object_10":"M_CatEyes","Object_13":"M_CatEyes","Object_16":"original Blackeye alpha material preserved"},
           "eye_side":{"coordinate":"pre-skinned local X interpolated from vertex shader","positive":"anatomical left","negative":"anatomical right","body_length_axis":"Y","height_axis":"Z","evidence":".local/appearance/inspection.json actual imported bounds + L/R bone component coordinates"},
           "original_imported_assets_unchanged":before==after,"original_imported_asset_sha256":before,
           "original_texture":texture.get_path_name(),"source_model_license":"CC-BY-NC-4.0, LostBoyz2078; personal noncommercial derivative",
           "limits":["Material templates are real PBR shader assets, not strand Groom or a new photographic coat texture.","The existing short-haired anatomy is recolored; this does not make the mesh an anatomically exact British Shorthair or Van breed.","Blackeye material is ear-area alpha geometry, not the iris; original Object_16 material is preserved.","Pupil/sclera and nose/mouth/ear atlas detail are retained with UV/color masks; actual rendered appearance needs review.","Pre-skinned vertex position interpolation must be validated on the real renderer; no GPU skin-cache compatibility claim is made."],
           "runtime_visual_acceptance":False,"script":"unreal/Mirmir/scripts/create-appearance.py"}
    output=ROOT/".local/appearance/materials.json";output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(audit,indent=2)+"\n")
    unreal.log("MIRMIR_APPEARANCE_CREATED "+json.dumps(audit))


if __name__=="__main__":main()
