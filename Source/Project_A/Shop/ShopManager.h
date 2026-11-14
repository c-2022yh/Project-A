#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShopTypes.h"
#include "ShopManager.generated.h"

class AShopTransactionManager;
class UShopOdds;

UCLASS(Blueprintable)
class PROJECT_A_API AShopManager : public AActor
{
    GENERATED_BODY()

public:
    AShopManager();

    static constexpr int32 ShopSlots = 5;
    static constexpr int32 RerollCost = 2;
    static constexpr int32 RewardSlots = 4;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Shop|Shop")
    TArray<FShopOffer> Catalog;

    UPROPERTY(EditDefaultsOnly, Category="Shop|Shop")
    TObjectPtr<UShopOdds> ShopOdds;

    UPROPERTY(EditDefaultsOnly, Category="Shop|Economy", meta=(ClampMin="0"))
    int32 StartingGold = 0;

    UPROPERTY(EditDefaultsOnly, Category="Shop|Economy", meta=(ClampMin="0"))
    int32 KillGold = 0;

    UPROPERTY(EditDefaultsOnly, Category="Shop|Economy", meta=(ClampMin="0"))
    int32 BonusKillGold = 0;

    UPROPERTY(EditDefaultsOnly, Category="Shop|Economy", meta=(ClampMin="0", ClampMax="1"))
    float BonusKillGoldChance = 0;

    UPROPERTY(EditDefaultsOnly, Category="Shop|Economy", meta=(ClampMin="0"))
    float OneStarRefundRate = 1;

    UPROPERTY(EditDefaultsOnly, Category="Shop|Economy", meta=(ClampMin="0"))
    float TwoStarRefundRate = 1;

    UPROPERTY(EditDefaultsOnly, Category="Shop|Rewards")
    TArray<FRewardOffer> RewardPool;

    UPROPERTY(BlueprintAssignable, Category="Shop")
    FManagerChanged OnShopChanged;

    UFUNCTION(BlueprintCallable, Category="Shop|Progression")
    bool BeginCombat();

    UFUNCTION(BlueprintCallable, Category="Shop|Progression")
    bool BeginSettlement();

    UFUNCTION(BlueprintCallable, Category="Shop|Progression")
    bool FinishMatch();

    UFUNCTION(BlueprintCallable, Category="Shop|Progression")
    bool GrantRoundGold(int32 Amount);

    UFUNCTION(BlueprintCallable, Category="Shop|Shop")
    ETransactionResult RequestReroll();

    UFUNCTION(BlueprintCallable, Category="Shop|Shop")
    ETransactionResult RequestShopLock(bool bLocked);

    UFUNCTION(BlueprintPure, Category="Shop")
    int32 GetGold() const { return Gold; }

    UFUNCTION(BlueprintPure, Category="Shop")
    int32 GetRound() const { return CurrentRound; }

    UFUNCTION(BlueprintPure, Category="Shop")
    int32 GetDeploymentLimit() const { return CurrentDeploymentLimit; }

    UFUNCTION(BlueprintPure, Category="Shop")
    EShopPhase GetPhase() const { return Phase; }

    UFUNCTION(BlueprintPure, Category="Shop")
    bool IsShopLocked() const { return bShopLocked; }

    UFUNCTION(BlueprintPure, Category="Shop")
    int32 GetShopRevision() const { return ShopRevision; }

    UFUNCTION(BlueprintPure, Category="Shop")
    TArray<FShopOffer> GetOffers() const { return Offers; }

    UFUNCTION(BlueprintPure, Category="Shop")
    int32 GetRewardRevision() const { return RewardRevision; }

    UFUNCTION(BlueprintPure, Category="Shop")
    TArray<FRewardOffer> GetRewards() const { return Rewards; }

    UFUNCTION(BlueprintPure, Category="Shop")
    FShopOffer GetOfferDefinition(FName UnitId) const;

private:
    friend class AShopTransactionManager;

    EShopPhase Phase = EShopPhase::Inactive;
    int32 Gold = 0;
    int32 CurrentRound = 0;
    int32 CurrentDeploymentLimit = 0;
    int32 ShopRevision = 0;
    int32 RewardRevision = 0;
    bool bShopLocked = false;
    bool bRewardReceived = false;
    bool bRoundGoldGranted = false;
    bool bChanging = false;

    TArray<FShopOffer> Offers;
    TArray<FRewardOffer> Rewards;
    TSet<FGuid> RewardedKills;

    bool CanTrade() const;
    bool CanManageBoard() const;
    void AddGold(int64 Amount);
    bool GenerateOffers(int32 Round, TArray<FShopOffer>& NewOffers) const;
    bool GenerateRewards(TArray<FRewardOffer>& NewRewards, TFunctionRef<bool(FName)> IsItemValid) const;
    void CommitPurchase(int32 Slot, int32 Cost);
    void CreditSale(const FRosterUnit& Unit);
    bool CreditKill(FGuid DefeatedEnemyId);
    void CommitPreparation(int32 Round, int32 DeploymentLimit, TArray<FShopOffer>&& NextOffers);
};
