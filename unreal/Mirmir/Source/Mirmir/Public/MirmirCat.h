#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "CatBehavior.h"
#include "CatLocomotion.h"
#include "MirmirCat.generated.h"

class USkeletalMeshComponent;
class USoundBase;
class UPrimitiveComponent;
class UAudioComponent;
class USkeletalMesh;
class UAnimSequence;
class AMirmirToy;
class AMirmirHabitat;
class UBoxComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

UENUM(BlueprintType)
enum class EMirmirActivity : uint8
{
    Idle, Wander, Groom, Sleep, Play, Eat, SeekAttention, Petted
};

UCLASS(Blueprintable)
class MIRMIR_API AMirmirCat : public AActor
{
    GENERATED_BODY()
public:
    AMirmirCat();
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|Toy")
    TObjectPtr<AMirmirToy> Toy;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|Toy")
    bool bWaitingAtToy = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|Home") TObjectPtr<AMirmirHabitat> Habitat;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|Home") bool bHabitatTraveling = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|Home") bool bHabitatArrived = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|Home") float RestPoseAlpha = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|Home") float EatingPoseStrength = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|Home") float RestBreathing = 0.0f;
    uint64 InteractionSerial = 0;
    FName LastInteractionAction = NAME_None;
    bool bLastInteractionAccepted = false;
    FString InteractionMessage;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|Visual")
    TObjectPtr<USkeletalMeshComponent> CatMesh;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mirmir|Visual")
    TObjectPtr<USkeletalMesh> CatSkeletalMesh;
    // Imported skinned details (eyes, mouth, etc.) sharing the primary mesh's
    // skeleton and local coordinates. They inherit its normalization transform.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mirmir|Visual")
    TArray<TObjectPtr<USkeletalMesh>> AdditionalSkeletalMeshes;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Visual")
    bool bNormalizeImportedMesh = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Visual", meta=(ClampMin="20.0", ClampMax="150.0"))
    float TargetBodyLength = 70.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|Appearance")
    FName AppearanceId = TEXT("van");
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|Appearance")
    bool bAppearanceReady = false;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mirmir|Appearance")
    TObjectPtr<UMaterialInterface> CoatMaterialTemplate;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mirmir|Appearance")
    TObjectPtr<UMaterialInterface> EyeMaterialTemplate;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mirmir|Appearance")
    TObjectPtr<UMaterialInterface> FurMaterialTemplate;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Appearance")
    bool bEnableFurShells = true;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|Appearance")
    bool bFurShellsReady = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Appearance", meta=(ClampMin="0.0", ClampMax="1.0"))
    float VanFurLengthCm = 0.18f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Appearance", meta=(ClampMin="0.0", ClampMax="1.0"))
    float BritishFurLengthCm = 0.18f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Appearance", meta=(ClampMin="0.0", ClampMax="1.0"))
    float TuxedoFurLengthCm = 0.18f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Appearance", meta=(ClampMin="0.0", ClampMax="1.0"))
    float FurDensityFraction = 0.65f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|Appearance")
    FName AppearanceHeadBoneName;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|Appearance")
    float AppearanceHeadScale = 1.0f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mirmir|Animation")
    TObjectPtr<UAnimSequence> WalkAnimation;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mirmir|Animation")
    TObjectPtr<UAnimSequence> IdleAnimation;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mirmir|Animation")
    TObjectPtr<UAnimSequence> IdleVariationAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Animation", meta=(ClampMin="0.05", ClampMax="2.0"))
    float IdlePlayRate = 0.35f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Animation", meta=(ClampMin="0.05", ClampMax="2.0"))
    float IdleVariationPlayRate = 0.6f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mirmir|Animation")
    TObjectPtr<UAnimSequence> SitDownAnimation;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mirmir|Animation")
    TObjectPtr<UAnimSequence> SitIdleAnimation;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mirmir|Animation")
    TObjectPtr<UAnimSequence> StandUpAnimation;
    // Continuous authored sleep loop; no fabricated lying-down action.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mirmir|Animation")
    TObjectPtr<UAnimSequence> SleepAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Animation")
    bool bUseNativeAnimationBlending = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Animation")
    bool bPreferSittingWhenResting = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Animation", meta=(ClampMin="0.05", ClampMax="1.0"))
    float AnimationBlendSeconds = 0.25f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|Animation")
    bool bAnimationTransitionBlocksMovement = false;
    // Centimeters per second matching the authored clip at play rate 1.0.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Animation", meta=(ClampMin="1.0"))
    float AuthoredWalkSpeed = 68.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Movement", meta=(ClampMin="0.0"))
    float WanderRadius = 120.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Movement", meta=(ClampMin="0.0"))
    float WalkSpeed = 35.0f;
    // Forward arcs only. A separately authored step-turn is needed for a
    // convincing turn in place; rotating a frozen walk is deliberately avoided.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Movement", meta=(ClampMin="45.0", ClampMax="100.0"))
    float MinimumTurnRadius = 50.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|Movement")
    float LocomotionYawRate = 0.0f; // radians / second
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|Movement")
    float LocomotionCurvature = 0.0f; // inverse centimeters
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Animation")
    bool bEnableStanceIK = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Animation", meta=(ClampMin="0.0", ClampMax="6.0"))
    float MaximumStanceCorrectionCm = 4.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Interaction")
    bool bEnableCuriosityApproach = true;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|Interaction")
    float CuriosityHeadYaw = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|Interaction")
    float CuriosityHeadPitch = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|Interaction")
    float PetReactionStrength = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|Interaction")
    float PetStrokeDirection = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|Movement") bool bWalkingBackwards = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|Movement") bool bMovementBlocked = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|Movement") bool bExitingBed = false;
    FVector GetDesktopOrigin() const { return HomeLocation; }
    bool IsBedOccupied() const;
    void SetDesktopBounds(FVector Origin, FVector Right, FVector Up, float PixelsPerCM, FVector2D Minimum, FVector2D Maximum);
    bool IsInsideDesktop(FVector Position) const;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Sound")
    TObjectPtr<USoundBase> PurrSound;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Sound")
    TObjectPtr<USoundBase> MeowSound;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Sound")
    TObjectPtr<USoundBase> FeedSound;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Sound")
    TObjectPtr<USoundBase> PlaySound;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Sound")
    bool bMuted = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Sound", meta=(ClampMin="0.0", ClampMax="1.0"))
    float SoundVolume = 0.35f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|State")
    EMirmirActivity Activity = EMirmirActivity::Idle;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|State")
    float Satiety = 0.8f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|State")
    float Energy = 0.85f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|State")
    float Bond = 0.15f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|State")
    float Curiosity = 0.6f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|State")
    float MovementSpeed = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|State")
    FString PersistenceStatus;

    UFUNCTION(BlueprintCallable, Category="Mirmir|Interaction") bool Pet();
    UFUNCTION(BlueprintCallable, Category="Mirmir|Interaction") bool Feed();
    UFUNCTION(BlueprintCallable, Category="Mirmir|Interaction") bool Play();
    UFUNCTION(BlueprintCallable, Category="Mirmir|Interaction") void ToggleMute();
    UFUNCTION(BlueprintCallable, Category="Mirmir|Interaction") void Rest();
    // Called by the private desktop exporter after TTL/session validation.
    // No global screen coordinates or input history enter the actor.
    void SetCuriosityTarget(const FVector& Target, bool bActive);
    UFUNCTION(BlueprintCallable, Category="Mirmir|Appearance") void SetAppearance(FName Id);
    UFUNCTION(BlueprintCallable, Category="Mirmir|Persistence") bool SaveCat();
    UFUNCTION(BlueprintCallable, Category="Mirmir|Persistence") void ResetCat();

    UFUNCTION(BlueprintImplementableEvent, Category="Mirmir|Visual")
    void OnActivityChanged(EMirmirActivity Previous, EMirmirActivity Current);
    UFUNCTION(BlueprintImplementableEvent, Category="Mirmir|Visual")
    void OnVisualUpdate(float DeltaSeconds, float CurrentMovementSpeed);
    UFUNCTION(BlueprintImplementableEvent, Category="Mirmir|Interaction")
    void OnInteractionAccepted(FName InteractionName);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    Mirmir::Behavior Behavior;
    FVector HomeLocation = FVector::ZeroVector;
    FVector WanderTarget = FVector::ZeroVector;
    float SaveElapsed = 0.0f;
    float DesktopCommandElapsed = 0.0f;
    float DesktopStatusElapsed = 0.0f;
    bool bDesktopMode = false;
    FRandomStream MovementRandom;
    Mirmir::LocomotionState Locomotion;
    FVector CuriosityTarget = FVector::ZeroVector;
    float CuriosityVisitRemaining = 0.0f;
    float CuriosityWarmup = 0.0f;
    bool bCuriosityTargetActive = false;
    float PetReactionRemaining = 0.0f;
    TArray<FVector> HabitatWaypoints;
    int32 HabitatWaypointIndex = 0;
    FVector HabitatDestination = FVector::ZeroVector;
    TArray<FVector> NavigationWaypoints;
    int32 NavigationWaypointIndex = 0;
    FVector NavigationDestination = FVector::ZeroVector;
    float NavigationReplanSeconds = 0.0f;
    uint64 ObservedLayoutRevision = 0;
    bool bBedEntryPermission = false;
    bool bBowlClearancePermission = false;
    bool bNeedBowlExit = false;
    bool bWaitingAtBlockedToy = false;
    bool bLeavingBedTraversal = false;
    FVector BowlApproachOutward = -FVector::ForwardVector;
    bool bHasDesktopBounds = false;
    FVector DesktopOrigin, DesktopRight, DesktopUp;
    float DesktopScale = 1.0f;
    FVector2D DesktopMinimum, DesktopMaximum;
    UPROPERTY() TObjectPtr<UBoxComponent> CollisionBody;
    double PoseSeconds = 0.0;
    float VoiceCooldown = 3.0f;
    bool bWelcomed = false;
    bool bPurring = false;
    bool bCanSave = true;
    bool bToyStartPending = false;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> ActiveSound;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> ActiveSequence;
    UPROPERTY(Transient) TArray<TObjectPtr<USkeletalMeshComponent>> AdditionalMeshComponents;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> CoatMaterialInstances;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> EyeMaterialInstances;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> FurMaterialInstances;
    UPROPERTY(Transient) TArray<TObjectPtr<USkeletalMeshComponent>> FurMeshComponents;
    bool bAppearanceMaterialCacheAttempted = false;
    FTransform AppearanceBaseTransform = FTransform::Identity;
    FVector AppearanceMeshOrigin = FVector::ZeroVector;
    int32 AppearanceWidthAxis = 0;

    void RefreshState();
    void ChooseWanderTarget();
    void UpdateCuriosity(float DeltaSeconds);
    void BeginHabitatApproach();
    void UpdateHabitat(float DeltaSeconds);
    FVector HabitatSteeringTarget();
    bool SegmentClear(FVector A, FVector B, bool BedCorridor, bool BowlCorridor, float Margin = .5f) const;
    bool PlanCurvedRoute(FVector Goal, FVector EndForward, bool BedCorridor, bool BowlCorridor, TArray<FVector>& Out) const;
    FVector NavigationSteeringTarget(FVector Goal);
    void UpdateAuthoredAnimation();
    void NormalizeMesh();
    void AssembleAdditionalMeshes();
    void CacheAppearanceMaterials();
    void AssembleFurShells();
    void ApplyAppearance();
    void Meow();
    void PollDesktopCommands();
    void WriteDesktopStatus();
    bool Interact(Mirmir::Interaction Action, FName Name, USoundBase* Sound);
    UFUNCTION() void HandleClicked(UPrimitiveComponent* Component, FKey Key);
};
