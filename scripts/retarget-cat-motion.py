#!/usr/bin/env python3
"""Build an UNACCEPTED Tuxedo-motion retarget on the existing cat's own rig.

Requires numpy. No source geometry, skeleton, material or original walk data is
rewritten. Motion transfers use reference-pose world rotation deltas; target
local segment translations/scales remain fixed. This is FK retargeting, NOT IK
foot locking, and is only a candidate for real Unreal visual review.
"""
from __future__ import annotations
import argparse, copy, hashlib, json, struct
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
DIM = {"SCALAR":1,"VEC2":2,"VEC3":3,"VEC4":4,"MAT4":16}
DTYPE = {5120:"i1",5121:"u1",5122:"<i2",5123:"<u2",5125:"<u4",5126:"<f4"}


def sha(data): return hashlib.sha256(data).hexdigest()


def qmatrix(q):
    x,y,z,w = np.asarray(q)/np.linalg.norm(q)
    return np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
        [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],
        [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])


def matrixq(m):
    # Matrix is an orthonormal proper rotation. Output is glTF [x,y,z,w].
    tr = np.trace(m)
    if tr > 0:
        s=np.sqrt(tr+1)*2; q=np.array([(m[2,1]-m[1,2])/s,(m[0,2]-m[2,0])/s,(m[1,0]-m[0,1])/s,s/4])
    else:
        i=int(np.argmax(np.diag(m)))
        if i==0:
            s=np.sqrt(1+m[0,0]-m[1,1]-m[2,2])*2;q=np.array([s/4,(m[0,1]+m[1,0])/s,(m[0,2]+m[2,0])/s,(m[2,1]-m[1,2])/s])
        elif i==1:
            s=np.sqrt(1+m[1,1]-m[0,0]-m[2,2])*2;q=np.array([(m[0,1]+m[1,0])/s,s/4,(m[1,2]+m[2,1])/s,(m[0,2]-m[2,0])/s])
        else:
            s=np.sqrt(1+m[2,2]-m[0,0]-m[1,1])*2;q=np.array([(m[0,2]+m[2,0])/s,(m[1,2]+m[2,1])/s,s/4,(m[1,0]-m[0,1])/s])
    return q/np.linalg.norm(q)


def rotation(m):
    u,_,v=np.linalg.svd(m[:3,:3]);r=u@v
    if np.linalg.det(r)<0:u[:,-1]*=-1;r=u@v
    return r


def trs(t,q,s):
    out=np.eye(4);out[:3,:3]=qmatrix(q)@np.diag(s);out[:3,3]=t;return out


def sample(times,values,t,quat=False):
    hi=int(np.searchsorted(times,t,side="right"))
    if hi==0:return values[0].copy()
    if hi>=len(times):return values[-1].copy()
    lo=hi-1;f=(t-times[lo])/(times[hi]-times[lo]);a=values[lo];b=values[hi]
    if not quat:return a*(1-f)+b*f
    dot=float(a@b)
    if dot<0:b=-b;dot=-dot
    if dot>.9995:q=a*(1-f)+b*f;return q/np.linalg.norm(q)
    theta=np.arccos(np.clip(dot,-1,1));return (a*np.sin((1-f)*theta)+b*np.sin(f*theta))/np.sin(theta)


class Rig:
    def __init__(self,path):
        self.path=path;self.raw=path.read_bytes()
        if path.suffix==".glb":
            if struct.unpack_from("<III",self.raw)!=(0x46546c67,2,len(self.raw)):raise ValueError("Invalid GLB")
            jl=struct.unpack_from("<I",self.raw,12)[0];self.g=json.loads(self.raw[20:20+jl]);self.blobs=[self.raw[28+jl:]]
        else:
            self.g=json.loads(self.raw);self.blobs=[(path.parent/b["uri"]).read_bytes() for b in self.g["buffers"]]
        self.nodes=self.g["nodes"];self.parents={c:i for i,n in enumerate(self.nodes) for c in n.get("children",[])}
        self.indices={n.get("name",str(i)):i for i,n in enumerate(self.nodes)}
        self.animations={a["name"]:a for a in self.g.get("animations",[])}
        self.tracks={}
        for name,a in self.animations.items():
            tracks=[]
            for channel in a["channels"]:
                path=channel["target"]["path"]
                if path not in {"translation","rotation","scale"}:continue
                sampler=a["samplers"][channel["sampler"]]
                if sampler.get("interpolation","LINEAR")!="LINEAR":raise ValueError("Only LINEAR source tracks supported")
                tracks.append((channel["target"]["node"],path,self.accessor(sampler["input"]).ravel(),self.accessor(sampler["output"])))
            self.tracks[name]=tracks

    def accessor(self,i):
        a=self.g["accessors"][i]
        if "sparse" in a:raise ValueError("Sparse accessor unsupported")
        v=self.g["bufferViews"][a["bufferView"]];dtype=np.dtype(DTYPE[a["componentType"]]);n=DIM[a["type"]]
        ar=np.ndarray((a["count"],n),dtype=dtype,buffer=self.blobs[v["buffer"]],offset=v.get("byteOffset",0)+a.get("byteOffset",0),strides=(v.get("byteStride",n*dtype.itemsize),dtype.itemsize)).copy()
        if a.get("normalized"):
            if a["componentType"] in (5121,5123):return ar.astype(float)/np.iinfo(dtype).max
            raise ValueError("Unsupported normalized accessor")
        return ar

    def pose(self,name,time):
        local=[]
        for n in self.nodes:
            local.append([np.array(n.get("translation",[0,0,0]),dtype=float),np.array(n.get("rotation",[0,0,0,1]),dtype=float),np.array(n.get("scale",[1,1,1]),dtype=float)])
        for i,path,times,values in self.tracks[name]:local[i][{"translation":0,"rotation":1,"scale":2}[path]]=sample(times,values,time,path=="rotation")
        return local,self.world(local)

    def world(self,local):
        cache={}
        def at(i):
            if i in cache:return cache[i]
            n=self.nodes[i];m=np.array(n["matrix"]).reshape(4,4,order="F") if "matrix" in n else trs(*local[i])
            cache[i]=at(self.parents[i])@m if i in self.parents else m;return cache[i]
        return np.array([at(i) for i in range(len(self.nodes))])

    def find(self,fragment):
        hits=[i for i,n in enumerate(self.nodes) if fragment in n.get("name","")]
        if len(hits)!=1:raise ValueError(f"Expected one bone for {fragment}: {hits}")
        return hits[0]

    def duration(self,name):return max(float(t[-1]) for _,_,t,_ in self.tracks[name])

    def skin_cache(self):
        cache=[]
        for node in self.nodes:
            if "mesh" not in node or "skin" not in node:continue
            skin=self.g["skins"][node["skin"]];ibm=self.accessor(skin["inverseBindMatrices"]).reshape(-1,4,4).transpose(0,2,1)
            for p in self.g["meshes"][node["mesh"]]["primitives"]:
                a=p["attributes"];pos=self.accessor(a["POSITION"]);pos=np.column_stack([pos,np.ones(len(pos))]);j=self.accessor(a["JOINTS_0"]).astype(int);w=self.accessor(a["WEIGHTS_0"]).astype(float)
                cache.append((np.array(skin["joints"]),ibm,pos,j,w))
        return cache


def skin_bounds(cache,world):
    pts=[]
    for ids,ibm,pos,j,w in cache:
        matrices=world[ids]@ibm
        blend=(matrices[j]*w[:,:,None,None]).sum(axis=1)
        pts.append(np.einsum("nij,nj->ni",blend,pos)[:,:3])
    points=np.concatenate(pts);return np.array([points.min(axis=0),points.max(axis=0)])


def body_basis(head,pelvis):
    forward=head-pelvis;forward[1]=0;forward/=np.linalg.norm(forward);up=np.array([0.,1.,0.]);right=np.cross(up,forward)
    return np.column_stack([right,up,forward])


def mean_walk_reference(rig, name):
    """Average the 24 authored phases, excluding the duplicate loop endpoint.

    Markley's quaternion mean cancels opposing stride rotations without making
    new limb poses with IK. It is a derived standing reference, not an authored
    idle clip, and its residual asymmetry remains part of the review metadata.
    """
    times=np.unique(np.concatenate([t for _,_,t,_ in rig.tracks[name]]))[:-1]
    poses=[rig.pose(name,float(t))[0] for t in times]
    local=[]
    for i in range(len(rig.nodes)):
        quats=np.array([pose[i][1] for pose in poses]);_,vectors=np.linalg.eigh(quats.T@quats)
        quat=vectors[:,-1]
        if quat@quats[0]<0:quat=-quat
        local.append([np.mean([pose[i][0] for pose in poses],axis=0),quat,
                      np.mean([pose[i][2] for pose in poses],axis=0)])
    return local,rig.world(local),times


def append_array(g,binary,array,kind):
    while len(binary)%4:binary.append(0)
    arr=np.asarray(array,dtype="<f4");payload=arr.tobytes();g["bufferViews"].append({"buffer":0,"byteOffset":len(binary),"byteLength":len(payload)});binary.extend(payload)
    accessor={"bufferView":len(g["bufferViews"])-1,"componentType":5126,"count":len(arr),"type":kind}
    if kind=="SCALAR":accessor.update(min=[float(arr.min())],max=[float(arr.max())])
    g["accessors"].append(accessor);return len(g["accessors"])-1


def encode(g,binary):
    text=json.dumps(g,separators=(",",":"),ensure_ascii=False).encode();text+=b" "*((-len(text))%4);binary=bytes(binary)+b"\0"*((-len(binary))%4)
    return struct.pack("<III",0x46546c67,2,28+len(text)+len(binary))+struct.pack("<II",len(text),0x4e4f534a)+text+struct.pack("<II",len(binary),0x004e4942)+binary


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--target",type=Path,default=ROOT/"assets/personal-use/cat-walk-loop.glb")
    ap.add_argument("--donor",type=Path,default=ROOT/"assets/provenance/tuxedo-motion/scene.gltf")
    ap.add_argument("--output",type=Path,default=ROOT/"assets/personal-use/cat-tuxedo-motion-candidate.glb")
    ap.add_argument("--metadata",type=Path,default=ROOT/"assets/personal-use/tuxedo-motion-candidate.json")
    ap.add_argument("--fps",type=float,default=30)
    args=ap.parse_args()
    if args.output.resolve() in {args.target.resolve(),args.donor.resolve()}:ap.error("A source file cannot be overwritten")
    target=Rig(args.target);source=Rig(args.donor);target_raw=target.raw;donor_raw=source.raw
    walk_name=next(iter(target.animations))
    target_local,target_ref,reference_times=mean_walk_reference(target,walk_name)
    cache=target.skin_cache();_,held_ref=target.pose(walk_name,1/30)
    held_floor=float(skin_bounds(cache,held_ref)[0,1])
    mean_floor_before_alignment=float(skin_bounds(cache,target_ref)[0,1])
    # One constant placement correction aligns the derived reference with the
    # existing walk's floor. It does not solve contact in any individual clip.
    target_root=target.indices["fst_1"]
    root_parent=target.parents[target_root]
    root_destination=target_ref[target_root,:3,3].copy()
    root_destination[1]+=held_floor-mean_floor_before_alignment
    target_local[target_root][0]=(np.linalg.inv(target_ref[root_parent])@np.r_[root_destination,1])[:3]
    target_ref=target.world(target_local)
    _,source_ref=source.pose("IdleNorm",0)
    # Every mapping is semantic, not a shared-Skeleton assumption. Source has
    # a branched backward spine; world-space deltas avoid copying its local axes.
    names={"cat_bip-Pelvis_2":"spine.005_metarig.004","cat_bip-Spine_3":"spine.006_metarig.004",
        "cat_bip-Spine1_4":"Root_metarig.004","cat_bip-Spine2_5":"spine.007_metarig.004",
        "cat_bip-Spine3_6":"spine.008_metarig.004","cat_bip-Neck_7":"spine.009_metarig.004",
        "cat_bip-Neck1_8":"spine.010_metarig.004","cat_bip-Head_9":"Head_metarig.004",
        "cat_bip-Tail_55":"TailBase_metarig.004","cat_bip-Tail1_56":"Tail2_metarig.004",
        "cat_bip-Tail2_57":"Tail3_metarig.004","cat_bip-Tail3_58":"TailTip_metarig.004"}
    for side,targs in [("L",(29,30,31,32,33,35,45,46,47,48)),("R",(37,38,39,40,41,43,50,51,52,53))]:
        for prefix,suffix,donor in zip(["Clavicle","UpperArm","Forearm","Hand","Finger0","Finger1","Thigh","Calf","Foot","Toe0"],targs,["shoulder","front_thigh","front_shin","front_foot","front_toe","front_toe","thigh","shin","foot","toe"]):names[f"cat_bip-{side}-{prefix}_{suffix}"]=f"{donor}.{side}_metarig.004"
    mapping={target.indices[t]:source.indices[s] for t,s in names.items()}
    hips_s=[source.indices["thigh.L_metarig.004"],source.indices["thigh.R_metarig.004"]]
    hips_t=[target.indices["cat_bip-L-Thigh_45"],target.indices["cat_bip-R-Thigh_50"]]
    hip_s=source_ref[hips_s,:3,3].mean(axis=0);hip_t=target_ref[hips_t,:3,3].mean(axis=0)
    head_s=source_ref[source.indices["Head_metarig.004"],:3,3];head_t=target_ref[target.indices["cat_bip-Head_9"],:3,3]
    alignment=body_basis(head_t,hip_t)@body_basis(head_s,hip_s).T
    paws_s=[source.indices[n] for n in ["front_toe.L_metarig.004","front_toe.R_metarig.004","toe.L_metarig.004","toe.R_metarig.004"]]
    paws_t={"front_left":[target.indices["cat_bip-L-Finger01_34"],target.indices["cat_bip-L-Finger11_36"]],"front_right":[target.indices["cat_bip-R-Finger01_42"],target.indices["cat_bip-R-Finger11_44"]],"hind_left":[target.indices["cat_bip-L-Toe01_49"]],"hind_right":[target.indices["cat_bip-R-Toe01_54"]]}
    foot_s=float(source_ref[paws_s,1,3].mean());foot_t=float(np.mean([target_ref[ids,1,3].mean() for ids in paws_t.values()]))
    translation_scale=float((hip_t[1]-foot_t)/(hip_s[1]-foot_s))
    joint_ids=sorted({i for skin in target.g["skins"] for i in skin["joints"]})
    ref_rot={i:rotation(target_ref[i]) for i in mapping};src_ref_rot={i:rotation(source_ref[i]) for i in set(mapping.values())}
    g=copy.deepcopy(target.g);binary=bytearray(target.blobs[0]);reference_bounds=skin_bounds(cache,target_ref)
    cm_scale=70/69.0270767211914;floor=float(reference_bounds[0,1]);reference_lengths={i:np.linalg.norm(target_ref[i,:3,3]-target_ref[target.parents[i],:3,3]) for i in joint_ids if i in target.parents and i!=target_root}
    clips={"IdleNorm":"Tuxedo_Idle","IdleTailSwoosh":"Tuxedo_IdleTail","SitDown":"Tuxedo_SitDown","IdleSit":"Tuxedo_IdleSit","StandUp":"Tuxedo_StandUp"}
    reports={};pose_ends={};decoded_expected={}
    for source_name,new_name in clips.items():
        duration=source.duration(source_name);times=np.arange(0,duration,1/args.fps);times=np.unique(np.r_[times,duration]);locals_out=[];worlds_out=[];root_shifts=[]
        for time in times:
            _,sw=source.pose(source_name,float(time));shift=alignment@(sw[hips_s,:3,3].mean(axis=0)-hip_s)*translation_scale;root_shifts.append(shift)
            desired={ti:alignment@rotation(sw[si])@src_ref_rot[si].T@alignment.T@ref_rot[ti] for ti,si in mapping.items()}
            current=copy.deepcopy(target_local);world_cache={}
            def build(i):
                if i in world_cache:return world_cache[i]
                parent=build(target.parents[i]) if i in target.parents else np.eye(4)
                if i==target_root:
                    destination=target_ref[i,:3,3]+shift;current[i][0]=(np.linalg.inv(parent)@np.r_[destination,1])[:3]
                if i in desired:current[i][1]=matrixq(rotation(parent).T@desired[i])
                n=target.nodes[i];m=np.array(n["matrix"]).reshape(4,4,order="F") if "matrix" in n else trs(*current[i]);world_cache[i]=parent@m;return world_cache[i]
            world=np.array([build(i) for i in range(len(target.nodes))]);locals_out.append(current);worlds_out.append(world)
        worlds=np.array(worlds_out);time_index=append_array(g,binary,times,"SCALAR");anim={"name":new_name,"samplers":[],"channels":[]}
        decoded_expected[new_name]=(times,worlds)
        max_local_t_error=0.;max_scale_error=0.;min_quat_norm=1.;max_quat_norm=1.
        for i in joint_ids:
            if "matrix" in target.nodes[i]:continue
            for component,path,kind in [(0,"translation","VEC3"),(1,"rotation","VEC4"),(2,"scale","VEC3")]:
                values=np.array([p[i][component] for p in locals_out])
                assert np.isfinite(values).all(),(new_name,i,path)
                if path=="translation" and i!=target_root:max_local_t_error=max(max_local_t_error,float(np.max(np.abs(values-target_local[i][0]))))
                if path=="scale":max_scale_error=max(max_scale_error,float(np.max(np.abs(values-target_local[i][2]))))
                if path=="rotation":
                    for j in range(1,len(values)):
                        if values[j]@values[j-1]<0:values[j]*=-1
                    norms=np.linalg.norm(values,axis=1);min_quat_norm=min(min_quat_norm,float(norms.min()));max_quat_norm=max(max_quat_norm,float(norms.max()))
                oi=append_array(g,binary,values,kind);si=len(anim["samplers"]);anim["samplers"].append({"input":time_index,"output":oi,"interpolation":"LINEAR"});anim["channels"].append({"sampler":si,"target":{"node":i,"path":path}})
        g["animations"].append(anim)
        paw_positions={name:np.take(worlds,ids,axis=1)[:,:,:3,3].mean(axis=1) for name,ids in paws_t.items()}
        bounds=np.array([skin_bounds(cache,w) for w in worlds]);min_geometry=float(bounds[:,0,1].min());max_geometry_floor=float(bounds[:,0,1].max());segment_error=max(float(np.max(np.abs(np.linalg.norm(worlds[:,i,:3,3]-worlds[:,target.parents[i],:3,3],axis=1)-length))) for i,length in reference_lengths.items())
        pose_ends[new_name]=(worlds[0],worlds[-1])
        reports[new_name]={"source_clip":source_name,"duration_s":duration,"sample_count":len(times),"resample_rate_hz":args.fps,"mapped_bones":len(mapping),"channels":len(anim["channels"]),"local_segment_translation_max_error_source_units":max_local_t_error,"local_scale_max_error":max_scale_error,"world_segment_length_max_error_cm":segment_error*cm_scale,"quaternion_norm_range":[min_quat_norm,max_quat_norm],"pelvis_root_shift_cm_min":(np.array(root_shifts).min(axis=0)*cm_scale).tolist(),"pelvis_root_shift_cm_max":(np.array(root_shifts).max(axis=0)*cm_scale).tolist(),"reference_geometry_floor_source_y":floor,"minimum_geometry_clearance_from_reference_floor_cm":(min_geometry-floor)*cm_scale,"maximum_geometry_floor_lift_cm":(max_geometry_floor-floor)*cm_scale,"paw_proxy_height_range_from_reference_floor_cm":{name:[float((p[:,1].min()-floor)*cm_scale),float((p[:,1].max()-floor)*cm_scale)] for name,p in paw_positions.items()},"geometry_bounds_cm_min":(bounds[:,0,:].min(axis=0)*cm_scale).tolist(),"geometry_bounds_cm_max":(bounds[:,1,:].max(axis=0)*cm_scale).tolist()}
        print(new_name,"floor clearance cm",round(reports[new_name]["minimum_geometry_clearance_from_reference_floor_cm"],3),"paw y",{k:[round(x,2) for x in v] for k,v in reports[new_name]["paw_proxy_height_range_from_reference_floor_cm"].items()},flush=True)
    assert target.raw==target_raw and source.raw==donor_raw
    assert bytes(binary[:len(target.blobs[0])])==target.blobs[0]
    for k in ["nodes","meshes","skins","materials","images","textures"]:assert g.get(k)==target.g.get(k),k
    assert g["animations"][:len(target.g["animations"])]==target.g["animations"]
    g["buffers"][0]["byteLength"]=len(binary);g["asset"].setdefault("extras",{})["mirmir_tuxedo_motion_candidate"]="UNACCEPTED FK retarget candidate; DreamNoms CC BY4 motion on LostBoyz2078 CC BY-NC4 cat rig. Existing target geometry/skin/materials/original walk untouched. Needs real Unreal review."
    data=encode(g,binary);args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_bytes(data);check=Rig(args.output)
    assert check.blobs[0][:len(target.blobs[0])]==target.blobs[0]
    for name,(times,expected) in decoded_expected.items():
        decoded=np.array([check.pose(name,float(t))[1] for t in times])
        error=float(np.max(np.linalg.norm(decoded[:,:,:3,3]-expected[:,:,:3,3],axis=2))*cm_scale)
        reports[name]["serialized_world_joint_position_max_error_cm"]=error
        assert error<.001,(name,error)
        is_idle=name in {"Tuxedo_Idle","Tuxedo_IdleTail"}
        reports[name]["review_gate"]="eligible_for_real_Unreal_visual_review_only" if is_idle else "rejected_for_product_static_contact_gate"
        reports[name]["review_reason"]=("Small floor drift; actual body deformation, ground contact and blending require Unreal review." if is_idle else "Residual asymmetric hind contact and floor penetration remain after the mean-reference correction. No global placement adjustment can correct relative foot error.")
    transitions={}
    for left,right in [("Tuxedo_SitDown","Tuxedo_IdleSit"),("Tuxedo_IdleSit","Tuxedo_StandUp"),("Tuxedo_StandUp","Tuxedo_Idle")]:
        a=pose_ends[left][1];b=pose_ends[right][0];transitions[left+" -> "+right]={name:float(np.linalg.norm(a[ids,:3,3].mean(axis=0)-b[ids,:3,3].mean(axis=0))*cm_scale) for name,ids in paws_t.items()}
    def reference_metrics(world):
        positions={name:world[ids,:3,3].mean(axis=0) for name,ids in paws_t.items()}
        basis=body_basis(world[target.indices["cat_bip-Head_9"],:3,3],world[hips_t,:3,3].mean(axis=0))
        return {"paw_proxy_positions_cm":{name:(p*cm_scale).tolist() for name,p in positions.items()},
                "left_right_forward_gap_cm":{side:abs(float((positions[side+"_left"]-positions[side+"_right"])@basis[:,2]))*cm_scale for side in ["front","hind"]},
                "left_right_height_gap_cm":{side:abs(float(positions[side+"_left"][1]-positions[side+"_right"][1]))*cm_scale for side in ["front","hind"]},
                "head_to_hips_horizontal_forward":basis[:,2].tolist()}
    report={"schema_version":2,"status":"candidate_not_accepted_for_product","method":"Use the Markley mean of 24 original walk-phase local rotations and mean translations/scales as the target reference, excluding the duplicated loop endpoint. Align reference-body bases, transfer source world rotation deltas onto target reference world rotations, then derive local target rotations through the target hierarchy. Preserve target local segment translations and scales. Translate target root by source mean-hip displacement scaled by reference hip-to-toe height ratio. One constant reference placement correction preserves the previous reference geometry floor; it does not correct contact errors. No IK/foot locking, no Skeleton reassignment.","target_source":str(args.target.relative_to(ROOT)),"target_source_sha256":sha(target_raw),"donor_source":str(args.donor.relative_to(ROOT)),"donor_gltf_sha256":sha(donor_raw),"donor_bin_sha256":sha(source.blobs[0]),"output":str(args.output.relative_to(ROOT)),"output_sha256":sha(data),"output_bytes":len(data),"target_reference_pose":{"animation":walk_name,"method":"Markley quaternion mean plus arithmetic TRS means over original authored phases","sample_count":len(reference_times),"first_sample_s":float(reference_times[0]),"last_sample_s":float(reference_times[-1]),"duplicate_loop_endpoint_excluded":True,"constant_vertical_floor_alignment_cm":(held_floor-mean_floor_before_alignment)*cm_scale,"derived_reference_metrics":reference_metrics(target_ref),"previous_held_walk_reference_metrics":reference_metrics(held_ref)},"donor_reference_pose":{"animation":"IdleNorm","time_s":0},"body_basis_alignment_matrix":alignment.tolist(),"hip_to_toe_translation_scale_target_units_per_donor_world_unit":translation_scale,"target_cm_per_source_world_unit_for_metrics":cm_scale,"mapping_target_to_source":names,"clips":reports,"transition_paw_proxy_endpoint_gaps_cm":transitions,"original_target_binary_prefix_sha256":sha(target.blobs[0]),"preserved_binary_prefix_sha256":sha(check.blobs[0][:len(target.blobs[0])]),"original_walk_animation_json_preserved":True,"target_geometry_skin_material_nodes_preserved":True,"source_files_untouched":args.target.read_bytes()==target_raw and args.donor.read_bytes()==donor_raw,"license":{"target":"CC-BY-NC-4.0, LostBoyz2078","motion":"CC-BY-4.0, DreamNoms","combined_scope":"Personal noncommercial candidate derivative. Credit both authors; no endorsement implied.","motion_source":"https://sketchfab.com/3d-models/tuxedo-cat-animated-20-783fcb78b55b4394a212c2b6392e1113","target_source":"https://sketchfab.com/3d-models/cat-walk-915680200c064815bba75e008ba9efb5","donor_provenance":"assets/provenance/tuxedo-motion/source.json"},"limits":["Foreign rig proportions and branched source spine make FK retargeting uncertain; body/leg lengths are retained but end effectors are not constrained.","The derived walk-cycle mean reference still has a 2.58 cm left/right hind forward mismatch; it is not an authored idle or anatomical neutral pose.","Floor metrics use skinned source geometry and distal bone proxies; no paw-pad solver or collision contacts are claimed.","SitDown, IdleSit and StandUp remain rejected for product use because asymmetric contact errors persist. They are retained only as isolated review data.","Current output contains idle, tail variation and sitting transitions only, not sleep/groom/eat clips.","Original target facial/ear/tongue bones without anatomical donor correspondence stay at target reference local transforms.","No Unreal import, screenshots, montage playback, state blending or quality acceptance has been performed for this candidate."],"reproduce":"python scripts/retarget-cat-motion.py"}
    args.metadata.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n")
    print(args.output,sha(data),flush=True)


if __name__=="__main__":main()
