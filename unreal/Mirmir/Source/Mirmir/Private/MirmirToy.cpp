#include "MirmirToy.h"

#include "MirmirCat.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
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
    BallMesh->SetHiddenInGame(true);
    BallMesh->SetCanEverAffectNavigation(false);
}

void AMirmirToy::ConfigureArena()
{
    const float Extent = FMath::Clamp(ArenaHalfExtentCm, 35.0f, 120.0f);
    // Surface matches the cat's normalized floor. This collision actor
    // stays fixed for the session and never expands the cat mesh's bounds.
    // The cat can start play on the 5 cm bed cushion. The arena still rests
    // on the world's floor, rather than lifting an invisible floor with it.
    Floor->SetRelativeLocation(FVector(0.0f, 0.0f, -SessionOrigin.Z - 1.0f));
    const AMirmirCat* Cat = OwnerCat.Get();
    const float FloorExtent = FMath::Max(Extent + 3.0f,
        FMath::Clamp(Cat ? Cat->WanderRadius : 120.0f, 0.0f, 500.0f) + 70.0f);
    Floor->SetBoxExtent(FVector(FloorExtent, FloorExtent, 1.0f));
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
    ConfigureArena();
    BallMesh->SetSimulatePhysics(false);
    BallMesh->AttachToComponent(ToyRoot, FAttachmentTransformRules::KeepWorldTransform);
    BallMesh->SetWorldScale3D(FVector(FMath::Clamp(BallRadiusCm, 1.0f, 6.0f) / 50.0f));
    BallMesh->SetPhysMaterialOverride(BallPhysicalMaterial);
    if (BallMaterial) BallMesh->SetMaterial(0, BallMaterial);

    const float Side = TossRandom.RandRange(0, 1) == 0 ? -1.0f : 1.0f;
    const FVector Forward = InCat->GetActorForwardVector().GetSafeNormal2D();
    const FVector Right = InCat->GetActorRightVector().GetSafeNormal2D();
    const FVector Start = SessionOrigin + Forward * 35.0f + Right * (12.0f * Side)
        + FVector::UpVector * (FMath::Clamp(BallRadiusCm, 1.0f, 6.0f) + 3.0f);
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
    BallMesh->SetPhysicsLinearVelocity(Forward * 36.0f + Right * (18.0f * Side)
        + FVector::UpVector * 60.0f);
    BallMesh->SetPhysicsAngularVelocityInDegrees(Right * (90.0f * Side));
    BallMesh->WakeAllRigidBodies();
    bToyActive = true;
    PlayingSeconds = 0.0f;
    LastStopReason = NAME_None;
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
    if (Position.ContainsNaN() || Velocity.ContainsNaN() || Position.Z < SessionOrigin.Z - 25.0f)
    { StopSession(TEXT("physics_out_of_bounds")); return; }
    // A final fail-safe ends the visible session rather than teleporting a
    // runaway ball or assigning a synthetic trajectory. Capture framing and
    // the cat's chase policy are the integrator's responsibility.
    if (FVector::DistSquared2D(Position, Cat->GetActorLocation()) >
        FMath::Square(FMath::Clamp(MaximumCatDistanceCm, 35.0f, 120.0f)))
    { StopSession(TEXT("outside_capture_radius")); }
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
