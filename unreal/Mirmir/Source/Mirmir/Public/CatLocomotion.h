#pragma once

#include <algorithm>
#include <cmath>

namespace Mirmir
{
// Planar kinematic steering, in cm and radians. This is not a rigid-body cat
// simulation. Body yaw can only change while the feet advance through a walk.
struct LocomotionSettings
{
    double MinimumRadius = 50.0;
    double Acceleration = 65.0;
    double Deceleration = 95.0;
    double LateralAcceleration = 40.0;
    double YawAcceleration = 2.2;
    double ArrivalRadius = 4.0;
};

struct LocomotionState
{
    double Speed = 0.0;
    double YawRate = 0.0;
    double TurnSide = 1.0;
};

struct LocomotionStep
{
    double Forward = 0.0;
    double Right = 0.0;
    double Yaw = 0.0;
};

inline double Approach(double Value, double Target, double Delta)
{
    return Value + std::clamp(Target - Value, -Delta, Delta);
}

inline LocomotionStep AdvanceLocomotion(LocomotionState& State,
    double TargetForward, double TargetRight, double CruiseSpeed, bool Travel,
    double DeltaSeconds, const LocomotionSettings& Settings = {})
{
    LocomotionStep Result;
    if (!std::isfinite(DeltaSeconds) || DeltaSeconds <= 0.0) return Result;
    if (!std::isfinite(TargetForward) || !std::isfinite(TargetRight) || !std::isfinite(CruiseSpeed)) Travel = false;
    const double Dt = std::min(DeltaSeconds, 0.1);
    const double Radius = std::max(20.0, Settings.MinimumRadius);
    const double Distance = Travel ? std::hypot(TargetForward, TargetRight) : 0.0;
    const double Error = Travel ? std::atan2(TargetRight, TargetForward) : 0.0;
    // A directly rearward target has sin(error)==0. Keep a deterministic turn
    // side so the cat takes a forward U-turn instead of stalling or pivoting.
    if (std::abs(TargetRight) > 2.0) State.TurnSide = TargetRight > 0.0 ? 1.0 : -1.0;
    double Curvature = 0.0;
    if (Travel && Distance > Settings.ArrivalRadius)
    {
        const double LookAhead = std::max(Radius, State.Speed * 0.8);
        Curvature = std::clamp(2.0 * std::sin(Error) / std::max(Distance, LookAhead), -1.0 / Radius, 1.0 / Radius);
        if (TargetForward < 0.0) Curvature = State.TurnSide / Radius;
    }
    double DesiredSpeed = Travel && Distance > Settings.ArrivalRadius ? std::max(0.0, CruiseSpeed) : 0.0;
    if (std::abs(Curvature) > 0.000001)
        DesiredSpeed = std::min(DesiredSpeed, std::sqrt(std::max(1.0, Settings.LateralAcceleration) / std::abs(Curvature)));
    // A turn's angular velocity settles over time. Its previous value still
    // constrains acceleration while the requested path becomes a straight.
    if (std::abs(State.YawRate) > 0.000001)
        DesiredSpeed = std::min(DesiredSpeed, std::max(1.0, Settings.LateralAcceleration) / std::abs(State.YawRate));
    // Only brake for an approaching target. A rearward target requires room
    // for a real arc, not a zero-speed rotation in front of the target.
    if (TargetForward > 0.0 && std::abs(Error) < 0.9)
        DesiredSpeed = std::min(DesiredSpeed, std::sqrt(2.0 * Settings.Deceleration * std::max(0.0, Distance - Settings.ArrivalRadius)));
    const double Acceleration = std::min(DesiredSpeed > State.Speed ? Settings.Acceleration : Settings.Deceleration,
        std::max(0.1, Settings.YawAcceleration) * Radius);
    State.Speed = Approach(State.Speed, DesiredSpeed, std::max(1.0, Acceleration) * Dt);
    const double DesiredYawRate = State.Speed * Curvature;
    State.YawRate = Approach(State.YawRate, DesiredYawRate, std::max(0.1, Settings.YawAcceleration) * Dt);
    const double YawLimit = std::min(State.Speed / Radius,
        std::max(1.0, Settings.LateralAcceleration) / std::max(0.001, State.Speed));
    State.YawRate = std::clamp(State.YawRate, -YawLimit, YawLimit);
    Result.Yaw = State.YawRate * Dt;
    // Integrate the arc rather than translating along an independently
    // interpolated heading. This preserves the forward-only velocity tangent.
    if (std::abs(State.YawRate) > 0.000001)
    {
        Result.Forward = State.Speed * std::sin(Result.Yaw) / State.YawRate;
        Result.Right = State.Speed * (1.0 - std::cos(Result.Yaw)) / State.YawRate;
    }
    else Result.Forward = State.Speed * Dt;
    return Result;
}
}
