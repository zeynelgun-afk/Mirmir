#include "MirmirCat.h"
#include "MirmirSaveGame.h"
#include "Components/AudioComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/DateTime.h"
#include "Sound/SoundBase.h"

AMirmirCat::AMirmirCat()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("CatRoot"));
    CatMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CatMesh"));
    CatMesh->SetupAttachment(RootComponent);
    CatMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    CatMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    CatMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

void AMirmirCat::BeginPlay()
{
    Super::BeginPlay();
    if (CatSkeletalMesh) CatMesh->SetSkeletalMesh(CatSkeletalMesh);
    HomeLocation = GetActorLocation();
    WanderTarget = HomeLocation;
    CatMesh->OnClicked.AddDynamic(this, &AMirmirCat::HandleClicked);
    UMirmirSaveGame* Save = NewObject<UMirmirSaveGame>(this);
    FString Error;
    bCanSave = UMirmirSaveGame::Load(*Save, Error);
    if (bCanSave)
    {
        Behavior.Restore(Save->CatState);
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
    RefreshState();
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
    Behavior.Advance(DeltaSeconds);
    RefreshState();
    const FVector Before = GetActorLocation();
    if (Activity == EMirmirActivity::Wander || Activity == EMirmirActivity::Play)
    {
        if (FVector::DistSquared2D(Before, WanderTarget) < 25.0f)
        {
            const float Angle = static_cast<float>(Behavior.Get().RandomState % 6283) / 1000.0f;
            const float Radius = FMath::Max(0.0f, WanderRadius) * 0.7f;
            WanderTarget = HomeLocation + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0);
        }
        const float Speed = FMath::Max(0.0f, WalkSpeed) * (Activity == EMirmirActivity::Play ? 1.6f : 1.0f);
        const FVector Next = FMath::VInterpConstantTo(Before, WanderTarget, DeltaSeconds, Speed);
        const FVector Direction = Next - Before;
        if (!Direction.IsNearlyZero())
        {
            SetActorRotation(FMath::RInterpTo(GetActorRotation(), Direction.Rotation(), DeltaSeconds, 6.0f));
            SetActorLocation(Next);
        }
    }
    MovementSpeed = DeltaSeconds > SMALL_NUMBER ? FVector::Dist(Before, GetActorLocation()) / DeltaSeconds : 0;
    UpdateAuthoredAnimation();
    OnVisualUpdate(DeltaSeconds, MovementSpeed);
    SaveElapsed += DeltaSeconds;
    if (SaveElapsed >= 30.0f)
    {
        SaveElapsed = 0;
        SaveCat();
    }
}

void AMirmirCat::UpdateAuthoredAnimation()
{
    // A user-selected AnimBlueprint owns animation completely. Otherwise use
    // authored clips, never procedural leg rotation or a walk cycle at rest.
    if (CatMesh->GetAnimationMode() == EAnimationMode::AnimationBlueprint && CatMesh->GetAnimClass()) return;
    UAnimSequence* Selected = MovementSpeed > 1.0f ? WalkAnimation.Get() : IdleAnimation.Get();
    if (Selected != ActiveSequence.Get() || CatMesh->GetAnimationMode() != EAnimationMode::AnimationSingleNode)
    {
        CatMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
        ActiveSequence = Selected;
        if (Selected) CatMesh->PlayAnimation(Selected, true);
        else
        {
            CatMesh->Stop();
            CatMesh->SetAnimation(nullptr); // No authored idle: stationary reference pose.
        }
    }
    if (Selected)
    {
        const float Rate = Selected == WalkAnimation.Get() && MovementSpeed > 1.0f
            ? FMath::Clamp(MovementSpeed / FMath::Max(1.0f, AuthoredWalkSpeed), 0.1f, 3.0f) : 1.0f;
        CatMesh->SetPlayRate(Rate);
    }
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
        if (IsValid(ActiveSound)) ActiveSound->Stop();
        WanderTarget = GetActorLocation();
        OnActivityChanged(Previous, Activity);
    }
}

bool AMirmirCat::Interact(Mirmir::Interaction Action, FName Name, USoundBase* Sound)
{
    const bool Accepted = Behavior.Interact(Action);
    RefreshState();
    if (Accepted)
    {
        if (IsValid(ActiveSound)) ActiveSound->Stop();
        if (!bMuted && Sound) ActiveSound = UGameplayStatics::SpawnSoundAtLocation(this, Sound, GetActorLocation());
        OnInteractionAccepted(Name);
    }
    return Accepted;
}

bool AMirmirCat::Pet() { return Interact(Mirmir::Interaction::Pet, TEXT("Pet"), PurrSound); }
bool AMirmirCat::Feed() { return Interact(Mirmir::Interaction::Feed, TEXT("Feed"), FeedSound); }
bool AMirmirCat::Play() { return Interact(Mirmir::Interaction::Play, TEXT("Play"), PlaySound); }
void AMirmirCat::ToggleMute()
{
    bMuted = !bMuted;
    if (bMuted && IsValid(ActiveSound)) ActiveSound->Stop();
}
void AMirmirCat::HandleClicked(UPrimitiveComponent* Component, FKey Key) { Pet(); }

bool AMirmirCat::SaveCat()
{
    if (!bCanSave) return false;
    UMirmirSaveGame* Save = NewObject<UMirmirSaveGame>(this);
    Save->CatState = Behavior.Get();
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
