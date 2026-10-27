#pragma once

// Deliberately independent of Unreal: state transitions can be checked without
// a licensed engine installation. Units are seconds and normalized [0, 1].
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Mirmir
{
enum class Activity : std::uint8_t
{
    Idle, Wander, Groom, Sleep, Play, Eat, SeekAttention, Petted
};

enum class Interaction : std::uint8_t { Pet, Feed, Play };

struct State
{
    float Satiety = 0.8f;
    float Energy = 0.85f;
    float Bond = 0.15f;
    float Curiosity = 0.6f;
    double OnlineSeconds = 0.0;
    double LastInteractionSeconds = -10.0;
    double ActivitySeconds = 0.0;
    double ActivityDuration = 7.0;
    Activity CurrentActivity = Activity::Idle;
    std::uint32_t RandomState = 0x4d49524du;
};

class Behavior
{
public:
    static constexpr double MaxOfflineSeconds = 8.0 * 60.0 * 60.0;

    Behavior() = default;
    explicit Behavior(const State& Initial) { Restore(Initial); }

    const State& Get() const { return Data; }

    void Restore(const State& Initial)
    {
        Data = Initial;
        Data.Satiety = Unit(Data.Satiety, 0.8f);
        Data.Energy = Unit(Data.Energy, 0.85f);
        Data.Bond = Unit(Data.Bond, 0.15f);
        Data.Curiosity = Unit(Data.Curiosity, 0.6f);
        Data.OnlineSeconds = std::min(NonNegative(Data.OnlineSeconds, 0.0), 1.0e12);
        Data.LastInteractionSeconds = std::isfinite(Data.LastInteractionSeconds)
            ? std::min(Data.LastInteractionSeconds, Data.OnlineSeconds) : -10.0;
        Data.ActivitySeconds = NonNegative(Data.ActivitySeconds, 0.0);
        Data.ActivityDuration = std::clamp(
            NonNegative(Data.ActivityDuration, 7.0), 1.0, 60.0);
        Data.ActivitySeconds = std::min(Data.ActivitySeconds, Data.ActivityDuration);
        if (static_cast<unsigned>(Data.CurrentActivity) >
            static_cast<unsigned>(Activity::Petted))
        {
            Data.CurrentActivity = Activity::Idle;
        }
        if (Data.RandomState == 0) Data.RandomState = 0x4d49524du;
    }

    void Advance(double DeltaSeconds)
    {
        if (!std::isfinite(DeltaSeconds) || DeltaSeconds <= 0.0) return;
        // A suspended process must not fast-forward hours of online decay.
        const double Dt = std::min(DeltaSeconds, 5.0);
        Data.OnlineSeconds += Dt;
        Data.ActivitySeconds += Dt;
        Data.Satiety = Unit(Data.Satiety - static_cast<float>(Dt / 14400.0));
        const bool Sleeping = Data.CurrentActivity == Activity::Sleep;
        const bool Playing = Data.CurrentActivity == Activity::Play;
        Data.Energy = Unit(Data.Energy + static_cast<float>(
            Dt * (Sleeping ? 1.0 / 150.0 : (Playing ? -1.0 / 300.0 : -1.0 / 3600.0))));
        Data.Curiosity = Unit(Data.Curiosity + static_cast<float>(
            Dt * (Playing ? -1.0 / 90.0 : 1.0 / 1200.0)));
        if (Data.ActivitySeconds >= Data.ActivityDuration) ChooseActivity();
    }

    bool Interact(Interaction Action)
    {
        if (Data.OnlineSeconds - Data.LastInteractionSeconds < 0.75) return false;
        switch (Action)
        {
        case Interaction::Pet:
            Data.Bond = Unit(Data.Bond + 0.025f);
            Begin(Activity::Petted, 4.0);
            break;
        case Interaction::Feed:
            Data.Satiety = Unit(Data.Satiety + 0.35f);
            Data.Bond = Unit(Data.Bond + 0.01f);
            Begin(Activity::Eat, 6.0);
            break;
        case Interaction::Play:
            if (Data.Energy < 0.12f)
            {
                Begin(Activity::Sleep, 30.0);
                return false;
            }
            Data.Energy = Unit(Data.Energy - 0.03f);
            Data.Bond = Unit(Data.Bond + 0.02f);
            Data.Curiosity = Unit(Data.Curiosity + 0.08f);
            Begin(Activity::Play, 15.0);
            break;
        default:
            return false;
        }
        Data.LastInteractionSeconds = Data.OnlineSeconds;
        return true;
    }

    void ApplyOffline(double ElapsedSeconds)
    {
        if (!std::isfinite(ElapsedSeconds) || ElapsedSeconds <= 0.0) return;
        const double Rest = std::min(ElapsedSeconds, MaxOfflineSeconds);
        Data.Energy = Unit(Data.Energy + static_cast<float>(Rest / 3600.0));
        // Absence never reduces food, bond, or curiosity. No neglect/death state.
        Begin(Activity::Idle, 5.0);
    }

    void Rest() { Begin(Activity::Sleep, 30.0); }

private:
    State Data;

    static float Unit(float Value, float Fallback = 0.0f)
    {
        return std::isfinite(Value) ? std::clamp(Value, 0.0f, 1.0f) : Fallback;
    }

    static double NonNegative(double Value, double Fallback)
    {
        return std::isfinite(Value) && Value >= 0.0 ? Value : Fallback;
    }

    float Random()
    {
        // Explicit PRNG state is persisted so a seed reproduces behavior.
        Data.RandomState = 1664525u * Data.RandomState + 1013904223u;
        return static_cast<float>(Data.RandomState >> 8) / 16777216.0f;
    }

    void Begin(Activity Next, double Duration)
    {
        Data.CurrentActivity = Next;
        Data.ActivitySeconds = 0.0;
        Data.ActivityDuration = Duration;
    }

    void ChooseActivity()
    {
        if (Data.Energy < 0.25f)
        {
            Begin(Activity::Sleep, 35.0);
            return;
        }
        if (Data.Satiety < 0.25f)
        {
            Begin(Activity::SeekAttention, 10.0);
            return;
        }
        const float Choice = Random();
        if (Choice < 0.16f) Begin(Activity::Groom, 12.0);
        else if (Choice < 0.32f) Begin(Activity::SeekAttention, 8.0);
        else if (Choice < 0.32f + 0.5f * Data.Curiosity) Begin(Activity::Wander, 10.0);
        else Begin(Activity::Idle, 5.0 + 7.0 * Random());
    }
};
}
