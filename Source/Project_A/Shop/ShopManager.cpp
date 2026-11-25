#include "ShopManager.h"
#include "ShopOdds.h"


namespace
{
int32 DrawWeightedIndex(const TArray<double>& Weights)
{
    double Total = 0;
    for (double Weight : Weights) Total += Weight;
    if (Total <= 0) return INDEX_NONE;

    double Ticket = FMath::FRand() * Total;
    for (int32 Index = 0; Index < Weights.Num(); ++Index)
    {
        if (Weights[Index] <= 0) continue;
        if (Ticket < Weights[Index]) return Index;
        Ticket -= Weights[Index];
    }
    return Weights.FindLastByPredicate([](double Weight) { return Weight > 0; });
}

double ValidWeight(float Weight)
{
    return FMath::IsFinite(Weight) && Weight > 0 ? Weight : 0;
}
}

AShopManager::AShopManager()
{
    Offers.SetNum(ShopSlots);
}

void AShopManager::AddGold(int64 Amount)
{
    Gold = static_cast<int32>(FMath::Clamp<int64>(Gold + Amount, 0, MAX_int32));
}

bool AShopManager::CreditKill(FGuid DefeatedEnemyId)
{
    if (!DefeatedEnemyId.IsValid() || RewardedKills.Contains(DefeatedEnemyId))
        return false;

    RewardedKills.Add(DefeatedEnemyId);
    AddGold(FMath::Max(0, KillGold));
    if (FMath::FRand() < BonusKillGoldChance) AddGold(FMath::Max(0, BonusKillGold));

    return true;
}

bool AShopManager::CreditRoundGold(int32 Round, int32 Amount)
{
    if (Round <= LastRoundGoldAward || Amount < 0) return false;
    LastRoundGoldAward = Round;
    AddGold(Amount);
    return true;
}

ETransactionResult AShopManager::ValidatePurchase(int32 Slot, int32 Revision, FShopOffer& Offer) const
{
    if (Revision != ShopRevision || !Offers.IsValidIndex(Slot) || Offers[Slot].UnitId.IsNone() || Offers[Slot].Cost <= 0)
        return ETransactionResult::InvalidSelection;
    Offer = Offers[Slot];
    return Gold >= Offer.Cost ? ETransactionResult::Success : ETransactionResult::InsufficientGold;
}

ETransactionResult AShopManager::Reroll(int32 Round)
{
    if (Gold < RerollCost) return ETransactionResult::InsufficientGold;
    TArray<FShopOffer> NextOffers;
    if (!GenerateOffers(Round, NextOffers)) return ETransactionResult::Unavailable;
    AddGold(-RerollCost);
    Offers = MoveTemp(NextOffers);
    ++ShopRevision;
    return ETransactionResult::Success;
}

void AShopManager::SetShopLock(bool bLocked)
{
    bShopLocked = bLocked;
}

bool AShopManager::OpenRewards(int32 Round, TFunctionRef<bool(FName)> IsItemValid, const TArray<FName>& RandomItemIds)
{
    if (Round <= LastRewardRound || !Rewards.IsEmpty()) return false;
    TArray<FRewardOffer> Drawn;
    if (!GenerateRewards(Drawn, IsItemValid, RandomItemIds)) return false;
    Rewards = MoveTemp(Drawn);
    LastRewardRound = Round;
    ++RewardRevision;
    return true;
}

bool AShopManager::GetReward(int32 Round, int32 Slot, int32 Revision, FRewardOffer& Reward) const
{
    if (Round != LastRewardRound || Revision != RewardRevision || !Rewards.IsValidIndex(Slot)) return false;
    Reward = Rewards[Slot];
    return true;
}

void AShopManager::CommitReward()
{
    Rewards.Reset();
}
FShopOffer AShopManager::GetOfferDefinition(FName UnitId) const
{
    const FShopOffer* Offer = Catalog.FindByPredicate([UnitId](const FShopOffer& Entry)
    {
        return Entry.UnitId == UnitId && !UnitId.IsNone() && Entry.Cost > 0;
    });
    return Offer ? *Offer : FShopOffer();
}

bool AShopManager::GenerateOffers(int32 Round, TArray<FShopOffer>& NewOffers) const
{
    NewOffers.Init(FShopOffer(), ShopSlots);
    const FShopRoundOdds* Odds = UnifiedRoundOdds.Find(Round);
    if (!Odds && ShopOdds) Odds = ShopOdds->Rounds.Find(Round);
    TMap<int32, TArray<const FShopOffer*>> OffersByCost;
    TSet<FName> Included;
    for (const FShopOffer& Offer : Catalog)
    {
        if (Offer.UnitId.IsNone() || Offer.Cost <= 0 || Included.Contains(Offer.UnitId)) continue;
        Included.Add(Offer.UnitId);
        OffersByCost.FindOrAdd(Offer.Cost).Add(&Offer);
    }
    TArray<int32> Costs;
    OffersByCost.GetKeys(Costs);
    Costs.Sort();
    TArray<double> Weights;
    for (int32 Cost : Costs)
    {
        Weights.Add(!Odds ? 1.0 : Odds->CostWeights.IsValidIndex(Cost - 1) ? ValidWeight(Odds->CostWeights[Cost - 1]) : 0.0);
    }
    for (FShopOffer& Slot : NewOffers)
    {
        const int32 Index = DrawWeightedIndex(Weights);
        if (Index == INDEX_NONE) return false;
        const auto& Candidates = OffersByCost.FindChecked(Costs[Index]);
        Slot = *Candidates[FMath::RandHelper(Candidates.Num())];
    }
    return true;
}

TArray<FShopOffer> AShopManager::GetRandomRewardCandidates(int32 Cost) const
{
    TArray<FShopOffer> Candidates;
    TSet<FName> Included;
    if (Cost < 1 || Cost > 5) return Candidates;
    for (const FShopOffer& Offer : Catalog)
    {
        if (Offer.UnitId.IsNone() || Offer.Cost != Cost || Included.Contains(Offer.UnitId)) continue;
        Included.Add(Offer.UnitId);
        Candidates.Add(Offer);
    }
    return Candidates;
}

bool AShopManager::IsRewardValid(const FRewardOffer& Reward, TFunctionRef<bool(FName)> IsItemValid,
    const TArray<FName>& RandomItemIds) const
{
    if (ValidWeight(Reward.Weight) <= 0) return false;
    switch (Reward.Kind)
    {
        case ERewardKind::Unit:
            return Reward.Stars >= 1 && Reward.Stars <= 2 && !GetOfferDefinition(Reward.DefinitionId).UnitId.IsNone();
        case ERewardKind::RandomUnit:
            return Reward.Stars >= 1 && Reward.Stars <= 2 && !GetRandomRewardCandidates(Reward.UnitCost).IsEmpty();
        case ERewardKind::Gold: return Reward.GoldAmount > 0;
        case ERewardKind::Item: return IsItemValid(Reward.DefinitionId);
        case ERewardKind::RandomItem:
            return RandomItemIds.ContainsByPredicate([&IsItemValid](FName Id)
            { return !Id.IsNone() && IsItemValid(Id); });
        default: return false;
    }
}

bool AShopManager::GenerateRewards(TArray<FRewardOffer>& NewRewards, TFunctionRef<bool(FName)> IsItemValid,
    const TArray<FName>& RandomItemIds) const
{
    TArray<FName> Items;
    for (FName Id : RandomItemIds)
        if (!Id.IsNone() && IsItemValid(Id)) Items.AddUnique(Id);
    TArray<FRewardOffer> Candidates;
    TArray<double> Weights;
    for (const FRewardOffer& Reward : RewardPool)
    {
        if (!IsRewardValid(Reward, IsItemValid, Items)) continue;
        Candidates.Add(Reward);
        Weights.Add(Reward.Weight);
    }
    for (int32 Slot = 0; Slot < RewardSlots; ++Slot)
    {
        const int32 Index = DrawWeightedIndex(Weights);
        if (Index == INDEX_NONE) return false;
        FRewardOffer Drawn = Candidates[Index];
        if (Drawn.Kind == ERewardKind::RandomUnit)
        {
            const TArray<FShopOffer> Units = GetRandomRewardCandidates(Drawn.UnitCost);
            // 카드 생성 시 결과를 고정한다. 벤치가 가득 차 수령을 재시도해도 다시 추첨하지 않는다.
            Drawn.DefinitionId = Units[FMath::RandHelper(Units.Num())].UnitId;
        }
        else if (Drawn.Kind == ERewardKind::RandomItem)
        {
            // 카드에는 종류를 숨기되, 실제 지급할 아이템은 생성 시 한 번만 추첨한다.
            Drawn.DefinitionId = Items[FMath::RandHelper(Items.Num())];
        }
        NewRewards.Add(Drawn);
        Weights[Index] = 0;
    }
    return true;
}

void AShopManager::CommitPurchase(int32 Slot, int32 Cost)
{
    AddGold(-Cost);
    Offers[Slot] = FShopOffer();
}

void AShopManager::CreditSale(const FRosterUnit& Unit)
{
    const double Rate = ValidWeight(Unit.Stars == 2 ? TwoStarRefundRate : OneStarRefundRate);
    AddGold(FMath::FloorToInt(FMath::Min(Unit.BaseCost * Rate, static_cast<double>(MAX_int32))));
}

void AShopManager::CommitPreparation(int32 Round, TArray<FShopOffer>&& NextOffers)
{
    if (LastPreparedRound == 0) Gold = FMath::Max(0, StartingGold);
    LastPreparedRound = Round;
    RewardedKills.Reset();
    if (!bShopLocked)
    {
        NextOffers.SetNum(ShopSlots);
        Offers = MoveTemp(NextOffers);
        ++ShopRevision;
    }
}
