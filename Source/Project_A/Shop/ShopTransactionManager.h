#pragma once

#include "GameFramework/Actor.h"
#include "ItemManager.h"
#include "ShopTypes.h"
#include "ShopTransactionManager.generated.h"

class ARosterManager;
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
    bool EnterPreparation(int32 Round, int32 DeploymentLimit);

    UFUNCTION(BlueprintCallable, Category="Transactions")
    ETransactionResult GrantStartingUnit(FName UnitId);

    UFUNCTION(BlueprintCallable, Category="Transactions")
    bool BeginRewardSelection(int32 NextDeploymentLimit);

    UFUNCTION(BlueprintCallable, Category="Transactions")
    bool GrantKillReward(FGuid DefeatedEnemyId);

    UFUNCTION(BlueprintCallable, Category="Transactions")
    ETransactionResult RequestBuy(int32 Slot, int32 OfferRevision);

    UFUNCTION(BlueprintCallable, Category="Transactions")
    ETransactionResult RequestSell(FGuid UnitId);

    UFUNCTION(BlueprintCallable, Category="Transactions")
    ETransactionResult RequestReward(int32 Slot, int32 SelectionRevision);

    UFUNCTION(BlueprintCallable, Category="Transactions")
    ETransactionResult RequestMove(FGuid UnitId, FUnitPosition Destination);

    UFUNCTION(BlueprintCallable, Category="Transactions")
    ETransactionResult RequestMoveToBench(FGuid UnitId, int32 Slot);

    UFUNCTION(BlueprintCallable, Category="Transactions")
    ETransactionResult RequestEquip(FGuid UnitId, FName ItemId);

    UFUNCTION(BlueprintCallable, Category="Transactions")
    ETransactionResult RequestUnequip(FGuid UnitId, int32 Slot);

    UFUNCTION(BlueprintPure, Category="Transactions")
    bool IsConfigured() const;

private:
    ETransactionResult AddUnit(const FShopOffer& Offer, int32 Stars, bool bReward);
    ETransactionResult ChangeEquipment(FGuid UnitId, FName ItemId, TOptional<int32> UnequipSlot);
    void PlanMerge(FName UnitId, const FRosterUnit* Incoming, FUnitPosition Destination,
        TArray<FRosterUnitChange>& Changes, AItemManager::FState& ItemState) const;
    bool ApplyChanges(TArray<FRosterUnitChange>& Changes, AItemManager::FState&& ItemState, EShopPhase Phase, int32 Limit);
};
