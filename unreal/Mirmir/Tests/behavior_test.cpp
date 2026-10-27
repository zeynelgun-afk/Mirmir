#include "CatBehavior.h"
#include <cassert>
#include <iostream>
#include <limits>

using namespace Mirmir;

bool Close(float A, float B) { return std::abs(A - B) < 0.00001f; }

int main()
{
    Behavior Cat;
    const float InitialBond = Cat.Get().Bond;
    assert(Cat.Interact(Interaction::Pet));
    assert(Cat.Get().CurrentActivity == Activity::Petted);
    assert(Cat.Get().Bond > InitialBond);
    assert(!Cat.Interact(Interaction::Feed)); // Debounce rapid repeated input.
    Cat.Advance(1.0);
    assert(Cat.Interact(Interaction::Feed));
    assert(Cat.Get().Satiety <= 1.0f);
    Cat.Advance(1.0);
    assert(Cat.Interact(Interaction::Play));
    assert(Cat.Get().CurrentActivity == Activity::Play);

    State Tired;
    Tired.Energy = 0.05f;
    Behavior Sleepy(Tired);
    assert(!Sleepy.Interact(Interaction::Play));
    assert(Sleepy.Get().CurrentActivity == Activity::Sleep);
    Sleepy.Advance(5.0);
    assert(Sleepy.Get().Energy > Tired.Energy);

    State Hungry;
    Hungry.Satiety = 0.1f;
    Hungry.ActivityDuration = 1.0;
    Behavior Waiting(Hungry);
    Waiting.Advance(1.0);
    assert(Waiting.Get().CurrentActivity == Activity::SeekAttention);
    // Asking for food never creates food by itself.
    assert(Waiting.Get().Satiety <= Hungry.Satiety);

    State BeforeAway;
    BeforeAway.Energy = 0.1f;
    BeforeAway.Satiety = 0.4f;
    BeforeAway.Bond = 0.6f;
    BeforeAway.Curiosity = 0.3f;
    Behavior Away(BeforeAway);
    Away.ApplyOffline(365.0 * 86400.0);
    assert(Close(Away.Get().Satiety, BeforeAway.Satiety));
    assert(Close(Away.Get().Bond, BeforeAway.Bond));
    assert(Close(Away.Get().Curiosity, BeforeAway.Curiosity));
    assert(Away.Get().Energy == 1.0f);
    Behavior Capped(BeforeAway);
    Capped.ApplyOffline(Behavior::MaxOfflineSeconds);
    assert(Close(Capped.Get().Energy, Away.Get().Energy));
    Away.ApplyOffline(-100);
    Away.ApplyOffline(std::numeric_limits<double>::infinity());
    assert(Away.Get().Energy == 1.0f);

    State Corrupt;
    Corrupt.Satiety = -10.0f;
    Corrupt.Energy = 10.0f;
    Corrupt.Bond = std::numeric_limits<float>::quiet_NaN();
    Corrupt.Curiosity = std::numeric_limits<float>::infinity();
    Corrupt.OnlineSeconds = std::numeric_limits<double>::quiet_NaN();
    Corrupt.ActivityDuration = -20;
    Corrupt.CurrentActivity = static_cast<Activity>(255);
    Corrupt.RandomState = 0;
    Behavior Recovered(Corrupt);
    assert(Recovered.Get().Satiety == 0.0f);
    assert(Recovered.Get().Energy == 1.0f);
    assert(std::isfinite(Recovered.Get().Bond));
    assert(std::isfinite(Recovered.Get().Curiosity));
    assert(Recovered.Get().CurrentActivity == Activity::Idle);
    assert(Recovered.Get().RandomState != 0);
    Recovered.Advance(std::numeric_limits<double>::quiet_NaN());
    Recovered.Advance(-1.0);
    assert(Recovered.Get().OnlineSeconds == 0.0);
    Recovered.Advance(100000.0);
    assert(Recovered.Get().OnlineSeconds == 5.0);

    Behavior First;
    Behavior Second;
    for (int I = 0; I < 1000; ++I)
    {
        First.Advance(0.1);
        Second.Advance(0.1);
        assert(First.Get().CurrentActivity == Second.Get().CurrentActivity);
        assert(First.Get().RandomState == Second.Get().RandomState);
    }
    Behavior Restored(First.Get());
    for (int I = 0; I < 1000; ++I)
    {
        First.Advance(0.1);
        Restored.Advance(0.1);
        assert(First.Get().CurrentActivity == Restored.Get().CurrentActivity);
        assert(First.Get().RandomState == Restored.Get().RandomState);
        assert(Close(First.Get().Energy, Restored.Get().Energy));
    }
    for (int I = 0; I < 100000; ++I) First.Advance(1.0);
    assert(First.Get().Satiety >= 0 && First.Get().Satiety <= 1);
    assert(First.Get().Energy >= 0 && First.Get().Energy <= 1);
    assert(First.Get().Bond >= 0 && First.Get().Bond <= 1);
    assert(First.Get().Curiosity >= 0 && First.Get().Curiosity <= 1);
    std::cout << "Mirmir behavior: interactions, cooldown, needs, deterministic restore, "
                 "offline cap, no absence penalty, invalid input and bounds passed.\n";
}
