#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "CatBehavior.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMirmirAbsenceTest,
    "Mirmir.Behavior.AbsencePreservesBondAndFood",
    EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)

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
    EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FMirmirInteractionTest::RunTest(const FString& Parameters)
{
    Mirmir::Behavior Cat;
    TestTrue(TEXT("Pet accepted"), Cat.Interact(Mirmir::Interaction::Pet));
    TestTrue(TEXT("Pet increases bond"), Cat.Get().Bond > 0.15f);
    TestFalse(TEXT("Input debounced"), Cat.Interact(Mirmir::Interaction::Pet));
    Mirmir::State Tired;
    Tired.Energy = 0.05f;
    Cat.Restore(Tired);
    TestFalse(TEXT("Tired cat declines play"), Cat.Interact(Mirmir::Interaction::Play));
    TestTrue(TEXT("Tired cat rests"), Cat.Get().CurrentActivity == Mirmir::Activity::Sleep);
    return true;
}
#endif
