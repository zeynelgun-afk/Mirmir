#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "CatBehavior.h"
#include "MirmirCat.generated.h"

class USkeletalMeshComponent;
class USoundBase;
class UPrimitiveComponent;
class UAudioComponent;
class USkeletalMesh;
class UAnimSequence;

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

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|Visual")
    TObjectPtr<USkeletalMeshComponent> CatMesh;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mirmir|Visual")
    TObjectPtr<USkeletalMesh> CatSkeletalMesh;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mirmir|Animation")
    TObjectPtr<UAnimSequence> WalkAnimation;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mirmir|Animation")
    TObjectPtr<UAnimSequence> IdleAnimation;
    // Centimeters per second matching the authored clip at play rate 1.0.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Animation", meta=(ClampMin="1.0"))
    float AuthoredWalkSpeed = 35.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Movement", meta=(ClampMin="0.0"))
    float WanderRadius = 120.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Movement", meta=(ClampMin="0.0"))
    float WalkSpeed = 35.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Sound")
    TObjectPtr<USoundBase> PurrSound;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Sound")
    TObjectPtr<USoundBase> FeedSound;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Sound")
    TObjectPtr<USoundBase> PlaySound;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Sound")
    bool bMuted = false;

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
    bool bCanSave = true;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> ActiveSound;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> ActiveSequence;

    void RefreshState();
    void UpdateAuthoredAnimation();
    bool Interact(Mirmir::Interaction Action, FName Name, USoundBase* Sound);
    UFUNCTION() void HandleClicked(UPrimitiveComponent* Component, FKey Key);
};
