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

    // 액터를 파괴하기 전에 현재 스킬 기록을 로스터에 보관한다.
    UFUNCTION(BlueprintCallable, Category="Roster")
    bool CacheUnitSkillProgress(FGuid Id);

    UFUNCTION(BlueprintPure, Category="Roster")
    FUnitPosition GetPosition(FGuid Id) const;

    UFUNCTION(BlueprintPure, Category="Roster")
    FGuid FindOccupant(FUnitPosition Position) const;

    UFUNCTION(BlueprintPure, Category="Roster")
    bool GetProfile(FGuid Id, FRosterUnitProfile& Profile) const;

    UFUNCTION(BlueprintPure, Category="Roster")
    FGuid GetHoveredUnit() const;

    UFUNCTION(BlueprintPure, Category="Roster")
    bool GetUnitEquipment(FGuid Id, TArray<FName>& Equipment) const;

private:
    friend class AShopTransactionManager;
    friend class UBenchManager;
    friend class UMainMapShopPlacement;

    bool bUseLiveBench = false;

    struct FMerge
    {
        TArray<FGuid> Materials;
        FGuid Kept;
    };

    UPROPERTY(VisibleAnywhere, Category="Roster")
    TObjectPtr<UBenchManager> Bench;

    UPROPERTY(Transient)
    TArray<FRosterUnit> Units;

    static bool IsPositionValid(const FUnitPosition& Position);
    const FRosterUnit* FindUnit(FGuid Id) const;

    FRosterUnitChange MakeChange(ERosterUnitEdit Edit, const FRosterUnit& Unit, FUnitPosition Destination = {}) const;
    bool Apply(const TArray<FRosterUnitChange>& Changes, EShopPhase Phase, int32 Limit) const;
    void Commit(const TArray<FRosterUnitChange>& Changes);
    ETransactionResult PlanAdmission(const FShopOffer& Offer, int32 Stars, FUnitPosition BenchSlot,
        EShopPhase Phase, TArray<FRosterUnitChange>& Changes, TArray<FMerge>& Merges) const;
    void PlanPendingMerges(TArray<FRosterUnitChange>& Changes, TArray<FMerge>& Merges) const;
    bool PlanMerge(FName UnitId, const FRosterUnit* Incoming, bool bBenchOnly,
        TArray<FRosterUnitChange>& Changes, FMerge& Merge) const;

    void NotifyChanged();
};
