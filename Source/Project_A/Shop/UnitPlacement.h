#pragma once

#include "CoreMinimal.h"
#include "ShopTypes.h"
#include "UObject/Interface.h"
#include "UnitPlacement.generated.h"

// Integration contract for the board and combat teams.
//
// Unit API required from combat:
//   GetEquipmentIds() returns the loadout held by the actual unit.
//   ReplaceEquipment(Ids) validates definitions and capacity and recomputes effective
//   stats from base + star modifiers + that loadout. Empty Ids unequips all.
//   Resolve the unit by the stable roster GUID, including after combat actor rebinds.
//   Expose the resulting values through GetUnitProfile. ItemManager.GetItem supplies
//   immutable definitions. It stores only unequipped inventory.
//
// Board input API proposed for the placement team:
//   TryMoveOwnedUnit(UnitId, Destination) reads GameMode permissions/limit and
//   validates source and target, then stages moves for both units in a swap.
//   Read bench availability from BenchManager. Use its ApplyChanges native entry
//   point to commit bench occupancy with the board/actor edits. Board-to-bench
//   requires an empty replacement loadout and a staged inventory return for each
//   recalled unit. The placement coordinator needs an atomic inventory-return
//   hook from the item side. This cross-boundary operation is pending team integration.
//   BenchManager.RequestMove handles moves within the bench independently.
UINTERFACE(BlueprintType, Blueprintable)
class PROJECT_A_API UUnitPlacement : public UInterface
{
    GENERATED_BODY()
};

class PROJECT_A_API IUnitPlacement
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Shop|Placement")
    FUnitPosition GetUnitPosition(FGuid Id) const;

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Shop|Placement")
    FGuid GetOccupant(FUnitPosition Position) const;

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Shop|Placement")
    bool GetUnitEquipment(FGuid Id, TArray<FName>& Equipment) const;

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Shop|Placement")
    bool GetUnitProfile(FGuid Id, FRosterUnitProfile& Profile) const;

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Shop|Placement")
    FGuid GetHoveredUnit() const;

    // Apply the whole batch synchronously, including equipment payloads and combat
    // actor changes, or return false with no externally visible changes. Do not
    // advance the phase or emit final success notifications until the caller commits.
    // If equipment replacement is unsupported, reject the batch.
    // BenchManager owns bench occupancy. The adapter applies actor changes and validates board-side edits.
    // A move into an
    // occupied position is expressed as two Move edits in the same batch.
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Shop|Placement")
    bool ApplyUnitChanges(const TArray<FRosterUnitChange>& Changes, EShopPhase Phase, int32 DeploymentLimit);
};

