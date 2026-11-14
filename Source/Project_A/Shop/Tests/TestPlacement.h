#pragma once

#include "CoreMinimal.h"
#include "../UnitPlacement.h"
#include "TestPlacement.generated.h"

UCLASS(NotBlueprintable, Transient)
class UTestPlacement : public UObject, public IUnitPlacement
{
    GENERATED_BODY()

public:
    bool bReject = false;
    TFunction<void()> OnApply;
    int32 AvailableBoardTiles = 2;

    virtual FUnitPosition GetUnitPosition_Implementation(FGuid Id) const override;
    virtual FGuid GetOccupant_Implementation(FUnitPosition Position) const override;
    virtual FUnitPosition FindRewardTile_Implementation(int32 DeploymentLimit) const override;
    virtual bool ApplyUnitChanges_Implementation(const TArray<FRosterUnitChange>& Changes, EShopPhase Phase, int32 Limit) override;

private:
    TMap<FGuid, FUnitPosition> Positions;
};
