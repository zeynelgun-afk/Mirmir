#include "MirmirCat.h"
#include "MirmirSaveGame.h"
#include "Components/AudioComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/DateTime.h"
#include "Sound/SoundBase.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
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
    CatMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    CatMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

void AMirmirCat::BeginPlay()
{
    Super::BeginPlay();
    if (CatSkeletalMesh) CatMesh->SetSkeletalMesh(CatSkeletalMesh);
    if (bNormalizeImportedMesh && CatMesh->GetSkeletalMeshAsset()) NormalizeMesh();
    CatMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
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
    Behavior.Advance(DeltaSeconds);
    RefreshState();
    const FVector Before = GetActorLocation();
    if (Activity == EMirmirActivity::Wander || Activity == EMirmirActivity::Play)
    {
        if (FVector::DistSquared2D(Before, WanderTarget) < 25.0f)
        {
            const float Angle = MovementRandom.FRandRange(0.0f, 2.0f * PI);
            const float Radius = FMath::Max(0.0f, WanderRadius) * MovementRandom.FRandRange(0.3f, 0.85f);
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
    if (bDesktopMode)
    {
        DesktopCommandElapsed += DeltaSeconds;
        DesktopStatusElapsed += DeltaSeconds;
        if (DesktopCommandElapsed >= 0.25f)
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
        if (Activity == EMirmirActivity::SeekAttention) Meow();
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
void AMirmirCat::Meow()
{
    if (bMuted || !MeowSound || VoiceCooldown > 0 || (IsValid(ActiveSound) && ActiveSound->IsPlaying())) return;
    ActiveSound = UGameplayStatics::SpawnSoundAtLocation(this, MeowSound, GetActorLocation());
    VoiceCooldown = 30.0f;
}
void AMirmirCat::Rest()
{
    Behavior.Rest();
    RefreshState();
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
        if (Command == TEXT("pet")) Pet();
        else if (Command == TEXT("feed")) Feed();
        else if (Command == TEXT("play")) Play();
        else if (Command == TEXT("rest")) Rest();
        else if (Command == TEXT("mute")) ToggleMute();
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
    Root->SetNumberField(TEXT("updated_at_unix"), static_cast<double>(FDateTime::UtcNow().ToUnixTimestamp()));
    FString Text;
    if (!FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text))) return;
    const FString Path = FPaths::Combine(FPaths::GetPath(UMirmirSaveGame::SavePath()), TEXT("desktop-status.json"));
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
