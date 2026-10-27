#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MirmirFrameExporter.generated.h"

class AMirmirCat;
class UCameraComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class IImageWrapperModule;

/**
 * Opt-in bridge from Unreal's isolated cat render to the native desktop shell.
 * Spawn explicitly, then Initialize(Cat, Camera); both operations are inert
 * without -MirmirDesktop. This never reads the desktop or another application's
 * pixels. Initial synchronous GPU readback/PNG encoding is capped at 15 fps.
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

    UPROPERTY(EditAnywhere, Category="Mirmir|Desktop", meta=(ClampMin="1.0", ClampMax="15.0"))
    float FramesPerSecond = 15.0f;

    // Physical image pixels per world centimeter; the Qt shell divides by DPR.
    UPROPERTY(EditAnywhere, Category="Mirmir|Desktop", meta=(ClampMin="0.01", ClampMax="20.0"))
    float DesktopPixelsPerCentimeter = 2.5f;
    // Match image projection so planted feet and shell movement use one scale.
    UPROPERTY(EditAnywhere, Category="Mirmir|Desktop")
    bool bMatchCameraPixelScale = true;

    // ReadPixels applies linear->sRGB once. This optional gain is applied after
    // that readback; it cannot recover HDR highlights already clipped to U8.
    UPROPERTY(EditAnywhere, Category="Mirmir|Desktop", meta=(ClampMin="0.1", ClampMax="4.0"))
    float DisplayGain = 1.0f;

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
    double NextFailureStatusSeconds = 0.0;
    int32 FileLockDescriptor = -1;
    int32 WarningCount = 0;
    bool bInitialized = false;
    bool bOwnsOutput = false;

    bool CaptureAndPublish();
    bool PublishRuntime(const TCHAR* Status, bool bReady, int64 PNGBytes = 0,
        uint32 PNGCRC32 = 0, int32 AlphaMin = 0, int32 AlphaMax = 0,
        int32 ForegroundPixels = 0, const FString& Error = FString());
    bool AtomicWrite(const FString& Basename, const TArray64<uint8>& Bytes) const;
    bool AcquireOutputLock();
    void ReleaseOutputLock();
    void ReportFailure(const FString& Message);
};
