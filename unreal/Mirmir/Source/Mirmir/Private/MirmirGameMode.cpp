#include "MirmirGameMode.h"
#include "MirmirCat.h"
#include "Camera/CameraActor.h"
#include "Components/InputComponent.h"
#include "Engine/Canvas.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

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
}

void AMirmirGameMode::BeginPlay()
{
    Super::BeginPlay();
    Cat = GetWorld()->SpawnActor<AMirmirCat>(CatClass, FVector::ZeroVector, FRotator::ZeroRotator);
    ACameraActor* Camera = GetWorld()->SpawnActor<ACameraActor>();
    if (Camera)
    {
        Camera->SetActorLocation(FVector(260, -260, 130));
        Camera->SetActorRotation((FVector(0, 0, 35) - Camera->GetActorLocation()).Rotation());
        if (APlayerController* Player = UGameplayStatics::GetPlayerController(this, 0))
            Player->SetViewTarget(Camera);
    }
    // Preview light only. No floor, stand-in cat, or claimed desktop overlay.
    GetWorld()->SpawnActor<ADirectionalLight>(FVector(0, 0, 300), FRotator(-45, -35, 0));
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
