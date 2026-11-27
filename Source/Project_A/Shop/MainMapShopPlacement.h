#pragma once

#include "CoreMinimal.h"
#include "UnitPlacement.h"
#include "MainMapShopPlacement.generated.h"

class AAutoBattleGameMode;
class AAutoBattlePlayerController;
class AItemManager;
class ARosterManager;

UCLASS(Transient)
class UMainMapShopPlacement : public UObject, public IUnitPlacement
{
    GENERATED_BODY()

public:
    bool Initialize(AAutoBattleGameMode& InMode, ARosterManager& Roster, AItemManager& InItems);
    FGuid FindUnitId(const ABattleUnitBase* Actor) const;
    bool CanRequestCombatStart(const AAutoBattlePlayerController& Player) const;
    void RequestCombatStart(AAutoBattlePlayerController& Player) const;
    TArray<FName> GetActorEquipment(FGuid Id) const { return EquipmentByUnit.FindRef(Id); }
    const UDataTable* GetUnitDefinitions() const { return UnitDefinitions; }

    virtual FUnitPosition GetUnitPosition_Implementation(FGuid Id) const override;
    virtual FGuid GetOccupant_Implementation(FUnitPosition Position) const override;
    virtual bool GetUnitEquipment_Implementation(FGuid Id, TArray<FName>& Equipment) const override;
    virtual bool GetUnitProfile_Implementation(FGuid Id, FRosterUnitProfile& Profile) const override;
    virtual FGuid GetHoveredUnit_Implementation() const override;
    virtual ABattleUnitBase* GetBattleUnit_Implementation(FGuid Id) const override;
    virtual bool ApplyUnitChanges_Implementation(const TArray<FRosterUnitChange>& Changes, EShopPhase Phase, int32 Limit) override;

private:
    UPROPERTY() TObjectPtr<AAutoBattleGameMode> Mode;
    UPROPERTY() TObjectPtr<AItemManager> Items;
    UPROPERTY() TObjectPtr<const UDataTable> UnitDefinitions;
    UPROPERTY() TSubclassOf<ABattleUnitBase> UnitClass;
    UPROPERTY() TMap<FGuid, TObjectPtr<ABattleUnitBase>> Units;
    TMap<FGuid, TArray<FName>> EquipmentByUnit;
};
