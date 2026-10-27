#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "CatBehavior.h"
#include "MirmirSaveGame.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMirmirAbsenceTest,
    "Mirmir.Behavior.AbsencePreservesBondAndFood",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FMirmirAbsenceTest::RunTest(const FString& Parameters)
{
    Mirmir::State Initial;
    Initial.Satiety = 0.3f;
    Initial.Energy = 0.1f;
    Initial.Bond = 0.7f;
    Mirmir::Behavior Cat(Initial);
    Cat.ApplyOffline(365.0 * 86400.0);
    TestEqual(TEXT("No absence food penalty"), Cat.Get().Satiety, Initial.Satiety);
    TestEqual(TEXT("No absence bond penalty"), Cat.Get().Bond, Initial.Bond);
    TestEqual(TEXT("Rest replenishes energy"), Cat.Get().Energy, 1.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMirmirInteractionTest,
    "Mirmir.Behavior.PettingAndTiredPlay",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FMirmirInteractionTest::RunTest(const FString& Parameters)
{
    Mirmir::Behavior Cat;
    TestTrue(TEXT("Pet accepted"), Cat.Interact(Mirmir::Interaction::Pet));
    TestTrue(TEXT("Pet increases bond"), Cat.Get().Bond > 0.15f);
    TestFalse(TEXT("Input debounced"), Cat.Interact(Mirmir::Interaction::Pet));
    TestTrue(TEXT("Explicit feed is not blocked by pet debounce"), Cat.Interact(Mirmir::Interaction::Feed));
    Cat.Advance(1.0);
    TestTrue(TEXT("Pet while eating is accepted"), Cat.Interact(Mirmir::Interaction::Pet));
    TestTrue(TEXT("Pet preserves the meal"), Cat.Get().CurrentActivity == Mirmir::Activity::Eat);
    TestTrue(TEXT("Play can follow an explicit feed immediately"), Cat.Interact(Mirmir::Interaction::Play));
    Cat.Advance(1.0);
    TestTrue(TEXT("Pet while playing is accepted"), Cat.Interact(Mirmir::Interaction::Pet));
    TestTrue(TEXT("Pet preserves the ball game"), Cat.Get().CurrentActivity == Mirmir::Activity::Play);
    Mirmir::State Tired;
    Tired.Energy = 0.05f;
    Cat.Restore(Tired);
    TestFalse(TEXT("Tired cat declines play"), Cat.Interact(Mirmir::Interaction::Play));
    TestTrue(TEXT("Tired cat rests"), Cat.Get().CurrentActivity == Mirmir::Activity::Sleep);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMirmirAppearancePersistenceTest,
    "Mirmir.Persistence.AppearanceBackwardCompatibility",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FMirmirAppearancePersistenceTest::RunTest(const FString& Parameters)
{
    const FString Previous = FPlatformMisc::GetEnvironmentVariable(TEXT("XDG_DATA_HOME"));
    const FString Temporary = FPaths::ConvertRelativePathToFull(FPaths::Combine(
        FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("Appearance-") + FGuid::NewGuid().ToString()));
    FPlatformMisc::SetEnvironmentVar(TEXT("XDG_DATA_HOME"), *Temporary);
    ON_SCOPE_EXIT
    {
        FPlatformMisc::SetEnvironmentVar(TEXT("XDG_DATA_HOME"), *Previous);
        IFileManager::Get().DeleteDirectory(*Temporary, false, true);
    };
    UMirmirSaveGame* Original = NewObject<UMirmirSaveGame>();
    Original->CatState.Satiety = 0.37f;
    Original->CatState.Energy = 0.42f;
    Original->CatState.Bond = 0.63f;
    Original->CatState.Curiosity = 0.54f;
    Original->bMuted = true;
    Original->AppearanceId = TEXT("british");
    Original->SavedAtUnixSeconds = 1000;
    FString Error;
    if (!TestTrue(TEXT("Isolated fixture saved"), UMirmirSaveGame::Store(*Original, Error))) return false;
    UMirmirSaveGame* Loaded = NewObject<UMirmirSaveGame>();
    if (!TestTrue(TEXT("New appearance round trip loads"), UMirmirSaveGame::Load(*Loaded, Error))) return false;
    TestEqual(TEXT("British selection persisted"), Loaded->AppearanceId, FName(TEXT("british")));
    TestEqual(TEXT("Bond preserved"), Loaded->CatState.Bond, Original->CatState.Bond);
    TestEqual(TEXT("Energy preserved"), Loaded->CatState.Energy, Original->CatState.Energy);
    TestEqual(TEXT("Curiosity preserved"), Loaded->CatState.Curiosity, Original->CatState.Curiosity);
    TestEqual(TEXT("Food preserved"), Loaded->CatState.Satiety, Original->CatState.Satiety);
    TestTrue(TEXT("Mute preserved"), Loaded->bMuted);

    FString Text;
    TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Text, *UMirmirSaveGame::SavePath()) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root)) return false;
    auto WriteFixture = [&]()
    {
        FString Fixture;
        return FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&Fixture)) &&
            FFileHelper::SaveStringToFile(Fixture, *UMirmirSaveGame::SavePath());
    };
    Root->RemoveField(TEXT("appearance"));
    if (!TestTrue(TEXT("Old v1 fixture written"), WriteFixture())) return false;
    if (!TestTrue(TEXT("Old v1 fixture loads"), UMirmirSaveGame::Load(*Loaded, Error))) return false;
    TestEqual(TEXT("Old save defaults to requested Van"), Loaded->AppearanceId, FName(TEXT("van")));
    TestEqual(TEXT("Old-save bond is unchanged"), Loaded->CatState.Bond, Original->CatState.Bond);
    TestTrue(TEXT("Old-save mute is unchanged"), Loaded->bMuted);
    Root->SetStringField(TEXT("appearance"), TEXT("unknown-cat"));
    if (!WriteFixture() || !UMirmirSaveGame::Load(*Loaded, Error)) return false;
    TestEqual(TEXT("Unknown selection safely falls back to Van"), Loaded->AppearanceId, FName(TEXT("van")));
    TestEqual(TEXT("Unknown selection keeps food"), Loaded->CatState.Satiety, Original->CatState.Satiety);
    return true;
}
#endif
