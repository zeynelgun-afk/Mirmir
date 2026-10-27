#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MirmirHabitat.generated.h"

class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;

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

    UStaticMeshComponent* AddShape(FName Name, USceneComponent* Parent,
        UStaticMesh* Mesh, FVector Position, FVector Scale);
    void ColorShapes(const TArray<TObjectPtr<UStaticMeshComponent>>& Shapes,
        FLinearColor Color, float Roughness);
};
