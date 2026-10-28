#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MirmirFrameExporter.generated.h"

class AMirmirCat;
class UCameraComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class IImageWrapperModule;
class FJsonObject;

struct FMirmirStationFrame
{
    FName Id;
    int32 Size = 512;
    bool bReady = false;
    FVector CameraShift = FVector::ZeroVector;
    uint64 Sequence = 0;
    int64 PNGBytes = 0;
    uint32 PNGCRC32 = 0;
    uint64 LayoutRevision = MAX_uint64;
    bool bFoodVisible = true;
};

/**
 * Opt-in bridge from Unreal's isolated cat render to the native desktop shell.
 * Spawn explicitly, then Initialize(Cat, Camera); both operations are inert
 * without -MirmirDesktop. This never reads the desktop or another application's
 * pixels. Synchronous GPU readback/PNG encoding uses at most 30 fps while
 * moving/turning or reacting, then 8 fps while idle. No frame queue is kept.
 *
 * Integration: add ImageWrapper, ImageCore, RenderCore and RHI to the module.
 * Output: $XDG_STATE_HOME/mirmir/unreal/{frame.png,runtime.json}; XDG fallback
 * is ~/.local/state. Consumers must check running/ready, session_id/frame_seq,
 * and png_crc32: the two fixed files cannot be renamed as one transaction.
 *
 * This source does not establish Linux/Unreal render or alpha correctness;
 * verify a checkerboard, edge colors, orientation and stride in the real build.
 */
UCLASS(NotBlueprintable, Transient)
class MIRMIR_API AMirmirFrameExporter : public AActor
{
    GENERATED_BODY()

public:
    AMirmirFrameExporter();

    UFUNCTION(BlueprintCallable, Category="Mirmir|Desktop")
    bool Initialize(AMirmirCat* InCat, UCameraComponent* InCamera);

    UFUNCTION(BlueprintCallable, Category="Mirmir|Desktop")
    void ShutdownExporter();

    UPROPERTY(EditAnywhere, Category="Mirmir|Desktop", meta=(ClampMin="128", ClampMax="1024"))
    int32 FrameSize = 512;

    UPROPERTY(EditAnywhere, Category="Mirmir|Desktop", meta=(ClampMin="1.0", ClampMax="30.0"))
    float FramesPerSecond = 30.0f;

    UPROPERTY(EditAnywhere, Category="Mirmir|Desktop", meta=(ClampMin="1.0", ClampMax="30.0"))
    float IdleFramesPerSecond = 8.0f;

    // Remain responsive through brief stops and animation crossfades rather
    // than switching engine cadence on each zero-speed sample.
    UPROPERTY(EditAnywhere, Category="Mirmir|Desktop", meta=(ClampMin="0.1", ClampMax="5.0"))
    float ActiveGraceSeconds = 1.0f;

    // Physical image pixels per world centimeter; the Qt shell divides by DPR.
    UPROPERTY(EditAnywhere, Category="Mirmir|Desktop", meta=(ClampMin="0.01", ClampMax="20.0"))
    float DesktopPixelsPerCentimeter = 2.5f;
    // Match image projection so planted feet and shell movement use one scale.
    UPROPERTY(EditAnywhere, Category="Mirmir|Desktop")
    bool bMatchCameraPixelScale = true;

    // Exposure is applied to float HDR before the filmic mapping and the one
    // final sRGB conversion. White coats retain highlights rather than clipping
    // in an intermediate U8 readback.
    UPROPERTY(EditAnywhere, Category="Mirmir|Desktop", meta=(ClampMin="0.1", ClampMax="4.0"))
    float DisplayGain = 1.0f;

    UPROPERTY(EditAnywhere, Category="Mirmir|Desktop|Calibration")
    bool bUseFilmicToneMap = true;

    // SCS_SceneColorHDR carries inverse opacity. Disable only after checking
    // actual capture semantics in the installed engine, not to mask a failure.
    UPROPERTY(EditAnywhere, Category="Mirmir|Desktop|Calibration")
    bool bInvertCaptureAlpha = true;

    // Keep false until a checkerboard review establishes premultiplied RGB.
    UPROPERTY(EditAnywhere, Category="Mirmir|Desktop|Calibration")
    bool bUnpremultiplyCapturedRGB = false;

    UPROPERTY(EditAnywhere, Category="Mirmir|Desktop|Calibration")
    bool bFlipVertically = false;

    // Suppress blank captures and opaque canvases instead of showing a box.
    UPROPERTY(EditAnywhere, Category="Mirmir|Desktop|Calibration")
    bool bRequireMixedAlpha = true;

    virtual void Tick(float DeltaSeconds) override;

protected:
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UPROPERTY(VisibleAnywhere, Category="Mirmir|Desktop")
    TObjectPtr<USceneCaptureComponent2D> CaptureComponent;

    UPROPERTY(Transient)
    TObjectPtr<UTextureRenderTarget2D> RenderTarget;

    UPROPERTY() TArray<TObjectPtr<USceneCaptureComponent2D>> StationCaptures;
    UPROPERTY(Transient) TArray<TObjectPtr<UTextureRenderTarget2D>> StationTargets;
    FMirmirStationFrame StationFrames[2];
    UPROPERTY() TObjectPtr<USceneCaptureComponent2D> CatDepthCapture;
    UPROPERTY() TObjectPtr<USceneCaptureComponent2D> WorldDepthCapture;
    UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> CatDepthTarget;
    UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> WorldDepthTarget;
    TArray<uint8> CatSupportPixels;
    int64 InputPNGBytes = 0;
    uint32 InputPNGCRC32 = 0;
    int32 VisibleCatPixels = 0;
    int32 OccludedCatPixels = 0;
    bool bPropsReady = false;
    FString PlacementRequestId;
    FString PlacementItemId;
    FString PlacementMessage;
    bool bPlacementAccepted = false;

    UPROPERTY(Transient)
    TWeakObjectPtr<AMirmirCat> Subject;

    UPROPERTY(Transient)
    TWeakObjectPtr<UCameraComponent> SourceCamera;

    IImageWrapperModule* ImageWrapper = nullptr;
    FString OutputDirectory;
    FString SessionId;
    FString LastWarning;
    FVector InitialCatLocation = FVector::ZeroVector;
    FVector CameraOffset = FVector::ZeroVector;
    FRotator CameraRotation = FRotator::ZeroRotator;
    uint64 FrameSequence = 0;
    double NextCaptureSeconds = 0.0;
    double ActiveUntilSeconds = 0.0;
    double NextFailureStatusSeconds = 0.0;
    FVector LastActorLocation = FVector::ZeroVector;
    FQuat LastActorRotation = FQuat::Identity;
    float CurrentCaptureFPS = 30.0f;
    float LastBond = 0.0f;
    float LastSatiety = 0.0f;
    float LastHeadYaw = 0.0f;
    float LastHeadPitch = 0.0f;
    uint8 LastActivity = 0;
    bool bLastMuted = false;
    FString RenderProfile = TEXT("warm");
    int32 FileLockDescriptor = -1;
    void* WindowsLockHandle = nullptr;
    int32 WarningCount = 0;
    float LastHDRPeak = 0.0f;
    int32 LastHDRHighlightPixels = 0;
    FName LastAppearance = NAME_None;
    bool bInitialized = false;
    bool bOwnsOutput = false;
    double LastCursorSampleMilliseconds = -1.0;

    void PollCursorInterest();
    void PollPlacement();
    void PollDesktopBounds();
    void CapturePropsIfNeeded();
    TSharedRef<FJsonObject> BuildPropsState(bool bReady) const;
    bool CaptureVisibleInput();
    bool CaptureAndPublish();
    void UpdateCadence(double Now, float DeltaSeconds);
    bool PublishRuntime(const TCHAR* Status, bool bReady, int64 PNGBytes = 0,
        uint32 PNGCRC32 = 0, int32 AlphaMin = 0, int32 AlphaMax = 0,
        int32 ForegroundPixels = 0, const FString& Error = FString());
    bool AtomicWrite(const FString& Basename, const TArray64<uint8>& Bytes) const;
    bool AcquireOutputLock();
    void ReleaseOutputLock();
    void ReportFailure(const FString& Message);
};
