#pragma once

#include "GameFramework/Actor.h"
#include "ItemManager.h"
#include "ShopTypes.h"
#include "ShopTransactionManager.generated.h"

class ARosterManager;
class AShopManager;
class ABattleUnitBase;

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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Transactions|Elementalist")
    FName ElementalistUnitId = TEXT("Elementalist");

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
    bool TryMoveRegisteredElementalist(ABattleUnitBase* Unit, FUnitPosition Destination, ETransactionResult& Result);

    UFUNCTION(BlueprintPure, Category="Transactions|Elementalist")
    bool GetPendingElementalPlacement(FGuid& UnitId, FUnitPosition& Destination, EElementalForm& PreviousForm, int32& Revision) const;

    UFUNCTION(BlueprintCallable, Category="Transactions|Elementalist")
    ETransactionResult RequestElementalPlacement(EElementalForm Form, int32 Revision);

    UFUNCTION(BlueprintCallable, Category="Transactions|Elementalist")
    void CancelElementalPlacement();

    UFUNCTION(BlueprintCallable, Category="Transactions")
    ETransactionResult RequestMoveToBench(FGuid UnitId, int32 Slot);

    UFUNCTION(BlueprintCallable, Category="Transactions")
    ETransactionResult RequestEquip(FGuid UnitId, FName ItemId);

    UFUNCTION(BlueprintCallable, Category="Transactions")
    ETransactionResult RequestUnequip(FGuid UnitId, int32 Slot);

    UFUNCTION(BlueprintPure, Category="Transactions")
    bool IsConfigured() const;

private:
    struct FPendingElementalPlacement
    {
        FGuid UnitId;
        FUnitPosition Source;
        FUnitPosition Destination;
        int32 Round = 0;
        int32 Revision = 0;
    };
    TOptional<FPendingElementalPlacement> PendingElementalPlacement;
    int32 ElementalPlacementRevision = 0;
    bool IsElementalist(const FRosterUnit& Unit) const;
    ETransactionResult RequestMoveInternal(FGuid UnitId, FUnitPosition Destination,
        const FPendingElementalPlacement* Confirmed, EElementalForm Form);
    ETransactionResult AddUnit(const FShopOffer& Offer, int32 Stars, bool bReward);
    ETransactionResult ChangeEquipment(FGuid UnitId, FName ItemId, TOptional<int32> UnequipSlot);
    bool PlanMerge(FName UnitId, const FRosterUnit* Incoming, FUnitPosition Destination,
        TArray<FRosterUnitChange>& Changes, AItemManager::FState& ItemState) const;
    bool ApplyChanges(TArray<FRosterUnitChange>& Changes, AItemManager::FState&& ItemState, EShopPhase Phase, int32 Limit);
};
