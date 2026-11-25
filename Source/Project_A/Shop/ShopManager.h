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

    UFUNCTION(BlueprintPure, Category="Shop")
    int32 GetGold() const { return Gold; }

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

    // 통합 DT_Rounds의 확률. 항목이 없는 라운드는 기존 ShopOdds를 사용한다.
    UPROPERTY(Transient)
    TMap<int32, FShopRoundOdds> UnifiedRoundOdds;

    bool IsRewardValid(const FRewardOffer& Reward, TFunctionRef<bool(FName)> IsItemValid,
        const TArray<FName>& RandomItemIds = {}) const;

private:
    friend class AShopTransactionManager;

    int32 Gold = 0;
    int32 ShopRevision = 0;
    int32 RewardRevision = 0;
    bool bShopLocked = false;
    bool bChanging = false;

    TArray<FShopOffer> Offers;
    TArray<FRewardOffer> Rewards;
    TSet<FGuid> RewardedKills;

    // Receipt for an applied preparation refresh. GameMode owns the live round.
    int32 LastPreparedRound = 0;
    int32 LastRoundGoldAward = 0;
    int32 LastRewardRound = 0;

    ETransactionResult ValidatePurchase(int32 Slot, int32 Revision, FShopOffer& Offer) const;
    ETransactionResult Reroll(int32 Round);
    void SetShopLock(bool bLocked);
    bool CreditRoundGold(int32 Round, int32 Amount);
    bool OpenRewards(int32 Round, TFunctionRef<bool(FName)> IsItemValid, const TArray<FName>& RandomItemIds);
    bool GetReward(int32 Round, int32 Slot, int32 Revision, FRewardOffer& Reward) const;
    void CommitReward();
    void AddGold(int64 Amount);
    bool GenerateOffers(int32 Round, TArray<FShopOffer>& NewOffers) const;
    bool GenerateRewards(TArray<FRewardOffer>& NewRewards, TFunctionRef<bool(FName)> IsItemValid,
        const TArray<FName>& RandomItemIds) const;
    TArray<FShopOffer> GetRandomRewardCandidates(int32 Cost) const;
    void CommitPurchase(int32 Slot, int32 Cost);
    void CreditSale(const FRosterUnit& Unit);
    bool CreditKill(FGuid DefeatedEnemyId);
    void CommitPreparation(int32 Round, TArray<FShopOffer>&& NextOffers);
};
