#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BenchTile.generated.h"

class UStaticMeshComponent;

UCLASS()
class PROJECT_A_API ABenchTile : public AActor
{
	GENERATED_BODY()

public:
	ABenchTile();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bench")
	TObjectPtr<UStaticMeshComponent> TileMesh;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Bench")
	int32 SlotIndex = INDEX_NONE;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Bench")
	bool bOccupied = false;

	// The bench owns this link; BattleUnitBase::CurrentTile stays null while a unit is waiting.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Bench")
	TObjectPtr<AActor> OccupiedUnit;

	UFUNCTION(BlueprintPure, Category = "Bench")
	bool CanPlaceUnit() const;

	UFUNCTION(BlueprintPure, Category = "Bench")
	FVector GetSurfaceLocation() const;

	UFUNCTION(BlueprintCallable, Category = "Bench")
	void SetOccupiedUnit(AActor* Unit);

	UFUNCTION(BlueprintCallable, Category = "Bench")
	void ClearTile();
};
