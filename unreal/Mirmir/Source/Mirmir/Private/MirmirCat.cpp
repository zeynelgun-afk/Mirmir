#include "MirmirCat.h"
#include "MirmirSaveGame.h"
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
        const FVector ToTarget = WanderTarget - Before;
        const float RemainingDistance = ToTarget.Size2D();
        if (RemainingDistance > SMALL_NUMBER)
        {
            const FVector DesiredDirection = ToTarget.GetSafeNormal2D();
            const FRotator DesiredRotation(0.0f, DesiredDirection.Rotation().Yaw, 0.0f);
            const FRotator Facing = FMath::RInterpConstantTo(
                GetActorRotation(), DesiredRotation, DeltaSeconds, 90.0f);
            SetActorRotation(Facing);
            const FVector Forward = Facing.Vector().GetSafeNormal2D();
            const float Alignment = FMath::Clamp(
                FVector::DotProduct(Forward, DesiredDirection), 0.0f, 1.0f);
            // Travel along the facing direction, not sideways toward the target
            // while yaw catches up. Slow around sharp turns; there is no
            // separately authored turn-in-place clip in the current asset.
            const float Step = FMath::Min(
                Speed * Alignment * Alignment * DeltaSeconds, RemainingDistance);
            SetActorLocation(Before + Forward * Step);
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

bool AMirmirCat::Pet() { return Interact(Mirmir::Interaction::Pet, TEXT("Pet"), PurrSound); }
bool AMirmirCat::Feed() { return Interact(Mirmir::Interaction::Feed, TEXT("Feed"), FeedSound); }
bool AMirmirCat::Play() { return Interact(Mirmir::Interaction::Play, TEXT("Play"), PlaySound); }
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
