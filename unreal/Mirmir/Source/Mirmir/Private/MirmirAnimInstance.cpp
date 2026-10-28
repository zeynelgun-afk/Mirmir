#include "MirmirAnimInstance.h"
#include "MirmirHabitat.h"
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
        FBoneReference Upper, Knee, End, Tip, OtherTip;
        bool bHasOtherTip = false;
        bool bValid = false;
        bool bPlanted = false;
        FVector Anchor = FVector::ZeroVector;
        FVector AppliedDelta = FVector::ZeroVector;
    };
    FLeg Legs[4];
    bool bEnabled = false;
    float Phase = 0.0f;
    float MaximumCorrectionCm = 4.0f;
    FTransform ComponentToWorld = FTransform::Identity;
    TArray<float> Weights{ 0.0f, 0.0f, 0.0f, 0.0f };
    TArray<float> Corrections{ 0.0f, 0.0f, 0.0f, 0.0f };
    TArray<float> Residuals{ 0.0f, 0.0f, 0.0f, 0.0f };
    int32 ValidLegs = 0;
    FBoneReference Pelvis;
    FBoneReference Tail;
    FVector RestTailAxis = FVector::RightVector;
    float RestAlpha = 0.0f;
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
        for (int32 I = 0; I < 4; ++I)
        {
            Legs[I].Upper.BoneName = Find(Upper[I]);
            Legs[I].Knee.BoneName = Find(Knee[I]);
            Legs[I].End.BoneName = Find(End[I]);
            Legs[I].Tip.BoneName = Find(Tip[I]);
            Legs[I].bHasOtherTip = I < 2;
            if (I < 2) Legs[I].OtherTip.BoneName = Find(Other[I]);
        }
    }

protected:
    virtual void InitializeBoneReferences(const FBoneContainer& Bones) override
    {
        ValidLegs = 0;
        Pelvis.Initialize(Bones);
        Tail.Initialize(Bones);
        for (FLeg& Leg : Legs)
        {
            Leg.Upper.Initialize(Bones); Leg.Knee.Initialize(Bones);
            Leg.End.Initialize(Bones); Leg.Tip.Initialize(Bones);
            if (Leg.bHasOtherTip) Leg.OtherTip.Initialize(Bones);
            Leg.bValid = Leg.Upper.IsValidToEvaluate(Bones) && Leg.Knee.IsValidToEvaluate(Bones) &&
                Leg.End.IsValidToEvaluate(Bones) && Leg.Tip.IsValidToEvaluate(Bones) &&
                (!Leg.bHasOtherTip || Leg.OtherTip.IsValidToEvaluate(Bones));
            if (Leg.bValid)
            {
                // Reject another rig with superficially similar bone names.
                Leg.bValid = Bones.GetParentBoneIndex(Leg.End.GetCompactPoseIndex(Bones)) == Leg.Knee.GetCompactPoseIndex(Bones) &&
                    Bones.GetParentBoneIndex(Leg.Knee.GetCompactPoseIndex(Bones)) == Leg.Upper.GetCompactPoseIndex(Bones);
            }
            Leg.bPlanted = false;
            Leg.AppliedDelta = FVector::ZeroVector;
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
        const bool bRest = RestAlpha > 0.001f && Pelvis.IsValidToEvaluate(Bones);
        const FVector RestDelta = ComponentToWorld.InverseTransformVector(FVector(0, 0, -12.0f * RestAlpha + BreathingCm));
        if (bRest)
        {
            FTransform Root = Output.Pose.GetComponentSpaceTransform(Pelvis.GetCompactPoseIndex(Bones));
            Root.AddToTranslation(RestDelta);
            Transforms.Emplace(Pelvis.GetCompactPoseIndex(Bones), Root);
            if (Tail.IsValidToEvaluate(Bones))
            {
                FTransform RelaxedTail = Output.Pose.GetComponentSpaceTransform(Tail.GetCompactPoseIndex(Bones));
                RelaxedTail.AddToTranslation(RestDelta);
                RelaxedTail.SetRotation(FQuat(RestTailAxis.GetSafeNormal(), FMath::DegreesToRadians(-55.0f * RestAlpha)) * RelaxedTail.GetRotation());
                Transforms.Emplace(Tail.GetCompactPoseIndex(Bones), RelaxedTail);
            }
        }
        for (int32 I = 0; I < 4; ++I)
        {
            FLeg& Leg = Legs[I];
            float Position = FMath::Fmod(Phase - Starts[I] + 1.0f, 1.0f);
            float Weight = bEnabled && Position < Lengths[I]
                ? FMath::Min(FMath::Clamp(Position / 0.055f, 0.0f, 1.0f), FMath::Clamp((Lengths[I] - Position) / 0.055f, 0.0f, 1.0f)) : 0.0f;
            Weight = Weight * Weight * (3.0f - 2.0f * Weight);
            Weights[I] = Weight; Corrections[I] = 0.0f; Residuals[I] = 0.0f;
            if (Weight <= 0.001f || bRest) Leg.bPlanted = false;
            if (!bEnabled && !bRest && Leg.AppliedDelta.IsNearlyZero(.001f)) continue;
            const FCompactPoseBoneIndex U = Leg.Upper.GetCompactPoseIndex(Bones), K = Leg.Knee.GetCompactPoseIndex(Bones), E = Leg.End.GetCompactPoseIndex(Bones);
            FTransform Upper = Output.Pose.GetComponentSpaceTransform(U);
            FTransform Knee = Output.Pose.GetComponentSpaceTransform(K);
            FTransform End = Output.Pose.GetComponentSpaceTransform(E);
            FVector Tip = Output.Pose.GetComponentSpaceTransform(Leg.Tip.GetCompactPoseIndex(Bones)).GetLocation();
            if (Leg.bHasOtherTip) Tip = (Tip + Output.Pose.GetComponentSpaceTransform(Leg.OtherTip.GetCompactPoseIndex(Bones)).GetLocation()) * 0.5;
            const FVector WorldTip = ComponentToWorld.TransformPosition(Tip);
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
            const FVector DesiredEnd = End.GetLocation() + (bRest ? FVector::ZeroVector : DeltaComponent);
            if (bRest)
            {
                Upper.AddToTranslation(RestDelta);
                Knee.AddToTranslation(RestDelta);
            }
            const FQuat EndRotation = End.GetRotation();
            const FVector OldEnd = End.GetLocation();
            // FK knee is the pole. No bone stretching, extra root offsets,
            // ankle twist or fabricated swing cycle is introduced.
            AnimationCore::SolveTwoBoneIK(Upper, Knee, End, Knee.GetLocation(), DesiredEnd, false, 1.0, 1.0);
            End.SetRotation(EndRotation);
            const FVector AchievedWorld = ComponentToWorld.TransformVector(End.GetLocation() - OldEnd);
            Corrections[I] = AchievedWorld.Size();
            Residuals[I] = Leg.bPlanted ? (Leg.Anchor - WorldTip - AchievedWorld).Size() : 0.0f;
            Transforms.Emplace(U, Upper); Transforms.Emplace(K, Knee); Transforms.Emplace(E, End);
        }
        Transforms.Sort([](const FBoneTransform& A, const FBoneTransform& B) { return A.BoneIndex < B.BoneIndex; });
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
};

class FMirmirAnimInstanceProxy final : public FAnimInstanceProxy
{
public:
    explicit FMirmirAnimInstanceProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance)
    {
        Stack.SetMaxActiveBlends(3);
        ToComponent.LocalPose.SetLinkNode(&Stack);
        Stance.ComponentPose.SetLinkNode(&ToComponent);
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
        }
        HeadShape.Alpha = HeadShape.BoneToModify.BoneName.IsNone() ? 0.0f : 1.0f;
        FAnimInstanceProxy::Initialize(Instance);
    }
    virtual FAnimNode_Base* GetCustomRootNode() override { return &ToLocal; }
    virtual void GetCustomNodes(TArray<FAnimNode_Base*>& Nodes) override
    {
        Nodes.Add(&Stack);
        Nodes.Add(&ToComponent);
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
        Input.bWaitingAtToy = Cat->bWaitingAtToy;
        Input.bHabitatTraveling = Cat->bHabitatTraveling;
        Input.bBackwards = Cat->bWalkingBackwards;
        Input.RestAlpha = Cat->RestPoseAlpha;
        Input.BreathingCm = Cat->RestBreathing;
        Input.bSuppressVariation = Cat->Activity == EMirmirActivity::Sleep || Cat->Activity == EMirmirActivity::Eat;
        Input.RestTailAxis = Cat->CatMesh->GetComponentQuat().Inverse().RotateVector(Cat->GetActorRightVector());
        Input.bEnableStance = Cat->bEnableStanceIK;
        Input.MaximumStanceCorrection = FMath::Clamp(Cat->MaximumStanceCorrectionCm, 0.0f, 6.0f);
        Input.ComponentToWorld = Cat->CatMesh->GetComponentTransform();
        const float Pet = FMath::Clamp(Cat->PetReactionStrength, 0.0f, 1.0f);
        float HeadYaw = Cat->CuriosityHeadYaw;
        float HeadPitch = Cat->CuriosityHeadPitch - Input.RestAlpha * 12.0f;
        if (Cat->Habitat && Cat->EatingPoseStrength > 0.0f)
        {
            const FVector Head = Cat->CatMesh->GetBoneLocation(Cat->AppearanceHeadBoneName);
            const FVector TowardBowl = Cat->GetActorQuat().UnrotateVector(Cat->Habitat->GetBowlLocation() - Head);
            const float BowlYaw = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(TowardBowl.Y, TowardBowl.X)), -45.0f, 45.0f);
            HeadYaw = FMath::Lerp(HeadYaw, BowlYaw, Cat->EatingPoseStrength);
            HeadPitch = FMath::Lerp(HeadPitch, -34.0f + 2.5f * FMath::Sin(Cat->GetWorld()->GetTimeSeconds() * 10.0f), Cat->EatingPoseStrength);
        }
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
        const bool bWantsMovement = Input.Activity == EMirmirActivity::Wander ||
            (Input.Activity == EMirmirActivity::Play && !Input.bWaitingAtToy) || Input.bHabitatTraveling;
        ECatPose Goal;
        if ((bWantsMovement || Input.Speed > 1.0f) && Input.Walk) Goal = ECatPose::Walk;
        else if (Input.Activity == EMirmirActivity::Sleep && Input.Sleep) Goal = ECatPose::Sleep;
        // Ordinary pauses use authored Idle. Sitting is an explicit rest
        // fallback when no actual sleep loop has been supplied.
        else if (Input.Activity == EMirmirActivity::Sleep && Input.bPreferSitting && Input.SitIdle) Goal = ECatPose::Sitting;
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
        Stance.RestAlpha = Input.RestAlpha;
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
    FCatStanceNode Stance;
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
