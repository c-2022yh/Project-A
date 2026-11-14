#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShopTypes.h"
#include "UnitPlacement.h"
#include "RosterManager.generated.h"

class AShopTransactionManager;
class UBenchManager;

UCLASS(Blueprintable)
class PROJECT_A_API ARosterManager : public AActor
{
    GENERATED_BODY()

public:
    ARosterManager();

    UFUNCTION(BlueprintPure, Category="Roster")
    UBenchManager* GetBench() const { return Bench; }

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Roster", meta=(ExposeOnSpawn=true))
    TScriptInterface<IUnitPlacement> Placement;

    UPROPERTY(EditDefaultsOnly, Category="Roster", meta=(ClampMin="1"))
    float TwoStarStatMultiplier = 1.8f;

    UPROPERTY(EditDefaultsOnly, Category="Roster", meta=(ClampMin="1"))
    float TwoStarVisualScale = 1.2f;

    UPROPERTY(BlueprintAssignable, Category="Roster")
    FManagerChanged OnRosterChanged;

    UFUNCTION(BlueprintPure, Category="Roster")
    TArray<FRosterUnit> GetUnits() const { return Units; }

    UFUNCTION(BlueprintPure, Category="Roster")
    bool GetUnit(FGuid Id, FRosterUnit& Unit) const;

    UFUNCTION(BlueprintPure, Category="Roster")
    FUnitPosition GetPosition(FGuid Id) const;

    UFUNCTION(BlueprintPure, Category="Roster")
    FGuid FindOccupant(FUnitPosition Position) const;

    UFUNCTION(BlueprintPure, Category="Roster")
    bool GetProfile(FGuid Id, FRosterUnitProfile& Profile) const;

    UFUNCTION(BlueprintPure, Category="Roster")
    FGuid GetHoveredUnit() const;

private:
    friend class AShopTransactionManager;

    UPROPERTY(VisibleAnywhere, Category="Roster")
    TObjectPtr<UBenchManager> Bench;

    TArray<FRosterUnit> Units;

    static bool IsPositionValid(const FUnitPosition& Position);
    const FRosterUnit* FindUnit(FGuid Id) const;

    FRosterUnitChange MakeChange(ERosterUnitEdit Edit, const FRosterUnit& Unit, FUnitPosition Destination = {}) const;
    bool Apply(const TArray<FRosterUnitChange>& Changes, EShopPhase Phase, int32 Limit) const;
    void Commit(const TArray<FRosterUnitChange>& Changes);
    TArray<const FRosterUnit*, TInlineAllocator<3>> FindMergeMaterials(FName UnitId, int32 Count,
        const TArray<FRosterUnitChange>& PendingChanges) const;
    void NotifyChanged();
};
