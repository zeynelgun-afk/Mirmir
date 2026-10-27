#include "CatLocomotion.h"
#include <cassert>
#include <iostream>
#include <limits>

int main()
{
    using namespace Mirmir;
    LocomotionState Left, Right;
    double PreviousSpeed = 0.0, PreviousYaw = 0.0;
    double Yaw = 0.0, Distance = 0.0;
    for (int Frame = 0; Frame < 180; ++Frame)
    {
        const auto L = AdvanceLocomotion(Left, -80.0, 30.0, 56.0, true, 1.0 / 30.0);
        const auto R = AdvanceLocomotion(Right, -80.0, -30.0, 56.0, true, 1.0 / 30.0);
        assert(L.Forward > 0.0); // Rearward targets still take walking steps.
        assert(std::abs(L.Yaw + R.Yaw) < 1e-10);
        assert(std::abs(L.Forward - R.Forward) < 1e-10);
        assert(std::abs(Left.YawRate) <= Left.Speed / 50.0 + 1e-10);
        assert(Left.Speed * std::abs(Left.YawRate) <= 40.0 + 1e-10);
        assert(std::abs(Left.YawRate - PreviousYaw) <= 2.2 / 30.0 + 1e-10);
        assert(Left.Speed - PreviousSpeed <= 65.0 / 30.0 + 1e-10);
        PreviousSpeed = Left.Speed; PreviousYaw = Left.YawRate;
        Yaw += L.Yaw; Distance += std::hypot(L.Forward, L.Right);
    }
    assert(Yaw > 3.14 && Distance > 100.0);
    for (int Frame = 0; Frame < 90; ++Frame)
    {
        const auto Step = AdvanceLocomotion(Left, 0.0, 0.0, 0.0, false, 1.0 / 30.0);
        assert(std::abs(Left.YawRate - PreviousYaw) <= 2.2 / 30.0 + 1e-10);
        if (Left.Speed == 0.0) assert(Step.Yaw == 0.0 && Step.Forward == 0.0 && Step.Right == 0.0);
        PreviousYaw = Left.YawRate;
    }
    assert(Left.Speed == 0.0 && Left.YawRate == 0.0);
    LocomotionState Switching;
    for (int Frame = 0; Frame < 1800; ++Frame)
    {
        const double OldYaw = Switching.YawRate;
        const int Stage = (Frame / 47) % 4;
        AdvanceLocomotion(Switching, Stage == 0 ? -80.0 : 200.0,
            Stage == 1 ? 0.0 : Stage == 2 ? -100.0 : 100.0,
            Stage == 3 ? 70.0 : 56.0, true, 1.0 / 30.0);
        assert(Switching.Speed * std::abs(Switching.YawRate) <= 40.0 + 1e-10);
        assert(std::abs(Switching.YawRate - OldYaw) <= 2.2 / 30.0 + 1e-10);
    }
    LocomotionState Invalid;
    assert(AdvanceLocomotion(Invalid, 100.0, 0.0, 35.0, true, -1.0).Forward == 0.0);
    AdvanceLocomotion(Invalid, std::numeric_limits<double>::quiet_NaN(), 0.0, 35.0, true, 0.1);
    assert(Invalid.Speed == 0.0 && Invalid.YawRate == 0.0);
    std::cout << "Mirmir locomotion: walking U-turn, mirrored steering, radius/lateral/yaw-acceleration bounds, settled stop passed\n";
}
