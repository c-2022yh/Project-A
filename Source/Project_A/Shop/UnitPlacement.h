#pragma once

#include "CoreMinimal.h"
#include "ShopTypes.h"
#include "UObject/Interface.h"
#include "UnitPlacement.generated.h"

// Implemented by the integration layer using the board and combat teams' APIs.
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
    FUnitPosition FindRewardTile(int32 DeploymentLimit) const;

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Shop|Placement")
    bool GetUnitProfile(FGuid Id, FRosterUnitProfile& Profile) const;

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Shop|Placement")
    FGuid GetHoveredUnit() const;

    // Apply the whole batch, including combat actor changes, or return false with no changes.
    // The adapter validates occupancy and deployment again at commit time. A move into an
    // occupied position is expressed as two Move edits in the same batch.
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Shop|Placement")
    bool ApplyUnitChanges(const TArray<FRosterUnitChange>& Changes, EShopPhase Phase, int32 DeploymentLimit);
};

