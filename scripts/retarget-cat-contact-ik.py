#!/usr/bin/env python3
"""Build one isolated, UNACCEPTED source-contact IK review variant.

Uses the stable FK candidate as input. Segment translations/scales, original
walk and geometry stay intact. This is an explicit contact correction, not a
claim that the foreign authored clips already match the target cat's anatomy.
"""
from __future__ import annotations
import copy
import importlib.util
import json
from pathlib import Path
import numpy as np

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("retarget_cat_motion",ROOT/"scripts/retarget-cat-motion.py")
rt=importlib.util.module_from_spec(spec);spec.loader.exec_module(rt)
BASE=ROOT/"assets/personal-use/cat-tuxedo-motion-candidate.glb"
BASE_META=ROOT/"assets/personal-use/tuxedo-motion-candidate.json"
OUTPUT=ROOT/"assets/personal-use/cat-tuxedo-motion-ik-candidate.glb"
META=ROOT/"assets/personal-use/tuxedo-motion-ik-candidate.json"
LEGS={
    "front_left":([47,48,49],[51,53],22),
    "front_right":([55,56,57],[59,61],27),
    "hind_left":([62,63,64],[66],39),
    "hind_right":([67,68,69],[71],44),
}


def from_to(a,b):
    a=a/np.linalg.norm(a);b=b/np.linalg.norm(b);dot=float(np.clip(a@b,-1,1))
    if dot>1-1e-12:return np.eye(3)
    if dot<-1+1e-8:raise ValueError("Antiparallel limb rotation is not accepted")
    v=np.cross(a,b);k=np.array([[0,-v[2],v[1]],[v[2],0,-v[0]],[-v[1],v[0],0]])
    return np.eye(3)+k+k@k/(1+dot)


def falling_weight(value,full,zero):
    x=np.clip((zero-value)/(zero-full),0,1)
    return float(x*x*(3-2*x))


def solve_chain(fk,chain,wanted_end):
    hip,knee,ankle=chain;a,b,c=fk[chain,:3,3]
    l1=np.linalg.norm(b-a);l2=np.linalg.norm(c-b)
    distance=np.linalg.norm(wanted_end-a);direction=(wanted_end-a)/distance
    clamped=float(np.clip(distance,abs(l1-l2)+1e-5,l1+l2-1e-5))
    pole=b-a-np.dot(b-a,direction)*direction;pole_distance=np.linalg.norm(pole)
    if pole_distance<1e-4:raise ValueError("Unstable FK knee pole")
    x=(l1*l1-l2*l2+clamped*clamped)/(2*clamped)
    y=np.sqrt(max(l1*l1-x*x,0));new_b=a+direction*x+pole/pole_distance*y
    new_c=a+direction*clamped
    return {hip:from_to(b-a,new_b-a)@rt.rotation(fk[hip]),
            knee:from_to(c-b,new_c-new_b)@rt.rotation(fk[knee]),
            ankle:rt.rotation(fk[ankle])},abs(distance-clamped),pole_distance


def apply_world_rotations(rig,local,wanted):
    result=copy.deepcopy(local);cache={}
    def at(i):
        if i in cache:return cache[i]
        parent=at(rig.parents[i]) if i in rig.parents else np.eye(4)
        if i in wanted:result[i][1]=rt.matrixq(rt.rotation(parent).T@wanted[i])
        node=rig.nodes[i]
        matrix=np.array(node["matrix"]).reshape(4,4,order="F") if "matrix" in node else rt.trs(*result[i])
        cache[i]=parent@matrix;return cache[i]
    for i in range(len(rig.nodes)):at(i)
    return result


def main():
    provenance=json.loads(BASE_META.read_text());base_bytes=BASE.read_bytes()
    assert rt.sha(base_bytes)==provenance["output_sha256"]
    rig=rt.Rig(BASE);donor=rt.Rig(ROOT/provenance["donor_source"])
    alignment=np.array(provenance["body_basis_alignment_matrix"])
    height_scale=provenance["hip_to_toe_translation_scale_target_units_per_donor_world_unit"]
    cm=provenance["target_cm_per_source_world_unit_for_metrics"]
    neutral=rig.pose("Tuxedo_Idle",0)[1];source_neutral=donor.pose("IdleNorm",0)[1]
    cache=rig.skin_cache();floor=provenance["clips"]["Tuxedo_Idle"]["reference_geometry_floor_source_y"]
    replacements={"Tuxedo_SitDown":"TuxedoIK_SitDown","Tuxedo_IdleSit":"TuxedoIK_IdleSit","Tuxedo_StandUp":"TuxedoIK_StandUp"}
    g=copy.deepcopy(rig.g);binary=bytearray(rig.blobs[0]);g["animations"]=[a for a in g["animations"] if a["name"] not in replacements]
    reports={};expected={}
    for old_name,new_name in replacements.items():
        source_name=provenance["clips"][old_name]["source_clip"]
        times=np.unique(np.concatenate([t for _,_,t,_ in rig.tracks[old_name]]));duration=rig.duration(old_name)
        source_world=np.array([donor.pose(source_name,float(t))[1] for t in times])
        minimum_y={name:float(source_world[:,sid,1,3].min()) for name,(_,_,sid) in LEGS.items()}
        output_local=[];fk_world=[];ik_world=[];stats={name:[] for name in LEGS}
        for time,source in zip(times,source_world):
            local,fk=rig.pose(old_name,float(time));wanted={};weights={};targets={};diagnostics={}
            for name,(chain,paws,sid) in LEGS.items():
                toe=fk[paws,:3,3].mean(axis=0)
                target=neutral[paws,:3,3].mean(axis=0)+alignment@(source[sid,:3,3]-source_neutral[sid,:3,3])*height_scale
                # The donor's low, slow distal joint is a contact proxy. The
                # thresholds are source-world units, not physical pad contact.
                lo=max(0,float(time)-1/240);hi=min(duration,float(time)+1/240)
                velocity=np.linalg.norm(donor.pose(source_name,hi)[1][sid,:3,3]-donor.pose(source_name,lo)[1][sid,:3,3])/max(hi-lo,1e-8)
                weight=falling_weight(source[sid,1,3]-minimum_y[name],.02,.05)*falling_weight(velocity,.2,.5)
                rotations,residual,pole=solve_chain(fk,chain,target-(toe-fk[chain[2],:3,3]))
                if weight>0:wanted.update(rotations)
                weights[name]=weight;targets[name]=target;diagnostics[name]=(residual,pole)
            full_ik=apply_world_rotations(rig,local,wanted)
            for name,(chain,_,_) in LEGS.items():
                weight=weights[name]
                for bone in chain:
                    full_ik[bone][1]=rt.sample(np.array([0.,1.]),np.array([local[bone][1],full_ik[bone][1]]),weight,True)
            corrected=rig.world(full_ik);output_local.append(full_ik);fk_world.append(fk);ik_world.append(corrected)
            for name,(chain,paws,_) in LEGS.items():
                achieved=corrected[paws,:3,3].mean(axis=0)
                wrist_angle=np.degrees(np.arccos(np.clip((np.trace(rt.rotation(corrected[chain[2]])@rt.rotation(fk[chain[2]]).T)-1)/2,-1,1)))
                stats[name].append((weights[name],np.linalg.norm(achieved-targets[name])*cm,diagnostics[name][0]*cm,diagnostics[name][1]*cm,wrist_angle))
        worlds=np.array(ik_world);fk_world=np.array(fk_world);expected[new_name]=(times,worlds)
        animation={"name":new_name,"channels":[],"samplers":[]};ti=rt.append_array(g,binary,times,"SCALAR")
        joints=sorted({i for skin in rig.g["skins"] for i in skin["joints"]})
        for bone in joints:
            if "matrix" in rig.nodes[bone]:continue
            for component,path,kind in [(0,"translation","VEC3"),(1,"rotation","VEC4"),(2,"scale","VEC3")]:
                values=np.array([p[bone][component] for p in output_local]);assert np.isfinite(values).all()
                if component==1:
                    for i in range(1,len(values)):
                        if values[i]@values[i-1]<0:values[i]*=-1
                oi=rt.append_array(g,binary,values,kind);si=len(animation["samplers"])
                animation["samplers"].append({"input":ti,"output":oi,"interpolation":"LINEAR"})
                animation["channels"].append({"sampler":si,"target":{"node":bone,"path":path}})
        g["animations"].append(animation)
        bounds=np.array([rt.skin_bounds(cache,w) for w in worlds]);legs_report={}
        max_segment_error=0.
        for name,(chain,paws,_) in LEGS.items():
            for a,b in zip(chain,chain[1:]):
                max_segment_error=max(max_segment_error,float(np.max(np.abs(np.linalg.norm(worlds[:,b,:3,3]-worlds[:,a,:3,3],axis=1)-np.linalg.norm(fk_world[:,b,:3,3]-fk_world[:,a,:3,3],axis=1))))*cm)
            ar=np.array(stats[name]);full=ar[:,0]>.999;toe=np.take(worlds,paws,axis=1)[:,:,:3,3].mean(axis=1)
            legs_report[name]={"active_contact_sample_count":int((ar[:,0]>0).sum()),"full_contact_sample_count":int(full.sum()),"full_contact_target_error_cm_max":float(ar[full,1].max()) if full.any() else None,"unreachable_distance_cm_max":float(ar[:,2].max()),"minimum_fk_knee_pole_cm":float(ar[:,3].min()),"full_contact_wrist_orientation_error_deg_max":float(ar[full,4].max()) if full.any() else None,"toe_proxy_floor_clearance_cm_range":[float((toe[:,1].min()-floor)*cm),float((toe[:,1].max()-floor)*cm)]}
        reports[new_name]={"source_clip":source_name,"fk_clip":old_name,"duration_s":duration,"sample_count":len(times),"review_gate":"unaccepted_contact_correction_candidate_requires_real_Unreal_review","minimum_geometry_clearance_from_reference_floor_cm":float((bounds[:,0,1].min()-floor)*cm),"previous_fk_minimum_geometry_clearance_cm":provenance["clips"][old_name]["minimum_geometry_clearance_from_reference_floor_cm"],"segment_length_change_cm_max":max_segment_error,"legs":legs_report}
        assert max_segment_error<.001
        print(new_name,'geometry floor cm',round(reports[new_name]['minimum_geometry_clearance_from_reference_floor_cm'],3),flush=True)
    g["buffers"][0]["byteLength"]=len(binary)
    g["asset"].setdefault("extras",{})["mirmir_contact_ik_candidate"]="UNACCEPTED bounded two-bone contact correction; preserve source FK wrist/toe orientation, clamp unreachable targets without stretching. Not product assigned."
    data=rt.encode(g,binary);OUTPUT.write_bytes(data);decoded=rt.Rig(OUTPUT)
    assert decoded.blobs[0][:len(rig.blobs[0])]==rig.blobs[0]
    for key in ["nodes","meshes","skins","materials","images","textures"]:assert decoded.g.get(key)==rig.g.get(key)
    for name,(times,worlds) in expected.items():
        actual=np.array([decoded.pose(name,float(t))[1] for t in times]);error=float(np.linalg.norm(actual[:,:,:3,3]-worlds[:,:,:3,3],axis=2).max()*cm)
        assert error<.001;reports[name]["serialized_world_joint_position_max_error_cm"]=error
    transitions={}
    for left,right in [("TuxedoIK_SitDown","TuxedoIK_IdleSit"),("TuxedoIK_IdleSit","TuxedoIK_StandUp"),("TuxedoIK_StandUp","Tuxedo_Idle")]:
        a=decoded.pose(left,decoded.duration(left))[1];b=decoded.pose(right,0)[1]
        transitions[left+" -> "+right]={name:float(np.linalg.norm(a[paws,:3,3].mean(axis=0)-b[paws,:3,3].mean(axis=0))*cm) for name,(_,paws,_) in LEGS.items()}
    for old in rig.g["animations"]:
        if old["name"] not in replacements:assert old in decoded.g["animations"]
    assert BASE.read_bytes()==base_bytes
    report={"schema_version":1,"status":"candidate_not_accepted_for_product","base_candidate":str(BASE.relative_to(ROOT)),"base_candidate_sha256":rt.sha(base_bytes),"base_provenance":str(BASE_META.relative_to(ROOT)),"output":str(OUTPUT.relative_to(ROOT)),"output_sha256":rt.sha(data),"output_bytes":len(data),"method":"One analytic two-bone hip-knee-ankle solve per leg, FK knee pole, donor distal-joint low-height/low-speed contact weights. Desired toe equals target neutral toe plus aligned donor animated-minus-neutral toe displacement, scaled by the FK retarget hip-height ratio. Preserve FK wrist world orientation at full contact. Clamp unreachable distances; do not stretch bones or add per-frame root/floor offsets.","contact_proxy_parameters":{"donor_height_above_per_clip_toe_minimum_full_weight":.02,"donor_height_zero_weight":.05,"donor_world_speed_full_weight":.2,"donor_world_speed_zero_weight":.5,"velocity_sample_half_window_s":1/240,"weight_curve":"smoothstep; low-height weight multiplied by low-speed weight"},"clips":reports,"transition_paw_proxy_endpoint_gaps_cm":transitions,"original_walk_and_idle_clips_preserved":True,"geometry_skin_nodes_materials_preserved":True,"stable_base_file_untouched":BASE.read_bytes()==base_bytes,"license":provenance["license"],"limits":["No actual Unreal import/playback or visual acceptance for this IK variant.","Contact classification uses animated distal bone joints, not real paw pads or collision contact. Per-clip minima and thresholds can misclassify contacts.","The donor front-left toe has about 3.53 source-world units/s velocity at the SitDown end and StandUp start despite matching the static IdleSit endpoint. The source-only contact gate releases IK there, leaving a 3.02 cm target endpoint discontinuity; no correction is made to hide this. Product acceptance remains rejected.","Two-bone solving changes transferred leg motion; it does not provide an additional authored animation. Foreign spine, body deformation, joint limits and self-intersection are not solved.","Unreachable targets are clamped with residual error. Partial contact weights can retain larger residuals and wrist orientation deviation.","Target neutral reference remains derived from its walk cycle, with residual hind asymmetry. No full sleep/eat/groom animation is added."],"reproduce":"python scripts/retarget-cat-contact-ik.py"}
    META.write_text(json.dumps(report,indent=2)+"\n");print(OUTPUT,rt.sha(data),flush=True)


if __name__=="__main__":main()
