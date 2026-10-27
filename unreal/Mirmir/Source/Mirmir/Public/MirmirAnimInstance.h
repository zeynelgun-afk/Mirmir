#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "MirmirCat.h"
#include "MirmirAnimInstance.generated.h"

// Parent an Animation Blueprint to this class and connect licensed in-place
// animation clips in its state machine. No animation assets are bundled.
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
};
