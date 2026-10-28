#include "MirmirFrameExporter.h"

#include "MirmirCat.h"
#include "MirmirToy.h"
#include "MirmirHabitat.h"
#include "MirmirAnimInstance.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Engine.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "IImageWrapperModule.h"
#include "ImageCore.h"
#include "Math/RotationMatrix.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Crc.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "RHI.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"

#include <cstdio>

#if PLATFORM_LINUX
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#elif PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogMirmirFrameExporter, Log, All);

namespace
{
FString StateDirectory()
{
    FString Output = FPlatformMisc::GetEnvironmentVariable(TEXT("MIRMIR_FRAME_DIR"));
    if (!Output.IsEmpty() && !FPaths::IsRelative(Output))
        return FPaths::ConvertRelativePathToFull(Output);
    FString Base = FPlatformMisc::GetEnvironmentVariable(TEXT("XDG_STATE_HOME"));
    if (Base.IsEmpty() || FPaths::IsRelative(Base))
        Base = FPaths::Combine(FPlatformProcess::UserHomeDir(), TEXT(".local/state"));
    return FPaths::Combine(Base, TEXT("mirmir/unreal"));
}

TArray<TSharedPtr<FJsonValue>> VectorJSON(const FVector& Value)
{
    return { MakeShared<FJsonValueNumber>(Value.X),
        MakeShared<FJsonValueNumber>(Value.Y), MakeShared<FJsonValueNumber>(Value.Z) };
}
}

AMirmirFrameExporter::AMirmirFrameExporter()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    CaptureComponent = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("CatCapture"));
    SetRootComponent(CaptureComponent);
    CaptureComponent->bCaptureEveryFrame = false;
    CaptureComponent->bCaptureOnMovement = false;
    CaptureComponent->bAlwaysPersistRenderingState = false;
    CaptureComponent->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    // Epic lists this source as "Scene Color (HDR) in RGB, Inv Opacity in A":
    // https://dev.epicgames.com/documentation/en-us/unreal-engine/planar-reflections-in-unreal-engine
    CaptureComponent->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
    CaptureComponent->ShowFlags.SetAtmosphere(false);
    CaptureComponent->ShowFlags.SetFog(false);
    CaptureComponent->ShowFlags.SetVolumetricFog(false);
    CaptureComponent->ShowFlags.SetPostProcessing(false);
    CaptureComponent->ShowFlags.SetMotionBlur(false);
    CaptureComponent->ShowFlags.SetTemporalAA(false);
    const FName StationNames[] = { TEXT("BedCapture"), TEXT("BowlCapture") };
    for (int32 I = 0; I < 2; ++I)
    {
        USceneCaptureComponent2D* Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(StationNames[I]);
        Capture->SetupAttachment(CaptureComponent);
        Capture->bCaptureEveryFrame = false;
        Capture->bCaptureOnMovement = false;
        Capture->bAlwaysPersistRenderingState = false;
        Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
        Capture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
        Capture->ShowFlags = CaptureComponent->ShowFlags;
        StationCaptures.Add(Capture);
    }
    CatDepthCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("CatInputDepth"));
    WorldDepthCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("WorldInputDepth"));
    for (USceneCaptureComponent2D* Depth : { CatDepthCapture.Get(), WorldDepthCapture.Get() })
    {
        Depth->SetupAttachment(CaptureComponent);
        Depth->bCaptureEveryFrame = false;
        Depth->bCaptureOnMovement = false;
        Depth->bAlwaysPersistRenderingState = false;
        Depth->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
        Depth->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
        Depth->ShowFlags = CaptureComponent->ShowFlags;
    }

}

bool AMirmirFrameExporter::Initialize(AMirmirCat* InCat, UCameraComponent* InCamera)
{
    if (bInitialized) return true;
    if (!FParse::Param(FCommandLine::Get(), TEXT("MirmirDesktop"))) return false;
    if (!IsValid(InCat) || !IsValid(InCamera) || !FApp::CanEverRender())
    {
        ReportFailure(TEXT("Desktop export requires a valid cat, camera and rendering RHI."));
        return false;
    }
#if !PLATFORM_LINUX && !PLATFORM_WINDOWS
    ReportFailure(TEXT("Desktop frame export requires Linux or Windows."));
    return false;
#endif
    OutputDirectory = StateDirectory();
    if (!IFileManager::Get().MakeDirectory(*OutputDirectory, true) || !AcquireOutputLock())
    {
        ReportFailure(TEXT("Desktop output directory is unavailable or another exporter owns it."));
        return false;
    }
    ImageWrapper = FModuleManager::LoadModulePtr<IImageWrapperModule>(TEXT("ImageWrapper"));
    if (!ImageWrapper)
    {
        ReportFailure(TEXT("ImageWrapper module could not be loaded."));
        ReleaseOutputLock();
        return false;
    }
    FrameSize = FMath::Clamp(FrameSize, 128, 1024);
    FramesPerSecond = FMath::Clamp(FMath::IsFinite(FramesPerSecond) ? FramesPerSecond : 30.0f, 1.0f, 30.0f);
    IdleFramesPerSecond = FMath::Clamp(FMath::IsFinite(IdleFramesPerSecond) ? IdleFramesPerSecond : 8.0f, 1.0f, FramesPerSecond);
    ActiveGraceSeconds = FMath::Clamp(FMath::IsFinite(ActiveGraceSeconds) ? ActiveGraceSeconds : 1.0f, 0.1f, 5.0f);
    CurrentCaptureFPS = FramesPerSecond;
    // Shipping excludes command-line ExecCmds, so apply the companion's cap in
    // code as well. Avoid an uncapped hidden game viewport behind the shell.
    if (GEngine) GEngine->SetMaxFPS(CurrentCaptureFPS);
    Subject = InCat;
    SourceCamera = InCamera;
    InitialCatLocation = InCat->GetDesktopOrigin();
    LastActorLocation = InitialCatLocation;
    LastActorRotation = InCat->GetActorQuat();
    LastActivity = uint8(InCat->Activity);
    LastBond = InCat->Bond;
    LastSatiety = InCat->Satiety;
    bLastMuted = InCat->bMuted;
    CameraOffset = InCamera->GetComponentLocation() - InitialCatLocation;
    CameraRotation = InCamera->GetComponentRotation();
    SessionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
    FrameSequence = 0;
    CaptureComponent->ShowOnlyActors.Reset();
    CaptureComponent->ShowOnlyActors.Add(InCat);
    if (IsValid(InCat->Habitat)) CaptureComponent->ShowOnlyActors.Add(InCat->Habitat);
    if (IsValid(InCat->Toy)) CaptureComponent->ShowOnlyActors.Add(InCat->Toy);
    CaptureComponent->ProjectionType = InCamera->ProjectionMode;
    CaptureComponent->FOVAngle = InCamera->FieldOfView;
    CaptureComponent->OrthoWidth = InCamera->OrthoWidth;
    if (bMatchCameraPixelScale)
    {
        if (InCamera->ProjectionMode == ECameraProjectionMode::Orthographic)
            DesktopPixelsPerCentimeter = FrameSize / FMath::Max(1.0f, InCamera->OrthoWidth);
        else
            DesktopPixelsPerCentimeter = FrameSize / FMath::Max(1.0,
                2.0 * CameraOffset.Size() * FMath::Tan(FMath::DegreesToRadians(InCamera->FieldOfView * 0.5f)));
    }
    CaptureComponent->SetWorldLocationAndRotation(InitialCatLocation + CameraOffset, CameraRotation);

    RenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("DesktopCatFrame"));
    // The background holds inverse opacity 1, so the published PNG has alpha 0.
    RenderTarget->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
    RenderTarget->bAutoGenerateMips = false;
    RenderTarget->InitCustomFormat(FrameSize, FrameSize, PF_FloatRGBA, true);
    RenderTarget->UpdateResourceImmediate(true);
    CaptureComponent->TextureTarget = RenderTarget;
    auto DepthTarget = [this](const TCHAR* Name, USceneCaptureComponent2D* Depth, bool bFurniture)
    {
        Depth->ProjectionType = CaptureComponent->ProjectionType;
        Depth->OrthoWidth = CaptureComponent->OrthoWidth;
        Depth->FOVAngle = CaptureComponent->FOVAngle;
        Depth->ShowOnlyActors.Add(Subject.Get());
        if (Subject->Toy) Depth->ShowOnlyActors.Add(Subject->Toy);
        if (bFurniture && Subject->Habitat) Depth->ShowOnlyActors.Add(Subject->Habitat);
        UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(this, Name);
        Target->ClearColor = FLinearColor(1.0e20f, 0, 0, 0);
        // Vulkan's FLinearColor readback supports RGBA32f, not R32f. Retain
        // centimetre depth precision rather than quantising to half floats.
        Target->InitCustomFormat(FrameSize, FrameSize, PF_A32B32G32R32F, true);
        Target->UpdateResourceImmediate(true);
        Depth->TextureTarget = Target;
        return Target;
    };
    CatDepthTarget = DepthTarget(TEXT("CatInputDepthTarget"), CatDepthCapture, false);
    WorldDepthTarget = DepthTarget(TEXT("WorldInputDepthTarget"), WorldDepthCapture, true);
    StationFrames[0].Id = TEXT("bed"); StationFrames[0].Size = FrameSize;
    StationFrames[1].Id = TEXT("bowl"); StationFrames[1].Size = FMath::Max(128, FrameSize / 2);
    if (IsValid(InCat->Habitat))
    {
        StationTargets.SetNum(2);
        for (int32 I = 0; I < 2; ++I)
        {
            USceneCaptureComponent2D* Station = StationCaptures[I];
            Station->ProjectionType = ECameraProjectionMode::Orthographic;
            Station->OrthoWidth = StationFrames[I].Size / DesktopPixelsPerCentimeter;
            UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(this);
            Target->ClearColor = FLinearColor(0, 0, 0, 1);
            Target->InitCustomFormat(StationFrames[I].Size, StationFrames[I].Size, PF_FloatRGBA, true);
            Target->UpdateResourceImmediate(true);
            StationTargets[I] = Target;
            Station->TextureTarget = Target;
        }
    }
    bInitialized = true;
    NextCaptureSeconds = FPlatformTime::Seconds();
    ActiveUntilSeconds = NextCaptureSeconds + ActiveGraceSeconds;
    if (!PublishRuntime(TEXT("waiting_for_frame"), false))
    {
        ReportFailure(TEXT("Initial desktop runtime metadata could not be published."));
        ShutdownExporter();
        return false;
    }
    bOwnsOutput = true;
    SetActorTickEnabled(true);
    return true;
}

void AMirmirFrameExporter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bInitialized) return;
    if (!Subject.IsValid() || !SourceCamera.IsValid())
    {
        ReportFailure(TEXT("Desktop subject or source camera was removed."));
        ShutdownExporter();
        return;
    }
    const double Now = FPlatformTime::Seconds();
    PollPlacement();
    PollCursorInterest();
    UpdateCadence(Now, DeltaSeconds);
    // The engine uses the same cap. Sub-millisecond timer jitter must not turn
    // one nominal 8 Hz tick into a missed capture and a visible 250 ms pause.
    if (Now + 0.001 < NextCaptureSeconds) return;
    // Keep the clock phase through ordinary frame jitter. Scheduling from Now
    // each time can skip every other frame when engine and capture limits match.
    // A genuinely missed deadline drops the backlog rather than catching up.
    const double Interval = 1.0 / CurrentCaptureFPS;
    NextCaptureSeconds += Interval;
    if (NextCaptureSeconds <= Now) NextCaptureSeconds = Now + Interval;
    CaptureAndPublish();
}

void AMirmirFrameExporter::PollCursorInterest()
{
    AMirmirCat* Cat = Subject.Get();
    if (!Cat) return;
    const FString Path = FPaths::Combine(OutputDirectory, TEXT("cursor.json"));
    FString Text;
    TSharedPtr<FJsonObject> Cursor;
    auto Reject = [this, Cat]()
    {
        LastCursorSampleMilliseconds = -1.0;
        Cat->SetCuriosityTarget(FVector::ZeroVector, false);
    };
    const int64 Size = IFileManager::Get().FileSize(*Path);
    if (Size <= 0 || Size > 2048 || !FFileHelper::LoadFileToString(Text, *Path) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Cursor) || !Cursor.IsValid())
    {
        Reject();
        return;
    }
    double Schema, Producer, Sample, Expiry;
    FString Session;
    bool bActive = false;
    const TArray<TSharedPtr<FJsonValue>>* Offset = nullptr;
    const FDateTime UTC = FDateTime::UtcNow();
    const double NowMS = double(UTC.ToUnixTimestamp()) * 1000.0 + UTC.GetMillisecond();
    if (!Cursor->TryGetNumberField(TEXT("schema_version"), Schema) || Schema != 1.0 ||
        !Cursor->TryGetNumberField(TEXT("producer_pid"), Producer) || Producer != FPlatformProcess::GetCurrentProcessId() ||
        !Cursor->TryGetStringField(TEXT("session_id"), Session) || Session != SessionId ||
        !Cursor->TryGetBoolField(TEXT("active"), bActive) || !bActive ||
        !Cursor->TryGetNumberField(TEXT("sampled_at_unix_ms"), Sample) || !FMath::IsFinite(Sample) ||
        !Cursor->TryGetNumberField(TEXT("expires_at_unix_ms"), Expiry) || !FMath::IsFinite(Expiry) ||
        Sample > NowMS + 100.0 || NowMS > Expiry || Expiry < Sample || Expiry - Sample > 750.0 ||
        !Cursor->TryGetArrayField(TEXT("target_offset_px"), Offset) || Offset->Num() != 2)
    {
        Reject();
        return;
    }
    double DX, DY;
    if (!(*Offset)[0].IsValid() || !(*Offset)[1].IsValid() ||
        !(*Offset)[0]->TryGetNumber(DX) || !(*Offset)[1]->TryGetNumber(DY) ||
        !FMath::IsFinite(DX) || !FMath::IsFinite(DY) ||
        FMath::Abs(DX) > FrameSize * 0.5 || FMath::Abs(DY) > FrameSize * 0.5)
    {
        Reject();
        return;
    }
    if (Sample == LastCursorSampleMilliseconds) return;
    // Invert the same orthographic projection used by desktop_offset_px.
    // Only the current offset inside our own render frame is exchanged; no
    // absolute desktop coordinate, other window or screenshot is collected.
    const FRotationMatrix Basis(CameraRotation);
    const FVector Right = Basis.GetUnitAxis(EAxis::Y);
    const FVector Up = Basis.GetUnitAxis(EAxis::Z);
    const double Determinant = Right.X * Up.Y - Right.Y * Up.X;
    if (FMath::Abs(Determinant) < 0.05 || CaptureComponent->ProjectionType != ECameraProjectionMode::Orthographic)
    {
        Reject();
        return;
    }
    const double Scale = FMath::Clamp(DesktopPixelsPerCentimeter, 0.01f, 20.0f);
    const double R = DX / Scale, U = -DY / Scale;
    FVector Delta((R * Up.Y - Right.Y * U) / Determinant,
        (Right.X * U - R * Up.X) / Determinant, 0.0);
    Delta = Delta.GetClampedToMaxSize(100.0);
    FVector Target = Cat->GetActorLocation() + Delta;
    Target.Z = InitialCatLocation.Z;
    LastCursorSampleMilliseconds = Sample;
    Cat->SetCuriosityTarget(Target, true);
}

void AMirmirFrameExporter::UpdateCadence(double Now, float DeltaSeconds)
{
    const AMirmirCat* Cat = Subject.Get();
    const FVector Location = Cat->GetActorLocation();
    const FQuat Rotation = Cat->GetActorQuat();
    const float SafeDelta = FMath::Max(DeltaSeconds, 0.001f);
    const bool bTranslated = FVector::DistSquared(Location, LastActorLocation) > FMath::Square(0.5f * SafeDelta);
    const bool bTurning = FMath::RadiansToDegrees(Rotation.AngularDistance(LastActorRotation)) > 0.05f;
    bool bAuthoredPlaying = Cat->bAnimationTransitionBlocksMovement;
    if (const USkeletalMeshComponent* Mesh = Cat->CatMesh.Get())
    {
        if (const UAnimSingleNodeInstance* Single = Mesh->GetSingleNodeInstance())
            bAuthoredPlaying |= Single->IsPlaying();
        if (const UMirmirAnimInstance* Native = Cast<UMirmirAnimInstance>(Mesh->GetAnimInstance()))
            bAuthoredPlaying |= Native->PlaybackState == TEXT("IdleVariation");
    }
    // Native BlendStack is not a SingleNode player: actor motion and its
    // explicit transition flag decide cadence without treating idle as walking.
    const bool bToyMoving = Cat->Toy && Cat->Toy->IsPlaying() && Cat->Toy->GetBallSpeedCmPerSecond() > 1.0f;
    const bool bHeadMoving = FMath::Abs(Cat->CuriosityHeadYaw - LastHeadYaw) > 0.1f ||
        FMath::Abs(Cat->CuriosityHeadPitch - LastHeadPitch) > 0.1f;
    const bool bRestTransition = Cat->RestPoseAlpha > 0.001f && Cat->RestPoseAlpha < 0.999f;
    const bool bActiveMotion = Cat->MovementSpeed > 1.0f || bTranslated || bTurning || bAuthoredPlaying || bToyMoving || bHeadMoving ||
        Cat->PetReactionStrength > 0.001f || bRestTransition || Cat->EatingPoseStrength > 0.001f;
    const bool bInteraction = uint8(Cat->Activity) != LastActivity || Cat->Bond > LastBond + 0.0001f ||
        Cat->Satiety > LastSatiety + 0.001f || Cat->bMuted != bLastMuted || Cat->AppearanceId != LastAppearance;
    if (bActiveMotion || bInteraction) ActiveUntilSeconds = Now + ActiveGraceSeconds;
    const bool bWarm = Now < ActiveUntilSeconds;
    const float Target = (bActiveMotion || bWarm) ? FramesPerSecond : IdleFramesPerSecond;
    RenderProfile = bActiveMotion ? TEXT("active") : bWarm ? TEXT("warm") : TEXT("idle");
    if (!FMath::IsNearlyEqual(Target, CurrentCaptureFPS))
    {
        CurrentCaptureFPS = Target;
        // Publish the first frame of a wake/sleep transition immediately; then
        // resume phase scheduling. Missed deadlines never create a catch-up loop.
        NextCaptureSeconds = Now;
    }
    if (GEngine && !FMath::IsNearlyEqual(GEngine->GetMaxFPS(), CurrentCaptureFPS))
        GEngine->SetMaxFPS(CurrentCaptureFPS);
    LastActorLocation = Location;
    LastActorRotation = Rotation;
    LastActivity = uint8(Cat->Activity);
    LastBond = Cat->Bond;
    LastSatiety = Cat->Satiety;
    bLastMuted = Cat->bMuted;
    LastAppearance = Cat->AppearanceId;
    LastHeadYaw = Cat->CuriosityHeadYaw;
    LastHeadPitch = Cat->CuriosityHeadPitch;
}

bool AMirmirFrameExporter::CaptureAndPublish()
{
    CapturePropsIfNeeded();
    if (!CaptureVisibleInput()) return false;
    CaptureComponent->SetWorldLocationAndRotation(
        Subject->GetActorLocation() + CameraOffset, CameraRotation);
    // CaptureScene is manual; enabling capture-every-frame would double render.
    // https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/USceneCaptureComponent2D/CaptureScene
    CaptureComponent->CaptureScene();
    FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource();
    TArray<FLinearColor> HDRPixels;
    FReadSurfaceDataFlags ReadFlags(RCM_MinMax);
    ReadFlags.SetLinearToGamma(false);
    if (!Resource || !Resource->ReadLinearColorPixels(HDRPixels, ReadFlags) || HDRPixels.Num() != FrameSize * FrameSize)
    {
        ReportFailure(TEXT("Unreal cat render-target readback failed."));
        return false;
    }
    int32 AlphaMin = 255, AlphaMax = 0, ForegroundPixels = 0;
    const float Gain = FMath::Clamp(DisplayGain, 0.1f, 4.0f);
    TArray<FColor> Pixels;
    Pixels.SetNumUninitialized(HDRPixels.Num());
    LastHDRPeak = 0.0f;
    LastHDRHighlightPixels = 0;
    const auto MapChannel = [this](float Value)
    {
        const float X = FMath::Max(0.0f, Value);
        // A compact fitted filmic shoulder. This is a display transform, not a
        // claim of a complete ACES color-management pipeline.
        return bUseFilmicToneMap ? FMath::Clamp(
            (X * (2.51f * X + 0.03f)) / (X * (2.43f * X + 0.59f) + 0.14f), 0.0f, 1.0f)
            : FMath::Clamp(X, 0.0f, 1.0f);
    };
    for (int32 Index = 0; Index < HDRPixels.Num(); ++Index)
    {
        FLinearColor Linear = HDRPixels[Index];
        const float Coverage = CatSupportPixels.IsValidIndex(Index) && CatSupportPixels[Index]
            ? FMath::Clamp(bInvertCaptureAlpha ? 1.0f - Linear.A : Linear.A, 0.0f, 1.0f) : 0.0f;
        FColor& Pixel = Pixels[Index];
        Pixel.A = static_cast<uint8>(FMath::RoundToInt(Coverage * 255.0f));
        AlphaMin = FMath::Min(AlphaMin, int32(Pixel.A));
        AlphaMax = FMath::Max(AlphaMax, int32(Pixel.A));
        ForegroundPixels += Pixel.A > 0 ? 1 : 0;
        if (Pixel.A == 0)
        {
            Pixel = FColor(0, 0, 0, 0);
            continue;
        }
        const uint8 Alpha = Pixel.A;
        const float Factor = Gain / (bUnpremultiplyCapturedRGB ? FMath::Max(Coverage, 1.0f / 255.0f) : 1.0f);
        Linear.R *= Factor; Linear.G *= Factor; Linear.B *= Factor;
        LastHDRPeak = FMath::Max(LastHDRPeak, FMath::Max3(Linear.R, Linear.G, Linear.B));
        LastHDRHighlightPixels += Linear.R > 1.0f || Linear.G > 1.0f || Linear.B > 1.0f ? 1 : 0;
        Linear.R = MapChannel(Linear.R); Linear.G = MapChannel(Linear.G); Linear.B = MapChannel(Linear.B);
        Pixel = Linear.ToFColorSRGB();
        Pixel.A = Alpha;
    }
    if (bRequireMixedAlpha && (AlphaMin != 0 || AlphaMax == 0))
    {
        ReportFailure(TEXT("Cat frame is blank or has no transparent background; inspect capture alpha before enabling display."));
        return false;
    }
    if (bFlipVertically)
    {
        for (int32 Y = 0; Y < FrameSize / 2; ++Y)
            for (int32 X = 0; X < FrameSize; ++X)
                Swap(Pixels[Y * FrameSize + X], Pixels[(FrameSize - Y - 1) * FrameSize + X]);
    }
    const FImageView Image(Pixels.GetData(), FrameSize, FrameSize, EGammaSpace::sRGB);
    TArray64<uint8> PNG;
    // Use the current image-oriented API, not deprecated individual wrappers.
    // https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/ImageWrapper/IImageWrapperModule/CompressImage
    if (!ImageWrapper->CompressImage(PNG, EImageFormat::PNG, Image, 0) || PNG.IsEmpty())
    {
        ReportFailure(TEXT("Unreal cat PNG encoding failed."));
        return false;
    }
    const uint32 CRC32 = FCrc::MemCrc32(PNG.GetData(), int32(PNG.Num()));
    if (!AtomicWrite(TEXT("frame.png"), PNG))
    {
        ReportFailure(TEXT("Desktop PNG could not be atomically published."));
        return false;
    }
    ++FrameSequence;
    if (!PublishRuntime(TEXT("ready"), true, PNG.Num(), CRC32, AlphaMin, AlphaMax, ForegroundPixels))
    {
        ReportFailure(TEXT("Desktop frame metadata could not be atomically published."));
        return false;
    }
    return true;
}

bool AMirmirFrameExporter::PublishRuntime(const TCHAR* Status, bool bReady, int64 PNGBytes,
    uint32 PNGCRC32, int32 AlphaMin, int32 AlphaMax, int32 ForegroundPixels, const FString& Error)
{
    TSharedRef<FJsonObject> JSON = MakeShared<FJsonObject>();
    JSON->SetNumberField(TEXT("schema_version"), 1);
    JSON->SetStringField(TEXT("producer"), TEXT("unreal-scene-capture"));
    JSON->SetNumberField(TEXT("producer_pid"), FPlatformProcess::GetCurrentProcessId());
    JSON->SetStringField(TEXT("session_id"), SessionId);
    JSON->SetStringField(TEXT("frame_file"), TEXT("frame.png"));
    const TSharedRef<FJsonObject> Input = MakeShared<FJsonObject>();
    Input->SetStringField(TEXT("frame_file"), TEXT("cat-input.png"));
    Input->SetNumberField(TEXT("frame_seq"), double(FrameSequence));
    Input->SetNumberField(TEXT("png_bytes"), double(InputPNGBytes));
    Input->SetNumberField(TEXT("png_crc32"), double(InputPNGCRC32));
    Input->SetNumberField(TEXT("width"), FrameSize);
    Input->SetNumberField(TEXT("height"), FrameSize);
    Input->SetNumberField(TEXT("visible_cat_pixels"), VisibleCatPixels);
    Input->SetNumberField(TEXT("occluded_cat_pixels"), OccludedCatPixels);
    JSON->SetObjectField(TEXT("input_mask"), Input);
    JSON->SetBoolField(TEXT("main_frame_includes_furniture"), true);
    JSON->SetNumberField(TEXT("frame_seq"), double(FrameSequence));
    JSON->SetNumberField(TEXT("width"), FrameSize);
    JSON->SetNumberField(TEXT("height"), FrameSize);
    JSON->SetNumberField(TEXT("fps_limit"), CurrentCaptureFPS);
    JSON->SetNumberField(TEXT("nominal_fps_limit"), FramesPerSecond);
    JSON->SetNumberField(TEXT("idle_fps_limit"), IdleFramesPerSecond);
    JSON->SetStringField(TEXT("render_profile"), RenderProfile);
    JSON->SetNumberField(TEXT("engine_fps_limit"), GEngine ? GEngine->GetMaxFPS() : 0.0f);
    JSON->SetBoolField(TEXT("running"), bInitialized);
    JSON->SetBoolField(TEXT("ready"), bReady);
    JSON->SetStringField(TEXT("status"), Status);
    JSON->SetStringField(TEXT("updated_at_utc"), FDateTime::UtcNow().ToIso8601());
    JSON->SetNumberField(TEXT("png_bytes"), double(PNGBytes));
    JSON->SetNumberField(TEXT("png_crc32"), double(PNGCRC32));
    JSON->SetStringField(TEXT("alpha_mode"), TEXT("straight"));
    JSON->SetStringField(TEXT("color_space"), TEXT("sRGB"));
    JSON->SetStringField(TEXT("tone_map"), bUseFilmicToneMap ? TEXT("filmic_fitted") : TEXT("clamp"));
    JSON->SetNumberField(TEXT("hdr_peak_linear"), LastHDRPeak);
    JSON->SetNumberField(TEXT("hdr_highlight_pixels"), LastHDRHighlightPixels);
    JSON->SetBoolField(TEXT("capture_alpha_inverted"), bInvertCaptureAlpha);
    JSON->SetBoolField(TEXT("rgb_unpremultiplied"), bUnpremultiplyCapturedRGB);
    JSON->SetBoolField(TEXT("flipped_vertically"), bFlipVertically);
    JSON->SetNumberField(TEXT("alpha_min"), AlphaMin);
    JSON->SetNumberField(TEXT("alpha_max"), AlphaMax);
    JSON->SetNumberField(TEXT("foreground_pixels"), ForegroundPixels);
    JSON->SetNumberField(TEXT("desktop_pixels_per_cm"), DesktopPixelsPerCentimeter);
    JSON->SetStringField(TEXT("desktop_offset_units"), TEXT("physical_pixels"));
    if (!Error.IsEmpty()) JSON->SetStringField(TEXT("error"), Error);
    if (const AMirmirCat* Cat = Subject.Get())
    {
        const TSharedRef<FJsonObject> Interaction = MakeShared<FJsonObject>();
        Interaction->SetNumberField(TEXT("serial"), double(Cat->InteractionSerial));
        Interaction->SetStringField(TEXT("action"), Cat->LastInteractionAction.ToString().ToLower());
        Interaction->SetBoolField(TEXT("accepted"), Cat->bLastInteractionAccepted);
        Interaction->SetStringField(TEXT("message"), Cat->InteractionMessage);
        JSON->SetObjectField(TEXT("interaction"), Interaction);
        if (Cat->Habitat)
        {
            JSON->SetObjectField(TEXT("props"), BuildPropsState(bReady));
            const TSharedRef<FJsonObject> Placement = MakeShared<FJsonObject>();
            Placement->SetStringField(TEXT("request_id"), PlacementRequestId);
            Placement->SetStringField(TEXT("item_id"), PlacementItemId);
            Placement->SetBoolField(TEXT("accepted"), bPlacementAccepted);
            Placement->SetStringField(TEXT("message"), PlacementMessage);
            Placement->SetNumberField(TEXT("layout_revision"), double(Cat->Habitat->GetLayoutRevision()));
            if (PlacementItemId == TEXT("bed") || PlacementItemId == TEXT("bowl"))
                Placement->SetArrayField(TEXT("world_floor_cm"), VectorJSON(Cat->Habitat->GetStationFloorPosition(FName(*PlacementItemId))));
            JSON->SetObjectField(TEXT("placement"), Placement);
        }
        JSON->SetStringField(TEXT("appearance"), Cat->AppearanceId.ToString());
        JSON->SetBoolField(TEXT("appearance_ready"), Cat->bAppearanceReady);
        JSON->SetBoolField(TEXT("fur_shells_ready"), Cat->bFurShellsReady);
        JSON->SetStringField(TEXT("appearance_head_bone"), Cat->AppearanceHeadBoneName.ToString());
        JSON->SetNumberField(TEXT("appearance_head_scale"), Cat->AppearanceHeadScale);
        if (const AMirmirToy* Toy = Cat->Toy.Get())
        {
            TSharedRef<FJsonObject> ToyState = MakeShared<FJsonObject>();
            ToyState->SetBoolField(TEXT("active"), Toy->IsPlaying());
            ToyState->SetArrayField(TEXT("ball_world_cm"), VectorJSON(Toy->GetBallLocation()));
            ToyState->SetArrayField(TEXT("velocity_cm_s"), VectorJSON(Toy->GetBallVelocity()));
            ToyState->SetNumberField(TEXT("speed_cm_s"), Toy->GetBallSpeedCmPerSecond());
            ToyState->SetNumberField(TEXT("playing_seconds"), Toy->PlayingSeconds);
            ToyState->SetStringField(TEXT("stop_reason"), Toy->LastStopReason.ToString());
            ToyState->SetBoolField(TEXT("cat_waiting"), Cat->bWaitingAtToy);
            JSON->SetObjectField(TEXT("toy"), ToyState);
        }
        const FVector Location = Cat->GetActorLocation();
        const FVector Delta = Location - InitialCatLocation;
        const FRotationMatrix Basis(CameraRotation);
        const double Scale = FMath::Clamp(DesktopPixelsPerCentimeter, 0.01f, 20.0f);
        JSON->SetArrayField(TEXT("actor_world_cm"), VectorJSON(Location));
        JSON->SetArrayField(TEXT("desktop_offset_px"), {
            MakeShared<FJsonValueNumber>(FVector::DotProduct(Delta, Basis.GetUnitAxis(EAxis::Y)) * Scale),
            MakeShared<FJsonValueNumber>(-FVector::DotProduct(Delta, Basis.GetUnitAxis(EAxis::Z)) * Scale) });
        TSharedRef<FJsonObject> Needs = MakeShared<FJsonObject>();
        Needs->SetNumberField(TEXT("satiety"), Cat->Satiety);
        Needs->SetNumberField(TEXT("energy"), Cat->Energy);
        Needs->SetNumberField(TEXT("bond"), Cat->Bond);
        Needs->SetNumberField(TEXT("curiosity"), Cat->Curiosity);
        Needs->SetBoolField(TEXT("muted"), Cat->bMuted);
        Needs->SetStringField(TEXT("activity"), StaticEnum<EMirmirActivity>()->GetNameStringByValue(int64(Cat->Activity)));
        JSON->SetObjectField(TEXT("needs"), Needs);
        TSharedRef<FJsonObject> Motion = MakeShared<FJsonObject>();
        Motion->SetNumberField(TEXT("sample_monotonic_s"), FPlatformTime::Seconds());
        Motion->SetNumberField(TEXT("movement_speed_cm_s"), Cat->MovementSpeed);
        Motion->SetBoolField(TEXT("walking_backwards"), Cat->bWalkingBackwards);
        Motion->SetBoolField(TEXT("movement_blocked"), Cat->bMovementBlocked);
        Motion->SetNumberField(TEXT("yaw_rate_rad_s"), Cat->LocomotionYawRate);
        Motion->SetNumberField(TEXT("curvature_per_cm"), Cat->LocomotionCurvature);
        Motion->SetNumberField(TEXT("minimum_turn_radius_cm"), Cat->MinimumTurnRadius);
        Motion->SetNumberField(TEXT("curiosity_head_yaw_deg"), Cat->CuriosityHeadYaw);
        Motion->SetNumberField(TEXT("curiosity_head_pitch_deg"), Cat->CuriosityHeadPitch);
        Motion->SetNumberField(TEXT("actor_yaw_deg"), Cat->GetActorRotation().Yaw);
        Motion->SetArrayField(TEXT("actor_forward_world"), VectorJSON(Cat->GetActorForwardVector()));
        if (const USkeletalMeshComponent* Mesh = Cat->CatMesh.Get())
        {
            Motion->SetArrayField(TEXT("mesh_relative_scale"), VectorJSON(Mesh->GetRelativeScale3D()));
            Motion->SetNumberField(TEXT("skeleton_bone_count"), Mesh->GetNumBones());
            // Importers can replace hyphens with underscores or retain numeric
            // suffixes. Match the semantic source names, never assume indices.
            auto BonePosition = [Mesh](const TCHAR* Fragment, FVector& Out)
            {
                for (int32 Index = 0; Index < Mesh->GetNumBones(); ++Index)
                {
                    const FName Bone = Mesh->GetBoneName(Index);
                    const FString Name = Bone.ToString().Replace(TEXT("-"), TEXT("_"));
                    if (Name.Contains(Fragment))
                    {
                        Out = Mesh->GetBoneLocation(Bone, EBoneSpaces::WorldSpace);
                        return true;
                    }
                }
                return false;
            };
            TSharedRef<FJsonObject> Paws = MakeShared<FJsonObject>();
            const TCHAR* Labels[] = { TEXT("front_left"), TEXT("front_right"), TEXT("hind_left"), TEXT("hind_right") };
            const TCHAR* First[] = { TEXT("cat_bip_L_Finger01"), TEXT("cat_bip_R_Finger01"),
                TEXT("cat_bip_L_Toe01"), TEXT("cat_bip_R_Toe01") };
            const TCHAR* Second[] = { TEXT("cat_bip_L_Finger11"), TEXT("cat_bip_R_Finger11"), nullptr, nullptr };
            for (int32 Paw = 0; Paw < 4; ++Paw)
            {
                FVector Position, Other;
                bool bFound = BonePosition(First[Paw], Position);
                if (bFound && Second[Paw])
                {
                    bFound = BonePosition(Second[Paw], Other);
                    if (bFound) Position = (Position + Other) * 0.5;
                }
                if (bFound) Paws->SetArrayField(Labels[Paw], VectorJSON(Position));
            }
            Motion->SetObjectField(TEXT("paw_proxies_world_cm"), Paws);
            Motion->SetStringField(TEXT("paw_proxy_note"), TEXT("Distal authored bone points; not measured paw-pad contacts."));
            FVector Head, Pelvis;
            if (BonePosition(TEXT("cat_bip_Head"), Head) && BonePosition(TEXT("cat_bip_Pelvis"), Pelvis))
                Motion->SetArrayField(TEXT("head_forward_world"), VectorJSON((Head - Pelvis).GetSafeNormal2D()));
            if (const UMirmirAnimInstance* Instance = Cast<UMirmirAnimInstance>(Mesh->GetAnimInstance()))
            {
                Motion->SetStringField(TEXT("animation_graph"), TEXT("native_blend_stack"));
                Motion->SetStringField(TEXT("animation_asset"), GetNameSafe(Instance->ActiveClip.Get()));
                Motion->SetNumberField(TEXT("animation_time_s"), Instance->ActiveClipTime);
                Motion->SetNumberField(TEXT("animation_rate"), Instance->ActivePlayRate);
                Motion->SetBoolField(TEXT("animation_playing"), FMath::Abs(Instance->ActivePlayRate) > SMALL_NUMBER);
                Motion->SetStringField(TEXT("animation_state"), Instance->PlaybackState.ToString());
                Motion->SetNumberField(TEXT("animation_blend_count"), Instance->ActiveBlendCount);
                Motion->SetNumberField(TEXT("stance_valid_legs"), Instance->StanceValidLegs);
                auto FloatArray = [](const TArray<float>& Values)
                {
                    TArray<TSharedPtr<FJsonValue>> Result;
                    for (float Value : Values) Result.Add(MakeShared<FJsonValueNumber>(Value));
                    return Result;
                };
                Motion->SetArrayField(TEXT("stance_weights"), FloatArray(Instance->StanceWeights));
                Motion->SetArrayField(TEXT("stance_correction_cm"), FloatArray(Instance->StanceCorrectionCm));
                Motion->SetArrayField(TEXT("stance_residual_cm"), FloatArray(Instance->StanceResidualCm));
                Motion->SetStringField(TEXT("stance_note"), TEXT("Bounded source-walk stance proxy IK; not paw collision sensing or full physical animal simulation."));
                Motion->SetBoolField(TEXT("animation_transition_blocks_movement"), Instance->bTransitionBlocksMovement);
                Motion->SetStringField(TEXT("rest_pose_note"), Instance->PlaybackState == TEXT("HeldWalk")
                    ? TEXT("Frozen sample of the existing authored walk; no authored idle/sleep clip assigned.") : TEXT("Assigned compatible authored clip."));
            }
            else if (const UAnimSingleNodeInstance* SingleInstance = Mesh->GetSingleNodeInstance())
            {
                Motion->SetStringField(TEXT("animation_graph"), TEXT("legacy_single_node"));
                Motion->SetStringField(TEXT("animation_asset"), GetNameSafe(SingleInstance->GetAnimationAsset()));
                Motion->SetNumberField(TEXT("animation_time_s"), SingleInstance->GetCurrentTime());
                Motion->SetNumberField(TEXT("animation_rate"), SingleInstance->GetPlayRate());
                Motion->SetBoolField(TEXT("animation_playing"), SingleInstance->IsPlaying());
            }
        }
        JSON->SetObjectField(TEXT("motion"), Motion);
    }
    FString Text;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
    if (!FJsonSerializer::Serialize(JSON, Writer)) return false;
    const FTCHARToUTF8 UTF8(*Text);
    TArray64<uint8> Bytes;
    Bytes.Append(reinterpret_cast<const uint8*>(UTF8.Get()), UTF8.Length());
    return AtomicWrite(TEXT("runtime.json"), Bytes);
}

bool AMirmirFrameExporter::AtomicWrite(const FString& Basename, const TArray64<uint8>& Bytes) const
{
    const FString Final = FPaths::Combine(OutputDirectory, Basename);
    const FString Temporary = Final + TEXT(".tmp");
    if (!FFileHelper::SaveArrayToFile(Bytes, *Temporary))
    {
        IFileManager::Get().Delete(*Temporary, false, true, true);
        return false;
    }
#if PLATFORM_LINUX
    // Same-directory POSIX rename atomically replaces an existing destination.
    // Generic Move implementations may delete the old file first; avoid that.
    const FTCHARToUTF8 From(*Temporary), To(*Final);
    if (::rename(From.Get(), To.Get()) == 0) return true;
#elif PLATFORM_WINDOWS
    // The reader opens with delete sharing. Replace on the same volume; never
    // delete the visible frame first or let a failed replace publish a hole.
    if (::MoveFileExW(*Temporary, *Final, MOVEFILE_REPLACE_EXISTING)) return true;
#endif
    IFileManager::Get().Delete(*Temporary, false, true, true);
    return false;
}

bool AMirmirFrameExporter::AcquireOutputLock()
{
#if PLATFORM_LINUX
    const FTCHARToUTF8 Path(*FPaths::Combine(OutputDirectory, TEXT("desktop-export.lock")));
    FileLockDescriptor = ::open(Path.Get(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (FileLockDescriptor >= 0 && ::flock(FileLockDescriptor, LOCK_EX | LOCK_NB) == 0)
        return true;
    ReleaseOutputLock();
#elif PLATFORM_WINDOWS
    const FString Path = FPaths::Combine(OutputDirectory, TEXT("desktop-export.lock"));
    // A non-inheritable zero-share handle gives this output directory exactly
    // one producer, with automatic release on process exit/crash.
    HANDLE Handle = ::CreateFileW(*Path, GENERIC_READ | GENERIC_WRITE, 0,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (Handle != INVALID_HANDLE_VALUE)
    {
        WindowsLockHandle = Handle;
        return true;
    }
#endif
    return false;
}

void AMirmirFrameExporter::ReleaseOutputLock()
{
#if PLATFORM_LINUX
    if (FileLockDescriptor >= 0)
    {
        ::flock(FileLockDescriptor, LOCK_UN);
        ::close(FileLockDescriptor);
        FileLockDescriptor = -1;
    }
#elif PLATFORM_WINDOWS
    if (WindowsLockHandle)
    {
        ::CloseHandle(WindowsLockHandle);
        WindowsLockHandle = nullptr;
    }
#endif
    // Keep the one fixed lock inode. Removing it could let two processes lock
    // different inodes at the same path. The OS releases flock after a crash.
}

void AMirmirFrameExporter::ReportFailure(const FString& Message)
{
    if (Message != LastWarning && WarningCount < 8)
    {
        UE_LOG(LogMirmirFrameExporter, Warning, TEXT("%s"), *Message);
        LastWarning = Message;
        ++WarningCount;
    }
    if (bInitialized && bOwnsOutput && FPlatformTime::Seconds() >= NextFailureStatusSeconds)
    {
        NextFailureStatusSeconds = FPlatformTime::Seconds() + 1.0;
        PublishRuntime(TEXT("waiting_for_valid_frame"), false, 0, 0, 0, 0, 0, Message);
    }
}

void AMirmirFrameExporter::ShutdownExporter()
{
    SetActorTickEnabled(false);
    bInitialized = false;
    if (bOwnsOutput)
    {
        PublishRuntime(TEXT("stopped"), false);
        IFileManager::Get().Delete(*FPaths::Combine(OutputDirectory, TEXT("frame.png")), false, true, true);
        IFileManager::Get().Delete(*FPaths::Combine(OutputDirectory, TEXT("frame.png.tmp")), false, true, true);
        IFileManager::Get().Delete(*FPaths::Combine(OutputDirectory, TEXT("runtime.json.tmp")), false, true, true);
        bOwnsOutput = false;
    }
    CaptureComponent->TextureTarget = nullptr;
    for (USceneCaptureComponent2D* Station : StationCaptures) Station->TextureTarget = nullptr;
    for (UTextureRenderTarget2D* Target : StationTargets) if (Target) Target->ReleaseResource();
    StationTargets.Reset();
    CatDepthCapture->TextureTarget = nullptr; WorldDepthCapture->TextureTarget = nullptr;
    if (CatDepthTarget) CatDepthTarget->ReleaseResource();
    if (WorldDepthTarget) WorldDepthTarget->ReleaseResource();
    CatDepthTarget = nullptr; WorldDepthTarget = nullptr;
    if (RenderTarget) RenderTarget->ReleaseResource();
    RenderTarget = nullptr;
    Subject.Reset();
    SourceCamera.Reset();
    ImageWrapper = nullptr;
    ReleaseOutputLock();
}

void AMirmirFrameExporter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ShutdownExporter();
    Super::EndPlay(EndPlayReason);
}
