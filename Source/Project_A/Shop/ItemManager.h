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

    // 랜덤 보상은 처치 드롭 체크와 무관하게 유효한 DT_Items 전체 행을 사용한다.
    TArray<FName> GetRandomRewardCandidates() const;

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
