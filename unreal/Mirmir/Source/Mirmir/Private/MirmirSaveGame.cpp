#include "MirmirSaveGame.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
bool Number(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, double& Out)
{
    return Object->TryGetNumberField(Key, Out) && FMath::IsFinite(Out);
}
}

FString UMirmirSaveGame::SavePath()
{
    FString Base = FPlatformMisc::GetEnvironmentVariable(TEXT("XDG_DATA_HOME"));
    if (Base.IsEmpty() || FPaths::IsRelative(Base))
    {
        Base = FPaths::Combine(FPlatformProcess::UserHomeDir(), TEXT(".local/share"));
    }
    return FPaths::Combine(Base, TEXT("mirmir/unreal/cat-v1.json"));
}

bool UMirmirSaveGame::Load(UMirmirSaveGame& Out, FString& Error)
{
    const FString Path = SavePath();
    if (!IFileManager::Get().FileExists(*Path)) return true;
    FString Text;
    TSharedPtr<FJsonObject> Root;
    if (IFileManager::Get().FileSize(*Path) > 65536 ||
        !FFileHelper::LoadFileToString(Text, *Path) || Text.Len() > 65536 ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
    {
        Error = TEXT("Cannot read cat save JSON; file left untouched.");
        return false;
    }
    double Version, SavedAt, Satiety, Energy, Bond, Curiosity, Online, LastInteraction;
    double Activity, ActivitySeconds, Duration, Seed;
    if (!Number(Root, TEXT("version"), Version) || Version != 1.0 ||
        !Number(Root, TEXT("saved_at"), SavedAt) || SavedAt < 0.0 || SavedAt > 253402300799.0 ||
        !Number(Root, TEXT("satiety"), Satiety) || !Number(Root, TEXT("energy"), Energy) ||
        !Number(Root, TEXT("bond"), Bond) || !Number(Root, TEXT("curiosity"), Curiosity) ||
        !Number(Root, TEXT("online_seconds"), Online) ||
        !Number(Root, TEXT("last_interaction_seconds"), LastInteraction) ||
        !Number(Root, TEXT("activity"), Activity) || Activity < 0 || Activity > 7 ||
        Activity != FMath::FloorToDouble(Activity) ||
        !Number(Root, TEXT("activity_seconds"), ActivitySeconds) ||
        !Number(Root, TEXT("activity_duration"), Duration) ||
        !Number(Root, TEXT("random_state"), Seed) || Seed < 1 || Seed > 4294967295.0 ||
        Seed != FMath::FloorToDouble(Seed))
    {
        Error = TEXT("Invalid or unsupported cat save; file left untouched.");
        return false;
    }
    Mirmir::State State;
    State.Satiety = static_cast<float>(Satiety);
    State.Energy = static_cast<float>(Energy);
    State.Bond = static_cast<float>(Bond);
    State.Curiosity = static_cast<float>(Curiosity);
    State.OnlineSeconds = Online;
    State.LastInteractionSeconds = LastInteraction;
    State.CurrentActivity = static_cast<Mirmir::Activity>(static_cast<uint8>(Activity));
    State.ActivitySeconds = ActivitySeconds;
    State.ActivityDuration = Duration;
    State.RandomState = static_cast<uint32>(Seed);
    Out.CatState = Mirmir::Behavior(State).Get();
    Root->TryGetBoolField(TEXT("muted"), Out.bMuted);
    Out.SavedAtUnixSeconds = static_cast<int64>(SavedAt);
    return true;
}

bool UMirmirSaveGame::Store(const UMirmirSaveGame& Value, FString& Error)
{
    const Mirmir::State S = Mirmir::Behavior(Value.CatState).Get();
    const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("version"), 1);
    Root->SetBoolField(TEXT("muted"), Value.bMuted);
    Root->SetNumberField(TEXT("saved_at"), static_cast<double>(Value.SavedAtUnixSeconds));
    Root->SetNumberField(TEXT("satiety"), S.Satiety);
    Root->SetNumberField(TEXT("energy"), S.Energy);
    Root->SetNumberField(TEXT("bond"), S.Bond);
    Root->SetNumberField(TEXT("curiosity"), S.Curiosity);
    Root->SetNumberField(TEXT("online_seconds"), S.OnlineSeconds);
    Root->SetNumberField(TEXT("last_interaction_seconds"), S.LastInteractionSeconds);
    Root->SetNumberField(TEXT("activity"), static_cast<uint8>(S.CurrentActivity));
    Root->SetNumberField(TEXT("activity_seconds"), S.ActivitySeconds);
    Root->SetNumberField(TEXT("activity_duration"), S.ActivityDuration);
    Root->SetNumberField(TEXT("random_state"), S.RandomState);
    FString Text;
    if (!FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text)))
    {
        Error = TEXT("Cannot serialize cat state.");
        return false;
    }
    const FString Path = SavePath();
    const FString Temporary = Path + TEXT(".tmp");
    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true) ||
        !FFileHelper::SaveStringToFile(Text, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) ||
        !IFileManager::Get().Move(*Path, *Temporary, true, true))
    {
        Error = TEXT("Cannot atomically store cat state.");
        return false;
    }
    return true;
}
