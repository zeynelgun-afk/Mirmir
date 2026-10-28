#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

namespace Mirmir::Navigation
{
constexpr double Pi = 3.1415926535897932384626433832795;
struct Point2 { double X = 0.0, Y = 0.0; };
struct Pose2 { Point2 Position; double YawRadians = 0.0; };
struct CircleObstacle { Point2 Center; double Radius = 0.0; };
struct PathSample { Point2 Position; double YawRadians = 0.0, Distance = 0.0; };
struct Candidate
{
    int StartTurn = 0, EndTurn = 0; // +1 left, -1 right
    double Radius = 0.0, Length = 0.0;
    std::vector<PathSample> Samples;
};

inline Point2 operator+(Point2 A, Point2 B) { return { A.X + B.X, A.Y + B.Y }; }
inline Point2 operator-(Point2 A, Point2 B) { return { A.X - B.X, A.Y - B.Y }; }
inline Point2 operator*(Point2 A, double Scale) { return { A.X * Scale, A.Y * Scale }; }
inline double Dot(Point2 A, Point2 B) { return A.X * B.X + A.Y * B.Y; }
inline double Length(Point2 A) { return std::hypot(A.X, A.Y); }
inline Point2 Left(Point2 A) { return { -A.Y, A.X }; }
inline Point2 Direction(double Yaw) { return { std::cos(Yaw), std::sin(Yaw) }; }
inline bool Finite(Point2 A) { return std::isfinite(A.X) && std::isfinite(A.Y); }
inline double PositiveAngle(double Angle)
{
    double Result = std::fmod(Angle, 2.0 * Pi);
    if (Result < 0.0) Result += 2.0 * Pi;
    // An almost-zero negative rounding error must not introduce a full circle.
    return Result < 1e-10 || 2.0 * Pi - Result < 1e-10 ? 0.0 : Result;
}
inline double SignedAngle(double Angle)
{
    const double Result = PositiveAngle(Angle);
    return Result > Pi ? Result - 2.0 * Pi : Result;
}

/** Exact signed clearance for a straight segment against an inflated disk.
 * Invalid data fails closed. Inflate by the actor footprint before calling.
 */
inline double SegmentClearance(Point2 A, Point2 B, CircleObstacle Obstacle)
{
    if (!Finite(A) || !Finite(B) || !Finite(Obstacle.Center) ||
        !std::isfinite(Obstacle.Radius) || Obstacle.Radius < 0.0)
        return -std::numeric_limits<double>::infinity();
    const Point2 Segment = B - A;
    const double Squared = Dot(Segment, Segment);
    if (!std::isfinite(Squared)) return -std::numeric_limits<double>::infinity();
    const double T = Squared > 1e-18 ? std::clamp(Dot(Obstacle.Center - A, Segment) / Squared, 0.0, 1.0) : 0.0;
    const double Clearance = Length(A + Segment * T - Obstacle.Center) - Obstacle.Radius;
    return std::isfinite(Clearance) ? Clearance : -std::numeric_limits<double>::infinity();
}

/** Generate the four circle-straight-circle families, shortest first.
 * This is CSC only, not the six-family globally shortest Dubins solver. Both
 * endpoint positions and headings are fixed. No obstacle or corridor policy
 * is hidden here: the caller validates each candidate and its swept segments.
 * A sampled arc lies up to spacing^2/(8*radius) from its chord; use clearance
 * margin or actual shape sweeps as well (8cm/45cm gives less than 0.18cm).
 */
inline std::vector<Candidate> BuildCSC(Pose2 Start, Pose2 Goal, double Radius,
    double Spacing = 8.0, std::size_t MaximumSamples = 10000)
{
    std::vector<Candidate> Results;
    if (!Finite(Start.Position) || !Finite(Goal.Position) ||
        !std::isfinite(Start.YawRadians) || !std::isfinite(Goal.YawRadians) ||
        !std::isfinite(Radius) || Radius < 45.0 || !std::isfinite(Spacing) ||
        Spacing <= 0.0 || MaximumSamples < 2) return Results;
    MaximumSamples = std::min<std::size_t>(MaximumSamples, 100000);
    Start.YawRadians = SignedAngle(Start.YawRadians);
    Goal.YawRadians = SignedAngle(Goal.YawRadians);
    if (Length(Goal.Position - Start.Position) < 1e-10 &&
        std::abs(SignedAngle(Goal.YawRadians - Start.YawRadians)) < 1e-10)
    {
        Candidate Trivial;
        Trivial.Radius = Radius;
        Trivial.Samples.push_back({ Start.Position, Start.YawRadians, 0.0 });
        Results.push_back(Trivial);
        return Results;
    }
    for (const int First : { 1, -1 })
        for (const int Last : { 1, -1 })
        {
            const Point2 C1 = Start.Position + Left(Direction(Start.YawRadians)) * (First * Radius);
            const Point2 C2 = Goal.Position + Left(Direction(Goal.YawRadians)) * (Last * Radius);
            const Point2 Between = C2 - C1;
            const double CentersDistance = Length(Between);
            if (!std::isfinite(CentersDistance)) continue;
            double TangentYaw;
            if (CentersDistance < 1e-9)
            {
                if (First != Last) continue;
                // A single arc on coincident circles needs no straight piece.
                TangentYaw = Goal.YawRadians;
            }
            else
            {
                const double Ratio = (First - Last) * Radius / CentersDistance;
                if (std::abs(Ratio) > 1.0 + 1e-10) continue;
                // For the forward tangent, Dot(C2-C1,Left(T))+(s1-s2)R=0.
                TangentYaw = std::atan2(Between.Y, Between.X) + std::asin(std::clamp(Ratio, -1.0, 1.0));
            }
            const Point2 TangentDirection = Direction(TangentYaw);
            const Point2 T1 = C1 - Left(TangentDirection) * (First * Radius);
            const Point2 T2 = C2 - Left(TangentDirection) * (Last * Radius);
            if (Dot(T2 - T1, TangentDirection) < -1e-7) continue;
            const double Sweep1 = PositiveAngle(First * (TangentYaw - Start.YawRadians));
            const double Sweep2 = PositiveAngle(Last * (Goal.YawRadians - TangentYaw));
            const double Arc1 = Sweep1 * Radius, Straight = Length(T2 - T1), Arc2 = Sweep2 * Radius;
            const double Total = Arc1 + Straight + Arc2;
            if (!std::isfinite(Total)) continue;
            const double Counts[] = { std::ceil(Arc1 / Spacing), std::ceil(Straight / Spacing), std::ceil(Arc2 / Spacing) };
            if (Counts[0] + Counts[1] + Counts[2] + 1.0 > double(MaximumSamples)) continue;
            Candidate Path;
            Path.StartTurn = First; Path.EndTurn = Last; Path.Radius = Radius; Path.Length = Total;
            Path.Samples.push_back({ Start.Position, Start.YawRadians, 0.0 });
            auto Arc = [&](Point2 Center, double FromYaw, int Sign, double Sweep, double Offset, int Count)
            {
                for (int I = 1; I <= Count; ++I)
                {
                    const double Angle = FromYaw + Sign * Sweep * I / Count;
                    Path.Samples.push_back({ Center - Left(Direction(Angle)) * (Sign * Radius),
                        SignedAngle(Angle), Offset + Radius * Sweep * I / Count });
                }
            };
            Arc(C1, Start.YawRadians, First, Sweep1, 0.0, int(Counts[0]));
            for (int I = 1; I <= int(Counts[1]); ++I)
            {
                const double Fraction = double(I) / Counts[1];
                Path.Samples.push_back({ T1 + (T2 - T1) * Fraction,
                    SignedAngle(TangentYaw), Arc1 + Straight * Fraction });
            }
            Arc(C2, TangentYaw, Last, Sweep2, Arc1 + Straight, int(Counts[2]));
            // Preserve exact caller endpoints rather than trigonometric residue.
            Path.Samples.back() = { Goal.Position, Goal.YawRadians, Total };
            Results.push_back(std::move(Path));
        }
    std::stable_sort(Results.begin(), Results.end(), [](const Candidate& A, const Candidate& B) { return A.Length < B.Length; });
    return Results;
}
}
