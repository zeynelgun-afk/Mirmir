#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MirmirCat.h"
#include "MirmirHabitat.h"
#include "MirmirToy.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

// This is a real Chaos body in an isolated test world, with synthetic measured
// paw samples. Evaluated character-paw contact is a separate RHI integration
// check; this fixture proves impulse/velocity, contact gates and bounded motion.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMirmirToyPhysicsTest,
    "Mirmir.Physics.PawImpulseAndSafeSpawn",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FMirmirToyPhysicsTest::RunTest(const FString& Parameters)
{
    const FString PreviousData = FPlatformMisc::GetEnvironmentVariable(TEXT("XDG_DATA_HOME"));
    const FString Temporary = FPaths::ConvertRelativePathToFull(FPaths::Combine(
        FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("ToyPhysics-") + FGuid::NewGuid().ToString()));
    FPlatformMisc::SetEnvironmentVar(TEXT("XDG_DATA_HOME"), *Temporary);
    ON_SCOPE_EXIT
    {
        FPlatformMisc::SetEnvironmentVar(TEXT("XDG_DATA_HOME"), *PreviousData);
        IFileManager::Get().DeleteDirectory(*Temporary, false, true);
    };
    const UWorld::InitializationValues Values = UWorld::InitializationValues()
        .AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true)
        .EnableTraceCollision(true).SetTransactional(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
        *FString::Printf(TEXT("ToyPhysics_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)),
        nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("Isolated physics world created"), World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    const uint64 CachedFrameCounter = GFrameCounter;
    ON_SCOPE_EXIT
    {
        World->EndPlay(EEndPlayReason::Quit);
        GFrameCounter = CachedFrameCounter;
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
    };
    AMirmirCat* Cat = World->SpawnActor<AMirmirCat>();
    AMirmirHabitat* Habitat = World->SpawnActor<AMirmirHabitat>();
    AMirmirToy* Toy = World->SpawnActor<AMirmirToy>();
    if (!TestNotNull(TEXT("Cat fixture"), Cat) || !TestNotNull(TEXT("Habitat fixture"), Habitat) ||
        !TestNotNull(TEXT("Toy fixture"), Toy)) return false;
    Cat->Habitat = Habitat;
    Cat->SetActorTickEnabled(false);
    AddExpectedMessagePlain(TEXT("Mirmir: no licensed cat mesh assigned. See README asset import steps."),
        ELogVerbosity::Warning);
    World->InitializeActorsForPlay(FURL());
    World->GetWorldSettings()->NotifyBeginPlay();
    World->BeginPlay();
    Cat->SetActorTickEnabled(false);
    // Deliberately occupy the preferred forward spawn. No layout/save API is
    // invoked; only this isolated fixture's component is moved.
    Habitat->GetStationRoot(TEXT("bowl"))->SetWorldLocation(FVector(31, 0, 0));
    if (!TestTrue(TEXT("A collision-free alternate spawn is found"), Toy->BeginPlayWithCat(Cat))) return false;
    const FVector Start = Toy->GetBallLocation();
    const FBox BowlBounds = Habitat->GetStationBounds(TEXT("bowl")).ExpandBy(Toy->BallRadiusCm);
    TestFalse(TEXT("Ball is not spawned inside the moved bowl footprint"),
        Start.X >= BowlBounds.Min.X && Start.X <= BowlBounds.Max.X &&
        Start.Y >= BowlBounds.Min.Y && Start.Y <= BowlBounds.Max.Y);
    double LowestBallZ = Toy->GetBallLocation().Z;
    auto Step = [World, Toy, &LowestBallZ](int32 Frames)
    {
        for (int32 Frame = 0; Frame < Frames; ++Frame)
        {
            World->Tick(LEVELTICK_All, 1.0f / 60.0f);
            LowestBallZ = FMath::Min(LowestBallZ, Toy->GetBallLocation().Z);
            // Match Engine AutomationCommon's standalone-world tick helper:
            // tick/physics tasks intentionally run at most once per frame.
            ++GFrameCounter;
        }
    };
    Step(30);
    if (!TestTrue(TEXT("Chaos ball remains active after settling"), Toy->IsPlaying())) return false;
    const FVector Ball = Toy->GetBallLocation();
    TestFalse(TEXT("Distant paw cannot strike"), Toy->TryPawSwipe(Ball + FVector(-40, 0, 0), FVector(75, 0, 0)));
    TestFalse(TEXT("Stationary paw cannot add momentum"), Toy->TryPawSwipe(Ball + FVector(-3.5, 0, 0), FVector::ZeroVector));
    TestEqual(TEXT("Rejected contacts do not increment swipe count"), Toy->PawSwipeCount, 0);
    const FVector Before = Toy->GetBallVelocity();
    const FVector BeforePosition = Toy->GetBallLocation();
    if (!TestTrue(TEXT("Moving paw at the ball applies a real impulse"),
        Toy->TryPawSwipe(BeforePosition + FVector(-3.5, 0, 0), FVector(75, 8, 0)))) return false;
    TestEqual(TEXT("One accepted paw impulse"), Toy->PawSwipeCount, 1);
    TestTrue(TEXT("Accepted measured paw input is recorded unchanged"),
        Toy->LastSwipePawWorld.Equals(BeforePosition + FVector(-3.5, 0, 0), .001) &&
        Toy->LastSwipePawVelocity.Equals(FVector(75, 8, 0), .001) && Toy->LastSwipePawSampleFrame >= 0);
    TestFalse(TEXT("The same strike cannot inject momentum repeatedly"),
        Toy->TryPawSwipe(BeforePosition + FVector(-3.5, 0, 0), FVector(75, 8, 0)));
    Step(2);
    TestTrue(TEXT("Post-physics velocity sample is ready"), Toy->bSwipeVelocitySampleReady);
    TestTrue(TEXT("Chaos velocity changes after the applied impulse"),
        (Toy->LastSwipeVelocityAfter - Before).Size2D() > 10.0);
    TestTrue(TEXT("Impulse is finite and nonzero"), !Toy->LastSwipeImpulse.ContainsNaN() && Toy->LastSwipeImpulse.Size() > .1);
    Step(90);
    TestTrue(TEXT("Physics moves the ball without a subsequent teleport"),
        FVector::Dist2D(BeforePosition, Toy->GetBallLocation()) > .5);
    TestTrue(TEXT("Real collision callbacks were observed"), Toy->PhysicsContactCount > 0);
    TestTrue(TEXT("Ball never crosses the world floor contact plane"), LowestBallZ >= Toy->BallRadiusCm - .35f);
    TestTrue(TEXT("Ball remains active and bounded"), Toy->IsPlaying() &&
        FVector::Dist2D(Toy->GetBallLocation(), Cat->GetActorLocation()) < Toy->MaximumCatDistanceCm &&
        Toy->GetBallSpeedCmPerSecond() < 120.0f);
    AddInfo(FString::Printf(TEXT("Real Chaos fixture: swipes=%d contacts=%d impulse=%s velocityBefore=%s velocityAfter=%s"),
        Toy->PawSwipeCount, Toy->PhysicsContactCount, *Toy->LastSwipeImpulse.ToString(),
        *Toy->LastSwipeVelocityBefore.ToString(), *Toy->LastSwipeVelocityAfter.ToString()));
    Toy->EndPlaySession();
    TestFalse(TEXT("Inactive session rejects contact"), Toy->TryPawSwipe(Ball, FVector(75, 0, 0)));
    return true;
}
#endif
