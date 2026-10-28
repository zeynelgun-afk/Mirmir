#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MirmirToy.generated.h"

class AMirmirCat;
class UBoxComponent;
class UMaterialInterface;
class UPhysicalMaterial;
class UPrimitiveComponent;
class USceneComponent;
class UStaticMeshComponent;

// A real simulated ball in its own bounded collision arena. The cat owns the
// chase/animation; this actor accepts only contact-range, moving-paw impulses.
UCLASS(Blueprintable)
class MIRMIR_API AMirmirToy : public AActor
{
    GENERATED_BODY()

public:
    AMirmirToy();
    virtual void Tick(float DeltaSeconds) override;

    UFUNCTION(BlueprintCallable, Category="Mirmir|Toy")
    bool BeginPlayWithCat(AMirmirCat* InCat);

    UFUNCTION(BlueprintCallable, Category="Mirmir|Toy")
    void EndPlaySession();

    // Call during the animated strike with a measured distal-paw position and
    // velocity. A short swept contact segment avoids missing a fast paw between
    // animation samples; it never pulls or teleports the ball toward the paw.
    UFUNCTION(BlueprintCallable, Category="Mirmir|Toy")
    bool TryPawSwipe(const FVector& PawWorldLocation, const FVector& PawWorldVelocity);

    UFUNCTION(BlueprintPure, Category="Mirmir|Toy")
    FVector GetBallLocation() const;

    UFUNCTION(BlueprintPure, Category="Mirmir|Toy")
    FVector GetBallVelocity() const;

    UFUNCTION(BlueprintPure, Category="Mirmir|Toy")
    float GetBallSpeedCmPerSecond() const;

    UFUNCTION(BlueprintPure, Category="Mirmir|Toy")
    bool IsPlaying() const { return bToyActive; }

    UFUNCTION(BlueprintPure, Category="Mirmir|Toy")
    AMirmirCat* GetOwnerCat() const;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir|Toy")
    TObjectPtr<UStaticMeshComponent> BallMesh;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mirmir|Toy")
    TObjectPtr<UMaterialInterface> BallMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Toy", meta=(ClampMin="1.0", ClampMax="6.0"))
    float BallRadiusCm = 2.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Toy", meta=(ClampMin="35.0", ClampMax="120.0"))
    float ArenaHalfExtentCm = 55.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Toy", meta=(ClampMin="35.0", ClampMax="120.0"))
    float MaximumCatDistanceCm = 52.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirmir|Toy", meta=(ClampMin="2.0", ClampMax="30.0"))
    float SessionDurationSeconds = 15.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Mirmir|Toy")
    bool bToyActive = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Mirmir|Toy")
    float PlayingSeconds = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Mirmir|Toy")
    FName LastStopReason;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Mirmir|Toy")
    int32 PawSwipeCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Mirmir|Toy")
    int32 PhysicsContactCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Mirmir|Toy")
    float LastSwipeTimeSeconds = -100.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Mirmir|Toy")
    FVector LastSwipeImpulse = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Mirmir|Toy")
    FVector LastSwipeVelocityBefore = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Mirmir|Toy")
    FVector LastSwipeVelocityAfter = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Mirmir|Toy")
    FVector LastSwipeContactWorld = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Mirmir|Toy")
    FVector LastSwipePawWorld = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Mirmir|Toy")
    FVector LastSwipePawVelocity = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Mirmir|Toy")
    int64 LastSwipePawSampleFrame = -1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Mirmir|Toy")
    bool bSwipeVelocitySampleReady = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Mirmir|Toy")
    FName LastSwipeRejectReason;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Mirmir|Toy")
    FVector LastPhysicsContactWorld = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Mirmir|Toy")
    float LastPhysicsContactImpulse = 0.0f;

protected:
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UPROPERTY(VisibleAnywhere, Category="Mirmir|Toy")
    TObjectPtr<USceneComponent> ToyRoot;

    UPROPERTY(VisibleAnywhere, Category="Mirmir|Toy")
    TObjectPtr<UBoxComponent> Floor;

    UPROPERTY(VisibleAnywhere, Category="Mirmir|Toy")
    TArray<TObjectPtr<UBoxComponent>> ArenaRims;

    UPROPERTY(Transient)
    TObjectPtr<UPhysicalMaterial> BallPhysicalMaterial;

    UPROPERTY(Transient)
    TObjectPtr<UPhysicalMaterial> FloorPhysicalMaterial;

    UPROPERTY(Transient)
    TWeakObjectPtr<AMirmirCat> OwnerCat;

    FVector SessionOrigin = FVector::ZeroVector;
    FRandomStream TossRandom;
    bool bSwipeVelocitySamplePending = false;
    uint64 PendingSwipeFrame = 0;

    void StopSession(FName Reason);
    void ConfigureArena();
    bool FindFreeSpawn(const AMirmirCat* Cat, FVector& Position) const;
    UFUNCTION()
    void OnBallHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
        UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);
};
