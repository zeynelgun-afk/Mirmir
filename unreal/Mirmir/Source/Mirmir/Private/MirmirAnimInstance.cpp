#include "MirmirAnimInstance.h"
#include "MirmirHabitat.h"
#include "MirmirToy.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimNodeSpaceConversions.h"
#include "BoneControllers/AnimNode_ModifyBone.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "TwoBoneIK.h"
#include "BlendStack/AnimNode_BlendStack.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"

namespace
{
enum class ECatPose : uint8 { None, Idle, IdleVariation, Walk, HeldWalk, SitDown, Sitting, StandUp, Sleep };

FName PoseName(ECatPose Pose)
{
    switch (Pose)
    {
    case ECatPose::Idle: return TEXT("Idle");
    case ECatPose::IdleVariation: return TEXT("IdleVariation");
    case ECatPose::Walk: return TEXT("Walk");
    case ECatPose::HeldWalk: return TEXT("HeldWalk");
    case ECatPose::SitDown: return TEXT("SitDown");
    case ECatPose::Sitting: return TEXT("Sitting");
    case ECatPose::StandUp: return TEXT("StandUp");
    case ECatPose::Sleep: return TEXT("Sleep");
    default: return TEXT("None");
    }
}

// Per-foot bounded contact correction on the ACTUAL source rig. The source
// walk's low-height/backward-moving distal bones define stance, not collision
// sensors. The authored swing, limb lengths and ankle orientation are retained.
struct FCatStanceNode final : FAnimNode_SkeletalControlBase
{
    struct FLeg
    {
        FBoneReference Upper, Knee, End, Tip, OtherTip, ToeBase;
        bool bHasOtherTip = false;
        bool bHasToeBase = false;
        bool bValid = false;
        bool bPlanted = false;
        FVector Anchor = FVector::ZeroVector;
        FVector AppliedDelta = FVector::ZeroVector;
        bool bActionAnchored = false;
        FVector ActionTipWorld = FVector::ZeroVector;
        FQuat ActionEndWorldRotation = FQuat::Identity;
        FQuat ActionToeWorldRotation = FQuat::Identity;
    };
    FLeg Legs[4];
    bool bEnabled = false;
    bool bApplyBody = true, bApplyFeet = true;
    float Phase = 0.0f;
    float MaximumCorrectionCm = 4.0f;
    FTransform ComponentToWorld = FTransform::Identity;
    TArray<float> Weights{ 0.0f, 0.0f, 0.0f, 0.0f };
    TArray<float> Corrections{ 0.0f, 0.0f, 0.0f, 0.0f };
    TArray<float> Residuals{ 0.0f, 0.0f, 0.0f, 0.0f };
    int32 ValidLegs = 0;
    FBoneReference Pelvis;
    FBoneReference Tail;
    FBoneReference Spine[3];
    FVector RestTailAxis = FVector::RightVector;
    float RestAlpha = 0.0f;
    float EatAlpha = 0.0f;
    float PawAlpha = 0.0f;
    int32 PawIndex = 0;
    FVector PawTargetWorld = FVector::ZeroVector;
    FVector ForwardWorld = FVector::ForwardVector;
    float BreathingCm = 0.0f;
    float DeltaSeconds = 1.0f / 30.0f;

    void Configure(const USkeletalMeshComponent* Mesh)
    {
        const TCHAR* Upper[] = { TEXT("cat_bip_L_UpperArm"), TEXT("cat_bip_R_UpperArm"), TEXT("cat_bip_L_Thigh"), TEXT("cat_bip_R_Thigh") };
        const TCHAR* Knee[] = { TEXT("cat_bip_L_Forearm"), TEXT("cat_bip_R_Forearm"), TEXT("cat_bip_L_Calf"), TEXT("cat_bip_R_Calf") };
        const TCHAR* End[] = { TEXT("cat_bip_L_Hand"), TEXT("cat_bip_R_Hand"), TEXT("cat_bip_L_Foot"), TEXT("cat_bip_R_Foot") };
        const TCHAR* Tip[] = { TEXT("cat_bip_L_Finger01"), TEXT("cat_bip_R_Finger01"), TEXT("cat_bip_L_Toe01"), TEXT("cat_bip_R_Toe01") };
        const TCHAR* Other[] = { TEXT("cat_bip_L_Finger11"), TEXT("cat_bip_R_Finger11") };
        auto Find = [Mesh](const TCHAR* Fragment) -> FName
        {
            for (int32 Bone = 0; Bone < Mesh->GetNumBones(); ++Bone)
            {
                const FName Name = Mesh->GetBoneName(Bone);
                if (Name.ToString().Replace(TEXT("-"), TEXT("_")).Contains(Fragment)) return Name;
            }
            return NAME_None;
        };
        Pelvis.BoneName = Find(TEXT("cat_bip_Pelvis"));
        Tail.BoneName = Find(TEXT("cat_bip_Tail"));
        Spine[0].BoneName=Find(TEXT("cat_bip_Spine_3"));
        Spine[1].BoneName=Find(TEXT("cat_bip_Spine1_4"));
        Spine[2].BoneName=Find(TEXT("cat_bip_Spine2_5"));
        for (int32 I = 0; I < 4; ++I)
        {
            Legs[I].Upper.BoneName = Find(Upper[I]);
            Legs[I].Knee.BoneName = Find(Knee[I]);
            Legs[I].End.BoneName = Find(End[I]);
            Legs[I].Tip.BoneName = Find(Tip[I]);
            Legs[I].bHasOtherTip = I < 2;
            if (I < 2) Legs[I].OtherTip.BoneName = Find(Other[I]);
            Legs[I].bHasToeBase=I>=2;
            if (I>=2) Legs[I].ToeBase.BoneName=Find(I==2 ? TEXT("cat_bip_L_Toe0_48") : TEXT("cat_bip_R_Toe0_53"));
        }
    }

protected:
    virtual void InitializeBoneReferences(const FBoneContainer& Bones) override
    {
        ValidLegs = 0;
        Pelvis.Initialize(Bones);
        Tail.Initialize(Bones);
        for (FBoneReference& S:Spine) S.Initialize(Bones);
        for (FLeg& Leg : Legs)
        {
            Leg.Upper.Initialize(Bones); Leg.Knee.Initialize(Bones);
            Leg.End.Initialize(Bones); Leg.Tip.Initialize(Bones);
            if (Leg.bHasOtherTip) Leg.OtherTip.Initialize(Bones);
            if (Leg.bHasToeBase) Leg.ToeBase.Initialize(Bones);
            Leg.bValid = Leg.Upper.IsValidToEvaluate(Bones) && Leg.Knee.IsValidToEvaluate(Bones) &&
                Leg.End.IsValidToEvaluate(Bones) && Leg.Tip.IsValidToEvaluate(Bones) &&
                (!Leg.bHasOtherTip || Leg.OtherTip.IsValidToEvaluate(Bones)) &&
                (!Leg.bHasToeBase || Leg.ToeBase.IsValidToEvaluate(Bones));
            if (Leg.bValid)
            {
                // Reject another rig with superficially similar bone names.
                Leg.bValid = Bones.GetParentBoneIndex(Leg.End.GetCompactPoseIndex(Bones)) == Leg.Knee.GetCompactPoseIndex(Bones) &&
                    Bones.GetParentBoneIndex(Leg.Knee.GetCompactPoseIndex(Bones)) == Leg.Upper.GetCompactPoseIndex(Bones);
            }
            Leg.bPlanted = false;
            Leg.AppliedDelta = FVector::ZeroVector;
            Leg.bActionAnchored = false;
            if (Leg.bValid) ++ValidLegs;
        }
    }
    virtual bool IsValidToEvaluate(const USkeleton*, const FBoneContainer&) override { return ValidLegs == 4; }
    virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& Transforms) override
    {
        // Calibrated normalized windows for the closed 0.8s CatWalk loop. Each
        // has smooth onset/release; fingers/toes are bone proxies, not paw pads.
        const float Starts[] = { 0.70f, 0.17f, 0.54f, 0.00f };
        const float Lengths[] = { 0.51f, 0.53f, 0.48f, 0.52f };
        const FBoneContainer& Bones = Output.Pose.GetPose().GetBoneContainer();
        const bool bRest = (RestAlpha > 0.001f || EatAlpha > .001f || PawAlpha > .001f) && Pelvis.IsValidToEvaluate(Bones);
        const FVector RestDelta = bApplyBody ? ComponentToWorld.InverseTransformVector(
            FVector(0, 0, -4.0f * RestAlpha - 0.5f * EatAlpha - 2.5f * PawAlpha + BreathingCm)) : FVector::ZeroVector;
        if (bRest && bApplyBody)
        {
            FTransform Root = Output.Pose.GetComponentSpaceTransform(Pelvis.GetCompactPoseIndex(Bones));
            Root.AddToTranslation(RestDelta);
            Transforms.Emplace(Pelvis.GetCompactPoseIndex(Bones), Root);
            if (Spine[0].IsValidToEvaluate(Bones)&&Spine[1].IsValidToEvaluate(Bones)&&Spine[2].IsValidToEvaluate(Bones))
            {
                FTransform Flex[3];
                for (int32 J=0;J<3;++J) {Flex[J]=Output.Pose.GetComponentSpaceTransform(Spine[J].GetCompactPoseIndex(Bones));Flex[J].AddToTranslation(RestDelta);}
                // Feeding should read as a shoulder/back hinge, not a long
                // horizontal neck. Spread the lowering through the thoracic
                // chain while keeping the standing pelvis nearly unchanged.
                const FQuat Bend(RestTailAxis.GetSafeNormal(),FMath::DegreesToRadians((10.0f*EatAlpha+12.0f*RestAlpha)/3.0f));
                for (int32 J=0;J<3;++J)
                {
                    const FVector Pivot=Flex[J].GetLocation();
                    for (int32 C=J;C<3;++C)
                    {Flex[C].SetLocation(Pivot+Bend.RotateVector(Flex[C].GetLocation()-Pivot));Flex[C].SetRotation(Bend*Flex[C].GetRotation());}
                }
                for (int32 J=0;J<3;++J) Transforms.Emplace(Spine[J].GetCompactPoseIndex(Bones),Flex[J]);
            }
            if (Tail.IsValidToEvaluate(Bones))
            {
                FTransform RelaxedTail = Output.Pose.GetComponentSpaceTransform(Tail.GetCompactPoseIndex(Bones));
                RelaxedTail.AddToTranslation(RestDelta);
                RelaxedTail.SetRotation(FQuat(RestTailAxis.GetSafeNormal(),
                    FMath::DegreesToRadians(-90.0f * RestAlpha - 18.0f * EatAlpha)) * RelaxedTail.GetRotation());
                Transforms.Emplace(Tail.GetCompactPoseIndex(Bones), RelaxedTail);
            }
        }
        if (!bApplyFeet)
        {Transforms.Sort([](const FBoneTransform& A,const FBoneTransform& B){return A.BoneIndex<B.BoneIndex;});return;}
        for (int32 I = 0; I < 4; ++I)
        {
            FLeg& Leg = Legs[I];
            float Position = FMath::Fmod(Phase - Starts[I] + 1.0f, 1.0f);
            float Weight = bEnabled && Position < Lengths[I]
                ? FMath::Min(FMath::Clamp(Position / 0.055f, 0.0f, 1.0f), FMath::Clamp((Lengths[I] - Position) / 0.055f, 0.0f, 1.0f)) : 0.0f;
            Weight = Weight * Weight * (3.0f - 2.0f * Weight);
            Weights[I] = Weight; Corrections[I] = 0.0f; Residuals[I] = 0.0f;
            if (Weight <= 0.001f || bRest) Leg.bPlanted = false;
            const FCompactPoseBoneIndex U = Leg.Upper.GetCompactPoseIndex(Bones), K = Leg.Knee.GetCompactPoseIndex(Bones), E = Leg.End.GetCompactPoseIndex(Bones);
            FTransform Upper = Output.Pose.GetComponentSpaceTransform(U);
            FTransform Knee = Output.Pose.GetComponentSpaceTransform(K);
            FTransform End = Output.Pose.GetComponentSpaceTransform(E);
            FTransform ToeBase;
            if (Leg.bHasToeBase) ToeBase=Output.Pose.GetComponentSpaceTransform(Leg.ToeBase.GetCompactPoseIndex(Bones));
            FVector Tip = Output.Pose.GetComponentSpaceTransform(Leg.Tip.GetCompactPoseIndex(Bones)).GetLocation();
            if (Leg.bHasOtherTip) Tip = (Tip + Output.Pose.GetComponentSpaceTransform(Leg.OtherTip.GetCompactPoseIndex(Bones)).GetLocation()) * 0.5;
            const FVector WorldTip = ComponentToWorld.TransformPosition(Tip);
            if (!bRest || !Leg.bActionAnchored)
            {
                Leg.ActionTipWorld=WorldTip;
                Leg.ActionEndWorldRotation=ComponentToWorld.GetRotation()*End.GetRotation();
                if (Leg.bHasToeBase) Leg.ActionToeWorldRotation=ComponentToWorld.GetRotation()*ToeBase.GetRotation();
                Leg.bActionAnchored=true;
            }
            if (!bEnabled && !bRest && Leg.AppliedDelta.IsNearlyZero(.001f)) continue;
            if (Weight > .001f && !bRest && !Leg.bPlanted)
            {
                Leg.Anchor = WorldTip + Leg.AppliedDelta;
                Leg.bPlanted = true;
            }
            const FVector DesiredDelta = Leg.bPlanted && !bRest
                ? (Leg.Anchor - WorldTip).GetClampedToMaxSize(MaximumCorrectionCm) * Weight : FVector::ZeroVector;
            // Plant correction is bounded; release decays across swing rather
            // than dropping four centimetres inside a .055-cycle window.
            Leg.AppliedDelta = FMath::VInterpConstantTo(Leg.AppliedDelta, DesiredDelta,
                DeltaSeconds, Leg.bPlanted ? 16.0f : 8.0f).GetClampedToMaxSize(MaximumCorrectionCm);
            const FVector DeltaComponent = ComponentToWorld.InverseTransformVector(Leg.AppliedDelta);
            FVector DesiredEnd = End.GetLocation() + (bRest ? FVector::ZeroVector : DeltaComponent);
            FQuat EndRotation=bRest ? ComponentToWorld.GetRotation().Inverse()*Leg.ActionEndWorldRotation : End.GetRotation();
            FTransform DesiredToeBase;
            bool bSolveHock=false;
            if (bRest)
            {
                const FVector TipLocal=End.InverseTransformPosition(Tip);
                DesiredEnd=ComponentToWorld.InverseTransformPosition(Leg.ActionTipWorld)-EndRotation.RotateVector(TipLocal*End.GetScale3D());
            }
            if (RestAlpha > .001f)
                DesiredEnd += ComponentToWorld.InverseTransformVector(ForwardWorld * (I<2 ? -5.0f : 3.0f) * RestAlpha + FVector(0,0,I<2 ? 1.0f*RestAlpha : 0.0f));
            // A raised bowl lets feeding retain the authored standing rear
            // quarters. Tarsal folding is reserved for an actual procedural
            // floor rest; authored sitting owns its own complete limb pose.
            const float CrouchAlpha=RestAlpha;
            if (Leg.bHasToeBase && CrouchAlpha>.001f)
            {
                const FQuat ToeRotation=ComponentToWorld.GetRotation().Inverse()*Leg.ActionToeWorldRotation;
                const FVector TipRelative=ToeBase.InverseTransformPosition(Tip)*ToeBase.GetScale3D();
                const FVector FootRelative=End.InverseTransformPosition(ToeBase.GetLocation())*End.GetScale3D();
                FVector DesiredTip=ComponentToWorld.InverseTransformPosition(Leg.ActionTipWorld);
                DesiredTip+=ComponentToWorld.InverseTransformVector(ForwardWorld*3.0f*RestAlpha);
                const FVector ToePosition=DesiredTip-ToeRotation.RotateVector(TipRelative);
                const FVector CurrentLever=EndRotation.RotateVector(FootRelative);
                const FVector WorldLever=ComponentToWorld.TransformVector(CurrentLever);
                const float Length=WorldLever.Size();
                const float Drop=FMath::Lerp(FMath::Max(1.0f,-WorldLever.Z),EatAlpha>RestAlpha ? 3.0f : 2.0f,CrouchAlpha);
                const FVector Right=FVector::CrossProduct(FVector::UpVector,ForwardWorld).GetSafeNormal();
                const float Lateral=FMath::Clamp(FVector::DotProduct(WorldLever,Right),-1.5f,1.5f);
                const float Ahead=FMath::Sqrt(FMath::Max(.01f,Length*Length-Drop*Drop-Lateral*Lateral));
                const FVector DesiredWorldLever=ForwardWorld*Ahead+Right*Lateral-FVector::UpVector*Drop;
                const FVector DesiredLever=ComponentToWorld.InverseTransformVector(DesiredWorldLever).GetSafeNormal()*CurrentLever.Size();
                EndRotation=FQuat::FindBetweenNormals(CurrentLever.GetSafeNormal(),DesiredLever.GetSafeNormal())*EndRotation;
                DesiredEnd=ToePosition-EndRotation.RotateVector(FootRelative);
                DesiredToeBase=ToeBase;
                DesiredToeBase.SetLocation(ToePosition);DesiredToeBase.SetRotation(ToeRotation);
                bSolveHock=true;
            }
            if (PawAlpha > .001f && I == PawIndex)
            {
                const FVector TipLocal=End.InverseTransformPosition(Tip);
                const FVector StrikeEnd=ComponentToWorld.InverseTransformPosition(PawTargetWorld)-EndRotation.RotateVector(TipLocal*End.GetScale3D());
                DesiredEnd=FMath::Lerp(DesiredEnd,StrikeEnd,PawAlpha);
            }
            if (bRest)
            {
                Upper.AddToTranslation(RestDelta);
                Knee.AddToTranslation(RestDelta);
            }
            const FVector OldEnd = End.GetLocation();
            // FK knee is the pole. No bone stretching, extra root offsets,
            // ankle twist or fabricated swing cycle is introduced.
            AnimationCore::SolveTwoBoneIK(Upper, Knee, End, Knee.GetLocation(), DesiredEnd, false, 1.0, 1.0);
            End.SetRotation(EndRotation);
            const FVector AchievedWorld = ComponentToWorld.TransformVector(End.GetLocation() - OldEnd);
            Corrections[I] = AchievedWorld.Size();
            Residuals[I] = Leg.bPlanted ? (Leg.Anchor - WorldTip - AchievedWorld).Size() : 0.0f;
            Transforms.Emplace(U, Upper); Transforms.Emplace(K, Knee); Transforms.Emplace(E, End);
            if (bSolveHock)
            {
                // Follow any no-stretch reach residual so Toe0 never detaches
                // from the metatarsal link while keeping the paw orientation.
                DesiredToeBase.AddToTranslation(End.GetLocation()-DesiredEnd);
                Transforms.Emplace(Leg.ToeBase.GetCompactPoseIndex(Bones),DesiredToeBase);
            }
        }
        Transforms.Sort([](const FBoneTransform& A, const FBoneTransform& B) { return A.BoneIndex < B.BoneIndex; });
    }
};

// A measured lip target, not a guessed Euler pitch. CCD rotates the three
// existing neck/head joints without stretching or translating any bone.
struct FCatActionNode final : FAnimNode_SkeletalControlBase
{
    FBoneReference Neck[3], Jaw, Tongue, Lids[4];
    FVector MuzzleLocal = FVector(-34.39959,3.01320,729.48403);
    FVector TargetWorld = FVector::ZeroVector;
    FTransform ComponentToWorld;
    float AimAlpha=0.0f, JawAlpha=0.0f, RestAlpha=0.0f;
    bool bPlayGaze=false, bFeedingAim=false;

    void Configure(const USkeletalMeshComponent* Mesh)
    {
        auto Find=[Mesh](const TCHAR* Fragment) -> FName
        {
            for (int32 I=0; I<Mesh->GetNumBones(); ++I)
            { const FName N=Mesh->GetBoneName(I); if (N.ToString().Replace(TEXT("-"),TEXT("_")).Contains(Fragment)) return N; }
            return NAME_None;
        };
        Neck[0].BoneName=Find(TEXT("cat_bip_Neck_7"));
        Neck[1].BoneName=Find(TEXT("cat_bip_Neck1_8"));
        Neck[2].BoneName=Find(TEXT("cat_bip_Head_9"));
        Jaw.BoneName=Find(TEXT("fst_Xtra01_16"));
        Tongue.BoneName=Find(TEXT("tongue_bone01_17"));
        Lids[0].BoneName=Find(TEXT("Bone03_12")); Lids[1].BoneName=Find(TEXT("Bone02(mirrored)_14"));
        Lids[2].BoneName=Find(TEXT("Bone04_13")); Lids[3].BoneName=Find(TEXT("Bone04(mirrored)_15"));
    }
    virtual void InitializeBoneReferences(const FBoneContainer& Bones) override
    { for (FBoneReference& N:Neck) N.Initialize(Bones); Jaw.Initialize(Bones); Tongue.Initialize(Bones); for (FBoneReference& L:Lids) L.Initialize(Bones); }
    virtual bool IsValidToEvaluate(const USkeleton*,const FBoneContainer& Bones) override
    { return Neck[0].IsValidToEvaluate(Bones)&&Neck[1].IsValidToEvaluate(Bones)&&Neck[2].IsValidToEvaluate(Bones); }
    virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,TArray<FBoneTransform>& Out) override
    {
        // Authored sitting keeps AimAlpha at zero so the clip owns the neck,
        // but its procedural eyelids still have to evaluate.
        if (AimAlpha<.001f && JawAlpha<.001f && RestAlpha<.001f) return;
        const FBoneContainer& Bones=Output.Pose.GetPose().GetBoneContainer();
        FTransform Original[3], Solved[3];
        for (int32 I=0; I<3; ++I) Original[I]=Solved[I]=Output.Pose.GetComponentSpaceTransform(Neck[I].GetCompactPoseIndex(Bones));
        const FVector Target=ComponentToWorld.InverseTransformPosition(TargetWorld);
        // Neck_7 also parents both clavicles on this source rig. Moving it
        // during feeding dragged both front legs forward and made the neck
        // look telescopic. The thoracic bend now supplies the body lowering;
        // feeding and ball gaze leave the shoulder girdle planted.
        const float Limits[]={bPlayGaze || bFeedingAim ? 0.0f : 35.0f,
            bPlayGaze ? 10.0f : bFeedingAim ? 36.0f : 40.0f,
            bPlayGaze ? 30.0f : bFeedingAim ? 55.0f : 65.0f};
        for (int32 Iteration=0; Iteration<8; ++Iteration)
        for (int32 J=2; J>=0; --J)
        {
            const FVector Pivot=Solved[J].GetLocation();
            const FVector A=Solved[2].TransformPosition(MuzzleLocal)-Pivot, B=Target-Pivot;
            if (A.IsNearlyZero()||B.IsNearlyZero()) continue;
            const FQuat Wanted=FQuat::FindBetweenNormals(A.GetSafeNormal(),B.GetSafeNormal())*Solved[J].GetRotation();
            FQuat Difference=Wanted*Original[J].GetRotation().Inverse(); Difference.Normalize();
            if (Difference.W<0) Difference=Difference*-1.0f;
            FVector Axis; float Angle; Difference.ToAxisAndAngle(Axis,Angle);
            const FQuat NewRotation=FQuat(Axis,FMath::Min(Angle,FMath::DegreesToRadians(Limits[J])))*Original[J].GetRotation();
            const FQuat Delta=NewRotation*Solved[J].GetRotation().Inverse();
            for (int32 Child=J; Child<3; ++Child)
            {
                Solved[Child].SetLocation(Pivot+Delta.RotateVector(Solved[Child].GetLocation()-Pivot));
                Solved[Child].SetRotation((Delta*Solved[Child].GetRotation()).GetNormalized());
            }
        }
        for (int32 I=0; I<3; ++I)
        {
            FTransform Blended; Blended.Blend(Original[I],Solved[I],AimAlpha);
            Solved[I]=Blended; Out.Emplace(Neck[I].GetCompactPoseIndex(Bones),Blended);
        }
        if (Jaw.IsValidToEvaluate(Bones))
        {
            const FTransform OriginalJaw=Output.Pose.GetComponentSpaceTransform(Jaw.GetCompactPoseIndex(Bones));
            FTransform LocalJaw=OriginalJaw.GetRelativeTransform(Original[2]);
            // Source+Z lateral maps to UE-Y for opening under glTF handedness.
            LocalJaw.SetRotation(LocalJaw.GetRotation()*FQuat(FVector(0,-1,0),FMath::DegreesToRadians(3.5f*JawAlpha)));
            const FTransform NewJaw=LocalJaw*Solved[2]; Out.Emplace(Jaw.GetCompactPoseIndex(Bones),NewJaw);
            if (Tongue.IsValidToEvaluate(Bones))
            {
                FTransform LocalTongue=Output.Pose.GetComponentSpaceTransform(Tongue.GetCompactPoseIndex(Bones)).GetRelativeTransform(OriginalJaw);
                // A small extension of the existing tongue mesh; no new face
                // geometry, jaw stretching, or fabricated contact is used.
                const FVector Outward=(Solved[2].TransformPosition(MuzzleLocal)-NewJaw.GetLocation()).GetSafeNormal();
                LocalTongue.AddToTranslation(NewJaw.InverseTransformVectorNoScale(Outward)*(80.0f*JawAlpha));
                Out.Emplace(Tongue.GetCompactPoseIndex(Bones),LocalTongue*NewJaw);
            }
        }
        const FVector LidAxis=Solved[2].GetRotation().RotateVector(FVector(0,-1,0));
        for (int32 I=0; I<4; ++I) if (Lids[I].IsValidToEvaluate(Bones))
        {
            FTransform Lid=Output.Pose.GetComponentSpaceTransform(Lids[I].GetCompactPoseIndex(Bones)).GetRelativeTransform(Original[2])*Solved[2];
            Lid.SetRotation(FQuat(LidAxis,FMath::DegreesToRadians((I<2 ? 20.0f : -10.0f)*RestAlpha))*Lid.GetRotation());
            Out.Emplace(Lids[I].GetCompactPoseIndex(Bones),Lid);
        }
        Out.Sort([](const FBoneTransform& A,const FBoneTransform& B){return A.BoneIndex<B.BoneIndex;});
    }
};

struct FCatAnimationInput
{
    UAnimSequence* Walk = nullptr;
    UAnimSequence* Idle = nullptr;
    UAnimSequence* IdleVariation = nullptr;
    UAnimSequence* SitDown = nullptr;
    UAnimSequence* SitIdle = nullptr;
    UAnimSequence* StandUp = nullptr;
    UAnimSequence* Sleep = nullptr;
    EMirmirActivity Activity = EMirmirActivity::Idle;
    float Speed = 0.0f;
    float ReferenceSpeed = 68.0f;
    float BlendSeconds = 0.25f;
    bool bPreferSitting = true;
    bool bWaitingAtToy = false;
    float HeadScale = 1.0f;
    float IdleRate = 0.35f;
    float VariationRate = 0.6f;
    bool bPetCue = false;
    bool bEnableStance = false;
    float MaximumStanceCorrection = 4.0f;
    FTransform ComponentToWorld = FTransform::Identity;
    FRotator HeadRotation = FRotator::ZeroRotator;
    bool bHabitatTraveling = false;
    bool bBackwards = false;
    float RestAlpha = 0.0f;
    float BreathingCm = 0.0f;
    FVector RestTailAxis = FVector::RightVector;
    bool bSuppressVariation = false;
    float EatAlpha=0.0f, JawAlpha=0.0f, PawAlpha=0.0f, PlayAttention=0.0f;
    int32 PawIndex=0;
    FVector ActionTargetWorld, PawTargetWorld, ForwardWorld, FeedingLeanDirection;
    bool bAuthoredRest=false;
};

class FMirmirAnimInstanceProxy final : public FAnimInstanceProxy
{
public:
    explicit FMirmirAnimInstanceProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance)
    {
        Stack.SetMaxActiveBlends(3);
        ToComponent.LocalPose.SetLinkNode(&Stack);
        Body.ComponentPose.SetLinkNode(&ToComponent);
        Body.bApplyFeet=false;
        Action.ComponentPose.SetLinkNode(&Body);
        Stance.ComponentPose.SetLinkNode(&Action);
        Stance.bApplyBody=false;
        HeadShape.ComponentPose.SetLinkNode(&Stance);
        HeadShape.ScaleMode = BMM_Additive;
        HeadShape.ScaleSpace = BCS_BoneSpace;
        HeadShape.TranslationMode = BMM_Ignore;
        HeadShape.RotationMode = BMM_Additive;
        HeadShape.RotationSpace = BCS_ComponentSpace;
        HeadShape.Scale = FVector::OneVector;
        ToLocal.ComponentPose.SetLinkNode(&HeadShape);
    }

protected:
    virtual void Initialize(UAnimInstance* Instance) override
    {
        if (const AMirmirCat* Cat = Cast<AMirmirCat>(Instance->GetOwningActor()))
        {
            HeadShape.BoneToModify.BoneName = Cat->AppearanceHeadBoneName;
            Stance.Configure(Cat->CatMesh);
            Body.Configure(Cat->CatMesh);
            Action.Configure(Cat->CatMesh);
        }
        HeadShape.Alpha = HeadShape.BoneToModify.BoneName.IsNone() ? 0.0f : 1.0f;
        FAnimInstanceProxy::Initialize(Instance);
    }
    virtual FAnimNode_Base* GetCustomRootNode() override { return &ToLocal; }
    virtual void GetCustomNodes(TArray<FAnimNode_Base*>& Nodes) override
    {
        Nodes.Add(&Stack);
        Nodes.Add(&ToComponent);
        Nodes.Add(&Body);
        Nodes.Add(&Action);
        Nodes.Add(&Stance);
        Nodes.Add(&HeadShape);
        Nodes.Add(&ToLocal);
    }

    virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override
    {
        FAnimInstanceProxy::PreUpdate(Instance, DeltaSeconds);
        Input = FCatAnimationInput();
        const AMirmirCat* Cat = Cast<AMirmirCat>(Instance->GetOwningActor());
        if (!Cat || !Cat->CatMesh) return;
        const USkeletalMesh* Mesh = Cat->CatMesh->GetSkeletalMeshAsset();
        auto Compatible = [Mesh](UAnimSequence* Clip) -> UAnimSequence*
        {
            return Clip && Mesh && Clip->GetSkeleton() == Mesh->GetSkeleton() ? Clip : nullptr;
        };
        // Actor/UObject state is copied on the game thread. The proxy owns
        // worker playback state; actor UPROPERTYs retain all source assets.
        Input.Walk = Compatible(Cat->WalkAnimation);
        Input.Idle = Compatible(Cat->IdleAnimation);
        Input.IdleVariation = Compatible(Cat->IdleVariationAnimation);
        if (Input.Idle == Input.Walk) Input.Idle = nullptr;
        Input.SitDown = Compatible(Cat->SitDownAnimation);
        Input.SitIdle = Compatible(Cat->SitIdleAnimation);
        Input.StandUp = Compatible(Cat->StandUpAnimation);
        Input.Sleep = Compatible(Cat->SleepAnimation);
        Input.Activity = Cat->Activity;
        Input.Speed = FMath::Max(0.0f, Cat->MovementSpeed);
        Input.ReferenceSpeed = FMath::Max(1.0f, Cat->AuthoredWalkSpeed);
        Input.BlendSeconds = FMath::Clamp(Cat->AnimationBlendSeconds, 0.05f, 1.0f);
        Input.bPreferSitting = Cat->bPreferSittingWhenResting;
        Input.bAuthoredRest = Input.Activity == EMirmirActivity::Sleep && Input.bPreferSitting && Input.SitIdle;
        Input.bWaitingAtToy = Cat->bWaitingAtToy;
        Input.bHabitatTraveling = Cat->bHabitatTraveling;
        Input.bBackwards = Cat->bWalkingBackwards;
        Input.RestAlpha = Cat->RestPoseAlpha;
        Input.EatAlpha = Cat->EatingPoseStrength;
        Input.JawAlpha = Cat->FeedingJawAlpha;
        Input.PawAlpha = Cat->PlayPawAlpha;
        Input.PawIndex = Cat->PlayPawIndex;
        Input.PawTargetWorld = Cat->PawTargetWorld;
        Input.ForwardWorld = Cat->GetActorForwardVector();
        Input.FeedingLeanDirection=(Cat->FeedingTargetWorld-Cat->GetActorLocation()).GetSafeNormal2D();
        Input.ActionTargetWorld = Input.EatAlpha>.001f ? Cat->FeedingTargetWorld :
            Cat->GetActorLocation()+Cat->GetActorForwardVector()*22.0f+FVector(0,0,9.0f);
        if (Cat->Activity==EMirmirActivity::Play && Cat->Toy && Cat->Toy->IsPlaying() && Input.RestAlpha<.001f && Input.EatAlpha<.001f)
        {
            Input.ActionTargetWorld=Cat->Toy->GetBallLocation()+FVector(0,0,4.0f);
            Input.PlayAttention=Cat->bWaitingAtToy ? .45f : .2f;
        }
        Input.BreathingCm = Cat->RestBreathing;
        Input.bSuppressVariation = Cat->Activity == EMirmirActivity::Sleep || Cat->Activity == EMirmirActivity::Eat;
        Input.RestTailAxis = Cat->CatMesh->GetComponentQuat().Inverse().RotateVector(Cat->GetActorRightVector());
        Input.bEnableStance = Cat->bEnableStanceIK;
        Input.MaximumStanceCorrection = FMath::Clamp(Cat->MaximumStanceCorrectionCm, 0.0f, 6.0f);
        Input.ComponentToWorld = Cat->CatMesh->GetComponentTransform();
        float Pet = FMath::Clamp(Cat->PetReactionStrength, 0.0f, 1.0f);
        float HeadYaw = Cat->CuriosityHeadYaw;
        float HeadPitch = Cat->CuriosityHeadPitch;
        const float AttentionAlpha=1.0f-FMath::Max(Input.RestAlpha,Input.EatAlpha);
        HeadYaw*=AttentionAlpha; HeadPitch*=AttentionAlpha;
        Pet*=AttentionAlpha;
        const FQuat WorldLook = FQuat(FVector::UpVector, FMath::DegreesToRadians(HeadYaw + Pet * Cat->PetStrokeDirection * 3.0f)) *
            FQuat(Cat->GetActorRightVector(), FMath::DegreesToRadians(-HeadPitch - Pet * 4.0f)) *
            FQuat(Cat->GetActorForwardVector(), FMath::DegreesToRadians(Pet * (1.0f + Cat->PetStrokeDirection * 2.0f)));
        const FQuat MeshRotation = Input.ComponentToWorld.GetRotation();
        Input.HeadRotation = (MeshRotation.Inverse() * WorldLook * MeshRotation).Rotator();
        Input.HeadScale = FMath::Clamp(Cat->AppearanceHeadScale, 1.0f, 1.1f);
        Input.IdleRate = FMath::Clamp(Cat->IdlePlayRate, 0.05f, 2.0f);
        Input.VariationRate = FMath::Clamp(Cat->IdleVariationPlayRate, 0.05f, 2.0f);
        const bool bPetted = Cat->Activity == EMirmirActivity::Petted;
        Input.bPetCue = (bPetted && !bWasPetted) || (PreviousBond >= 0.0f && Cat->Bond > PreviousBond + 0.0001f);
        bWasPetted = bPetted;
        PreviousBond = Cat->Bond;
    }

    virtual void UpdateAnimationNode(const FAnimationUpdateContext& Context) override
    {
        ECatPose Goal;
        if (Input.Speed > 1.0f && Input.Walk) Goal = ECatPose::Walk;
        else if (Input.Activity == EMirmirActivity::Sleep && !Input.bHabitatTraveling && Input.Sleep) Goal = ECatPose::Sleep;
        // Ordinary pauses use authored Idle. Sitting is an explicit rest
        // fallback when no actual sleep loop has been supplied.
        else if (Input.Activity == EMirmirActivity::Sleep && !Input.bHabitatTraveling && Input.bPreferSitting && Input.SitIdle) Goal = ECatPose::Sitting;
        else Goal = Input.Idle ? ECatPose::Idle : (Input.Walk ? ECatPose::HeldWalk : ECatPose::None);

        ECatPose Next = Goal;
        const float Delta = FMath::Max(0.0f, Context.GetDeltaTime());
        VariationCooldown = FMath::Max(0.0f, VariationCooldown - Delta);
        if (Input.bSuppressVariation) bPendingVariationCue = false;
        else if (Pose != ECatPose::IdleVariation && VariationCooldown <= 0.0f && Input.bPetCue)
            bPendingVariationCue = true;
        if (Goal == ECatPose::Idle && Pose != ECatPose::IdleVariation) IdleElapsed += Delta;
        else if (Goal != ECatPose::Idle) IdleElapsed = 0.0f;
        const float EndTolerance = FMath::Max(0.001f, Delta * FMath::Max(0.001f, ActiveRate));
        const bool bOneShotFinished = ActiveClip &&
            Stack.GetCurrentAssetTime() >= ActiveClip->GetPlayLength() - EndTolerance;
        // Commit an authored posture transition once started. The latest Goal
        // remains queued; an input cannot restart the opposite clip mid-pose.
        if (Pose == ECatPose::IdleVariation)
        {
            const bool bInterruptedByIntent = Goal != ECatPose::Idle || Input.bSuppressVariation;
            Next = bOneShotFinished || bInterruptedByIntent ? Goal : ECatPose::IdleVariation;
            // A repeated pet cannot restart or queue another copy of the same
            // variation while it is still playing.
            bPendingVariationCue = false;
            if (bOneShotFinished || bInterruptedByIntent)
            {
                IdleElapsed = 0.0f;
                NextVariationAfter = VariationRandom.FRandRange(10.0f, 14.0f);
                VariationCooldown = 5.0f;
            }
        }
        else if (Pose == ECatPose::SitDown)
        {
            if (!bOneShotFinished) Next = ECatPose::SitDown;
            else if (Goal == ECatPose::Sitting || Goal == ECatPose::Sleep) Next = Goal;
            else Next = Input.StandUp ? ECatPose::StandUp : Goal;
        }
        else if (Pose == ECatPose::StandUp)
        {
            if (!bOneShotFinished) Next = ECatPose::StandUp;
            else if (Goal == ECatPose::Sitting && Input.SitDown) Next = ECatPose::SitDown;
            else Next = Goal;
        }
        else if (Goal == ECatPose::Sitting && Pose != ECatPose::Sitting && Input.SitDown)
            Next = ECatPose::SitDown;
        else if ((Pose == ECatPose::Sitting || Pose == ECatPose::SitDown || Pose == ECatPose::Sleep)
                 && Goal != ECatPose::Sitting && Goal != ECatPose::Sleep && Input.StandUp)
            Next = ECatPose::StandUp;
        else if (Goal == ECatPose::Idle && !Input.bSuppressVariation && Input.IdleVariation && VariationCooldown <= 0.0f &&
                 (bPendingVariationCue || IdleElapsed >= NextVariationAfter))
        {
            Next = ECatPose::IdleVariation;
            IdleElapsed = 0.0f;
            bPendingVariationCue = false;
        }

        UAnimSequence* Selected = ClipFor(Next);
        const float Rate = Next == ECatPose::HeldWalk ? 0.0f
            : (Next == ECatPose::Walk ? FMath::Clamp(Input.Speed / Input.ReferenceSpeed, 0.0f, 3.0f) * (Input.bBackwards ? -1.0f : 1.0f)
                : (Next == ECatPose::Idle ? Input.IdleRate : (Next == ECatPose::IdleVariation ? Input.VariationRate : 1.0f)));
        if (Selected && (Next != Pose || Selected != ActiveClip))
        {
            const bool bLoop = Next != ECatPose::SitDown && Next != ECatPose::StandUp &&
                Next != ECatPose::HeldWalk && Next != ECatPose::IdleVariation;
            // HeldWalk is a frozen existing walk sample, not an invented clip.
            float Start = Next == ECatPose::HeldWalk ? FMath::Min(1.0f / 30.0f, Selected->GetPlayLength()) : 0.0f;
            if (Next == ECatPose::Walk && Input.bBackwards) Start = FMath::Max(.001f, Selected->GetPlayLength() - .001f);
            if (Next == ECatPose::Walk && Pose == ECatPose::HeldWalk && Selected == ActiveClip)
                Start = Stack.GetCurrentAssetTime();
            Stack.BlendTo(Context, Selected, Start, bLoop, false, nullptr,
                Pose == ECatPose::None ? 0.0f : Input.BlendSeconds,
                nullptr, EAlphaBlendOption::Cubic, false, NAME_None, FVector::ZeroVector, Rate);
            ActiveClip = Selected;
            Pose = Next;
        }
        else if (!Selected && ActiveClip)
        {
            Stack.Reset();
            ActiveClip = nullptr;
            Pose = ECatPose::None;
        }
        ActiveRate = Selected ? Rate : 0.0f;
        Stack.UpdatePlayRate(ActiveRate);
        HeadShape.Scale = FVector(Input.HeadScale);
        HeadShape.Rotation = Input.HeadRotation;
        Stance.ComponentToWorld = Input.ComponentToWorld;
        // Authored sitting owns the supported body/limbs. Keep only the face,
        // head, tail and breathing action layers; applying the old procedural
        // crouch as well produced an unmistakable play-bow.
        const float ProceduralRestAlpha=Input.bAuthoredRest ? 0.0f : Input.RestAlpha;
        Stance.RestAlpha = ProceduralRestAlpha;
        Stance.EatAlpha = Input.EatAlpha;
        Stance.PawAlpha = Input.PawAlpha;
        Stance.PawIndex = Input.PawIndex;
        Stance.PawTargetWorld = Input.PawTargetWorld;
        Stance.ForwardWorld = Input.ForwardWorld;
        Body.ComponentToWorld=Input.ComponentToWorld;
        Body.RestAlpha=ProceduralRestAlpha; Body.EatAlpha=Input.EatAlpha; Body.PawAlpha=Input.PawAlpha;
        Body.ForwardWorld=Input.EatAlpha>.001f ? Input.FeedingLeanDirection : Input.ForwardWorld;
        Body.BreathingCm=Input.BreathingCm;
        Body.RestTailAxis=Input.RestTailAxis;
        Action.ComponentToWorld=Input.ComponentToWorld;
        Action.TargetWorld=Input.ActionTargetWorld;
        Action.MuzzleLocal=FVector(-34.39959,3.01320,729.48403)*Input.HeadScale;
        PlayAttention=FMath::FInterpConstantTo(PlayAttention,Input.PlayAttention,Delta,.8f);
        // The authored sitting clip already owns a believable neck pose.
        // Closing its eyelids must not also pull the complete neck chain toward
        // the old floor-rest target (which stretched the neck vertically).
        Action.AimAlpha=FMath::Max(FMath::Max(Input.EatAlpha,
            Input.bAuthoredRest ? 0.0f : Input.RestAlpha),PlayAttention);
        Action.JawAlpha=Input.JawAlpha;
        Action.RestAlpha=Input.RestAlpha;
        Action.bPlayGaze=Input.PlayAttention>0 && Input.EatAlpha<.001f && Input.RestAlpha<.001f;
        Action.bFeedingAim=Input.EatAlpha>.001f;
        Stance.BreathingCm = Input.BreathingCm;
        Stance.RestTailAxis = Input.RestTailAxis;
        Stance.DeltaSeconds = Delta;
        Stance.MaximumCorrectionCm = Input.MaximumStanceCorrection;
        FAnimInstanceProxy::UpdateAnimationNode(Context);
        Stance.bEnabled = Input.bEnableStance && Pose == ECatPose::Walk && Input.Speed > 4.0f &&
            Stack.AnimPlayers.Num() == 1 && Input.Walk && FMath::Abs(Input.Walk->GetPlayLength() - 0.8f) < 0.035f;
        Stance.Phase = Input.Walk ? FMath::Fmod(Stack.GetCurrentAssetTime() / FMath::Max(0.001f, Input.Walk->GetPlayLength()), 1.0f) : 0.0f;
    }

    virtual void PostUpdate(UAnimInstance* Instance) const override
    {
        FAnimInstanceProxy::PostUpdate(Instance);
        if (UMirmirAnimInstance* CatInstance = Cast<UMirmirAnimInstance>(Instance))
        {
            CatInstance->StanceValidLegs = Stance.ValidLegs;
            CatInstance->StanceWeights = Stance.Weights;
            CatInstance->StanceCorrectionCm = Stance.Corrections;
            CatInstance->StanceResidualCm = Stance.Residuals;
            CatInstance->ActiveClip = ActiveClip;
            CatInstance->ActiveClipTime = Stack.GetCurrentAssetTime();
            CatInstance->ActivePlayRate = ActiveRate;
            CatInstance->ActiveBlendCount = Stack.AnimPlayers.Num();
            CatInstance->PlaybackState = PoseName(Pose);
            CatInstance->bTransitionBlocksMovement = Pose == ECatPose::SitDown ||
                Pose == ECatPose::StandUp;
        }
    }

private:
    UAnimSequence* ClipFor(ECatPose Desired) const
    {
        switch (Desired)
        {
        case ECatPose::Walk:
        case ECatPose::HeldWalk: return Input.Walk;
        case ECatPose::Idle: return Input.Idle;
        case ECatPose::IdleVariation: return Input.IdleVariation;
        case ECatPose::SitDown: return Input.SitDown;
        case ECatPose::Sitting: return Input.SitIdle;
        case ECatPose::StandUp: return Input.StandUp;
        case ECatPose::Sleep: return Input.Sleep;
        default: return nullptr;
        }
    }

    FAnimNode_BlendStack_Standalone Stack;
    FAnimNode_ConvertLocalToComponentSpace ToComponent;
    FCatStanceNode Body;
    FCatStanceNode Stance;
    FCatActionNode Action;
    FAnimNode_ModifyBone HeadShape;
    FAnimNode_ConvertComponentToLocalSpace ToLocal;
    FCatAnimationInput Input;
    ECatPose Pose = ECatPose::None;
    UAnimSequence* ActiveClip = nullptr;
    float ActiveRate = 0.0f;
    float IdleElapsed = 0.0f;
    float NextVariationAfter = 12.0f;
    float VariationCooldown = 0.0f;
    float PreviousBond = -1.0f;
    bool bWasPetted = false;
    bool bPendingVariationCue = false;
    float PlayAttention=0.0f;
    FRandomStream VariationRandom{ 0x4d49524d };
};
}

FAnimInstanceProxy* UMirmirAnimInstance::CreateAnimInstanceProxy()
{
    // Explicit Blueprint subclasses keep the usual engine graph proxy.
    return GetClass() == StaticClass() ? new FMirmirAnimInstanceProxy(this) : Super::CreateAnimInstanceProxy();
}

void UMirmirAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy)
{
    delete Proxy;
}

void UMirmirAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);
    if (const AMirmirCat* Cat = Cast<AMirmirCat>(GetOwningActor()))
    {
        Activity = Cat->Activity;
        Speed = Cat->MovementSpeed;
        Energy = Cat->Energy;
        Bond = Cat->Bond;
    }
}
