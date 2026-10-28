#include "MirmirCat.h"
#include "MirmirToy.h"
#include "MirmirHabitat.h"
#include "CatNavigation.h"
#include "Components/BoxComponent.h"
#include "MirmirSaveGame.h"
#include "MirmirAnimInstance.h"
#include "Components/AudioComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "GroomComponent.h"
#include "HairStrandsInterface.h"
#include "RHI.h"
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
    CollisionBody = CreateDefaultSubobject<UBoxComponent>(TEXT("CatRoot"));
    RootComponent = CollisionBody;
    CollisionBody->SetBoxExtent(FVector(23.0, 12.0, 18.0));
    CollisionBody->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    CollisionBody->SetCollisionObjectType(ECC_WorldDynamic);
    CollisionBody->SetCollisionResponseToAllChannels(ECR_Ignore);
    CollisionBody->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
    CollisionBody->SetHiddenInGame(true);
    CollisionBody->SetCanEverAffectNavigation(false);
    CatMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CatMesh"));
    CatMesh->SetupAttachment(RootComponent);
    CatMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    CatMesh->SetCollisionObjectType(ECC_WorldDynamic);
    CatMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    CatMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    CatMesh->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
    CatGroom = CreateDefaultSubobject<UGroomComponent>(TEXT("CatGroom"));
    CatGroom->SetupAttachment(CatMesh);
    CatGroom->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    CatGroom->SetCastShadow(true);
    CatGroom->SetVisibility(false, true);
}

void AMirmirCat::BeginPlay()
{
    Super::BeginPlay();
    // The reviewed CC-BY motion set already shipped with the project. Product
    // Blueprint defaults intentionally left these transition fields empty
    // while the old foot-contact implementation was being rejected. The
    // current action IK can now review them in the real bed without mutating
    // the Blueprint asset; incompatible or missing clips fail closed.
    if (!SitDownAnimation)
        SitDownAnimation = LoadObject<UAnimSequence>(nullptr,
            TEXT("/Game/Mirmir/MotionReview/Retarget_3f65719d5a3f/SkeletalMeshes/cat-tuxedo-motion-candidateTuxedo_SitDown.cat-tuxedo-motion-candidateTuxedo_SitDown"));
    if (!SitIdleAnimation)
        SitIdleAnimation = LoadObject<UAnimSequence>(nullptr,
            TEXT("/Game/Mirmir/MotionReview/Retarget_3f65719d5a3f/SkeletalMeshes/cat-tuxedo-motion-candidateTuxedo_IdleSit.cat-tuxedo-motion-candidateTuxedo_IdleSit"));
    if (!StandUpAnimation)
        StandUpAnimation = LoadObject<UAnimSequence>(nullptr,
            TEXT("/Game/Mirmir/MotionReview/Retarget_3f65719d5a3f/SkeletalMeshes/cat-tuxedo-motion-candidateTuxedo_StandUp.cat-tuxedo-motion-candidateTuxedo_StandUp"));
    bPreferSittingWhenResting = SitDownAnimation && SitIdleAnimation && StandUpAnimation;
    // Isolated contact calibration; never changes the saved cat profile.
    if (FParse::Param(FCommandLine::Get(), TEXT("MirmirNoStanceIK"))) bEnableStanceIK = false;
    if (CatSkeletalMesh) CatMesh->SetSkeletalMesh(CatSkeletalMesh);
    if (CatGroomAsset)
    {
        CatGroom->SetGroomAsset(CatGroomAsset, CatGroomBinding, true);
        if (CatGroomMaterial) CatGroom->SetMaterial(0, CatGroomMaterial);
        CatGroom->PhysicsAsset = CatMesh->GetPhysicsAsset();
        if (GroomHairWidthCm > 0.0f)
            CatGroom->SetHairWidth(FMath::Clamp(GroomHairWidthCm, 0.01f, 0.5f));
        CatGroom->SetStableRasterization(true);
        CatGroom->SetScatterSceneLighting(true);
        CatGroom->SetVisibility(true, true);
        CatGroom->UpdateBounds();
        const bool bBindingValid = CatGroomBinding &&
            UGroomBindingAsset::IsBindingAssetValid(CatGroomBinding, false, true);
        const bool bMeshCompatible = CatGroomBinding &&
            UGroomBindingAsset::IsCompatible(CatMesh->GetSkeletalMeshAsset(), CatGroomBinding, true);
        const bool bGroomCompatible = CatGroomBinding &&
            UGroomBindingAsset::IsCompatible(CatGroomAsset, CatGroomBinding, true);
        const bool bStrandsSupported =
            IsHairStrandsSupported(EHairStrandsShaderType::Strands, GMaxRHIShaderPlatform);
        UE_LOG(LogTemp, Display,
            TEXT("Mirmir: Groom initialized asset=%s binding=%s material=%s visible=%d registered=%d valid=%d mesh_compatible=%d groom_compatible=%d strands_supported=%d bounds_extent=%s."),
            *GetNameSafe(CatGroomAsset), *GetNameSafe(CatGroomBinding),
            *GetNameSafe(CatGroom->GetMaterial(0)), CatGroom->IsVisible(), CatGroom->IsRegistered(), bBindingValid,
            bMeshCompatible, bGroomCompatible, bStrandsSupported,
            *CatGroom->Bounds.BoxExtent.ToString());
        if (FParse::Param(FCommandLine::Get(), TEXT("MirmirGroomUnboundReview")))
        {
            CatGroom->SetGroomAsset(CatGroomAsset, nullptr, true);
            CatMesh->SetVisibility(false, false);
            UE_LOG(LogTemp, Display, TEXT("Mirmir: unbound Groom review enabled."));
        }
    }
    if (bNormalizeImportedMesh && CatMesh->GetSkeletalMeshAsset()) NormalizeMesh();
    // GLTF ConvertVec3(X,Y,Z)=(X,Z,Y), import units *100. Atlas
    // upper-lip landmark is 100% Head weighted (anatomy-calibration report).
    MuzzleBoneLocal = FVector(-34.39959, 3.01320, 729.48403);
    for (int32 Bone=0; Bone<CatMesh->GetNumBones(); ++Bone)
    {
        const FString Name=CatMesh->GetBoneName(Bone).ToString();
        if (Name.Replace(TEXT("-"),TEXT("_")).Contains(TEXT("cat_bip_L_Finger01"))) PawTipNames[0]=CatMesh->GetBoneName(Bone);
        if (Name.Replace(TEXT("-"),TEXT("_")).Contains(TEXT("cat_bip_R_Finger01"))) PawTipNames[1]=CatMesh->GetBoneName(Bone);
    }
    CatMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    // Native graph input must see this frame's movement, not the previous
    // speed. Component ticks do not automatically wait for their owning actor.
    CatMesh->AddTickPrerequisiteActor(this);
    CatMesh->RegisterOnBoneTransformsFinalizedDelegate(FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(this, &AMirmirCat::HandleBonesFinalized));
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
    PoseMeasurementDelta = DeltaSeconds;
    VoiceCooldown = FMath::Max(0.0f, VoiceCooldown - DeltaSeconds);
    if (!bWelcomed && VoiceCooldown <= 0)
    {
        bWelcomed = true;
        Meow();
    }
    Behavior.Advance(DeltaSeconds, !bHabitatTraveling);
    RefreshState();
    const FVector Before = GetActorLocation();
    const float BeforeYaw = GetActorRotation().Yaw;
    const float MovementDelta = FMath::Clamp(DeltaSeconds, 0.0f, 0.1f);
    NavigationReplanSeconds = FMath::Max(0.0f, NavigationReplanSeconds - MovementDelta);
    if (Habitat && ObservedLayoutRevision != Habitat->GetLayoutRevision())
    {
        ObservedLayoutRevision = Habitat->GetLayoutRevision();
        NavigationWaypoints.Reset();
        if (Activity == EMirmirActivity::Eat || Activity == EMirmirActivity::Sleep) BeginHabitatApproach();
    }
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
            (RestPoseAlpha > 0.02f && Activity != EMirmirActivity::Sleep) ||
            (EatingPoseStrength > 0.02f && Activity != EMirmirActivity::Eat);
    }
    if (bToyStartPending && !bAnimationTransitionBlocksMovement && !bNeedBowlExit && !IsBedOccupied() && Toy && Activity == EMirmirActivity::Play)
    {
        if (Toy->BeginPlayWithCat(this)) Behavior.RestartActivity(Toy->SessionDurationSeconds);
        else
        {
            LastInteractionAction=TEXT("play"); bLastInteractionAccepted=false; ++InteractionSerial;
            InteractionMessage=TEXT("Top için güvenli bir alan bulunamadı; Mırmır çevresine bakıyor.");
        }
        bToyStartPending = false;
    }
    const bool bChasingToy = Activity == EMirmirActivity::Play && Toy && Toy->IsPlaying();
    if (!bChasingToy) { bWaitingAtToy = false; bWaitingAtBlockedToy = false; }
    else
    {
        const float Distance = FVector::Dist2D(Toy->GetBallLocation(), Before);
        const bool ProtectedBall = Habitat &&
            (FVector::Dist2D(Toy->GetBallLocation(), Habitat->GetStationFloorPosition(TEXT("bowl"))) < 52.0f ||
             FVector::Dist2D(Toy->GetBallLocation(), Habitat->GetStationFloorPosition(TEXT("bed"))) < 77.0f);
        if (Distance > 60.0f || !ProtectedBall) bWaitingAtBlockedToy = false;
        if (bWaitingAtBlockedToy || Distance <= 38.0f) bWaitingAtToy = true;
        else if (Distance > 46.0f) bWaitingAtToy = false;
        if (bNeedBowlExit) { bWaitingAtToy = false; bWaitingAtBlockedToy = false; }
    }
    UpdatePlayPose(MovementDelta, bChasingToy);
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
            bTravel = !bWaitingAtToy && PlayPhase != TEXT("Plant") && PlayPhase != TEXT("Strike") && PlayPhase != TEXT("Recover");
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
    const bool WasBacking = bWalkingBackwards;
    bWalkingBackwards = false;
    bExitingBed = false;
    const FVector SupportedBed=Habitat ? Before-Habitat->GetStationFloorPosition(TEXT("bed")) : FVector::ZeroVector;
    bBedEntryPermission = Habitat && Before.Z>HomeLocation.Z+.5f && FMath::Abs(SupportedBed.X)<65 && FMath::Abs(SupportedBed.Y)<13;
    bBowlClearancePermission = (Activity==EMirmirActivity::Eat && bHabitatArrived) || bNeedBowlExit;
    if (Habitat && bTravel)
    {
        const FVector Bed = Habitat->GetStationFloorPosition(TEXT("bed"));
        const FVector Bowl = Habitat->GetStationFloorPosition(TEXT("bowl"));
        const FVector LocalBed = Before - Bed;
        const float AlongBed=LocalBed.X*BedForwardSign;
        // The bed is open at both ends. The sleeping pose finishes facing +X,
        // allowing a forward exit instead of an in-place frozen rotation.
        if (Activity != EMirmirActivity::Sleep && FMath::Abs(LocalBed.Y) < 13.0 &&
            AlongBed > -65.0 && AlongBed < 85.0 && Before.Z > HomeLocation.Z + .5) bLeavingBedTraversal = true;
        if (AlongBed >= 95.0 || FMath::Abs(LocalBed.Y) > 18.0) bLeavingBedTraversal = false;
        bExitingBed = bLeavingBedTraversal;
        const bool SupportedAtBed = FMath::Abs(LocalBed.X) < 65.0 && FMath::Abs(LocalBed.Y) < 13.0 && Before.Z > HomeLocation.Z + .5;
        const bool FinalBedLane = Activity == EMirmirActivity::Sleep && bHabitatTraveling &&
            !HabitatWaypoints.IsEmpty() && HabitatWaypointIndex >= HabitatWaypoints.Num() - 18;
        bBedEntryPermission = (SupportedAtBed || bExitingBed || FinalBedLane) &&
            FMath::Abs(LocalBed.Y) < 13.0 && FMath::Abs(LocalBed.X) < 110.0 && GetActorForwardVector().X*BedForwardSign > .85;
        if (bExitingBed) Target = Bed + FVector(105*BedForwardSign, 0, 0);
        // A real backwards playback of the walk clears the food bowl before
        // forward steering resumes. The stationary body is never spun through it.
        const FVector ToBowl = Bowl - Before;
        if (ToBowl.Size2D() >= 105.0 && bNeedBowlExit)
        {
            bNeedBowlExit=false;
            if (bHabitatTraveling) BeginHabitatApproach();
        }
        bWalkingBackwards = bNeedBowlExit && Activity != EMirmirActivity::Eat && !bExitingBed && ToBowl.Size2D() < 105.0 &&
            FVector::DotProduct(ToBowl.GetSafeNormal2D(), GetActorForwardVector()) > .35;
        if (bWalkingBackwards && !WasBacking && MovementSpeed > 1.0f)
        {
            bWalkingBackwards = false;
            bTravel = false; // Settle the forward step before reversing velocity.
        }
        if (WasBacking && !bWalkingBackwards && Locomotion.Speed > .1)
        {
            bWalkingBackwards = true;
            bTravel = false; // Brake in the existing direction before going forward.
        }
        bBowlClearancePermission = bBowlClearancePermission || bWalkingBackwards || (Activity == EMirmirActivity::Eat && bHabitatTraveling);
        if (bWalkingBackwards) Target = Before - GetActorForwardVector() * 80.0;
        else if (!bExitingBed && !bHabitatTraveling) Target = NavigationSteeringTarget(Target);
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
    if (bWalkingBackwards) Locomotion.YawRate = 0.0;
    const Mirmir::LocomotionStep Step = Mirmir::AdvanceLocomotion(Locomotion,
        bWalkingBackwards ? 80.0 : FVector::DotProduct(ToTarget, Forward),
        bWalkingBackwards ? 0.0 : FVector::DotProduct(ToTarget, Right),
        bWalkingBackwards ? 24.0f : bCuriosityVisit ? FMath::Min(Cruise, 25.0f) : Cruise, bTravel, MovementDelta, Settings);
    FVector Proposed = Before + (bWalkingBackwards ? -Forward : Forward) * Step.Forward + Right * Step.Right;
    if (Habitat)
    {
        const float BedDistance = FVector::Dist2D(Proposed, Habitat->GetBedLocation());
        const bool bOnSupport = (bBedEntryPermission || (bHabitatArrived && Activity == EMirmirActivity::Sleep) || bExitingBed) && BedDistance < 58.0f;
        const float FloorZ = bOnSupport ? Habitat->GetBedLocation().Z : HomeLocation.Z;
        Proposed.Z = FMath::FInterpConstantTo(Proposed.Z, FloorZ, MovementDelta, 12.0f);
        CollisionBody->IgnoreComponentWhenMoving(Habitat->GetBedCollision(), bBedEntryPermission || (bHabitatArrived && Activity == EMirmirActivity::Sleep));
        // The analytically constrained food corridor owns the last 64 cm of
        // the approach. Ignore only the bowl's coarse axis-aligned query box
        // there; otherwise the rotated cat box can touch a corner before the
        // visually calibrated 31 cm feeding pose.
        CollisionBody->IgnoreComponentWhenMoving(Habitat->GetBowlCollision(), bBowlClearancePermission);
    }
    bMovementBlocked = !SegmentClear(Before, Proposed, bBedEntryPermission || (bHabitatArrived && Activity == EMirmirActivity::Sleep), bBowlClearancePermission);
    FHitResult Hit;
    if (!bMovementBlocked)
    {
        const FQuat Rotation = FRotator(0, GetActorRotation().Yaw + (bWalkingBackwards ? 0.0 : FMath::RadiansToDegrees(Step.Yaw)), 0).Quaternion();
        CollisionBody->MoveComponent(Proposed - Before, Rotation, true, &Hit);
        bMovementBlocked = Hit.bBlockingHit;
        if (Hit.bBlockingHit)
        {
            const double RequestedDistance = FVector::Dist2D(Before, Proposed);
            const double Fraction = RequestedDistance > .0001
                ? FMath::Clamp(FVector::Dist2D(Before, GetActorLocation()) / RequestedDistance, 0.0, 1.0) : 0.0;
            // UE's linear sweep can apply the requested rotation even when
            // translation hits a wall. Keep actual yaw tied to the accepted arc.
            SetActorRotation(FRotator(0, BeforeYaw + (bWalkingBackwards ? 0.0 : FMath::RadiansToDegrees(Step.Yaw) * Fraction), 0));
        }
    }
    if (bMovementBlocked)
    {
        Locomotion.Speed = 0.0;
        Locomotion.YawRate = 0.0;
        NavigationWaypoints.Reset();
    }
    MovementSpeed = DeltaSeconds > SMALL_NUMBER ? FVector::Dist2D(Before, GetActorLocation()) / DeltaSeconds : 0.0f;
    LocomotionYawRate = DeltaSeconds > SMALL_NUMBER
        ? FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(BeforeYaw, GetActorRotation().Yaw)) / DeltaSeconds : 0.0f;
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

void AMirmirCat::SetDesktopBounds(FVector Origin, FVector Right, FVector Up, float PixelsPerCM, FVector2D Minimum, FVector2D Maximum)
{
    if (Origin.ContainsNaN() || Right.ContainsNaN() || Up.ContainsNaN() || Minimum.ContainsNaN() || Maximum.ContainsNaN() ||
        !FMath::IsFinite(PixelsPerCM) || PixelsPerCM <= .01f || PixelsPerCM > 20.0f ||
        Minimum.X >= Maximum.X || Minimum.Y >= Maximum.Y) return;
    const bool Changed = !bHasDesktopBounds || !DesktopMinimum.Equals(Minimum,.5f) || !DesktopMaximum.Equals(Maximum,.5f);
    bHasDesktopBounds = true;
    DesktopOrigin = Origin; DesktopRight = Right.GetSafeNormal(); DesktopUp = Up.GetSafeNormal();
    DesktopScale = PixelsPerCM; DesktopMinimum = Minimum; DesktopMaximum = Maximum;
    if (Changed) { NavigationWaypoints.Reset(); if (bHabitatTraveling) HabitatWaypoints.Reset(); NavigationReplanSeconds = 0; }
}

bool AMirmirCat::IsInsideDesktop(FVector Position) const
{
    if (!bHasDesktopBounds) return true;
    const FVector Delta = Position - DesktopOrigin;
    const double X = FVector::DotProduct(Delta, DesktopRight) * DesktopScale;
    const double Y = -FVector::DotProduct(Delta, DesktopUp) * DesktopScale;
    return X >= DesktopMinimum.X && X <= DesktopMaximum.X && Y >= DesktopMinimum.Y && Y <= DesktopMaximum.Y;
}

bool AMirmirCat::IsBedOccupied() const
{
    if (!Habitat) return false;
    const FVector Delta = GetActorLocation() - Habitat->GetStationFloorPosition(TEXT("bed"));
    return RestPoseAlpha > .01f || bExitingBed ||
        (FMath::Abs(Delta.X) < 65.0 && FMath::Abs(Delta.Y) < 18.0 && Delta.Z > .5) ||
        (Activity == EMirmirActivity::Sleep && bHabitatTraveling && Delta.Size2D() < 80.0);
}

bool AMirmirCat::SegmentClear(FVector A, FVector B, bool BedCorridor, bool BowlCorridor, float Margin) const
{
    const bool bRouteDebug=FParse::Param(FCommandLine::Get(),TEXT("MirmirRouteDebug"));
    if (!IsInsideDesktop(A) || !IsInsideDesktop(B))
    { if (bRouteDebug) UE_LOG(LogTemp,Warning,TEXT("Mirmir segment outside A=%s B=%s"),*A.ToCompactString(),*B.ToCompactString()); return false; }
    if (!Habitat) return true;
    using namespace Mirmir::Navigation;
    const FVector Bed = Habitat->GetStationFloorPosition(TEXT("bed"));
    const FVector Bowl = Habitat->GetStationFloorPosition(TEXT("bowl"));
    auto InBedGate = [Bed](FVector P)
    {
        return FMath::Abs(P.Y - Bed.Y) <= 13.0 && FMath::Abs(P.X - Bed.X) <= 115.0;
    };
    if (!(BedCorridor && InBedGate(A) && InBedGate(B)) &&
        SegmentClearance({ A.X,A.Y }, { B.X,B.Y }, {{ Bed.X,Bed.Y }, 40.0 + 28.0 + Margin}) < 0.0)
    { if (bRouteDebug) UE_LOG(LogTemp,Warning,TEXT("Mirmir segment bed A=%s B=%s margin=%.1f"),*A.ToCompactString(),*B.ToCompactString(),Margin); return false; }
    auto InFoodGate = [Bowl, this](FVector P)
    {
        const FVector Radial = BowlApproachOutward;
        const FVector D = P - Bowl;
        return FVector::DotProduct(D, Radial) >= 30.0 && FMath::Abs(D.X * Radial.Y - D.Y * Radial.X) <= 13.0;
    };
    const double FoodA = FVector::Dist2D(A, Bowl), FoodB = FVector::Dist2D(B, Bowl);
    const bool SafeBackoff = bWalkingBackwards && FoodA >= 35.0 && FoodB >= FoodA - .001;
    if (!(BowlCorridor && (SafeBackoff || (InFoodGate(A) && InFoodGate(B)))) &&
        SegmentClearance({ A.X,A.Y }, { B.X,B.Y }, {{ Bowl.X,Bowl.Y }, 15.0 + 28.0 + Margin}) < 0.0)
    { if (bRouteDebug) UE_LOG(LogTemp,Warning,TEXT("Mirmir segment bowl A=%s B=%s margin=%.1f"),*A.ToCompactString(),*B.ToCompactString(),Margin); return false; }
    return true;
}

bool AMirmirCat::PlanCurvedRoute(FVector Goal, FVector EndForward, bool BedCorridor, bool BowlCorridor, TArray<FVector>& Out) const
{
    using namespace Mirmir::Navigation;
    const FVector Start = GetActorLocation();
    const Pose2 Initial{{ Start.X,Start.Y }, FMath::DegreesToRadians(GetActorRotation().Yaw)};
    const Pose2 Final{{ Goal.X,Goal.Y }, FMath::Atan2(EndForward.Y, EndForward.X)};
    auto AppendValid = [&](const Candidate& CandidatePath, TArray<FVector>& Points)
    {
        for (size_t I = 1; I < CandidatePath.Samples.size(); ++I)
        {
            const Point2 A = CandidatePath.Samples[I-1].Position, B = CandidatePath.Samples[I].Position;
            if (!SegmentClear(FVector(A.X,A.Y,0), FVector(B.X,B.Y,0), BedCorridor, BowlCorridor, 8.0f)) return false;
        }
        for (const auto& Sample : CandidatePath.Samples)
            Points.Add(FVector(Sample.Position.X, Sample.Position.Y, HomeLocation.Z));
        return true;
    };
    for (const Candidate& CandidatePath : BuildCSC(Initial, Final, MinimumTurnRadius, 6.0))
    {
        TArray<FVector> Points;
        if (AppendValid(CandidatePath, Points)) { Out = MoveTemp(Points); return true; }
    }
    // Two-station scenes sometimes need a detour beyond the four direct CSC
    // families. Search bounded staging poses; every chord is checked again.
    double BestLength = TNumericLimits<double>::Max();
    TArray<FVector> Best;
    // A single 160 cm detour ring was insufficient when the user placed the
    // bed between the cat and bowl near a screen edge. Search several bounded
    // rings and headings; every generated chord still passes SegmentClear.
    for (double DetourRadius : {80.0,110.0,150.0,200.0,260.0})
    for (int32 I = 0; I < 24; ++I)
    {
        const double Angle = I * UE_TWO_PI / 24.0;
        const FVector Mid = (Start + Goal) * .5 + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * DetourRadius;
        const FVector Toward = (Goal - Mid).GetSafeNormal2D();
        const Pose2 Middle{{ Mid.X,Mid.Y }, FMath::Atan2(Toward.Y,Toward.X)};
        for (const Candidate& First : BuildCSC(Initial, Middle, MinimumTurnRadius, 6.0))
        {
            if (First.Length >= BestLength) continue;
            TArray<FVector> Prefix;
            if (!AppendValid(First, Prefix)) continue;
            for (const Candidate& Second : BuildCSC(Middle, Final, MinimumTurnRadius, 6.0))
            {
                if (First.Length + Second.Length >= BestLength) continue;
                TArray<FVector> Suffix;
                if (!AppendValid(Second, Suffix)) continue;
                Best = Prefix; Best.Append(Suffix); BestLength = First.Length + Second.Length;
            }
        }
    }
    if (Best.IsEmpty()) return false;
    Out = MoveTemp(Best);
    return true;
}

FVector AMirmirCat::NavigationSteeringTarget(FVector Goal)
{
    if (!Habitat) return Goal;
    if (NavigationWaypoints.IsEmpty() && NavigationReplanSeconds > 0.0f) return GetActorLocation();
    for (const FName Id : { FName(TEXT("bed")), FName(TEXT("bowl")) })
    {
        const FVector Center = Habitat->GetStationFloorPosition(Id);
        const float Radius = Id == TEXT("bed") ? 84.0f : 59.0f;
        if (FVector::Dist2D(Goal, Center) < Radius)
        {
            FVector Outward = (Goal - Center).GetSafeNormal2D();
            if (Outward.IsNearlyZero()) Outward = (GetActorLocation() - Center).GetSafeNormal2D();
            Goal = Center + Outward * Radius;
        }
    }
    if (FVector::Dist2D(NavigationDestination, Goal) > 8.0 || (NavigationWaypoints.IsEmpty() && NavigationReplanSeconds <= 0))
    {
        NavigationDestination = Goal;
        NavigationWaypoints.Reset(); NavigationWaypointIndex = 0;
        NavigationReplanSeconds = .5f;
        FVector Heading = (Goal - GetActorLocation()).GetSafeNormal2D();
        if (Heading.IsNearlyZero()) Heading = GetActorForwardVector();
        for (float Offset : { 0.0f, 90.0f, -90.0f, 180.0f })
            if (PlanCurvedRoute(Goal, Heading.RotateAngleAxis(Offset, FVector::UpVector), false, false, NavigationWaypoints)) break;
    }
    const bool NearProtectedToy = Toy && Toy->IsPlaying() && Activity == EMirmirActivity::Play && !bNeedBowlExit &&
        FVector::Dist2D(GetActorLocation(), Toy->GetBallLocation()) <= 60.0f &&
        (FVector::Dist2D(Toy->GetBallLocation(), Habitat->GetStationFloorPosition(TEXT("bowl"))) < 52.0f ||
         FVector::Dist2D(Toy->GetBallLocation(), Habitat->GetStationFloorPosition(TEXT("bed"))) < 77.0f);
    if (NearProtectedToy && (NavigationWaypoints.IsEmpty() || FVector::Dist2D(GetActorLocation(), Goal) < 9.0f))
    {
        if (!bWaitingAtBlockedToy)
        {
            ++InteractionSerial; LastInteractionAction = TEXT("play"); bLastInteractionAccepted = true;
            InteractionMessage = TEXT("Top eşyanın yanında; Mırmır güvenli mesafede bekliyor.");
        }
        bWaitingAtBlockedToy = true; bWaitingAtToy = true;
        return GetActorLocation();
    }
    if (NavigationWaypoints.IsEmpty())
    {
        if (Activity == EMirmirActivity::Wander) ChooseWanderTarget();
        return GetActorLocation();
    }
    int32 Best = NavigationWaypointIndex;
    double Nearest = TNumericLimits<double>::Max();
    for (int32 I = Best; I < FMath::Min(NavigationWaypoints.Num(), Best + 18); ++I)
    {
        const double D = FVector::DistSquared2D(GetActorLocation(), NavigationWaypoints[I]);
        if (D < Nearest) { Nearest = D; Best = I; }
    }
    NavigationWaypointIndex = Best;
    return NavigationWaypoints[FMath::Min(Best + int32(Mirmir::Navigation::SteeringLookAheadSamples), NavigationWaypoints.Num() - 1)];
}

void AMirmirCat::BeginHabitatApproach()
{
    bHabitatTraveling = false; bHabitatArrived = false;
    HabitatWaypoints.Reset(); HabitatWaypointIndex = 0;
    if (!Habitat || (Activity != EMirmirActivity::Eat && Activity != EMirmirActivity::Sleep)) return;
    const bool Sleeping = Activity == EMirmirActivity::Sleep;
    const FVector Station = Habitat->GetStationFloorPosition(Sleeping ? TEXT("bed") : TEXT("bowl"));
    FVector Outward = (GetActorLocation() - Station).GetSafeNormal2D();
    if (Outward.IsNearlyZero()) Outward = -GetActorForwardVector();
    if (!Sleeping) BowlApproachOutward = Outward;
    FVector Heading = Sleeping ? FVector::ForwardVector : -Outward;
    HabitatDestination = Sleeping ? Station : Station + Outward * 31.0f;
    FVector Staging = Sleeping ? Station - FVector(90,0,0) : Station + Outward * 100.0f;
    const bool AlreadyOnBed=Sleeping && GetActorLocation().Z>HomeLocation.Z+.5f && FVector::Dist2D(GetActorLocation(),Station)<12;
    if (AlreadyOnBed || (FVector::Dist2D(GetActorLocation(), HabitatDestination) < (Sleeping ? 10.0f : 4.5f) &&
        FVector::DotProduct(GetActorForwardVector(), Heading) > .9))
    {
        if (AlreadyOnBed) BedForwardSign=GetActorForwardVector().X>=0 ? 1.0f : -1.0f;
        bHabitatArrived = true;
        Behavior.RestartActivity(Sleeping ? 30.0 : 12.0);
        FeedingSeconds = 0.0f;
        return;
    }
    bool bPlanned=false;
    if (Sleeping)
    {
        float BestDistance=TNumericLimits<float>::Max();
        TArray<FVector> BestPath;
        for (float Sign : {1.0f,-1.0f})
        {
            TArray<FVector> Candidate;
            if (!PlanCurvedRoute(Station-FVector(90*Sign,0,0),FVector(Sign,0,0),false,false,Candidate)) continue;
            float Distance=0; FVector Previous=GetActorLocation();
            for (const FVector& Point:Candidate) {Distance+=FVector::Dist2D(Previous,Point);Previous=Point;}
            if (Distance<BestDistance) {BestDistance=Distance;BedForwardSign=Sign;BestPath=MoveTemp(Candidate);}
        }
        bPlanned=!BestPath.IsEmpty(); HabitatWaypoints=MoveTemp(BestPath);
        Staging=Station-FVector(90*BedForwardSign,0,0); Heading=FVector(BedForwardSign,0,0);
    }
    // A close bowl cannot always be approached on the initial radial line
    // with the cat's finite turning radius. Try other real approach sides.
    if (!Sleeping)
    {
        const FVector InitialOutward=Outward;
        float BestDistance=TNumericLimits<float>::Max();
        FVector BestOutward=Outward;
        TArray<FVector> BestPath;
        for (float Angle : {0.0f,30.0f,-30.0f,60.0f,-60.0f,90.0f,-90.0f,135.0f,-135.0f,180.0f})
        {
            Outward=InitialOutward.RotateAngleAxis(Angle,FVector::UpVector);
            BowlApproachOutward=Outward;
            Staging=Station+Outward*100.0f;
            HabitatDestination=Station+Outward*31.0f;
            Heading=-Outward;
            TArray<FVector> Candidate;
            const bool bLaneClear=SegmentClear(Staging,HabitatDestination,false,true);
            const bool bRouteClear=bLaneClear && PlanCurvedRoute(Staging,Heading,false,true,Candidate);
            if (FParse::Param(FCommandLine::Get(),TEXT("MirmirRouteDebug")))
                UE_LOG(LogTemp,Warning,TEXT("Mirmir route angle=%.1f lane=%d route=%d stage=%s destination=%s"),
                    Angle,bLaneClear,bRouteClear,*Staging.ToCompactString(),*HabitatDestination.ToCompactString());
            if (!bRouteClear) continue;
            float Distance=0; FVector Previous=GetActorLocation();
            for (const FVector& Point:Candidate) { Distance+=FVector::Dist2D(Previous,Point); Previous=Point; }
            if (Distance<BestDistance) { BestDistance=Distance; BestOutward=Outward; BestPath=MoveTemp(Candidate); }
        }
        bPlanned=!BestPath.IsEmpty(); HabitatWaypoints=MoveTemp(BestPath);
        Outward=BestOutward; BowlApproachOutward=Outward; Heading=-Outward;
        Staging=Station+Outward*100.0f; HabitatDestination=Station+Outward*31.0f;
    }
    if (!bPlanned)
    {
        InteractionMessage = TEXT("Mırmır güvenli bir yaklaşma yolu arıyor.");
        bHabitatTraveling = true;
        NavigationReplanSeconds = .5f;
        return;
    }
    const int32 N = FMath::Max(1, FMath::CeilToInt(FVector::Dist2D(Staging, HabitatDestination) / 6.0));
    FVector Previous = Staging;
    for (int32 I = 1; I <= N; ++I)
    {
        const FVector Next = FMath::Lerp(Staging, HabitatDestination, double(I)/N);
        if (!SegmentClear(Previous, Next, Sleeping, !Sleeping, 0.5f))
        {
            HabitatWaypoints.Reset(); bHabitatTraveling=true; NavigationReplanSeconds=.5f;
            InteractionMessage=TEXT("Mırmır güvenli bir yaklaşma yolu arıyor."); return;
        }
        HabitatWaypoints.Add(Next); Previous = Next;
    }
    bHabitatTraveling = true;
    if (!Sleeping) Habitat->SetFoodVisible(true);
}

FVector AMirmirCat::HabitatSteeringTarget()
{
    if (HabitatWaypoints.IsEmpty()) return GetActorLocation();
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
    return HabitatWaypoints[FMath::Min(Best + int32(Mirmir::Navigation::SteeringLookAheadSamples), HabitatWaypoints.Num() - 1)];
}

void AMirmirCat::UpdateHabitat(float DeltaSeconds)
{
    PoseSeconds += DeltaSeconds;
    if (bHabitatTraveling)
    {
        Behavior.KeepActivityFor(30.0);
        if (HabitatWaypoints.IsEmpty() && NavigationReplanSeconds <= 0) BeginHabitatApproach();
        if (!HabitatWaypoints.IsEmpty() && HabitatWaypointIndex >= HabitatWaypoints.Num() - 8 &&
            FVector::Dist2D(GetActorLocation(), HabitatDestination) <= (Activity == EMirmirActivity::Eat ? 4.5f : 12.0f))
        {
            bHabitatTraveling = false;
            bHabitatArrived = true;
            Behavior.RestartActivity(Activity == EMirmirActivity::Eat ? 12.0 : 30.0);
            FeedingSeconds = 0.0f;
            InteractionMessage = Activity == EMirmirActivity::Eat ? TEXT("Mırmır mamasını yiyor.") : TEXT("Mırmır yatağında dinleniyor.");
            LastInteractionAction = Activity == EMirmirActivity::Eat ? TEXT("feed") : TEXT("rest");
            bLastInteractionAccepted = true;
            ++InteractionSerial;
        }
    }
    const bool bResting = Activity == EMirmirActivity::Sleep && bHabitatArrived && !bHabitatTraveling && MovementSpeed < 1.0f;
    RestSettleSeconds = bResting ? RestSettleSeconds+DeltaSeconds : 0.0f;
    RestPoseAlpha = FMath::FInterpConstantTo(RestPoseAlpha, bResting && RestSettleSeconds>.45f ? 1.0f : 0.0f,
        DeltaSeconds, bResting ? .45f : .65f);
    RestPhase = bHabitatTraveling && Activity == EMirmirActivity::Sleep ? TEXT("Approach") :
        bResting ? (RestPoseAlpha < .99f ? TEXT("Settle") : TEXT("Sleeping")) :
        RestPoseAlpha > .01f ? TEXT("Rise") : TEXT("Standing");
    RestBreathing = RestPoseAlpha * FMath::Sin(PoseSeconds * UE_TWO_PI * .24) * .12f;
    const bool bEating = Activity == EMirmirActivity::Eat && bHabitatArrived && !bHabitatTraveling && MovementSpeed < 1.0f;
    if (bEating) FeedingSeconds += DeltaSeconds;
    const bool LowerHead = bEating && FeedingSeconds > .55f && FeedingSeconds < 10.2f;
    if (bEating)
    {
        const float LowerBlend=FMath::SmoothStep(0.0f,1.0f,FMath::Clamp((FeedingSeconds-.55f)/1.45f,0.0f,1.0f));
        const float RaiseBlend=1.0f-FMath::SmoothStep(0.0f,1.0f,FMath::Clamp((FeedingSeconds-10.2f)/1.15f,0.0f,1.0f));
        EatingPoseStrength=FMath::Min(LowerBlend,RaiseBlend);
    }
    else EatingPoseStrength=FMath::FInterpTo(EatingPoseStrength,0.0f,DeltaSeconds,4.0f);
    // Two short, asymmetric licks followed by a pause avoid the mechanical
    // metronome motion of the previous single sine wave.
    const float Cycle = FMath::Fmod(FMath::Max(0.0f,FeedingSeconds-2.0f), 1.05f);
    const float FirstLick=Cycle<.22f ? FMath::Square(FMath::Sin(Cycle/.22f*UE_PI)) : 0.0f;
    const float SecondT=(Cycle-.31f)/.26f;
    const float SecondLick=SecondT>=0.0f && SecondT<=1.0f ? .72f*FMath::Square(FMath::Sin(SecondT*UE_PI)) : 0.0f;
    FeedingJawAlpha = LowerHead && EatingPoseStrength > .97f ? FMath::Max(FirstLick,SecondLick) : 0.0f;
    FeedingPhase = bHabitatTraveling && Activity == EMirmirActivity::Eat ? TEXT("Approach") :
        bEating ? (FeedingSeconds < .55f ? TEXT("Settle") : FeedingSeconds > 10.2f ? TEXT("Raise") :
            EatingPoseStrength < .99f ? TEXT("Lower") : TEXT("Lick")) :
        EatingPoseStrength > .01f ? TEXT("Raise") : TEXT("None");
    if (Habitat)
    {
        // A cat eats from the near side of the food surface, not through the
        // ceramic centre. This keeps the collision-safe 36 cm body clearance
        // while placing the measured lip/tongue over visible kibble.
        const float SearchAlpha=EatingPoseStrength>.97f ? 1.0f : 0.0f;
        const FVector BowlTangent=FVector::CrossProduct(FVector::UpVector,BowlApproachOutward).GetSafeNormal();
        // A feeding cat samples neighbouring pieces instead of holding one
        // mathematically frozen muzzle point. Keep the search well inside the
        // visible near rim and small enough that the paws never have to move.
        const FVector SearchOffset=BowlTangent*(.65f*FMath::Sin(FeedingSeconds*.83f)+.25f*FMath::Sin(FeedingSeconds*1.73f))+
            BowlApproachOutward*(.25f*FMath::Sin(FeedingSeconds*.47f));
        FeedingTargetWorld = Habitat->GetBowlLocation() + BowlApproachOutward * 7.2f + SearchOffset*SearchAlpha +
            FVector(0,0,3.0f + .12f*FeedingJawAlpha + .18f*SearchAlpha*FMath::Sin(FeedingSeconds*1.1f));
    }
}

void AMirmirCat::UpdatePlayPose(float DeltaSeconds, bool bChasingToy)
{
    if (!bChasingToy || bNeedBowlExit || bAnimationTransitionBlocksMovement)
    {
        PlayPhase=TEXT("None"); PlayPhaseSeconds=0; PlayPawAlpha=0; bPawStrikeWindow=false;
        return;
    }
    const FVector LocalBall=GetActorTransform().InverseTransformPosition(Toy->GetBallLocation());
    const bool Reachable=LocalBall.X>12.0f && LocalBall.X<40.0f && FMath::Abs(LocalBall.Y)<18.0f;
    if (PlayPhase == TEXT("None")) { PlayPhase=TEXT("Chase"); PlayPhaseSeconds=0; }
    PlayPhaseSeconds += DeltaSeconds;
    if (PlayPhase==TEXT("Chase") && Reachable && Toy->GetBallSpeedCmPerSecond()<45.0f)
    {
        PlayPhase=TEXT("Plant"); PlayPhaseSeconds=0; bWaitingAtToy=true;
        PlayPawIndex=LocalBall.Y<0 ? 0 : 1;
        StrikeBallWorld=Toy->GetBallLocation();
    }
    if (PlayPhase==TEXT("Plant") && MovementSpeed<1.0f && PlayPhaseSeconds>.4f)
    { PlayPhase=TEXT("Strike"); PlayPhaseSeconds=0; StrikeBallWorld=Toy->GetBallLocation(); }
    if (PlayPhase==TEXT("Strike") && PlayPhaseSeconds>.62f)
    { PlayPhase=TEXT("Recover"); PlayPhaseSeconds=0; }
    if (PlayPhase==TEXT("Recover") && PlayPhaseSeconds>.6f)
    { PlayPhase=TEXT("Chase"); PlayPhaseSeconds=0; bWaitingAtToy=false; }
    bPawStrikeWindow=PlayPhase==TEXT("Strike") && PlayPhaseSeconds>.20f && PlayPhaseSeconds<.50f;
    const FVector Forward=GetActorForwardVector();
    // First lift and draw back, then pass through the ball along the floor.
    if (PlayPhase==TEXT("Strike"))
    {
        const float T=FMath::Clamp(PlayPhaseSeconds/.55f,0.0f,1.0f);
        const float Sweep=FMath::SmoothStep(.20f,.85f,T);
        PawTargetWorld=StrikeBallWorld + Forward*FMath::Lerp(-8.0f,8.0f,Sweep) + FVector(0,0,4.0f*FMath::Sin(T*UE_PI));
        PlayPawAlpha=FMath::SmoothStep(0.0f,.18f,T);
    }
    else if (PlayPhase==TEXT("Recover")) PlayPawAlpha=FMath::Max(0.0f,1.0f-PlayPhaseSeconds/.5f);
    else PlayPawAlpha=0;
    if (PlayPhase!=TEXT("Chase")) bWaitingAtToy=true;
}

void AMirmirCat::MeasureEvaluatedPose(float DeltaSeconds)
{
    if (!AppearanceHeadBoneName.IsNone())
        MuzzleWorldLocation=CatMesh->GetSocketTransform(AppearanceHeadBoneName).TransformPosition(MuzzleBoneLocal);
    if (PawTipNames[PlayPawIndex].IsNone()) return;
    const FVector Paw=CatMesh->GetBoneLocation(PawTipNames[PlayPawIndex]);
    if (bHasPreviousPaw && bPawStrikeWindow && Toy && DeltaSeconds>SMALL_NUMBER)
    {
        const FVector Velocity=(Paw-PreviousPawWorld)/DeltaSeconds;
        if (Toy->TryPawSwipe(Paw,Velocity))
        { PlayPhase=TEXT("Recover"); PlayPhaseSeconds=0; bPawStrikeWindow=false; }
    }
    PreviousPawWorld=Paw; bHasPreviousPaw=true;
}

void AMirmirCat::HandleBonesFinalized()
{
    MeasureEvaluatedPose(PoseMeasurementDelta);
}

void AMirmirCat::ChooseWanderTarget()
{
    const FVector Position = GetActorLocation();
    const float Radius = FMath::Max(60.0f, WanderRadius);
    const FVector TowardHome = HomeLocation + (HomeLocation - Position).GetSafeNormal2D() * 75.0f;
    if (FVector::Dist2D(Position, HomeLocation) > Radius * .75f &&
        IsInsideDesktop(TowardHome) && SegmentClear(TowardHome,TowardHome,false,false,8.0f))
    {
        WanderTarget = TowardHome;
        return;
    }
    float BestCost = TNumericLimits<float>::Max();
    FVector Best = Position;
    for (int32 Attempt = 0; Attempt < 24; ++Attempt)
    {
        const float Offset = MovementRandom.FRandRange(-135.0f, 135.0f);
        const float Angle = GetActorRotation().Yaw + Offset;
        const float MinimumDistance = FMath::Max(90.0f,
            2.0f * FMath::Clamp(MinimumTurnRadius,45.0f,100.0f) * FMath::Abs(FMath::Sin(FMath::DegreesToRadians(Offset))) + 20.0f);
        const FVector Candidate = Position + FRotator(0,Angle,0).Vector() * MovementRandom.FRandRange(MinimumDistance, MinimumDistance + 25.0f);
        if (!IsInsideDesktop(Candidate) || !SegmentClear(Candidate,Candidate,false,false,8.0f)) continue;
        const float Distance = FVector::Dist2D(Candidate, HomeLocation);
        const float Cost = FMath::Max(0.0f,Distance - Radius * .7f) * 10.0f + Distance * .1f;
        if (Cost < BestCost) { BestCost = Cost; Best = Candidate; }
    }
    WanderTarget = Best;
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
        if (Previous == EMirmirActivity::Eat)
        {
            bNeedBowlExit = bHabitatArrived || (Habitat && FVector::Dist2D(GetActorLocation(),Habitat->GetBowlLocation())<55.0f);
            if (Habitat && bHabitatArrived) Habitat->SetFoodVisible(false);
        }
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
    if (Accepted) { FeedingSeconds=0.0f; if (bHabitatArrived) Behavior.RestartActivity(12.0); }
    if (Accepted && Activity == EMirmirActivity::Eat && !bHabitatTraveling && !bHabitatArrived) BeginHabitatApproach();
    if (Accepted && Habitat) Habitat->SetFoodVisible(true);
    return Accepted;
}
bool AMirmirCat::Play()
{
    const bool Accepted = Interact(Mirmir::Interaction::Play, TEXT("Play"), PlaySound);
    if (Accepted)
    {
        bToyStartPending=true; bWaitingAtBlockedToy=false;
        if (bNeedBowlExit) InteractionMessage=TEXT("Mırmır önce kabından uzaklaşıyor; sonra topuyla oynayacak.");
    }
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
