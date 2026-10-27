#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/HUD.h"
#include "MirmirGameMode.generated.h"

class AMirmirCat;

UCLASS()
class MIRMIR_API AMirmirPlayerController : public APlayerController
{
    GENERATED_BODY()
protected:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
private:
    void PetCat();
    void FeedCat();
    void PlayWithCat();
    void MuteCat();
    void QuitCompanion();
};

UCLASS()
class MIRMIR_API AMirmirHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};

UCLASS(Blueprintable)
class MIRMIR_API AMirmirGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AMirmirGameMode();
    virtual void BeginPlay() override;
    UPROPERTY(EditDefaultsOnly, Category="Mirmir") TSubclassOf<AMirmirCat> CatClass;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mirmir") TObjectPtr<AMirmirCat> Cat;
};
