#include "CatNavigation.h"
#include <cassert>
#include <iostream>
#include <random>

using namespace Mirmir::Navigation;

void Check(const Candidate& Path, Pose2 Start, Pose2 End, double Spacing)
{
    assert(!Path.Samples.empty());
    assert(Length(Path.Samples.front().Position - Start.Position) < 1e-7);
    assert(std::abs(SignedAngle(Path.Samples.front().YawRadians - Start.YawRadians)) < 1e-7);
    assert(Length(Path.Samples.back().Position - End.Position) < 1e-7);
    assert(std::abs(SignedAngle(Path.Samples.back().YawRadians - End.YawRadians)) < 1e-7);
    assert(std::abs(Path.Samples.back().Distance - Path.Length) < 1e-7);
    for (std::size_t I = 1; I < Path.Samples.size(); ++I)
    {
        const auto& A = Path.Samples[I - 1]; const auto& B = Path.Samples[I];
        const double Distance = B.Distance - A.Distance;
        assert(Distance >= -1e-8 && Distance <= Spacing + 1e-7);
        assert(Finite(B.Position) && std::isfinite(B.YawRadians));
        assert(Length(B.Position - A.Position) <= Distance + 1e-7);
        assert(std::abs(SignedAngle(B.YawRadians - A.YawRadians)) <= Distance / Path.Radius + 1e-7);
    }
}

int main()
{
    assert(std::abs(SegmentClearance({ 0, 0 }, { 10, 0 }, { { 5, 3 }, 2 }) - 1.0) < 1e-10);
    assert(std::abs(SegmentClearance({ 0, 0 }, { 10, 0 }, { { 5, 2 }, 2 })) < 1e-10);
    assert(SegmentClearance({ 0, 0 }, { 10, 0 }, { { 5, 0 }, 2 }) < 0.0);
    assert(std::abs(SegmentClearance({ 0, 0 }, { 0, 0 }, { { 3, 4 }, 2 }) - 3.0) < 1e-10);
    assert(!std::isfinite(SegmentClearance({ 0, 0 }, { 1, 1 }, { { 0, 0 }, -1 })));
    const Pose2 Origin{ { 0, 0 }, 0 };
    auto Trivial = BuildCSC(Origin, Origin, 45);
    assert(Trivial.size() == 1 && Trivial[0].Length == 0.0);
    Check(Trivial[0], Origin, Origin, 8);
    auto Quarter = BuildCSC(Origin, { { 45, 45 }, Pi / 2 }, 45);
    assert(!Quarter.empty() && std::abs(Quarter[0].Length - 45 * Pi / 2) < 1e-7);
    for (const auto& Path : Quarter) Check(Path, Origin, { { 45, 45 }, Pi / 2 }, 8);
    auto Straight = BuildCSC(Origin, { { 150, 0 }, 0 }, 45);
    assert(Straight.size() == 4 && std::abs(Straight[0].Length - 150) < 1e-7);
    for (const auto& Path : Straight) Check(Path, Origin, { { 150, 0 }, 0 }, 8);
    assert(BuildCSC(Origin, { { 20, 0 }, 0 }, 44).empty());
    assert(BuildCSC(Origin, { { 20, 0 }, 0 }, 45, 0).empty());
    assert(BuildCSC(Origin, { { std::numeric_limits<double>::quiet_NaN(), 0 }, 0 }, 45).empty());
    assert(BuildCSC(Origin, { { 150, 0 }, 0 }, 45, 1e-8, 100).empty());
    std::mt19937 Random(109782);
    std::uniform_real_distribution<double> Position(-300, 300), Yaw(-Pi, Pi);
    for (int I = 0; I < 160; ++I)
    {
        const Pose2 Start{ { Position(Random), Position(Random) }, Yaw(Random) };
        const Pose2 End{ { Position(Random), Position(Random) }, Yaw(Random) };
        const auto Paths = BuildCSC(Start, End, 50);
        assert(Paths.size() >= 2);
        double Previous = -1;
        for (const auto& Path : Paths)
        {
            Check(Path, Start, End, 8);
            assert(Path.Length >= Previous); Previous = Path.Length;
        }
    }
    const auto LeftPaths = BuildCSC(Origin, { { 140, 70 }, 1.1 }, 50);
    const auto RightPaths = BuildCSC(Origin, { { 140, -70 }, -1.1 }, 50);
    for (const auto& Path : LeftPaths)
    {
        auto Match = std::find_if(RightPaths.begin(), RightPaths.end(), [&](const Candidate& Other) {
            return Other.StartTurn == -Path.StartTurn && Other.EndTurn == -Path.EndTurn;
        });
        assert(Match != RightPaths.end() && std::abs(Match->Length - Path.Length) < 1e-7);
    }
    std::cout << "Mirmir navigation: exact disk clearance, CSC endpoints/headings, curvature, mirrored turns, finite inputs and bounded sampling passed\n";
}
