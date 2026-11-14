#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShopTypes.h"
#include "ItemManager.generated.h"

class AShopTransactionManager;

UCLASS(Blueprintable)
class PROJECT_A_API AItemManager : public AActor
{
    GENERATED_BODY()

public:
    static constexpr int32 EquipmentSlots = 3;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Items")
    TObjectPtr<UDataTable> ItemDefinitions;

    UPROPERTY(EditDefaultsOnly, Category="Items", meta=(ClampMin="0", ClampMax="1"))
    float KillItemChance = 0;

    UFUNCTION(BlueprintPure, Category="Items")
    TArray<FName> GetInventory() const { return State.Inventory; }

    UFUNCTION(BlueprintPure, Category="Items")
    TArray<FName> GetEquipment(FGuid UnitId) const;

    UFUNCTION(BlueprintPure, Category="Items")
    bool GetItem(FName Id, FItemDefinition& Item) const;

private:
    friend class AShopTransactionManager;

    struct FState
    {
        TArray<FName> Inventory;
        TMap<FGuid, TArray<FName>> Equipment;

        void ReturnEquipment(FGuid UnitId);
        void InheritEquipment(const TArray<FGuid>& Materials, FGuid Kept);
    };

    FState State;

    FState Snapshot() const { return State; }
    void Commit(FState&& Proposed) { State = MoveTemp(Proposed); }
    FItemBonuses GetBonuses(const FState& Proposed, FGuid UnitId) const;
    ETransactionResult PlanEquipmentChange(FState& Proposed, FGuid UnitId, FName ItemId, TOptional<int32> UnequipSlot) const;
    bool GrantItem(FName Id);
    void TryGrantKillDrop();
};
