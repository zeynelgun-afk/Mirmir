#include "MirmirAnimInstance.h"

void UMirmirAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);
    if (const AMirmirCat* Cat = Cast<AMirmirCat>(GetOwningActor()))
    {
        Activity = Cat->Activity;
        Speed = Cat->MovementSpeed;
        Energy = Cat->Energy;
        Bond = Cat->Bond;
    }
}
