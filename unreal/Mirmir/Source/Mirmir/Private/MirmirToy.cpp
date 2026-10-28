#include "MirmirToy.h"

#include "MirmirCat.h"
#include "MirmirHabitat.h"
#include "CollisionQueryParams.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
void ConfigureArenaCollision(UBoxComponent* Box)
{
    Box->SetMobility(EComponentMobility::Movable);
    Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Ignore);
    Box->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
    Box->SetGenerateOverlapEvents(false);
    Box->SetHiddenInGame(true);
    Box->SetVisibility(false);
    Box->SetCanEverAffectNavigation(false);
}
}

AMirmirToy::AMirmirToy()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
    TossRandom.Initialize(34721);

    ToyRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ToyRoot"));
    SetRootComponent(ToyRoot);

    Floor = CreateDefaultSubobject<UBoxComponent>(TEXT("ToyFloor"));
    Floor->SetupAttachment(ToyRoot);
    ConfigureArenaCollision(Floor);
    for (int32 Index = 0; Index < 4; ++Index)
    {
        UBoxComponent* Rim = CreateDefaultSubobject<UBoxComponent>(
            *FString::Printf(TEXT("ToyArenaRim%d"), Index));
        Rim->SetupAttachment(ToyRoot);
        ConfigureArenaCollision(Rim);
        ArenaRims.Add(Rim);
    }

    BallMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ToyBall"));
    BallMesh->SetupAttachment(ToyRoot);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(
        TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (Sphere.Succeeded()) BallMesh->SetStaticMesh(Sphere.Object);
    // Engine BasicShapes Sphere has a 50 cm radius. Scale its existing simple
    // collision with the visible geometry instead of adding a second body.
    BallMesh->SetRelativeScale3D(FVector(BallRadiusCm / 50.0f));
    BallMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BallMesh->SetCollisionObjectType(ECC_PhysicsBody);
    BallMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    BallMesh->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
    BallMesh->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
    BallMesh->SetGenerateOverlapEvents(false);
    BallMesh->SetEnableGravity(true);
    BallMesh->SetLinearDamping(0.9f);
    BallMesh->SetAngularDamping(1.2f);
    BallMesh->SetUseCCD(true);
    BallMesh->SetNotifyRigidBodyCollision(true);
    BallMesh->OnComponentHit.AddDynamic(this, &AMirmirToy::OnBallHit);
    BallMesh->SetHiddenInGame(true);
    BallMesh->SetCanEverAffectNavigation(false);
}

bool AMirmirToy::FindFreeSpawn(const AMirmirCat* Cat, FVector& Position) const
{
    if (!IsValid(Cat) || !GetWorld()) return false;
    const float Radius = FMath::Clamp(BallRadiusCm, 1.0f, 6.0f);
    const FVector Forward = Cat->GetActorForwardVector().GetSafeNormal2D();
    const FVector Right = Cat->GetActorRightVector().GetSafeNormal2D();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MirmirToySpawn), false, this);
    // Prefer a reachable point in front of the planted forepaws. Check both
    // sides and progressively wider angles if a moved bowl/bed occupies it.
    for (float Distance : { 31.0f, 38.0f, 44.0f })
    for (float Angle : { 0.0f, 28.0f, -28.0f, 55.0f, -55.0f, 90.0f, -90.0f })
    {
        FVector Candidate = Cat->GetActorLocation() +
            (Forward * FMath::Cos(FMath::DegreesToRadians(Angle)) +
             Right * FMath::Sin(FMath::DegreesToRadians(Angle))) * Distance;
        Candidate.Z = Radius + 1.0f;
        bool bInsideFurniture = false;
        if (Cat->Habitat)
        {
            for (FName Id : { FName(TEXT("bed")), FName(TEXT("bowl")) })
            {
                const FBox Bounds = Cat->Habitat->GetStationBounds(Id).ExpandBy(Radius + 3.0f);
                if (Bounds.IsValid && Candidate.X >= Bounds.Min.X && Candidate.X <= Bounds.Max.X &&
                    Candidate.Y >= Bounds.Min.Y && Candidate.Y <= Bounds.Max.Y)
                { bInsideFurniture = true; break; }
            }
        }
        if (bInsideFurniture || GetWorld()->OverlapBlockingTestByChannel(Candidate,
            FQuat::Identity, ECC_PhysicsBody, FCollisionShape::MakeSphere(Radius + .5f), Query)) continue;
        Position = Candidate;
        return true;
    }
    return false;
}

void AMirmirToy::ConfigureArena()
{
    const float Extent = FMath::Clamp(ArenaHalfExtentCm, 35.0f, 120.0f);
    // Surface matches the cat's normalized floor. This collision actor
    // stays fixed for the session and never expands the cat mesh's bounds.
    // The cat can start play on the 5 cm bed cushion. The arena still rests
    // on the world's floor, rather than lifting an invisible floor with it.
    // Keep the visible contact plane at world Z=0, with a 20 cm solid volume
    // beneath it. The previous two-centimetre slab was breached during the
    // animated-contact RHI regression; retain CCD and a solid recovery volume.
    constexpr float FloorHalfThicknessCm = 10.0f;
    Floor->SetRelativeLocation(FVector(0.0f, 0.0f, -SessionOrigin.Z - FloorHalfThicknessCm));
    const AMirmirCat* Cat = OwnerCat.Get();
    const float FloorExtent = FMath::Max(Extent + 3.0f,
        FMath::Clamp(Cat ? Cat->WanderRadius : 120.0f, 0.0f, 500.0f) + 70.0f);
    Floor->SetBoxExtent(FVector(FloorExtent, FloorExtent, FloorHalfThicknessCm));
    for (int32 Index = 0; Index < ArenaRims.Num(); ++Index)
    {
        UBoxComponent* Rim = ArenaRims[Index];
        const bool bAlongX = Index < 2;
        const float Sign = Index % 2 == 0 ? 1.0f : -1.0f;
        Rim->SetRelativeLocation(bAlongX ? FVector(Sign * Extent, 0.0f, 9.0f)
                                        : FVector(0.0f, Sign * Extent, 9.0f));
        Rim->SetBoxExtent(bAlongX ? FVector(1.0f, Extent + 2.0f, 10.0f)
                                 : FVector(Extent + 2.0f, 1.0f, 10.0f));
        Rim->SetPhysMaterialOverride(FloorPhysicalMaterial);
        Rim->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    }
    Floor->SetPhysMaterialOverride(FloorPhysicalMaterial);
    Floor->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

bool AMirmirToy::BeginPlayWithCat(AMirmirCat* InCat)
{
    if (!IsValid(InCat) || !BallMesh || !BallMesh->GetStaticMesh()) return false;
    if (bToyActive) StopSession(TEXT("restarted"));
    OwnerCat = InCat;
    SessionOrigin = InCat->GetActorLocation();
    SetActorLocationAndRotation(SessionOrigin, FRotator::ZeroRotator);

    if (!BallPhysicalMaterial)
    {
        BallPhysicalMaterial = NewObject<UPhysicalMaterial>(this);
        BallPhysicalMaterial->Friction = 0.55f;
        BallPhysicalMaterial->StaticFriction = 0.65f;
        BallPhysicalMaterial->Restitution = 0.38f;
        BallPhysicalMaterial->bOverrideRestitutionCombineMode = true;
        BallPhysicalMaterial->RestitutionCombineMode = EFrictionCombineMode::Max;
        FloorPhysicalMaterial = NewObject<UPhysicalMaterial>(this);
        FloorPhysicalMaterial->Friction = 0.6f;
        FloorPhysicalMaterial->StaticFriction = 0.7f;
        FloorPhysicalMaterial->Restitution = 0.12f;
    }
    BallMesh->SetSimulatePhysics(false);
    BallMesh->AttachToComponent(ToyRoot, FAttachmentTransformRules::KeepWorldTransform);
    BallMesh->SetWorldScale3D(FVector(FMath::Clamp(BallRadiusCm, 1.0f, 6.0f) / 50.0f));
    BallMesh->SetPhysMaterialOverride(BallPhysicalMaterial);
    if (BallMaterial) BallMesh->SetMaterial(0, BallMaterial);

    FVector Start;
    if (!FindFreeSpawn(InCat, Start))
    {
        StopSession(TEXT("no_safe_spawn"));
        return false;
    }
    ConfigureArena();
    const float Side = TossRandom.RandRange(0, 1) == 0 ? -1.0f : 1.0f;
    const FVector Forward = InCat->GetActorForwardVector().GetSafeNormal2D();
    const FVector Right = InCat->GetActorRightVector().GetSafeNormal2D();
    BallMesh->SetWorldLocation(Start, false, nullptr, ETeleportType::TeleportPhysics);
    BallMesh->SetHiddenInGame(false);
    BallMesh->SetVisibility(true);
    BallMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    BallMesh->SetSimulatePhysics(true);
    if (!BallMesh->IsSimulatingPhysics())
    {
        StopSession(TEXT("physics_unavailable"));
        return false;
    }
    BallMesh->SetMassOverrideInKg(NAME_None, 0.025f, true);
    // A gentle initial toss makes the object discoverable. Every subsequent
    // trajectory is Chaos integration/contact or a verified moving-paw impulse.
    BallMesh->SetPhysicsLinearVelocity(Forward * 12.0f + Right * (8.0f * Side)
        + FVector::UpVector * 20.0f);
    BallMesh->SetPhysicsAngularVelocityInDegrees(Right * (90.0f * Side));
    BallMesh->WakeAllRigidBodies();
    bToyActive = true;
    PlayingSeconds = 0.0f;
    LastStopReason = NAME_None;
    PawSwipeCount = 0;
    PhysicsContactCount = 0;
    LastSwipeTimeSeconds = -100.0f;
    LastSwipeImpulse = LastSwipeVelocityBefore = LastSwipeVelocityAfter = FVector::ZeroVector;
    LastSwipeContactWorld = LastPhysicsContactWorld = FVector::ZeroVector;
    LastSwipePawWorld = LastSwipePawVelocity = FVector::ZeroVector;
    LastSwipePawSampleFrame = -1;
    LastPhysicsContactImpulse = 0.0f;
    LastSwipeRejectReason = NAME_None;
    bSwipeVelocitySampleReady = bSwipeVelocitySamplePending = false;
    SetActorTickEnabled(true);
    return true;
}

void AMirmirToy::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bToyActive) return;
    PlayingSeconds += FMath::Max(DeltaSeconds, 0.0f);
    AMirmirCat* Cat = OwnerCat.Get();
    if (!IsValid(Cat)) { StopSession(TEXT("owner_missing")); return; }
    if (PlayingSeconds >= FMath::Clamp(SessionDurationSeconds, 2.0f, 30.0f))
    { StopSession(TEXT("timeout")); return; }
    const FVector Position = GetBallLocation();
    const FVector Velocity = GetBallVelocity();
    // Bone-finalization callbacks can run after this frame's physics solve.
    // Observe a later solved frame rather than calling unchanged velocity the
    // result of an impulse which has only just been queued to Chaos.
    if (bSwipeVelocitySamplePending && GFrameCounter > PendingSwipeFrame)
    {
        LastSwipeVelocityAfter = Velocity;
        bSwipeVelocitySampleReady = true;
        bSwipeVelocitySamplePending = false;
    }
    if (Position.ContainsNaN() || Velocity.ContainsNaN() || Position.Z < SessionOrigin.Z - 25.0f)
    { StopSession(TEXT("physics_out_of_bounds")); return; }
    const FVector Relative = Position - SessionOrigin;
    if (FMath::Abs(Relative.X) > ArenaHalfExtentCm + 12.0f ||
        FMath::Abs(Relative.Y) > ArenaHalfExtentCm + 12.0f ||
        Position.Z > 55.0f || Velocity.SizeSquared() > FMath::Square(240.0f))
    { StopSession(TEXT("physics_safety_limit")); return; }
    // A final fail-safe ends the visible session rather than teleporting a
    // runaway ball or assigning a synthetic trajectory. Capture framing and
    // the cat's chase policy are the integrator's responsibility.
    if (FVector::DistSquared2D(Position, Cat->GetActorLocation()) >
        FMath::Square(FMath::Clamp(MaximumCatDistanceCm, 35.0f, 120.0f)))
    { StopSession(TEXT("outside_capture_radius")); }
}

bool AMirmirToy::TryPawSwipe(const FVector& PawWorldLocation, const FVector& PawWorldVelocity)
{
    auto Reject = [this](FName Reason) { LastSwipeRejectReason = Reason; return false; };
    if (!bToyActive || !BallMesh || !BallMesh->IsSimulatingPhysics()) return Reject(TEXT("not_playing"));
    const AMirmirCat* Cat = OwnerCat.Get();
    if (!IsValid(Cat) || PawWorldLocation.ContainsNaN() || PawWorldVelocity.ContainsNaN())
        return Reject(TEXT("invalid_paw"));
    if (PlayingSeconds - LastSwipeTimeSeconds < .65f) return Reject(TEXT("cooldown"));
    if (FVector::DistSquared2D(PawWorldLocation, Cat->GetActorLocation()) > FMath::Square(58.0f))
        return Reject(TEXT("outside_paw_reach"));

    const FVector Ball = GetBallLocation();
    const FVector Velocity = GetBallVelocity();
    // Paw velocity is measured from consecutive animated bone positions. Its
    // recent segment may cross a small ball between two animation samples.
    const float SampleSeconds = FMath::Clamp(GetWorld()->GetDeltaSeconds(), .001f, .067f);
    const FVector Step = (PawWorldVelocity * SampleSeconds).GetClampedToMaxSize(18.0f);
    const FVector ContactPaw = FMath::ClosestPointOnSegment(Ball, PawWorldLocation - Step, PawWorldLocation);
    const float Radius = FMath::Clamp(BallRadiusCm, 1.0f, 6.0f);
    if (FVector::DistSquared(ContactPaw, Ball) > FMath::Square(Radius + 3.25f))
        return Reject(TEXT("no_paw_contact"));
    FVector Normal = (Ball - ContactPaw).GetSafeNormal2D();
    if (Normal.IsNearlyZero()) Normal = PawWorldVelocity.GetSafeNormal2D();
    const FVector RelativeVelocity = PawWorldVelocity - Velocity;
    const float ClosingSpeed = FVector::DotProduct(RelativeVelocity, Normal);
    if (!FMath::IsFinite(ClosingSpeed) || ClosingSpeed < 9.0f) return Reject(TEXT("paw_not_closing"));

    const float BallMass = FMath::Max(BallMesh->GetMass(), .001f);
    constexpr float EffectivePawMassKg = .08f;
    // Reduced-mass contact with a modest restitution. Tangential friction also
    // produces natural spin from an off-centre strike at the visible surface.
    const float ReducedMass = BallMass * EffectivePawMassKg / (BallMass + EffectivePawMassKg);
    const FVector Tangent = FVector::VectorPlaneProject(RelativeVelocity, Normal).GetClampedToMaxSize(35.0f);
    FVector Impulse = Normal * (1.18f * ReducedMass * FMath::Min(ClosingSpeed, 120.0f)) +
        FVector(Tangent.X, Tangent.Y, 0.0) * (ReducedMass * .16f);
    // Scale the applied momentum, never assign a synthetic post-hit velocity.
    // This keeps a small desktop toy controllable even if an animation jumps.
    constexpr float MaximumSwipeSpeed = 100.0f;
    float Scale = 1.0f;
    while (Scale > .03f && (Velocity + Impulse * (Scale / BallMass)).Size2D() > MaximumSwipeSpeed) Scale *= .7f;
    if (Scale <= .03f) return Reject(TEXT("speed_limit"));
    Impulse *= Scale;
    const FVector Contact = Ball - Normal * Radius;
    LastSwipeImpulse = Impulse;
    LastSwipeVelocityBefore = Velocity;
    LastSwipeVelocityAfter = Velocity;
    LastSwipeContactWorld = Contact;
    LastSwipePawWorld = PawWorldLocation;
    LastSwipePawVelocity = PawWorldVelocity;
    LastSwipePawSampleFrame = static_cast<int64>(GFrameCounter);
    LastSwipeTimeSeconds = PlayingSeconds;
    LastSwipeRejectReason = NAME_None;
    bSwipeVelocitySampleReady = false;
    bSwipeVelocitySamplePending = true;
    PendingSwipeFrame = GFrameCounter;
    ++PawSwipeCount;
    BallMesh->WakeAllRigidBodies();
    BallMesh->AddImpulseAtLocation(Impulse, Contact);
    return true;
}

void AMirmirToy::OnBallHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
    UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
    if (!bToyActive || NormalImpulse.ContainsNaN()) return;
    ++PhysicsContactCount;
    LastPhysicsContactWorld = Hit.ImpactPoint;
    LastPhysicsContactImpulse = static_cast<float>(NormalImpulse.Size());
}

FVector AMirmirToy::GetBallLocation() const
{
    return BallMesh ? BallMesh->GetComponentLocation() : GetActorLocation();
}

FVector AMirmirToy::GetBallVelocity() const
{
    return bToyActive && BallMesh ? BallMesh->GetPhysicsLinearVelocity() : FVector::ZeroVector;
}

float AMirmirToy::GetBallSpeedCmPerSecond() const
{
    return static_cast<float>(GetBallVelocity().Size());
}

AMirmirCat* AMirmirToy::GetOwnerCat() const
{
    return OwnerCat.Get();
}

void AMirmirToy::StopSession(FName Reason)
{
    if (BallMesh)
    {
        if (BallMesh->IsSimulatingPhysics())
        {
            BallMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
            BallMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
            BallMesh->SetSimulatePhysics(false);
        }
        BallMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        BallMesh->SetHiddenInGame(true);
    }
    Floor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    for (UBoxComponent* Rim : ArenaRims) Rim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    bToyActive = false;
    LastStopReason = Reason;
    OwnerCat.Reset();
    SetActorTickEnabled(false);
}

void AMirmirToy::EndPlaySession()
{
    StopSession(TEXT("session_ended"));
}

void AMirmirToy::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopSession(TEXT("actor_end_play"));
    Super::EndPlay(EndPlayReason);
}
