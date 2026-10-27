#include "MirmirCat.h"
#include "MirmirToy.h"
#include "MirmirHabitat.h"
#include "MirmirSaveGame.h"
#include "MirmirAnimInstance.h"
#include "Components/AudioComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/DateTime.h"
#include "Sound/SoundBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

AMirmirCat::AMirmirCat()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("CatRoot"));
    CatMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CatMesh"));
    CatMesh->SetupAttachment(RootComponent);
    CatMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    CatMesh->SetCollisionObjectType(ECC_WorldDynamic);
    CatMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    CatMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    CatMesh->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
}

void AMirmirCat::BeginPlay()
{
    Super::BeginPlay();
    // Isolated contact calibration; never changes the saved cat profile.
    if (FParse::Param(FCommandLine::Get(), TEXT("MirmirNoStanceIK"))) bEnableStanceIK = false;
    if (CatSkeletalMesh) CatMesh->SetSkeletalMesh(CatSkeletalMesh);
    if (bNormalizeImportedMesh && CatMesh->GetSkeletalMeshAsset()) NormalizeMesh();
    CatMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    // Native graph input must see this frame's movement, not the previous
    // speed. Component ticks do not automatically wait for their owning actor.
    CatMesh->AddTickPrerequisiteActor(this);
    AssembleAdditionalMeshes();
    HomeLocation = GetActorLocation();
    WanderTarget = HomeLocation;
    CatMesh->OnClicked.AddDynamic(this, &AMirmirCat::HandleClicked);
    UMirmirSaveGame* Save = NewObject<UMirmirSaveGame>(this);
    FString Error;
    bCanSave = UMirmirSaveGame::Load(*Save, Error);
    if (bCanSave)
    {
        Behavior.Restore(Save->CatState);
        bMuted = Save->bMuted;
        AppearanceId = Save->AppearanceId;
        if (Save->SavedAtUnixSeconds > 0)
        {
            Behavior.ApplyOffline(static_cast<double>(
                FDateTime::UtcNow().ToUnixTimestamp() - Save->SavedAtUnixSeconds));
        }
        PersistenceStatus = TEXT("Ready");
    }
    else
    {
        PersistenceStatus = Error + TEXT(" Saving disabled until explicit ResetCat.");
        UE_LOG(LogTemp, Warning, TEXT("Mirmir: %s"), *PersistenceStatus);
    }
    CacheAppearanceMaterials();
    AssembleFurShells();
    ApplyAppearance();
    RefreshState();
    MovementRandom.Initialize(static_cast<int32>(Behavior.Get().RandomState));
    bDesktopMode = FParse::Param(FCommandLine::Get(), TEXT("MirmirDesktop"));
    if (bDesktopMode)
    {
        IFileManager::Get().MakeDirectory(*FPaths::Combine(
            FPaths::GetPath(UMirmirSaveGame::SavePath()), TEXT("commands")), true);
        WriteDesktopStatus();
    }
    if (!CatMesh->GetSkeletalMeshAsset())
    {
        UE_LOG(LogTemp, Warning, TEXT("Mirmir: no licensed cat mesh assigned. See README asset import steps."));
        if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 20.0f, FColor::Yellow,
            TEXT("Mirmir source starter: assign a licensed cat mesh and animations; no default cat asset included."));
    }
}

void AMirmirCat::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    VoiceCooldown = FMath::Max(0.0f, VoiceCooldown - DeltaSeconds);
    if (!bWelcomed && VoiceCooldown <= 0)
    {
        bWelcomed = true;
        Meow();
    }
    Behavior.Advance(DeltaSeconds, !bHabitatTraveling);
    RefreshState();
    const FVector Before = GetActorLocation();
    const float MovementDelta = FMath::Clamp(DeltaSeconds, 0.0f, 0.1f);
    UpdateHabitat(MovementDelta);
    UpdateCuriosity(MovementDelta);
    const bool bVisitAllowed = Activity == EMirmirActivity::Idle || Activity == EMirmirActivity::Wander ||
        Activity == EMirmirActivity::SeekAttention;
    const bool bCuriosityVisit = bEnableCuriosityApproach && bVisitAllowed &&
        CuriosityVisitRemaining > 0.0f && CuriosityWarmup <= 0.0f;
    const bool bWantsTravel = Activity == EMirmirActivity::Wander || Activity == EMirmirActivity::Play || bCuriosityVisit || bHabitatTraveling;
    if (const UMirmirAnimInstance* Animation = Cast<UMirmirAnimInstance>(CatMesh->GetAnimInstance()))
    {
        const bool bMustRise = bWantsTravel && StandUpAnimation &&
            (Animation->PlaybackState == TEXT("Sitting") || Animation->PlaybackState == TEXT("SitDown") ||
             Animation->PlaybackState == TEXT("Sleep"));
        bAnimationTransitionBlocksMovement = Animation->bTransitionBlocksMovement || bMustRise ||
            (RestPoseAlpha > 0.02f && Activity != EMirmirActivity::Sleep);
    }
    if (bToyStartPending && !bAnimationTransitionBlocksMovement && Toy && Activity == EMirmirActivity::Play)
    {
        Toy->BeginPlayWithCat(this);
        bToyStartPending = false;
    }
    const bool bChasingToy = Activity == EMirmirActivity::Play && Toy && Toy->IsPlaying();
    if (!bChasingToy) bWaitingAtToy = false;
    else
    {
        const float Distance = FVector::Dist2D(Toy->GetBallLocation(), Before);
        if (Distance <= 32.0f) bWaitingAtToy = true;
        else if (Distance > 37.0f) bWaitingAtToy = false;
    }
    const ECollisionEnabled::Type DesiredCollision = bChasingToy
        ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::QueryOnly;
    if (CatMesh->GetCollisionEnabled() != DesiredCollision) CatMesh->SetCollisionEnabled(DesiredCollision);
    bool bTravel = bWantsTravel && !bAnimationTransitionBlocksMovement;
    FVector Target = WanderTarget;
    if (bTravel)
    {
        if (bHabitatTraveling)
        {
            Target = HabitatSteeringTarget();
        }
        else if (bChasingToy)
        {
            const FVector Ball = Toy->GetBallLocation();
            const FVector ToBall = (Ball - Before).GetSafeNormal2D();
            Target = FVector(Ball.X, Ball.Y, Before.Z) - ToBall * 32.0f;
            bTravel = !bWaitingAtToy;
        }
        else if (bCuriosityVisit)
        {
            const FVector ToInterest = (CuriosityTarget - Before).GetSafeNormal2D();
            Target = CuriosityTarget - ToInterest * 35.0f;
            if (FVector::Dist2D(CuriosityTarget, Before) <= 38.0f) bTravel = false;
        }
        else
        {
            // Pick the next waypoint before braking to a full stop. Samples are
            // ahead of the cat, with an inward bias near its roaming boundary.
            if (FVector::DistSquared2D(Before, WanderTarget) < FMath::Square(18.0f)) ChooseWanderTarget();
            Target = WanderTarget;
        }
    }
    const FVector ToTarget = Target - Before;
    const FVector Forward = GetActorForwardVector().GetSafeNormal2D();
    const FVector Right = GetActorRightVector().GetSafeNormal2D();
    Mirmir::LocomotionSettings Settings;
    Settings.MinimumRadius = FMath::Clamp(MinimumTurnRadius, 45.0f, 100.0f);
    const float Cruise = FMath::Max(0.0f, WalkSpeed) * (Activity == EMirmirActivity::Play ? 1.6f : 1.0f);
    if (bAnimationTransitionBlocksMovement)
    {
        Locomotion.Speed = 0.0;
        Locomotion.YawRate = 0.0;
    }
    const Mirmir::LocomotionStep Step = Mirmir::AdvanceLocomotion(Locomotion,
        FVector::DotProduct(ToTarget, Forward), FVector::DotProduct(ToTarget, Right),
        bCuriosityVisit ? FMath::Min(Cruise, 25.0f) : Cruise, bTravel, MovementDelta, Settings);
    SetActorLocation(Before + Forward * Step.Forward + Right * Step.Right);
    if (Habitat)
    {
        FVector Position = GetActorLocation();
        const float BedDistance = FVector::Dist2D(Position, Habitat->GetBedLocation());
        const float FloorZ = BedDistance < 32.0f ? Habitat->GetBedLocation().Z : HomeLocation.Z;
        Position.Z = FMath::FInterpConstantTo(Position.Z, FloorZ, MovementDelta, 12.0f);
        SetActorLocation(Position);
    }
    if (FMath::Abs(Step.Yaw) > 0.0000001)
        SetActorRotation(FRotator(0.0f, GetActorRotation().Yaw + FMath::RadiansToDegrees(Step.Yaw), 0.0f));
    MovementSpeed = DeltaSeconds > SMALL_NUMBER ? FVector::Dist2D(Before, GetActorLocation()) / DeltaSeconds : 0.0f;
    LocomotionYawRate = DeltaSeconds > SMALL_NUMBER ? Step.Yaw / DeltaSeconds : 0.0f;
    LocomotionCurvature = MovementSpeed > 0.01f ? LocomotionYawRate / MovementSpeed : 0.0f;
    UpdateAuthoredAnimation();
    OnVisualUpdate(DeltaSeconds, MovementSpeed);
    SaveElapsed += DeltaSeconds;
    if (SaveElapsed >= 30.0f)
    {
        SaveElapsed = 0;
        SaveCat();
    }
    if (bDesktopMode)
    {
        DesktopCommandElapsed += DeltaSeconds;
        DesktopStatusElapsed += DeltaSeconds;
        if (DesktopCommandElapsed >= 0.1f)
        {
            DesktopCommandElapsed = 0;
            PollDesktopCommands();
        }
        if (DesktopStatusElapsed >= 1.0f)
        {
            DesktopStatusElapsed = 0;
            WriteDesktopStatus();
        }
    }
}

void AMirmirCat::BeginHabitatApproach()
{
    bHabitatTraveling = false;
    bHabitatArrived = false;
    HabitatWaypoints.Reset();
    HabitatWaypointIndex = 0;
    if (!Habitat || (Activity != EMirmirActivity::Eat && Activity != EMirmirActivity::Sleep)) return;
    const FVector Start = GetActorLocation();
    const FVector Forward = GetActorForwardVector().GetSafeNormal2D();
    const FVector Left(-Forward.Y, Forward.X, 0.0);
    const FVector Station = Activity == EMirmirActivity::Eat ? Habitat->GetBowlLocation() : Habitat->GetBedLocation();
    FVector Direction = (Station - Start).GetSafeNormal2D();
    if (Direction.IsNearlyZero()) Direction = Forward;
    HabitatDestination = Station - (Activity == EMirmirActivity::Eat ? Direction * 27.0f : FVector::ZeroVector);
    HabitatDestination.Z = HomeLocation.Z;
    if (FVector::Dist2D(Start, HabitatDestination) <= 12.0f)
    {
        bHabitatArrived = true;
        Behavior.RestartActivity(Activity == EMirmirActivity::Eat ? 6.0 : 30.0);
        return;
    }
    // A finite forward circular arc and its tangent line reach even a nearby
    // rearward station. Chasing a point inside the turning circle can orbit.
    const double Radius = FMath::Clamp(MinimumTurnRadius, 45.0f, 100.0f);
    double BestLength = TNumericLimits<double>::Max();
    FVector BestCenter, BestTangent;
    double BestStartAngle = 0.0, BestSweep = 0.0, BestSign = 1.0;
    for (int32 Sign : { -1, 1 })
    {
        const FVector Center = Start + Left * (Sign * Radius);
        const FVector ToGoal = HabitatDestination - Center;
        const double Distance = ToGoal.Size2D();
        if (Distance < Radius + .001) continue;
        const double StartAngle = FMath::Atan2(Start.Y - Center.Y, Start.X - Center.X);
        const double EndAngle = FMath::Atan2(ToGoal.Y, ToGoal.X) - Sign * FMath::Acos(FMath::Clamp(Radius / Distance, -1.0, 1.0));
        double Sweep = FMath::Fmod(Sign * (EndAngle - StartAngle) + UE_TWO_PI, UE_TWO_PI);
        if (Sweep < 0.0) Sweep += UE_TWO_PI;
        const FVector Tangent = Center + FVector(FMath::Cos(EndAngle), FMath::Sin(EndAngle), 0) * Radius;
        const double Length = Sweep * Radius + FVector::Dist2D(Tangent, HabitatDestination);
        if (Length < BestLength)
        {
            BestLength = Length; BestCenter = Center; BestTangent = Tangent;
            BestStartAngle = StartAngle; BestSweep = Sweep; BestSign = Sign;
        }
    }
    if (BestLength == TNumericLimits<double>::Max()) return;
    const int32 ArcSamples = FMath::Max(1, FMath::CeilToInt(BestSweep * Radius / 8.0));
    for (int32 I = 1; I <= ArcSamples; ++I)
    {
        const double Angle = BestStartAngle + BestSign * BestSweep * I / ArcSamples;
        FVector Point = BestCenter + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * Radius;
        Point.Z = HomeLocation.Z;
        HabitatWaypoints.Add(Point);
    }
    const int32 LineSamples = FMath::Max(1, FMath::CeilToInt(FVector::Dist2D(BestTangent, HabitatDestination) / 8.0));
    for (int32 I = 1; I <= LineSamples; ++I)
    {
        FVector Point = FMath::Lerp(BestTangent, HabitatDestination, double(I) / LineSamples);
        Point.Z = HomeLocation.Z;
        HabitatWaypoints.Add(Point);
    }
    bHabitatTraveling = true;
    if (Activity == EMirmirActivity::Eat) Habitat->SetFoodVisible(true);
}

FVector AMirmirCat::HabitatSteeringTarget()
{
    if (HabitatWaypoints.IsEmpty()) return HabitatDestination;
    const FVector Position = GetActorLocation();
    float Closest = TNumericLimits<float>::Max();
    int32 Best = HabitatWaypointIndex;
    const int32 End = FMath::Min(HabitatWaypoints.Num(), HabitatWaypointIndex + 16);
    for (int32 I = HabitatWaypointIndex; I < End; ++I)
    {
        const float Distance = FVector::DistSquared2D(Position, HabitatWaypoints[I]);
        if (Distance < Closest) { Closest = Distance; Best = I; }
    }
    HabitatWaypointIndex = Best;
    return HabitatWaypoints[FMath::Min(Best + 6, HabitatWaypoints.Num() - 1)];
}

void AMirmirCat::UpdateHabitat(float DeltaSeconds)
{
    PoseSeconds += DeltaSeconds;
    if (bHabitatTraveling)
    {
        Behavior.KeepActivityFor(30.0);
        if (HabitatWaypointIndex >= HabitatWaypoints.Num() - 8 &&
            FVector::Dist2D(GetActorLocation(), HabitatDestination) <= 12.0f)
        {
            bHabitatTraveling = false;
            bHabitatArrived = true;
            Behavior.RestartActivity(Activity == EMirmirActivity::Eat ? 6.0 : 30.0);
            InteractionMessage = Activity == EMirmirActivity::Eat ? TEXT("Mırmır mamasını yiyor.") : TEXT("Mırmır yatağında dinleniyor.");
            LastInteractionAction = Activity == EMirmirActivity::Eat ? TEXT("feed") : TEXT("rest");
            bLastInteractionAccepted = true;
            ++InteractionSerial;
        }
    }
    const bool bResting = Activity == EMirmirActivity::Sleep && bHabitatArrived && !bHabitatTraveling && MovementSpeed < 1.0f;
    RestPoseAlpha = FMath::FInterpConstantTo(RestPoseAlpha, bResting ? 1.0f : 0.0f,
        DeltaSeconds, bResting ? .65f : 1.1f);
    RestBreathing = RestPoseAlpha * FMath::Sin(PoseSeconds * UE_TWO_PI * .24) * .12f;
    const bool bEating = Activity == EMirmirActivity::Eat && bHabitatArrived && !bHabitatTraveling && MovementSpeed < 1.0f;
    EatingPoseStrength = FMath::FInterpConstantTo(EatingPoseStrength, bEating ? 1.0f : 0.0f, DeltaSeconds, 2.0f);
}

void AMirmirCat::ChooseWanderTarget()
{
    const FVector Position = GetActorLocation();
    const float Radius = FMath::Max(60.0f, WanderRadius);
    if (FVector::Dist2D(Position, HomeLocation) > Radius * 0.75f)
    {
        // A point inside the minimum turn circle can create a permanent orbit.
        // Travel through home toward the far side, leaving room to complete
        // the forward arc instead of chasing an unreachable center point.
        WanderTarget = HomeLocation + (HomeLocation - Position).GetSafeNormal2D() * 75.0f;
        return;
    }
    float BestCost = TNumericLimits<float>::Max();
    for (int32 Attempt = 0; Attempt < 12; ++Attempt)
    {
        const float Offset = MovementRandom.FRandRange(-55.0f, 55.0f);
        const float Angle = GetActorRotation().Yaw + Offset;
        const float MinimumDistance = FMath::Max(90.0f,
            2.0f * FMath::Clamp(MinimumTurnRadius, 45.0f, 100.0f) * FMath::Abs(FMath::Sin(FMath::DegreesToRadians(Offset))) + 20.0f);
        const FVector Candidate = Position + FRotator(0.0f, Angle, 0.0f).Vector() * MovementRandom.FRandRange(MinimumDistance, MinimumDistance + 25.0f);
        const float HomeDistance = FVector::Dist2D(Candidate, HomeLocation);
        const float Cost = FMath::Max(0.0f, HomeDistance - Radius * 0.7f) * 10.0f + HomeDistance * 0.1f;
        if (Cost < BestCost) { BestCost = Cost; WanderTarget = Candidate; }
    }
}

void AMirmirCat::SetCuriosityTarget(const FVector& Target, bool bActive)
{
    if (!bActive || Target.ContainsNaN()) { bCuriosityTargetActive = false; return; }
    const FVector Offset = Target - GetActorLocation();
    if (Offset.Size2D() > 160.0f) { bCuriosityTargetActive = false; return; }
    CuriosityTarget = Target;
    CuriosityTarget.Z = HomeLocation.Z;
    const FVector FromHome = CuriosityTarget - HomeLocation;
    if (FromHome.Size2D() > FMath::Max(60.0f, WanderRadius))
        CuriosityTarget = HomeLocation + FromHome.GetSafeNormal2D() * FMath::Max(60.0f, WanderRadius);
    // A 5Hz sample refresh must not restart the visit or prolong it indefinitely.
    if (!bCuriosityTargetActive)
    {
        CuriosityVisitRemaining = 4.0f;
        CuriosityWarmup = 0.75f;
    }
    bCuriosityTargetActive = true;
}

void AMirmirCat::UpdateCuriosity(float DeltaSeconds)
{
    PetReactionRemaining = FMath::Max(0.0f, PetReactionRemaining - DeltaSeconds);
    const float PetEnvelope = FMath::Clamp(PetReactionRemaining / 0.8f, 0.0f, 1.0f);
    PetReactionStrength = FMath::FInterpConstantTo(PetReactionStrength, PetEnvelope, DeltaSeconds,
        PetEnvelope > PetReactionStrength ? 5.0f : 1.25f);
    CuriosityVisitRemaining = FMath::Max(0.0f, CuriosityVisitRemaining - DeltaSeconds);
    CuriosityWarmup = FMath::Max(0.0f, CuriosityWarmup - DeltaSeconds);
    float DesiredYaw = 0.0f, DesiredPitch = 0.0f;
    if (bCuriosityTargetActive)
    {
        const FVector Origin = !AppearanceHeadBoneName.IsNone()
            ? CatMesh->GetBoneLocation(AppearanceHeadBoneName) : GetActorLocation() + FVector(0, 0, 30);
        const FVector Direction = GetActorQuat().UnrotateVector(CuriosityTarget - Origin);
        DesiredYaw = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X)), -25.0f, 25.0f);
        DesiredPitch = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(Direction.Z, Direction.Size2D())), -12.0f, 12.0f);
    }
    CuriosityHeadYaw = FMath::FInterpConstantTo(CuriosityHeadYaw, DesiredYaw, DeltaSeconds, 45.0f);
    CuriosityHeadPitch = FMath::FInterpConstantTo(CuriosityHeadPitch, DesiredPitch, DeltaSeconds, 30.0f);
}

void AMirmirCat::UpdateAuthoredAnimation()
{
    UClass* ExistingClass = CatMesh->GetAnimClass();
    if (!bUseNativeAnimationBlending && ExistingClass == UMirmirAnimInstance::StaticClass())
    {
        CatMesh->SetAnimInstanceClass(nullptr);
        ExistingClass = nullptr;
    }
    if (bUseNativeAnimationBlending && (!ExistingClass || ExistingClass == UMirmirAnimInstance::StaticClass()))
    {
        if (ExistingClass != UMirmirAnimInstance::StaticClass())
            CatMesh->SetAnimInstanceClass(UMirmirAnimInstance::StaticClass());
        if (const UMirmirAnimInstance* Instance = Cast<UMirmirAnimInstance>(CatMesh->GetAnimInstance()))
        {
            ActiveSequence = Instance->ActiveClip;
            bAnimationTransitionBlocksMovement = Instance->bTransitionBlocksMovement;
        }
        return;
    }
    bAnimationTransitionBlocksMovement = false;
    // A user-selected AnimBlueprint owns animation completely. Otherwise use
    // authored clips, never procedural leg rotation or a walk cycle at rest.
    if (CatMesh->GetAnimationMode() == EAnimationMode::AnimationBlueprint && CatMesh->GetAnimClass()) return;
    const bool bMoving = MovementSpeed > 1.0f;
    UAnimSequence* RestAnimation = IdleAnimation.Get() != WalkAnimation.Get() ? IdleAnimation.Get() : nullptr;
    const bool bHoldAuthoredWalkPose = !bMoving && !RestAnimation && WalkAnimation;
    UAnimSequence* Selected = bMoving ? WalkAnimation.Get()
        : (RestAnimation ? RestAnimation : WalkAnimation.Get());
    if (Selected != ActiveSequence.Get() || CatMesh->GetAnimationMode() != EAnimationMode::AnimationSingleNode)
    {
        CatMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
        ActiveSequence = Selected;
        if (Selected) CatMesh->PlayAnimation(Selected, true);
        else
        {
            CatMesh->Stop();
            CatMesh->SetAnimation(nullptr); // No assigned authored asset at all.
        }
    }
    if (Selected)
    {
        if (bHoldAuthoredWalkPose)
        {
            // This is a held existing walk sample, not an idle/sleep/groom clip.
            // Source joint review found the 1/30 s pose closest to planted paws;
            // the imported reference pose has a visibly lifted leg.
            CatMesh->SetPosition(FMath::Min(1.0f / 30.0f, Selected->GetPlayLength()), false);
            CatMesh->Stop();
            return;
        }
        if (UAnimSingleNodeInstance* Instance = CatMesh->GetSingleNodeInstance())
            Instance->SetPlaying(true); // Resume from a held phase without restarting.
        const float Rate = Selected == WalkAnimation.Get() && bMoving
            ? FMath::Clamp(MovementSpeed / FMath::Max(1.0f, AuthoredWalkSpeed), 0.01f, 3.0f) : 1.0f;
        CatMesh->SetPlayRate(Rate);
    }
}

void AMirmirCat::NormalizeMesh()
{
    const USkeletalMesh* Mesh = CatMesh->GetSkeletalMeshAsset();
    const FBoxSphereBounds Bounds = Mesh->GetBounds();
    const double Length = 2.0 * FMath::Max(Bounds.BoxExtent.X, Bounds.BoxExtent.Y);
    if (!FMath::IsFinite(Length) || Length < 0.001) return;
    const double Scale = FMath::Clamp(TargetBodyLength, 20.0f, 150.0f) / Length;
    const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
    int32 Head = INDEX_NONE, Pelvis = INDEX_NONE;
    for (int32 Bone = 0; Bone < Skeleton.GetNum(); ++Bone)
    {
        const FString Name = Skeleton.GetBoneName(Bone).ToString();
        if (Head == INDEX_NONE && Name.Contains(TEXT("Head"))) Head = Bone;
        if (Pelvis == INDEX_NONE && Name.Contains(TEXT("Pelvis"))) Pelvis = Bone;
    }
    const auto& Poses = Skeleton.GetRefBonePose();
    auto GlobalRest = [&](int32 Bone)
    {
        FTransform Transform(Poses[Bone]);
        for (int32 Parent = Skeleton.GetParentIndex(Bone); Parent != INDEX_NONE; Parent = Skeleton.GetParentIndex(Parent))
            Transform *= FTransform(Poses[Parent]);
        return Transform.GetLocation();
    };
    float Yaw = Bounds.BoxExtent.Y > Bounds.BoxExtent.X ? -90.0f : 0.0f;
    if (Head != INDEX_NONE && Pelvis != INDEX_NONE)
    {
        const FVector Forward = GlobalRest(Head) - GlobalRest(Pelvis);
        if (Forward.SizeSquared2D() > 0.001)
            Yaw = -FMath::RadiansToDegrees(FMath::Atan2(Forward.Y, Forward.X));
    }
    const FRotator Rotation(0, Yaw, 0);
    const FVector Offset(-Bounds.Origin.X * Scale, -Bounds.Origin.Y * Scale,
        -(Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale);
    CatMesh->SetRelativeScale3D(FVector(Scale));
    CatMesh->SetRelativeRotation(Rotation);
    CatMesh->SetRelativeLocation(Rotation.RotateVector(Offset));
}

void AMirmirCat::AssembleAdditionalMeshes()
{
    USkeletalMesh* PrimaryMesh = CatMesh->GetSkeletalMeshAsset();
    if (!PrimaryMesh) return;
    TSet<USkeletalMesh*> AssignedMeshes;
    AssignedMeshes.Add(PrimaryMesh);
    for (USkeletalMesh* DetailMesh : AdditionalSkeletalMeshes)
    {
        if (!IsValid(DetailMesh) || AssignedMeshes.Contains(DetailMesh)) continue;
        AssignedMeshes.Add(DetailMesh);
        if (DetailMesh->GetSkeleton() != PrimaryMesh->GetSkeleton())
        {
            UE_LOG(LogTemp, Warning, TEXT("Mirmir: skipping detail mesh %s because its skeleton differs from the primary cat mesh."),
                *GetNameSafe(DetailMesh));
            continue;
        }
        USkeletalMeshComponent* Detail = NewObject<USkeletalMeshComponent>(this);
        AddInstanceComponent(Detail);
        Detail->SetupAttachment(CatMesh);
        Detail->SetRelativeTransform(FTransform::Identity);
        Detail->SetSkeletalMesh(DetailMesh);
        Detail->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Detail->SetCollisionResponseToAllChannels(ECR_Ignore);
        Detail->SetGenerateOverlapEvents(false);
        Detail->SetCanEverAffectNavigation(false);
        Detail->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        Detail->bUseBoundsFromLeaderPoseComponent = true;
        // Reuse the primary mesh's authored bone pose, with no separate
        // animation evaluation or independent normalization on the details.
        Detail->SetLeaderPoseComponent(CatMesh, true, false);
        Detail->RegisterComponent();
        AdditionalMeshComponents.Add(Detail);
    }
}

void AMirmirCat::CacheAppearanceMaterials()
{
    if (bAppearanceMaterialCacheAttempted) return;
    bAppearanceMaterialCacheAttempted = true;
    bAppearanceReady = false;
    AppearanceBaseTransform = CatMesh->GetRelativeTransform();
    if (const USkeletalMesh* Mesh = CatMesh->GetSkeletalMeshAsset())
    {
        const FBoxSphereBounds Bounds = Mesh->GetBounds();
        AppearanceMeshOrigin = Bounds.Origin;
        // Actual source/editor anatomy audit: Object_7 length Y, width X, up Z.
        AppearanceWidthAxis = Mesh->GetName() == TEXT("Object_7") ? 0
            : (Bounds.BoxExtent.X > Bounds.BoxExtent.Y ? 1 : 0);
        const FReferenceSkeleton& Bones = Mesh->GetRefSkeleton();
        for (int32 Index = 0; Index < Bones.GetNum(); ++Index)
        {
            const FName Name = Bones.GetBoneName(Index);
            if (Name.ToString().Replace(TEXT("-"), TEXT("_")).Contains(TEXT("cat_bip_Head")))
            {
                AppearanceHeadBoneName = Name;
                break;
            }
        }
    }
    // Both templates are required. Missing assets must retain original visuals.
    if (!CoatMaterialTemplate || !EyeMaterialTemplate) return;

    struct FBinding
    {
        USkeletalMeshComponent* Component;
        int32 Slot;
        UMaterialInstanceDynamic* Material;
    };
    TArray<FBinding> Bindings;
    TArray<USkeletalMeshComponent*> Components;
    Components.Add(CatMesh);
    for (USkeletalMeshComponent* Detail : AdditionalMeshComponents) Components.Add(Detail);
    int32 EyeComponents = 0;
    int32 CoatComponents = 0;
    for (USkeletalMeshComponent* Component : Components)
    {
        if (!Component || !Component->GetSkeletalMeshAsset()) continue;
        const FString Name = Component->GetSkeletalMeshAsset()->GetName();
        // Audited source anatomy: Blackeye/Object_16 is ear alpha geometry,
        // not an iris. Do not recolor it. Object_10 also contains teeth/whiskers;
        // the eye template preserves all atlas regions outside its iris mask.
        const bool bEye = Name == TEXT("Object_10") || Name == TEXT("Object_13");
        const bool bCoat = Name == TEXT("Object_7");
        if (!bEye && !bCoat) continue;
        if (bEye) ++EyeComponents;
        else ++CoatComponents;
        for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
        {
            UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(
                bEye ? EyeMaterialTemplate.Get() : CoatMaterialTemplate.Get(), this);
            if (!Material) return;
            if (bEye) EyeMaterialInstances.Add(Material);
            else CoatMaterialInstances.Add(Material);
            Bindings.Add({ Component, Slot, Material });
        }
    }
    if (CoatComponents != 1 || EyeComponents != 2 || CoatMaterialInstances.IsEmpty() || EyeMaterialInstances.IsEmpty())
        return;
    for (const FBinding& Binding : Bindings) Binding.Component->SetMaterial(Binding.Slot, Binding.Material);
    bAppearanceReady = true;
}

void AMirmirCat::ApplyAppearance()
{
    AppearanceId = UMirmirSaveGame::NormalizeAppearance(AppearanceId);
    AppearanceHeadScale = 1.0f;
    if (!bAppearanceReady) return;
    const bool bBritish = AppearanceId == TEXT("british");
    const bool bTuxedo = AppearanceId == TEXT("tuxedo");
    const FLinearColor Coat = bBritish ? FLinearColor(0.30f, 0.36f, 0.42f)
        : FLinearColor(0.90f, 0.89f, 0.86f);
    const FLinearColor Amber(0.82f, 0.38f, 0.045f);
    const FLinearColor Blue(0.08f, 0.38f, 0.78f);
    for (UMaterialInstanceDynamic* Material : CoatMaterialInstances)
    {
        Material->SetVectorParameterValue(TEXT("CoatColor"), Coat);
        Material->SetScalarParameterValue(TEXT("OriginalCoatWeight"), bTuxedo ? 1.0f : 0.0f);
        Material->SetScalarParameterValue(TEXT("FurDetailStrength"), bBritish ? 0.35f : 0.25f);
        Material->SetScalarParameterValue(TEXT("FurRoughness"), bBritish ? 0.92f : 0.88f);
    }
    for (UMaterialInstanceDynamic* Material : EyeMaterialInstances)
    {
        Material->SetVectorParameterValue(TEXT("CoatColor"), Coat);
        Material->SetScalarParameterValue(TEXT("FurDetailStrength"), bBritish ? 0.35f : 0.25f);
        Material->SetScalarParameterValue(TEXT("FurRoughness"), bBritish ? 0.92f : 0.88f);
        Material->SetVectorParameterValue(TEXT("EyeLeftColor"), bBritish || bTuxedo ? Amber : Blue);
        Material->SetVectorParameterValue(TEXT("EyeRightColor"), Amber);
        Material->SetScalarParameterValue(TEXT("OriginalCoatWeight"), bTuxedo ? 1.0f : 0.0f);
    }
    for (UMaterialInstanceDynamic* Material : FurMaterialInstances)
    {
        Material->SetVectorParameterValue(TEXT("CoatColor"), Coat);
        Material->SetScalarParameterValue(TEXT("OriginalCoatWeight"), bTuxedo ? 1.0f : 0.0f);
        Material->SetScalarParameterValue(TEXT("FurDetailStrength"), bBritish ? 0.35f : 0.25f);
        Material->SetScalarParameterValue(TEXT("FurRoughness"), bBritish ? 0.92f : 0.88f);
        Material->SetScalarParameterValue(TEXT("FurLengthCm"), FMath::Clamp(
            bBritish ? BritishFurLengthCm : (bTuxedo ? TuxedoFurLengthCm : VanFurLengthCm), 0.0f, 1.0f));
        Material->SetScalarParameterValue(TEXT("FurDensity"), FMath::Clamp(FurDensityFraction, 0.0f, 1.0f));
    }
    FVector Scale = AppearanceBaseTransform.GetScale3D();
    if (bBritish) Scale[AppearanceWidthAxis] *= 1.12;
    CatMesh->SetRelativeScale3D(Scale);
    const FVector OriginalScale = AppearanceBaseTransform.GetScale3D();
    const FVector CenterCorrection((OriginalScale.X - Scale.X) * AppearanceMeshOrigin.X,
        (OriginalScale.Y - Scale.Y) * AppearanceMeshOrigin.Y, 0.0);
    CatMesh->SetRelativeLocation(AppearanceBaseTransform.GetLocation() +
        AppearanceBaseTransform.GetRotation().RotateVector(CenterCorrection));
    AppearanceHeadScale = bBritish && !AppearanceHeadBoneName.IsNone() ? 1.1f : 1.0f;
}

void AMirmirCat::AssembleFurShells()
{
    bFurShellsReady = false;
    if (!bEnableFurShells || !FurMaterialTemplate || !bAppearanceReady || !FurMeshComponents.IsEmpty()) return;
    USkeletalMesh* Body = CatMesh->GetSkeletalMeshAsset();
    if (!Body || Body->GetName() != TEXT("Object_7")) return;
    constexpr int32 ShellCount = 6;
    for (int32 Index = 0; Index < ShellCount; ++Index)
    {
        UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(FurMaterialTemplate, this);
        if (!Material) return;
        Material->SetScalarParameterValue(TEXT("ShellIndex"), float(Index + 1) / float(ShellCount));
        Material->SetScalarParameterValue(TEXT("FurDensity"), FMath::Clamp(FurDensityFraction, 0.0f, 1.0f));
        USkeletalMeshComponent* Shell = NewObject<USkeletalMeshComponent>(this);
        AddInstanceComponent(Shell);
        Shell->SetupAttachment(CatMesh);
        Shell->SetRelativeTransform(FTransform::Identity);
        Shell->SetSkeletalMesh(Body);
        Shell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Shell->SetCollisionResponseToAllChannels(ECR_Ignore);
        Shell->SetGenerateOverlapEvents(false);
        Shell->SetCanEverAffectNavigation(false);
        Shell->SetCastShadow(false);
        Shell->bUseBoundsFromLeaderPoseComponent = true;
        Shell->SetLeaderPoseComponent(CatMesh, true, false);
        for (int32 Slot = 0; Slot < Shell->GetNumMaterials(); ++Slot) Shell->SetMaterial(Slot, Material);
        Shell->RegisterComponent();
        FurMaterialInstances.Add(Material);
        FurMeshComponents.Add(Shell);
    }
    bFurShellsReady = FurMeshComponents.Num() == ShellCount;
}

void AMirmirCat::SetAppearance(FName Id)
{
    const FString Requested = Id.ToString().ToLower();
    if (Requested != TEXT("van") && Requested != TEXT("british") && Requested != TEXT("tuxedo")) return;
    AppearanceId = UMirmirSaveGame::NormalizeAppearance(Id);
    ApplyAppearance();
    SaveCat();
    if (bDesktopMode) WriteDesktopStatus();
}

void AMirmirCat::RefreshState()
{
    const Mirmir::State& State = Behavior.Get();
    const EMirmirActivity Previous = Activity;
    Activity = static_cast<EMirmirActivity>(State.CurrentActivity);
    Satiety = State.Satiety;
    Energy = State.Energy;
    Bond = State.Bond;
    Curiosity = State.Curiosity;
    if (Activity != Previous)
    {
        if (Previous == EMirmirActivity::Eat && Habitat && bHabitatArrived) Habitat->SetFoodVisible(false);
        bHabitatTraveling = false;
        bHabitatArrived = false;
        HabitatWaypoints.Reset();
        if (Activity == EMirmirActivity::Eat || Activity == EMirmirActivity::Sleep) BeginHabitatApproach();
        if (Activity != EMirmirActivity::Play)
        {
            bToyStartPending = false;
            if (Toy && Toy->IsPlaying()) Toy->EndPlaySession();
        }
        if (IsValid(ActiveSound)) ActiveSound->Stop();
        bPurring = false;
        WanderTarget = GetActorLocation();
        OnActivityChanged(Previous, Activity);
        if (Activity == EMirmirActivity::SeekAttention) Meow();
    }
}

bool AMirmirCat::Interact(Mirmir::Interaction Action, FName Name, USoundBase* Sound)
{
    const bool Accepted = Behavior.Interact(Action);
    RefreshState();
    ++InteractionSerial;
    LastInteractionAction = Name;
    bLastInteractionAccepted = Accepted;
    InteractionMessage = Accepted ? (Action == Mirmir::Interaction::Feed
        ? TEXT("Mama hazır; Mırmır kabına gidiyor.") : Action == Mirmir::Interaction::Play
        ? TEXT("Oyun başladı; Mırmır topuyla oynuyor.") : TEXT("Mırmır sevildi."))
        : (Action == Mirmir::Interaction::Play ? TEXT("Mırmır yoruldu; yatağında dinlenecek.") : TEXT("Mırmır seni hissediyor; biraz yavaş okşa."));
    if (Accepted)
    {
        const bool ContinuePurr = Action == Mirmir::Interaction::Pet && bPurring &&
            IsValid(ActiveSound) && ActiveSound->IsPlaying();
        if (!ContinuePurr)
        {
            if (IsValid(ActiveSound)) ActiveSound->Stop();
            bPurring = false;
            if (!bMuted && Sound)
            {
                ActiveSound = UGameplayStatics::SpawnSoundAtLocation(this, Sound, GetActorLocation(),
                    FRotator::ZeroRotator, FMath::Clamp(SoundVolume, 0.0f, 1.0f));
                bPurring = Action == Mirmir::Interaction::Pet && IsValid(ActiveSound);
            }
        }
        OnInteractionAccepted(Name);
    }
    return Accepted;
}

bool AMirmirCat::Pet()
{
    const bool Accepted = Interact(Mirmir::Interaction::Pet, TEXT("Pet"), PurrSound);
    if (Accepted) PetReactionRemaining = 1.1f;
    return Accepted;
}
bool AMirmirCat::Feed()
{
    const bool Accepted = Interact(Mirmir::Interaction::Feed, TEXT("Feed"), FeedSound);
    if (Accepted && Activity == EMirmirActivity::Eat && !bHabitatTraveling && !bHabitatArrived) BeginHabitatApproach();
    if (Accepted && Habitat) Habitat->SetFoodVisible(true);
    return Accepted;
}
bool AMirmirCat::Play()
{
    const bool Accepted = Interact(Mirmir::Interaction::Play, TEXT("Play"), PlaySound);
    if (Accepted) bToyStartPending = true;
    return Accepted;
}
void AMirmirCat::ToggleMute()
{
    bMuted = !bMuted;
    if (bMuted && IsValid(ActiveSound)) ActiveSound->Stop();
    if (bMuted) bPurring = false;
}
void AMirmirCat::Meow()
{
    if (bMuted || !MeowSound || VoiceCooldown > 0 || (IsValid(ActiveSound) && ActiveSound->IsPlaying())) return;
    ActiveSound = UGameplayStatics::SpawnSoundAtLocation(this, MeowSound, GetActorLocation(),
        FRotator::ZeroRotator, FMath::Clamp(SoundVolume, 0.0f, 1.0f));
    bPurring = false;
    VoiceCooldown = 30.0f;
}
void AMirmirCat::Rest()
{
    Behavior.Rest();
    RefreshState();
    if (!bHabitatTraveling && !bHabitatArrived) BeginHabitatApproach();
    ++InteractionSerial;
    LastInteractionAction = TEXT("rest");
    bLastInteractionAccepted = true;
    InteractionMessage = TEXT("Mırmır yatağına gidiyor.");
}

void AMirmirCat::PollDesktopCommands()
{
    const FString Directory = FPaths::Combine(FPaths::GetPath(UMirmirSaveGame::SavePath()), TEXT("commands"));
    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *FPaths::Combine(Directory, TEXT("*.json")), true, false);
    Files.Sort();
    int32 Consumed = 0;
    for (const FString& Name : Files)
    {
        if (++Consumed > 20) break;
        const FString Path = FPaths::Combine(Directory, Name);
        FString Text;
        TSharedPtr<FJsonObject> Root;
        const bool Valid = IFileManager::Get().FileSize(*Path) <= 1024 &&
            FFileHelper::LoadFileToString(Text, *Path) &&
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid();
        IFileManager::Get().Delete(*Path, false, true, true);
        FString Command;
        if (!Valid || !Root->TryGetStringField(TEXT("command"), Command)) continue;
        if (Command == TEXT("pet"))
        {
            double Direction = 0.0;
            if (Root->TryGetNumberField(TEXT("stroke_normalized"), Direction) && FMath::IsFinite(Direction))
                PetStrokeDirection = FMath::Clamp(static_cast<float>(Direction), -1.0f, 1.0f);
            else
            {
                const TArray<TSharedPtr<FJsonValue>>* Stroke = nullptr;
                if (Root->TryGetArrayField(TEXT("stroke_normalized"), Stroke) && Stroke->Num() > 0 &&
                    (*Stroke)[0]->TryGetNumber(Direction) && FMath::IsFinite(Direction))
                    PetStrokeDirection = FMath::Clamp(static_cast<float>(Direction), -1.0f, 1.0f);
                else PetStrokeDirection = 0.0f;
            }
            Pet();
        }
        else if (Command == TEXT("feed")) Feed();
        else if (Command == TEXT("play")) Play();
        else if (Command == TEXT("rest")) Rest();
        else if (Command == TEXT("mute")) ToggleMute();
        else if (Command == TEXT("appearance"))
        {
            FString Id;
            if (Root->TryGetStringField(TEXT("appearance"), Id) && Id.Len() <= 16) SetAppearance(FName(*Id));
        }
        else if (Command == TEXT("quit"))
        {
            SaveCat();
            WriteDesktopStatus();
            UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
            return;
        }
        WriteDesktopStatus();
    }
}

void AMirmirCat::WriteDesktopStatus()
{
    const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("implementation"), TEXT("unreal-engine"));
    Root->SetNumberField(TEXT("satiety"), Satiety);
    Root->SetNumberField(TEXT("energy"), Energy);
    Root->SetNumberField(TEXT("bond"), Bond);
    Root->SetNumberField(TEXT("activity"), static_cast<uint8>(Activity));
    Root->SetBoolField(TEXT("muted"), bMuted);
    Root->SetStringField(TEXT("appearance"), AppearanceId.ToString().ToLower());
    Root->SetBoolField(TEXT("appearance_ready"), bAppearanceReady);
    Root->SetNumberField(TEXT("updated_at_unix"), static_cast<double>(FDateTime::UtcNow().ToUnixTimestamp()));
    FString Text;
    if (!FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text))) return;
    FString StatusDirectory = FPlatformMisc::GetEnvironmentVariable(TEXT("MIRMIR_FRAME_DIR"));
    if (StatusDirectory.IsEmpty() || FPaths::IsRelative(StatusDirectory))
        StatusDirectory = FPaths::GetPath(UMirmirSaveGame::SavePath());
    IFileManager::Get().MakeDirectory(*StatusDirectory, true);
    const FString Path = FPaths::Combine(StatusDirectory, TEXT("desktop-status.json"));
    if (FFileHelper::SaveStringToFile(Text, *(Path + TEXT(".tmp")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        IFileManager::Get().Move(*Path, *(Path + TEXT(".tmp")), true, true);
}
void AMirmirCat::HandleClicked(UPrimitiveComponent* Component, FKey Key) { Pet(); }

bool AMirmirCat::SaveCat()
{
    if (!bCanSave) return false;
    UMirmirSaveGame* Save = NewObject<UMirmirSaveGame>(this);
    Save->CatState = Behavior.Get();
    Save->bMuted = bMuted;
    Save->AppearanceId = AppearanceId;
    Save->SavedAtUnixSeconds = FDateTime::UtcNow().ToUnixTimestamp();
    FString Error;
    const bool Success = UMirmirSaveGame::Store(*Save, Error);
    PersistenceStatus = Success ? TEXT("Saved") : Error;
    if (!Success) UE_LOG(LogTemp, Warning, TEXT("Mirmir: %s"), *Error);
    return Success;
}

void AMirmirCat::ResetCat()
{
    Behavior = Mirmir::Behavior();
    bCanSave = true;
    RefreshState();
    SaveCat();
}

void AMirmirCat::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (IsValid(ActiveSound)) ActiveSound->Stop();
    SaveCat();
    Super::EndPlay(EndPlayReason);
}
