#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "CatBehavior.h"
#include "MirmirSaveGame.generated.h"

// Data container; persistence uses versioned JSON under XDG_DATA_HOME rather
// than Unreal's platform SaveGameToSlot path.
UCLASS()
class MIRMIR_API UMirmirSaveGame : public USaveGame
{
    GENERATED_BODY()
public:
    Mirmir::State CatState;
    int64 SavedAtUnixSeconds = 0;
    bool bMuted = false;
    FName AppearanceId = TEXT("van");

    static FName NormalizeAppearance(FName Value);
    static FString SavePath();
    static bool Load(UMirmirSaveGame& Out, FString& Error);
    static bool Store(const UMirmirSaveGame& Value, FString& Error);
};
