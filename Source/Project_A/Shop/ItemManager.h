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
    TArray<FName> GetInventory() const { return Inventory; }

    UFUNCTION(BlueprintPure, Category="Items")
    bool GetItem(FName Id, FItemDefinition& Item) const;

private:
    friend class AShopTransactionManager;

    TArray<FName> Inventory;

    void Commit(TArray<FName>&& Proposed) { Inventory = MoveTemp(Proposed); }
    static void PlanReturn(TArray<FName>& Proposed, const TArray<FName>& Equipment);
    static void LimitInheritedEquipment(TArray<FName>& Equipment, TArray<FName>& Returned);
    ETransactionResult PlanEquipmentChange(TArray<FName>& Proposed, TArray<FName>& Equipment,
        FName ItemId, TOptional<int32> UnequipSlot) const;
    bool GrantItem(FName Id);
    void TryGrantKillDrop();
};
