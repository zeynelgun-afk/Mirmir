#include "MirmirGameMode.h"
#include "MirmirCat.h"
#include "MirmirToy.h"
#include "MirmirHabitat.h"
#include "MirmirFrameExporter.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/LightComponent.h"
#include "Engine/Canvas.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Engine/TextureCube.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
AMirmirCat* GetCat(const UObject* Context)
{
    const AMirmirGameMode* Mode = Cast<AMirmirGameMode>(UGameplayStatics::GetGameMode(Context));
    return Mode ? Mode->Cat.Get() : nullptr;
}
}

AMirmirGameMode::AMirmirGameMode()
{
    DefaultPawnClass = nullptr;
    PlayerControllerClass = AMirmirPlayerController::StaticClass();
    HUDClass = AMirmirHUD::StaticClass();
    CatClass = AMirmirCat::StaticClass();
    static ConstructorHelpers::FObjectFinder<UTextureCube> Ambient(
        TEXT("/Engine/EngineResources/GrayLightTextureCube.GrayLightTextureCube"));
    if (Ambient.Succeeded()) AmbientCubemap = Ambient.Object;
}

void AMirmirGameMode::BeginPlay()
{
    Super::BeginPlay();
    Cat = GetWorld()->SpawnActor<AMirmirCat>(CatClass, FVector::ZeroVector, FRotator::ZeroRotator);
    ACameraActor* Camera = GetWorld()->SpawnActor<ACameraActor>();
    const bool bDesktop = FParse::Param(FCommandLine::Get(), TEXT("MirmirDesktop"));
    if (Camera)
    {
        const FBox Bounds = Cat ? Cat->GetComponentsBoundingBox() : FBox(ForceInit);
        const FVector Center = Bounds.IsValid ? Bounds.GetCenter() : FVector(0, 0, 35);
        Camera->SetActorLocation(Center + FVector(210, -250, 105));
        Camera->SetActorRotation((Center - Camera->GetActorLocation()).Rotation());
        if (bDesktop)
        {
            Camera->GetCameraComponent()->ProjectionMode = ECameraProjectionMode::Orthographic;
            Camera->GetCameraComponent()->OrthoWidth = Bounds.IsValid
                ? FMath::Max(100.0f, static_cast<float>(Bounds.GetSize().GetMax()) * 1.5f) : 120.0f;
        }
        if (APlayerController* Player = UGameplayStatics::GetPlayerController(this, 0))
            Player->SetViewTarget(Camera);
    }
    if (ADirectionalLight* Key = GetWorld()->SpawnActor<ADirectionalLight>(FVector(0, 0, 300), FRotator(-45, -35, 0)))
        Key->GetLightComponent()->SetIntensity(4.0f);
    if (ADirectionalLight* Fill = GetWorld()->SpawnActor<ADirectionalLight>(FVector(0, 0, 300), FRotator(-25, 140, 0)))
    {
        Fill->GetLightComponent()->SetIntensity(1.3f);
        Fill->GetLightComponent()->SetLightColor(FLinearColor(0.8f, 0.87f, 1.0f));
    }
    if (AmbientCubemap)
        if (ASkyLight* Ambient = GetWorld()->SpawnActor<ASkyLight>())
        {
            // A neutral environment lights the eyes and underside of white
            // fur. Only cat/toy actors enter the RGBA capture; no sky backdrop
            // or user's desktop is read or painted into the image.
            USkyLightComponent* Light = Ambient->GetLightComponent();
            Light->SetMobility(EComponentMobility::Movable);
            Light->SourceType = SLS_SpecifiedCubemap;
            Light->bLowerHemisphereIsBlack = false;
            Light->SetIntensity(FMath::Clamp(AmbientIntensity, 0.0f, 2.0f));
            Light->SetCubemap(AmbientCubemap);
        }
    if (Cat)
    {
        Habitat = GetWorld()->SpawnActor<AMirmirHabitat>(Cat->GetActorLocation(), FRotator::ZeroRotator);
        Cat->Habitat = Habitat;
        Toy = GetWorld()->SpawnActor<AMirmirToy>();
        if (Toy)
        {
            Toy->BallMaterial = ToyMaterial;
            Cat->Toy = Toy;
        }
    }
    if (bDesktop && Cat && Camera)
        if (AMirmirFrameExporter* Exporter = GetWorld()->SpawnActor<AMirmirFrameExporter>())
            Exporter->Initialize(Cat, Camera->GetCameraComponent());
}

void AMirmirPlayerController::BeginPlay()
{
    Super::BeginPlay();
    bShowMouseCursor = true;
    bEnableClickEvents = true;
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Mode);
}

void AMirmirPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::P, IE_Pressed, this, &AMirmirPlayerController::PetCat);
    InputComponent->BindKey(EKeys::F, IE_Pressed, this, &AMirmirPlayerController::FeedCat);
    InputComponent->BindKey(EKeys::B, IE_Pressed, this, &AMirmirPlayerController::PlayWithCat);
    InputComponent->BindKey(EKeys::M, IE_Pressed, this, &AMirmirPlayerController::MuteCat);
    InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AMirmirPlayerController::QuitCompanion);
}

void AMirmirPlayerController::PetCat() { if (AMirmirCat* Cat = GetCat(this)) Cat->Pet(); }
void AMirmirPlayerController::FeedCat() { if (AMirmirCat* Cat = GetCat(this)) Cat->Feed(); }
void AMirmirPlayerController::PlayWithCat() { if (AMirmirCat* Cat = GetCat(this)) Cat->Play(); }
void AMirmirPlayerController::MuteCat() { if (AMirmirCat* Cat = GetCat(this)) Cat->ToggleMute(); }
void AMirmirPlayerController::QuitCompanion()
{
    if (AMirmirCat* Cat = GetCat(this)) Cat->SaveCat();
    UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
}

void AMirmirHUD::DrawHUD()
{
    Super::DrawHUD();
    DrawText(TEXT("Mirmir source preview | P: Pet   F: Feed   B: Play   M: Mute   Esc: Quit"),
        FLinearColor::White, 20, 20, nullptr, 1.0f);
    if (const AMirmirCat* Cat = GetCat(this))
    {
        DrawText(FString::Printf(TEXT("Satiety %.0f%%   Energy %.0f%%   Bond %.0f%%"),
            Cat->Satiety * 100, Cat->Energy * 100, Cat->Bond * 100),
            FLinearColor::White, 20, 45, nullptr, 1.0f);
        DrawText(Cat->PersistenceStatus, FLinearColor::White, 20, 70, nullptr, 1.0f);
    }
}
