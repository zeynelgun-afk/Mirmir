#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MirmirHabitat.generated.h"

class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UBoxComponent;
class UPrimitiveComponent;
class AMirmirCat;

/** Fixed, owned bed and feeding station. Only Engine basic shapes are used. */
UCLASS(NotBlueprintable)
class MIRMIR_API AMirmirHabitat : public AActor
{
    GENERATED_BODY()
public:
    AMirmirHabitat();
    virtual void BeginPlay() override;
    FVector GetBedLocation() const;
    FVector GetBowlLocation() const;
    void SetFoodVisible(bool bVisible);
    bool IsFoodVisible() const { return bFoodVisible; }
    void AttachCat(AMirmirCat* InCat);
    bool SetStationPosition(FName Id, const FVector& FloorPosition, FString& Error);
    FVector GetStationFloorPosition(FName Id) const;
    FBox GetStationBounds(FName Id) const;
    uint64 GetLayoutRevision() const { return LayoutRevision; }
    UPrimitiveComponent* GetBedCollision() const;
    UPrimitiveComponent* GetBowlCollision() const;
    USceneComponent* GetStationRoot(FName Id) const;
    void GetStationComponents(FName Id, TArray<UPrimitiveComponent*>& Out) const;
    const FString& GetLayoutStatus() const { return LayoutStatus; }
    static FString LayoutSavePath();

private:
    UPROPERTY() TObjectPtr<USceneComponent> HabitatRoot;
    UPROPERTY() TObjectPtr<USceneComponent> BedRoot;
    UPROPERTY() TObjectPtr<USceneComponent> BowlRoot;
    UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> BedFabric;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> BedCushion;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> BowlCeramic;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Food;
    bool bFoodVisible = true;
    UPROPERTY() TObjectPtr<UPrimitiveComponent> BedBlocker;
    UPROPERTY() TObjectPtr<UBoxComponent> BowlBlocker;
    UPROPERTY() TArray<TObjectPtr<UBoxComponent>> ToyBarriers;
    TWeakObjectPtr<AMirmirCat> OwnerCat;
    uint64 LayoutRevision = 1;
    bool bLayoutCanSave = true;
    FString LayoutStatus = TEXT("Ready");

    UStaticMeshComponent* AddShape(FName Name, USceneComponent* Parent,
        UStaticMesh* Mesh, FVector Position, FVector Scale);
    void ColorShapes(const TArray<TObjectPtr<UStaticMeshComponent>>& Shapes,
        FLinearColor Color, float Roughness);
    UBoxComponent* AddBarrier(FName Name, USceneComponent* Parent, FVector Position,
        FVector Extent, bool bCatBlocker);
    bool ValidateLayout(FVector Bed, FVector Bowl, FString& Error) const;
    bool SaveLayout(FString& Error) const;
    void LoadLayout();
};
