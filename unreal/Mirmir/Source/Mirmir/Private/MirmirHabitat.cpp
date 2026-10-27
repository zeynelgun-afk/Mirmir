#include "MirmirHabitat.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AMirmirHabitat::AMirmirHabitat()
{
    PrimaryActorTick.bCanEverTick = false;
    HabitatRoot = CreateDefaultSubobject<USceneComponent>(TEXT("HabitatRoot"));
    SetRootComponent(HabitatRoot);
    BedRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BedRoot"));
    BedRoot->SetupAttachment(HabitatRoot);
    BedRoot->SetRelativeLocation(FVector(-60.0, 25.0, 0.0));
    BowlRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BowlRoot"));
    BowlRoot->SetupAttachment(HabitatRoot);
    BowlRoot->SetRelativeLocation(FVector(36.0, -22.0, 0.0));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (Material.Succeeded()) BaseMaterial = Material.Object;
    if (!Sphere.Succeeded() || !Cylinder.Succeeded()) return;

    // World units are centimetres; Engine sphere/cylinder bounds are 100 cm.
    BedFabric.Add(AddShape(TEXT("BedBase"), BedRoot, Sphere.Object,
        FVector(0, 0, 2.0), FVector(.72, .60, .065)));
    BedCushion.Add(AddShape(TEXT("BedCushion"), BedRoot, Sphere.Object,
        FVector(0, 0, 3.2), FVector(.57, .45, .055)));
    for (int32 I = 0; I < 16; ++I)
    {
        const double Angle = I * UE_TWO_PI / 16.0;
        const FVector Position(30.0 * FMath::Cos(Angle), 24.0 * FMath::Sin(Angle), 6.3);
        UStaticMeshComponent* Pillow = AddShape(*FString::Printf(TEXT("BedBolster%d"), I),
            BedRoot, Sphere.Object, Position, FVector(.17, .115, .13));
        Pillow->SetRelativeRotation(FRotator(0.0, FMath::RadiansToDegrees(Angle) + 90.0, 0.0));
        BedFabric.Add(Pillow);
    }
    BowlCeramic.Add(AddShape(TEXT("BowlBase"), BowlRoot, Cylinder.Object,
        FVector(0, 0, 1.0), FVector(.19, .19, .02)));
    for (int32 I = 0; I < 16; ++I)
    {
        const double Angle = I * UE_TWO_PI / 16.0;
        BowlCeramic.Add(AddShape(*FString::Printf(TEXT("BowlRim%d"), I), BowlRoot,
            Sphere.Object, FVector(8.0 * FMath::Cos(Angle), 8.0 * FMath::Sin(Angle), 3.0),
            FVector(.047, .047, .065)));
    }
    FRandomStream Seed(49831);
    for (int32 I = 0; I < 22; ++I)
    {
        const double Angle = I * 2.399963229728653;
        const double Radius = 6.0 * FMath::Sqrt((I + .5) / 22.0);
        Food.Add(AddShape(*FString::Printf(TEXT("Kibble%d"), I), BowlRoot, Sphere.Object,
            FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), 2.8 + Seed.FRandRange(-.25, .25)),
            FVector(.021, .017, .013)));
    }
}

UStaticMeshComponent* AMirmirHabitat::AddShape(FName Name, USceneComponent* Parent,
    UStaticMesh* Mesh, FVector Position, FVector Scale)
{
    UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
    Part->SetupAttachment(Parent);
    Part->SetStaticMesh(Mesh);
    Part->SetRelativeLocation(Position);
    Part->SetRelativeScale3D(Scale);
    Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Part->SetGenerateOverlapEvents(false);
    Part->SetCanEverAffectNavigation(false);
    if (BaseMaterial) Part->SetMaterial(0, BaseMaterial);
    return Part;
}

void AMirmirHabitat::ColorShapes(const TArray<TObjectPtr<UStaticMeshComponent>>& Shapes,
    FLinearColor Color, float Roughness)
{
    if (!BaseMaterial) return;
    UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, this);
    Material->SetVectorParameterValue(TEXT("Color"), Color);
    Material->SetScalarParameterValue(TEXT("Roughness"), Roughness);
    for (UStaticMeshComponent* Shape : Shapes) Shape->SetMaterial(0, Material);
}

void AMirmirHabitat::BeginPlay()
{
    Super::BeginPlay();
    ColorShapes(BedFabric, FLinearColor(.11, .17, .16), .95f);
    ColorShapes(BedCushion, FLinearColor(.42, .34, .25), .95f);
    ColorShapes(BowlCeramic, FLinearColor(.07, .28, .30), .27f);
    ColorShapes(Food, FLinearColor(.20, .075, .025), .86f);
}

FVector AMirmirHabitat::GetBedLocation() const
{
    return BedRoot->GetComponentLocation() + FVector(0, 0, 5.0);
}

FVector AMirmirHabitat::GetBowlLocation() const
{
    return BowlRoot->GetComponentLocation() + FVector(0, 0, 3.0);
}

void AMirmirHabitat::SetFoodVisible(bool bVisible)
{
    bFoodVisible = bVisible;
    for (UStaticMeshComponent* Piece : Food) Piece->SetHiddenInGame(!bVisible);
}
