#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "MirmirCat.h"
#include "MirmirAnimInstance.generated.h"

// Native BlendStack graph. Blueprint subclasses retain their own graph.
UCLASS(Blueprintable, Transient)
class MIRMIR_API UMirmirAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
    UPROPERTY(BlueprintReadOnly, Category="Mirmir") EMirmirActivity Activity = EMirmirActivity::Idle;
    UPROPERTY(BlueprintReadOnly, Category="Mirmir") float Speed = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category="Mirmir") float Energy = 0.85f;
    UPROPERTY(BlueprintReadOnly, Category="Mirmir") float Bond = 0.15f;
    UPROPERTY(BlueprintReadOnly, Transient, Category="Mirmir|Animation") TObjectPtr<UAnimSequence> ActiveClip;
    UPROPERTY(BlueprintReadOnly, Category="Mirmir|Animation") float ActiveClipTime = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category="Mirmir|Animation") float ActivePlayRate = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category="Mirmir|Animation") int32 ActiveBlendCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Mirmir|Animation") FName PlaybackState = TEXT("None");
    UPROPERTY(BlueprintReadOnly, Category="Mirmir|Animation") bool bTransitionBlocksMovement = false;
    UPROPERTY(BlueprintReadOnly, Category="Mirmir|Animation") int32 StanceValidLegs = 0;
    UPROPERTY(BlueprintReadOnly, Category="Mirmir|Animation") TArray<float> StanceWeights;
    UPROPERTY(BlueprintReadOnly, Category="Mirmir|Animation") TArray<float> StanceCorrectionCm;
    UPROPERTY(BlueprintReadOnly, Category="Mirmir|Animation") TArray<float> StanceResidualCm;

protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
};
