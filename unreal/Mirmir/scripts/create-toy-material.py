"""Create an owned PBR rubber-ball material using no downloaded textures.

Material creation only. It never spawns the ball, enables simulation, changes
the game map/Blueprint or claims actual Chaos contact verification.
"""
from pathlib import Path
import hashlib
import json
import unreal

ROOT=Path(__file__).resolve().parents[3]
DEST="/Game/Mirmir/Toys"
NAME="M_ToyBall"
OWNER="mirmir-toy-material-v1"
LIB=unreal.MaterialEditingLibrary


def main():
    subsystem=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    path=DEST+"/"+NAME
    if subsystem.does_asset_exist(path):
        material=unreal.load_asset(path)
        if not isinstance(material,unreal.Material) or subsystem.get_metadata_tag(material,"MirmirToyOwner")!=OWNER:
            raise RuntimeError("Unowned material left untouched: "+path)
        LIB.delete_all_material_expressions(material)
    else:
        material=unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME,DEST,unreal.Material,unreal.MaterialFactoryNew())
    subsystem.set_metadata_tag(material,"MirmirToyOwner",OWNER)
    material.set_editor_property("blend_mode",unreal.BlendMode.BLEND_OPAQUE)
    def node(cls,x,y,**properties):
        value=LIB.create_material_expression(material,cls,x,y)
        for key,item in properties.items():value.set_editor_property(key,item)
        return value
    color=node(unreal.MaterialExpressionVectorParameter,-700,0,parameter_name="ToyColor",default_value=unreal.LinearColor(.72,.115,.025,1))
    stripe=node(unreal.MaterialExpressionVectorParameter,-700,150,parameter_name="StripeColor",default_value=unreal.LinearColor(.035,.20,.42,1))
    uv=node(unreal.MaterialExpressionTextureCoordinate,-700,300)
    inputs=[]
    for name in ["ToyColor","StripeColor","UV"]:
        pin=unreal.CustomInput();pin.set_editor_property("input_name",name);inputs.append(pin)
    custom=node(unreal.MaterialExpressionCustom,-350,0,inputs=inputs,output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                code="float band=.5+.5*sin(UV.y*12.5663706); float aa=max(fwidth(band),.015); return lerp(ToyColor,StripeColor,smoothstep(.86-aa,.86+aa,band));")
    for source,name in [(color,"ToyColor"),(stripe,"StripeColor"),(uv,"UV")]:
        if not LIB.connect_material_expressions(source,"",custom,name):raise RuntimeError("Toy material input failed")
    for source,prop in [(custom,unreal.MaterialProperty.MP_BASE_COLOR),
                        (node(unreal.MaterialExpressionConstant,-350,250,r=.68),unreal.MaterialProperty.MP_ROUGHNESS),
                        (node(unreal.MaterialExpressionConstant,-350,350,r=.3),unreal.MaterialProperty.MP_SPECULAR),
                        (node(unreal.MaterialExpressionConstant,-350,450,r=0),unreal.MaterialProperty.MP_METALLIC)]:
        if not LIB.connect_material_property(source,"",prop):raise RuntimeError("Toy material output failed")
    errors=list(LIB.recompile_material(material))
    if errors:raise RuntimeError("Toy shader compilation failed: "+str(errors))
    if not subsystem.save_loaded_asset(material,False):raise RuntimeError("Toy material save failed")
    output=ROOT/".local/toy-material.json"
    record={"schema_version":1,"owner":OWNER,"actual_editor_python_executed":True,"material":material.get_path_name(),"shader_compile_errors":errors,"external_textures":[],"geometry":"/Engine/BasicShapes/Sphere.Sphere (provided with the installed Unreal Engine)","material_changes":"PBR rubber roughness and antialiased UV bands, authored in this helper","runtime_physics_verified":False,"map_or_blueprint_modified":False,"script_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(record,indent=2)+"\n")
    unreal.log("MIRMIR_TOY_MATERIAL_CREATED "+json.dumps(record))


if __name__=="__main__":main()
