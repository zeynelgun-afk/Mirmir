#include "MirmirFrameExporter.h"

#include "MirmirCat.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Dom/JsonObject.h"
#include "Engine/TextureRenderTarget2D.h"
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
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"

#include <cstdio>

#if PLATFORM_LINUX
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

DEFINE_LOG_CATEGORY_STATIC(LogMirmirFrameExporter, Log, All);

namespace
{
FString StateDirectory()
{
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
#if !PLATFORM_LINUX
    ReportFailure(TEXT("This initial atomic desktop exporter is implemented for Linux only."));
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
    FramesPerSecond = FMath::Clamp(FramesPerSecond, 1.0f, 15.0f);
    Subject = InCat;
    SourceCamera = InCamera;
    InitialCatLocation = InCat->GetActorLocation();
    CameraOffset = InCamera->GetComponentLocation() - InitialCatLocation;
    CameraRotation = InCamera->GetComponentRotation();
    SessionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
    FrameSequence = 0;
    CaptureComponent->ShowOnlyActors.Reset();
    CaptureComponent->ShowOnlyActors.Add(InCat);
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
    bInitialized = true;
    NextCaptureSeconds = FPlatformTime::Seconds();
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
    if (Now < NextCaptureSeconds) return;
    // Do not accumulate a backlog or run catch-up captures after a long frame.
    NextCaptureSeconds = Now + 1.0 / FMath::Clamp(FramesPerSecond, 1.0f, 15.0f);
    CaptureAndPublish();
}

bool AMirmirFrameExporter::CaptureAndPublish()
{
    CaptureComponent->SetWorldLocationAndRotation(
        Subject->GetActorLocation() + CameraOffset, CameraRotation);
    // CaptureScene is manual; enabling capture-every-frame would double render.
    // https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/USceneCaptureComponent2D/CaptureScene
    CaptureComponent->CaptureScene();
    FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource();
    TArray<FColor> Pixels;
    FReadSurfaceDataFlags ReadFlags(RCM_MinMax);
    ReadFlags.SetLinearToGamma(true);
    // HDR float -> sRGB FColor, exactly once. Alpha remains linear coverage.
    // https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/FRenderTarget/ReadPixels
    if (!Resource || !Resource->ReadPixels(Pixels, ReadFlags) || Pixels.Num() != FrameSize * FrameSize)
    {
        ReportFailure(TEXT("Unreal cat render-target readback failed."));
        return false;
    }
    int32 AlphaMin = 255, AlphaMax = 0, ForegroundPixels = 0;
    const float Gain = FMath::Clamp(DisplayGain, 0.1f, 4.0f);
    for (FColor& Pixel : Pixels)
    {
        Pixel.A = bInvertCaptureAlpha ? 255 - Pixel.A : Pixel.A;
        AlphaMin = FMath::Min(AlphaMin, int32(Pixel.A));
        AlphaMax = FMath::Max(AlphaMax, int32(Pixel.A));
        ForegroundPixels += Pixel.A > 0 ? 1 : 0;
        if (Pixel.A == 0)
        {
            Pixel = FColor(0, 0, 0, 0);
            continue;
        }
        if (Gain != 1.0f || bUnpremultiplyCapturedRGB)
        {
            const uint8 Alpha = Pixel.A;
            FLinearColor Linear = FLinearColor::FromSRGBColor(Pixel);
            const float Factor = Gain / (bUnpremultiplyCapturedRGB ? Alpha / 255.0f : 1.0f);
            Linear.R *= Factor; Linear.G *= Factor; Linear.B *= Factor;
            Pixel = Linear.ToFColorSRGB();
            Pixel.A = Alpha;
        }
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
    JSON->SetNumberField(TEXT("frame_seq"), double(FrameSequence));
    JSON->SetNumberField(TEXT("width"), FrameSize);
    JSON->SetNumberField(TEXT("height"), FrameSize);
    JSON->SetNumberField(TEXT("fps_limit"), FMath::Clamp(FramesPerSecond, 1.0f, 15.0f));
    JSON->SetBoolField(TEXT("running"), bInitialized);
    JSON->SetBoolField(TEXT("ready"), bReady);
    JSON->SetStringField(TEXT("status"), Status);
    JSON->SetStringField(TEXT("updated_at_utc"), FDateTime::UtcNow().ToIso8601());
    JSON->SetNumberField(TEXT("png_bytes"), double(PNGBytes));
    JSON->SetNumberField(TEXT("png_crc32"), double(PNGCRC32));
    JSON->SetStringField(TEXT("alpha_mode"), TEXT("straight"));
    JSON->SetStringField(TEXT("color_space"), TEXT("sRGB"));
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
