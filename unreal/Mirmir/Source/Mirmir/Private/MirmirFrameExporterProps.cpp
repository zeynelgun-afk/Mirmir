#include "MirmirFrameExporter.h"
#include "MirmirCat.h"
#include "MirmirHabitat.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/PlatformTime.h"
#include "IImageWrapperModule.h"
#include "ImageCore.h"
#include "Math/RotationMatrix.h"
#include "Misc/Crc.h"
#include "UnrealClient.h"

void AMirmirFrameExporter::CapturePropsIfNeeded()
{
    const AMirmirCat* Cat = Subject.Get();
    if (!Cat || !Cat->Habitat || StationTargets.Num() != 2) return;
    const AMirmirHabitat* Habitat = Cat->Habitat;
    const FRotationMatrix Basis(CameraRotation);
    const FVector Right = Basis.GetUnitAxis(EAxis::Y), Up = Basis.GetUnitAxis(EAxis::Z);
    bPropsReady = true;
    for (int32 I = 0; I < 2; ++I)
    {
        FMirmirStationFrame& State = StationFrames[I];
        const uint64 Revision = Habitat->GetLayoutRevision();
        const bool bFood = Habitat->IsFoodVisible();
        if (State.bReady && State.LayoutRevision == Revision && (I == 0 || State.bFoodVisible == bFood)) continue;
        State.bReady = false;
        USceneCaptureComponent2D* Capture = StationCaptures[I];
        Capture->ClearShowOnlyComponents();
        TArray<UPrimitiveComponent*> Components;
        Habitat->GetStationComponents(State.Id, Components);
        if (Components.IsEmpty()) { bPropsReady = false; continue; }
        for (UPrimitiveComponent* Primitive : Components) Capture->ShowOnlyComponent(Primitive);
        const FBox Bounds = Habitat->GetStationBounds(State.Id);
        const FVector Center = Bounds.IsValid ? Bounds.GetCenter() : Habitat->GetStationFloorPosition(State.Id);
        const FVector Difference = Center - InitialCatLocation - CameraOffset;
        // Project onto the camera plane. This centres either item regardless
        // of distance from its sibling, while the published desktop offset
        // preserves the exact shared world/pixel scale and depth registration.
        State.CameraShift = Right * FVector::DotProduct(Difference, Right) + Up * FVector::DotProduct(Difference, Up);
        Capture->SetWorldLocationAndRotation(InitialCatLocation + CameraOffset + State.CameraShift, CameraRotation);
        Capture->CaptureScene();
        TArray<FLinearColor> HDR;
        FReadSurfaceDataFlags Flags(RCM_MinMax); Flags.SetLinearToGamma(false);
        FTextureRenderTargetResource* Resource = StationTargets[I]->GameThread_GetRenderTargetResource();
        if (!Resource || !Resource->ReadLinearColorPixels(HDR, Flags) || HDR.Num() != State.Size * State.Size)
        { bPropsReady = false; continue; }
        TArray<FColor> Pixels; Pixels.SetNumUninitialized(HDR.Num());
        int32 Low = 255, High = 0;
        auto Map = [this](float Value)
        {
            const float X = FMath::Max(0.0f, Value * FMath::Clamp(DisplayGain, .1f, 4.0f));
            return bUseFilmicToneMap ? FMath::Clamp((X * (2.51f * X + .03f)) /
                (X * (2.43f * X + .59f) + .14f), 0.0f, 1.0f) : FMath::Clamp(X, 0.0f, 1.0f);
        };
        for (int32 P = 0; P < HDR.Num(); ++P)
        {
            const float Coverage = FMath::Clamp(bInvertCaptureAlpha ? 1.0f - HDR[P].A : HDR[P].A, 0.0f, 1.0f);
            const uint8 Alpha = uint8(FMath::RoundToInt(Coverage * 255.0f));
            Low = FMath::Min(Low, int32(Alpha)); High = FMath::Max(High, int32(Alpha));
            Pixels[P] = Alpha ? FLinearColor(Map(HDR[P].R), Map(HDR[P].G), Map(HDR[P].B)).ToFColorSRGB() : FColor(0, 0, 0, 0);
            Pixels[P].A = Alpha;
        }
        if (Low != 0 || High == 0) { bPropsReady = false; continue; }
        if (bFlipVertically)
            for (int32 Y = 0; Y < State.Size / 2; ++Y)
                for (int32 X = 0; X < State.Size; ++X)
                    Swap(Pixels[Y * State.Size + X], Pixels[(State.Size - Y - 1) * State.Size + X]);
        const FImageView Image(Pixels.GetData(), State.Size, State.Size, EGammaSpace::sRGB);
        TArray64<uint8> PNG;
        const FString Filename = State.Id.ToString() + TEXT(".png");
        if (!ImageWrapper->CompressImage(PNG, EImageFormat::PNG, Image, 0) || PNG.IsEmpty() || !AtomicWrite(Filename, PNG))
        { bPropsReady = false; continue; }
        State.PNGBytes = PNG.Num(); State.PNGCRC32 = FCrc::MemCrc32(PNG.GetData(), int32(PNG.Num()));
        ++State.Sequence; State.LayoutRevision = Revision; State.bFoodVisible = bFood; State.bReady = true;
    }
    bPropsReady = StationFrames[0].bReady && StationFrames[1].bReady;
}

TSharedRef<FJsonObject> AMirmirFrameExporter::BuildPropsState(bool bReady) const
{
    const TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
    const AMirmirCat* Cat = Subject.Get();
    Props->SetNumberField(TEXT("schema_version"), 2);
    Props->SetBoolField(TEXT("ready"), bPropsReady && bReady);
    const AMirmirHabitat* Habitat = Cat ? Cat->Habitat.Get() : nullptr;
    if (!Cat || !Habitat) return Props;
    Props->SetNumberField(TEXT("layout_revision"), double(Habitat->GetLayoutRevision()));
    Props->SetBoolField(TEXT("food_visible"), Habitat->IsFoodVisible());
    Props->SetBoolField(TEXT("traveling"), Cat->bHabitatTraveling);
    Props->SetBoolField(TEXT("arrived"), Cat->bHabitatArrived);
    Props->SetNumberField(TEXT("rest_pose_alpha"), Cat->RestPoseAlpha);
    Props->SetNumberField(TEXT("eating_pose_strength"), Cat->EatingPoseStrength);
    auto Vector = [](const FVector& V)
    {
        return TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y), MakeShared<FJsonValueNumber>(V.Z) };
    };
    Props->SetArrayField(TEXT("bed_world_cm"), Vector(Habitat->GetBedLocation()));
    Props->SetArrayField(TEXT("bowl_world_cm"), Vector(Habitat->GetBowlLocation()));
    const FRotationMatrix Basis(CameraRotation);
    const double Scale = FMath::Clamp(DesktopPixelsPerCentimeter, .01f, 20.0f);
    // The native anchor must fit the cat's complete canvas at the bed gates
    // and feeding approaches as well as the visible furniture. Otherwise an
    // apparently on-screen bed can have an unreachable off-screen entrance.
    const FVector Bed = Habitat->GetStationFloorPosition(TEXT("bed"));
    const FVector Bowl = Habitat->GetStationFloorPosition(TEXT("bowl"));
    const FVector NavigationPoints[] = { InitialCatLocation,
        Bed + FVector(-110,0,0), Bed + FVector(110,0,0),
        Bowl + FVector(-60,0,0), Bowl + FVector(60,0,0),
        Bowl + FVector(0,-60,0), Bowl + FVector(0,60,0) };
    TArray<TSharedPtr<FJsonValue>> NavigationOffsets;
    for (const FVector& Point : NavigationPoints)
    {
        const FVector Delta = Point - InitialCatLocation;
        NavigationOffsets.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
            MakeShared<FJsonValueNumber>(FVector::DotProduct(Delta, Basis.GetUnitAxis(EAxis::Y)) * Scale),
            MakeShared<FJsonValueNumber>(-FVector::DotProduct(Delta, Basis.GetUnitAxis(EAxis::Z)) * Scale) }));
    }
    Props->SetArrayField(TEXT("navigation_offsets_px"), NavigationOffsets);
    const TSharedRef<FJsonObject> Items = MakeShared<FJsonObject>();
    for (const FMirmirStationFrame& State : StationFrames)
    {
        const TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetBoolField(TEXT("ready"), bReady && State.bReady);
        Item->SetStringField(TEXT("frame_file"), State.Id.ToString() + TEXT(".png"));
        Item->SetNumberField(TEXT("frame_seq"), double(State.Sequence));
        Item->SetNumberField(TEXT("png_bytes"), double(State.PNGBytes));
        Item->SetNumberField(TEXT("png_crc32"), double(State.PNGCRC32));
        Item->SetNumberField(TEXT("width"), State.Size); Item->SetNumberField(TEXT("height"), State.Size);
        Item->SetNumberField(TEXT("layout_revision"), double(State.LayoutRevision));
        // Offsets describe image top-left corners. Smaller captures share the
        // main capture's pixel scale, but not its half-width/half-height.
        const double CenterAdjustment = (FrameSize - State.Size) * .5;
        Item->SetArrayField(TEXT("desktop_offset_px"), {
            MakeShared<FJsonValueNumber>(CenterAdjustment + FVector::DotProduct(State.CameraShift, Basis.GetUnitAxis(EAxis::Y)) * Scale),
            MakeShared<FJsonValueNumber>(CenterAdjustment - FVector::DotProduct(State.CameraShift, Basis.GetUnitAxis(EAxis::Z)) * Scale) });
        Item->SetArrayField(TEXT("center_px"), { MakeShared<FJsonValueNumber>(State.Size * .5), MakeShared<FJsonValueNumber>(State.Size * .5) });
        Item->SetArrayField(TEXT("world_floor_cm"), Vector(Habitat->GetStationFloorPosition(State.Id)));
        Items->SetObjectField(State.Id.ToString(), Item);
    }
    Props->SetObjectField(TEXT("items"), Items);
    return Props;
}

bool AMirmirFrameExporter::CaptureVisibleInput()
{
    const FVector Camera = Subject->GetActorLocation() + CameraOffset;
    CatDepthCapture->SetWorldLocationAndRotation(Camera, CameraRotation);
    WorldDepthCapture->SetWorldLocationAndRotation(Camera, CameraRotation);
    CatDepthCapture->CaptureScene(); WorldDepthCapture->CaptureScene();
    TArray<FLinearColor> CatDepth, SceneDepth;
    FReadSurfaceDataFlags Flags(RCM_MinMax); Flags.SetLinearToGamma(false);
    const int32 Count = FrameSize * FrameSize;
    if (!CatDepthTarget->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(CatDepth, Flags) ||
        !WorldDepthTarget->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(SceneDepth, Flags) ||
        CatDepth.Num() != Count || SceneDepth.Num() != Count)
    { ReportFailure(TEXT("Depth-tested input readback failed.")); return false; }
    TArray<FColor> Mask; Mask.SetNumUninitialized(Count);
    TArray<int32> Prefix; Prefix.SetNumZeroed((FrameSize + 1) * (FrameSize + 1));
    VisibleCatPixels = 0; OccludedCatPixels = 0;
    for (int32 Y = 0; Y < FrameSize; ++Y)
    {
        int32 RowSum = 0;
        for (int32 X = 0; X < FrameSize; ++X)
        {
            const int32 P = Y * FrameSize + X;
            const float Cat = CatDepth[P].R, Scene = SceneDepth[P].R;
            const bool bCat = FMath::IsFinite(Cat) && Cat > 0.0f && Cat < 10000.0f;
            const bool bVisible = bCat && FMath::IsFinite(Scene) && Scene > 0.0f && Cat <= Scene + .05f;
            VisibleCatPixels += bVisible ? 1 : 0;
            OccludedCatPixels += bCat && !bVisible ? 1 : 0;
            Mask[P] = bVisible ? FColor(255, 255, 255, 255) : FColor(0, 0, 0, 0);
            RowSum += bCat ? 1 : 0;
            Prefix[(Y + 1) * (FrameSize + 1) + X + 1] = Prefix[Y * (FrameSize + 1) + X + 1] + RowSum;
        }
    }
    if (VisibleCatPixels + OccludedCatPixels == 0)
    { ReportFailure(TEXT("Depth-tested input contains no cat geometry.")); return false; }
    CatSupportPixels.SetNumUninitialized(Count);
    const int32 Stride = FrameSize + 1;
    for (int32 Y = 0; Y < FrameSize; ++Y)
        for (int32 X = 0; X < FrameSize; ++X)
        {
            const int32 L = FMath::Max(0, X - 8), R = FMath::Min(FrameSize, X + 9);
            const int32 T = FMath::Max(0, Y - 8), B = FMath::Min(FrameSize, Y + 9);
            CatSupportPixels[Y * FrameSize + X] = Prefix[B * Stride + R] - Prefix[T * Stride + R] - Prefix[B * Stride + L] + Prefix[T * Stride + L] > 0 ? 1 : 0;
        }
    if (bFlipVertically)
        for (int32 Y = 0; Y < FrameSize / 2; ++Y)
            for (int32 X = 0; X < FrameSize; ++X)
                Swap(Mask[Y * FrameSize + X], Mask[(FrameSize - Y - 1) * FrameSize + X]);
    const FImageView Image(Mask.GetData(), FrameSize, FrameSize, EGammaSpace::sRGB);
    TArray64<uint8> PNG;
    if (!ImageWrapper->CompressImage(PNG, EImageFormat::PNG, Image, 0) || PNG.IsEmpty() || !AtomicWrite(TEXT("cat-input.png"), PNG))
    { ReportFailure(TEXT("Depth-tested input PNG could not be published.")); return false; }
    InputPNGBytes = PNG.Num(); InputPNGCRC32 = FCrc::MemCrc32(PNG.GetData(), int32(PNG.Num()));
    return true;
}
