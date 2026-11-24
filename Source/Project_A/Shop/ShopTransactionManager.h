#pragma once

#include "GameFramework/Actor.h"
#include "RosterManager.h"
#include "ShopTypes.h"
#include "ShopTransactionManager.generated.h"

class AItemManager;
class AShopManager;

UCLASS(Blueprintable)
class PROJECT_A_API AShopTransactionManager : public AActor
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Transactions", meta=(ExposeOnSpawn=true))
    TObjectPtr<AShopManager> Shop;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Transactions", meta=(ExposeOnSpawn=true))
    TObjectPtr<ARosterManager> Roster;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Transactions", meta=(ExposeOnSpawn=true))
    TObjectPtr<AItemManager> Items;

    UPROPERTY(BlueprintAssignable, Category="Transactions")
    FManagerChanged OnRewardReceived;

    UFUNCTION(BlueprintCallable, Category="Transactions")
    bool EnterPreparation();

    UFUNCTION(BlueprintCallable, Category="Transactions")
    ETransactionResult GrantStartingUnit(FName UnitId);

    UFUNCTION(BlueprintCallable, Category="Transactions")
    bool OpenRewards();

    UFUNCTION(BlueprintCallable, Category="Transactions")
    bool GrantKillReward(FGuid DefeatedEnemyId);

    UFUNCTION(BlueprintCallable, Category="Transactions")
    ETransactionResult RequestBuy(int32 Slot, int32 OfferRevision);

    UFUNCTION(BlueprintCallable, Category="Transactions")
    ETransactionResult RequestSell(FGuid UnitId);

    UFUNCTION(BlueprintCallable, Category="Transactions")
    ETransactionResult RequestReward(int32 Slot, int32 SelectionRevision);

    UFUNCTION(BlueprintCallable, Category="Transactions")
    ETransactionResult RequestReroll();

    UFUNCTION(BlueprintCallable, Category="Transactions")
    ETransactionResult RequestShopLock(bool bLocked);

    UFUNCTION(BlueprintCallable, Category="Transactions")
    bool GrantRoundGold(int32 Amount);

    UFUNCTION(BlueprintCallable, Category="Transactions")
    ETransactionResult RequestEquip(FGuid UnitId, FName ItemId);

    UFUNCTION(BlueprintCallable, Category="Transactions")
    ETransactionResult RequestUnequip(FGuid UnitId, int32 Slot);

    UFUNCTION(BlueprintPure, Category="Transactions")
    bool IsConfigured() const;
    bool ReturnEquipmentFromBench(FGuid UnitId);

    UFUNCTION(BlueprintPure, Category="Transactions")
    FShopMatchState GetMatchState() const;

private:
    bool CanStartRequest() const;
    ETransactionResult AddUnit(const FShopOffer& Offer, int32 Stars, const FShopMatchState& Match);
    ETransactionResult ChangeEquipment(FGuid UnitId, FName ItemId, TOptional<int32> UnequipSlot);
    bool ApplyMerges(TArray<FRosterUnitChange>& Changes, const TArray<ARosterManager::FMerge>& Merges,
        const FShopMatchState& Match);
    bool CommitChanges(const TArray<FRosterUnitChange>& Changes, TArray<FName>&& Inventory,
        const FShopMatchState& Match);
};
