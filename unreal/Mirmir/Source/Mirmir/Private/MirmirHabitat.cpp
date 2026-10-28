#include "MirmirHabitat.h"
#include "MirmirCat.h"
#include "MirmirSaveGame.h"
#include "Components/BoxComponent.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Components/SphereComponent.h"
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
    BedRoot->SetRelativeLocation(FVector(-90.0, 45.0, 0.0));
    BowlRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BowlRoot"));
    BowlRoot->SetupAttachment(HabitatRoot);
    BowlRoot->SetRelativeLocation(FVector(68.0, -30.0, 0.0));
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
        // Two genuine visual openings along +/-X. A cat can enter west and
        // leave east without a frozen-pose pivot or walking through bolsters.
        if (FMath::Abs(FMath::Cos(Angle)) > .70) continue;
        const FVector Position(30.0 * FMath::Cos(Angle), 24.0 * FMath::Sin(Angle), 6.3);
        UStaticMeshComponent* Pillow = AddShape(*FString::Printf(TEXT("BedBolster%d"), I),
            BedRoot, Sphere.Object, Position, FVector(.17, .115, .13));
        Pillow->SetRelativeRotation(FRotator(0.0, FMath::RadiansToDegrees(Angle) + 90.0, 0.0));
        BedFabric.Add(Pillow);
        UBoxComponent* Barrier = AddBarrier(*FString::Printf(TEXT("BedRimCollision%d"), I),
            BedRoot, Position, FVector(7.5, 4.8, 6.3), false);
        Barrier->SetRelativeRotation(Pillow->GetRelativeRotation());
        ToyBarriers.Add(Barrier);
    }
    USphereComponent* BedSphere = CreateDefaultSubobject<USphereComponent>(TEXT("BedNavigationBlocker"));
    BedSphere->SetupAttachment(BedRoot);
    BedSphere->SetRelativeLocation(FVector(0,0,5));
    BedSphere->SetSphereRadius(40.0f);
    BedSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    BedSphere->SetCollisionObjectType(ECC_WorldStatic);
    BedSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
    BedSphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
    BedSphere->SetHiddenInGame(true);
    BedSphere->SetCanEverAffectNavigation(false);
    BedBlocker = BedSphere;
    // A raised feeding station matches a domestic cat's short neck without
    // forcing an extreme play-bow. The root remains on the floor, so saved XY
    // placement and native-window dragging keep their existing contract.
    BowlBlocker = AddBarrier(TEXT("BowlNavigationBlocker"), BowlRoot, FVector(0, 0, 7.75), FVector(12.5, 12.5, 7.75), true);
    ToyBarriers.Add(AddBarrier(TEXT("BedCushionCollision"), BedRoot, FVector(0, 0, 3), FVector(28, 22, 2.5), false));
    ToyBarriers.Add(AddBarrier(TEXT("BowlToyCollision"), BowlRoot, FVector(0, 0, 7.5), FVector(12, 12, 7.5), false));
    BowlStand.Add(AddShape(TEXT("BowlStandFoot"), BowlRoot, Cylinder.Object,
        FVector(0, 0, 2.0), FVector(.22, .22, .04)));
    BowlStand.Add(AddShape(TEXT("BowlStandStem"), BowlRoot, Cylinder.Object,
        FVector(0, 0, 7.0), FVector(.075, .075, .10)));
    BowlStand.Add(AddShape(TEXT("BowlStandTray"), BowlRoot, Cylinder.Object,
        FVector(0, 0, 11.0), FVector(.205, .205, .02)));
    BowlCeramic.Add(AddShape(TEXT("BowlBase"), BowlRoot, Cylinder.Object,
        FVector(0, 0, 13.0), FVector(.19, .19, .02)));
    for (int32 I = 0; I < 16; ++I)
    {
        const double Angle = I * UE_TWO_PI / 16.0;
        BowlCeramic.Add(AddShape(*FString::Printf(TEXT("BowlRim%d"), I), BowlRoot,
            Sphere.Object, FVector(8.0 * FMath::Cos(Angle), 8.0 * FMath::Sin(Angle), 15.0),
            FVector(.047, .047, .065)));
    }
    FRandomStream Seed(49831);
    for (int32 I = 0; I < 22; ++I)
    {
        const double Angle = I * 2.399963229728653;
        const double Radius = 6.0 * FMath::Sqrt((I + .5) / 22.0);
        Food.Add(AddShape(*FString::Printf(TEXT("Kibble%d"), I), BowlRoot, Sphere.Object,
            FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), 14.8 + Seed.FRandRange(-.25, .25)),
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
    LoadLayout();
    ColorShapes(BedFabric, FLinearColor(.11, .17, .16), .95f);
    ColorShapes(BedCushion, FLinearColor(.42, .34, .25), .95f);
    ColorShapes(BowlStand, FLinearColor(.09, .10, .11), .62f);
    ColorShapes(BowlCeramic, FLinearColor(.07, .28, .30), .27f);
    ColorShapes(Food, FLinearColor(.20, .075, .025), .86f);
}

FVector AMirmirHabitat::GetBedLocation() const
{
    return BedRoot->GetComponentLocation() + FVector(0, 0, 5.0);
}

FVector AMirmirHabitat::GetBowlLocation() const
{
    return BowlRoot->GetComponentLocation() + FVector(0, 0, 15.0);
}

void AMirmirHabitat::SetFoodVisible(bool bVisible)
{
    bFoodVisible = bVisible;
    for (UStaticMeshComponent* Piece : Food) Piece->SetHiddenInGame(!bVisible);
}

UBoxComponent* AMirmirHabitat::AddBarrier(FName Name, USceneComponent* Parent,
    FVector Position, FVector Extent, bool bCatBlocker)
{
    UBoxComponent* Box = CreateDefaultSubobject<UBoxComponent>(Name);
    Box->SetupAttachment(Parent);
    Box->SetRelativeLocation(Position);
    Box->SetBoxExtent(Extent);
    Box->SetMobility(EComponentMobility::Movable);
    Box->SetCollisionEnabled(bCatBlocker ? ECollisionEnabled::QueryOnly : ECollisionEnabled::QueryAndPhysics);
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Ignore);
    Box->SetCollisionResponseToChannel(bCatBlocker ? ECC_WorldDynamic : ECC_PhysicsBody, ECR_Block);
    Box->SetGenerateOverlapEvents(false);
    Box->SetHiddenInGame(true);
    Box->SetCanEverAffectNavigation(false);
    return Box;
}

USceneComponent* AMirmirHabitat::GetStationRoot(FName Id) const
{
    if (Id == TEXT("bed")) return BedRoot;
    if (Id == TEXT("bowl")) return BowlRoot;
    return nullptr;
}

FVector AMirmirHabitat::GetStationFloorPosition(FName Id) const
{
    const USceneComponent* Station = GetStationRoot(Id);
    if (!Station) return FVector::ZeroVector;
    FVector Point = Station->GetComponentLocation();
    Point.Z = 0.0;
    return Point;
}

void AMirmirHabitat::GetStationComponents(FName Id, TArray<UPrimitiveComponent*>& Out) const
{
    Out.Reset();
    const USceneComponent* Station = GetStationRoot(Id);
    if (!Station) return;
    TArray<USceneComponent*> Children;
    Station->GetChildrenComponents(true, Children);
    for (USceneComponent* Child : Children)
        if (UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Child)) Out.Add(Mesh);
}

FBox AMirmirHabitat::GetStationBounds(FName Id) const
{
    FBox Bounds(ForceInit);
    TArray<UPrimitiveComponent*> Parts;
    GetStationComponents(Id, Parts);
    for (const UPrimitiveComponent* Part : Parts) Bounds += Part->Bounds.GetBox();
    return Bounds;
}

UPrimitiveComponent* AMirmirHabitat::GetBedCollision() const { return BedBlocker; }
UPrimitiveComponent* AMirmirHabitat::GetBowlCollision() const { return BowlBlocker; }

FString AMirmirHabitat::LayoutSavePath()
{
    return FPaths::Combine(FPaths::GetPath(UMirmirSaveGame::SavePath()), TEXT("habitat-v1.json"));
}

bool AMirmirHabitat::ValidateLayout(FVector Bed, FVector Bowl, FString& Error) const
{
    auto Valid = [](FVector Point)
    {
        return !Point.ContainsNaN() && FMath::Abs(Point.X) <= 5000.0 &&
            FMath::Abs(Point.Y) <= 5000.0 && FMath::Abs(Point.Z) < .01;
    };
    if (!Valid(Bed) || !Valid(Bowl))
    {
        Error = TEXT("Eşya konumu geçerli masaüstü sınırında olmalı.");
        return false;
    }
    if (FVector::Dist2D(Bed, Bowl) < 63.0)
    {
        Error = TEXT("Yatak ve mama kabı üst üste gelemez.");
        return false;
    }
    const FVector GateA = Bed + FVector(-90, 0, 0), GateB = Bed + FVector(90, 0, 0);
    if (FMath::PointDistToSegment(Bowl, GateA, GateB) < 45.0)
    {
        Error = TEXT("Mama kabı yatağın giriş ve çıkışını kapatmamalı.");
        return false;
    }
    return true;
}

bool AMirmirHabitat::SaveLayout(FString& Error) const
{
    const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("schema_version"), 1);
    Root->SetNumberField(TEXT("revision"), double(LayoutRevision));
    for (FName Id : { FName(TEXT("bed")), FName(TEXT("bowl")) })
    {
        const FVector Floor = GetStationFloorPosition(Id);
        Root->SetArrayField(Id.ToString(), { MakeShared<FJsonValueNumber>(Floor.X), MakeShared<FJsonValueNumber>(Floor.Y) });
    }
    FString Text;
    const FString Path = LayoutSavePath(), Temporary = Path + TEXT(".tmp");
    if (!FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text)) ||
        !IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true) ||
        !FFileHelper::SaveStringToFile(Text, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) ||
        !IFileManager::Get().Move(*Path, *Temporary, true, true))
    {
        Error = TEXT("Eşya konumları kaydedilemedi; önceki yerleşim korundu.");
        return false;
    }
    return true;
}

void AMirmirHabitat::LoadLayout()
{
    const FString Path = LayoutSavePath();
    if (!IFileManager::Get().FileExists(*Path)) return;
    FString Text, Error;
    TSharedPtr<FJsonObject> Root;
    double Schema;
    FVector Bed, Bowl;
    auto ReadPoint = [&](const TCHAR* Name, FVector& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* Point = nullptr;
        double X, Y;
        if (!Root->TryGetArrayField(Name, Point) || Point->Num() != 2 ||
            !(*Point)[0].IsValid() || !(*Point)[1].IsValid() || !(*Point)[0]->TryGetNumber(X) || !(*Point)[1]->TryGetNumber(Y) || !FMath::IsFinite(X) || !FMath::IsFinite(Y)) return false;
        Out = FVector(X, Y, 0);
        return true;
    };
    if (IFileManager::Get().FileSize(*Path) > 4096 || !FFileHelper::LoadFileToString(Text, *Path) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid() ||
        !Root->TryGetNumberField(TEXT("schema_version"), Schema) || Schema != 1.0 ||
        !ReadPoint(TEXT("bed"), Bed) || !ReadPoint(TEXT("bowl"), Bowl) || !ValidateLayout(Bed, Bowl, Error))
    {
        bLayoutCanSave = false;
        LayoutStatus = TEXT("Yerleşim kaydı bozuk; dosya korundu, varsayılan yerleşim kullanılıyor.");
        return;
    }
    BedRoot->SetWorldLocation(Bed);
    BowlRoot->SetWorldLocation(Bowl);
    double Revision = 1;
    if (Root->TryGetNumberField(TEXT("revision"), Revision) && FMath::IsFinite(Revision) && Revision >= 1 && Revision <= 1.0e12)
        LayoutRevision = uint64(Revision);
}

void AMirmirHabitat::AttachCat(AMirmirCat* InCat)
{
    OwnerCat = InCat;
    if (!InCat) return;
    const FVector Home = InCat->GetDesktopOrigin();
    auto Free = [&](FVector Point)
    {
        return FVector::Dist2D(Point, GetStationFloorPosition(TEXT("bed"))) >= 80.0 &&
            FVector::Dist2D(Point, GetStationFloorPosition(TEXT("bowl"))) >= 52.0;
    };
    if (Free(InCat->GetActorLocation())) return;
    // A saved station may cover the old spawn. Resolve before the first frame;
    // the canonical desktop origin remains fixed so furniture does not drift.
    for (int32 Ring = 1; Ring <= 6; ++Ring)
        for (int32 Sample = 0; Sample < 32; ++Sample)
        {
            const double Angle = Sample * UE_TWO_PI / 32.0;
            const FVector Point = Home + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * (Ring * 40.0);
            if (Free(Point))
            {
                InCat->SetActorLocation(Point, false, nullptr, ETeleportType::TeleportPhysics);
                return;
            }
        }
}

bool AMirmirHabitat::SetStationPosition(FName Id, const FVector& FloorPosition, FString& Error)
{
    USceneComponent* Station = GetStationRoot(Id);
    if (!Station) { Error = TEXT("Bilinmeyen eşya."); return false; }
    if (!bLayoutCanSave) { Error = LayoutStatus; return false; }
    FVector Floor = FloorPosition;
    if (Floor.ContainsNaN() || FMath::Abs(Floor.Z) > .01) { Error = TEXT("Geçersiz eşya konumu."); return false; }
    FVector Bed = Id == TEXT("bed") ? Floor : GetStationFloorPosition(TEXT("bed"));
    FVector Bowl = Id == TEXT("bowl") ? Floor : GetStationFloorPosition(TEXT("bowl"));
    if (!ValidateLayout(Bed, Bowl, Error)) return false;
    if (const AMirmirCat* Cat = OwnerCat.Get())
    {
        if (!Cat->IsInsideDesktop(Floor) || (Id == TEXT("bed") &&
            (!Cat->IsInsideDesktop(Floor + FVector(-110,0,0)) || !Cat->IsInsideDesktop(Floor + FVector(110,0,0)))))
        {
            Error = TEXT("Eşyayı Mırmır'ın ulaşabileceği masaüstü alanında bırakmalısın.");
            return false;
        }
        if (Id == TEXT("bed") && Cat->IsBedOccupied())
        {
            Error = TEXT("Mırmır yatağı kullanıyor; çıktıktan sonra taşıyabilirsin.");
            return false;
        }
        const FVector Delta = Cat->GetActorQuat().UnrotateVector(Floor - Cat->GetActorLocation());
        const double DX = FMath::Max(0.0, FMath::Abs(Delta.X) - 23.0);
        const double DY = FMath::Max(0.0, FMath::Abs(Delta.Y) - 12.0);
        const double Radius = Id == TEXT("bed") ? 42.0 : 15.0;
        const float Clearance = Id == TEXT("bed") ? 80.0f : 52.0f;
        if (DX * DX + DY * DY < Radius * Radius || FVector::Dist2D(Floor, Cat->GetActorLocation()) < Clearance)
        {
            Error = TEXT("Eşyayı Mırmır'ın üstüne bırakamazsın.");
            return false;
        }
    }
    const FVector Previous = Station->GetComponentLocation();
    const uint64 PreviousRevision = LayoutRevision;
    Station->SetWorldLocation(Floor);
    ++LayoutRevision;
    if (!SaveLayout(Error))
    {
        Station->SetWorldLocation(Previous);
        LayoutRevision = PreviousRevision;
        return false;
    }
    LayoutStatus = TEXT("Eşya konumları kaydedildi.");
    return true;
}
