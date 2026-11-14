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

bool AShopManager::CanTrade() const
{
    return Phase == EShopPhase::Preparation || Phase == EShopPhase::Combat;
}

bool AShopManager::CanManageBoard() const
{
    return Phase == EShopPhase::Preparation || Phase == EShopPhase::RewardSelection;
}

bool AShopManager::BeginCombat()
{
    if (bChanging || Phase != EShopPhase::Preparation) return false;
    TGuardValue<bool> Guard(bChanging, true);
    Phase = EShopPhase::Combat;
    OnShopChanged.Broadcast();
    return true;
}

bool AShopManager::BeginSettlement()
{
    if (bChanging || Phase != EShopPhase::Combat) return false;
    TGuardValue<bool> Guard(bChanging, true);
    Phase = EShopPhase::Settlement;
    OnShopChanged.Broadcast();
    return true;
}

bool AShopManager::FinishMatch()
{
    if (bChanging || (Phase != EShopPhase::Settlement
        && (Phase != EShopPhase::RewardSelection || !bRewardReceived))) return false;
    TGuardValue<bool> Guard(bChanging, true);
    Phase = EShopPhase::Finished;
    OnShopChanged.Broadcast();
    return true;
}

void AShopManager::AddGold(int64 Amount)
{
    Gold = static_cast<int32>(FMath::Clamp<int64>(Gold + Amount, 0, MAX_int32));
}

bool AShopManager::CreditKill(FGuid DefeatedEnemyId)
{
    if (Phase != EShopPhase::Combat || !DefeatedEnemyId.IsValid() || RewardedKills.Contains(DefeatedEnemyId))
        return false;

    RewardedKills.Add(DefeatedEnemyId);
    AddGold(FMath::Max(0, KillGold));
    if (FMath::FRand() < BonusKillGoldChance) AddGold(FMath::Max(0, BonusKillGold));

    return true;
}

bool AShopManager::GrantRoundGold(int32 Amount)
{
    if (bChanging || Phase != EShopPhase::Settlement || bRoundGoldGranted || Amount < 0) return false;
    TGuardValue<bool> Guard(bChanging, true);
    bRoundGoldGranted = true;
    AddGold(Amount);
    OnShopChanged.Broadcast();
    return true;
}

ETransactionResult AShopManager::RequestReroll()
{
    if (!CanTrade()) return ETransactionResult::WrongPhase;
    if (bChanging) return ETransactionResult::Unavailable;
    if (Gold < RerollCost) return ETransactionResult::InsufficientGold;

    TGuardValue<bool> Guard(bChanging, true);
    TArray<FShopOffer> NextOffers;
    if (!GenerateOffers(CurrentRound, NextOffers)) return ETransactionResult::Unavailable;
    AddGold(-RerollCost);
    Offers = MoveTemp(NextOffers);
    ++ShopRevision;
    OnShopChanged.Broadcast();
    return ETransactionResult::Success;
}

ETransactionResult AShopManager::RequestShopLock(bool bLocked)
{
    if (!CanTrade()) return ETransactionResult::WrongPhase;
    if (bChanging) return ETransactionResult::Unavailable;
    TGuardValue<bool> Guard(bChanging, true);
    bShopLocked = bLocked;
    OnShopChanged.Broadcast();
    return ETransactionResult::Success;
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
    const FShopRoundOdds* Odds = ShopOdds ? ShopOdds->Rounds.Find(Round) : nullptr;
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

bool AShopManager::GenerateRewards(TArray<FRewardOffer>& NewRewards, TFunctionRef<bool(FName)> IsItemValid) const
{
    TArray<FRewardOffer> Candidates;
    TArray<double> Weights;
    for (const FRewardOffer& Reward : RewardPool)
    {
        if (ValidWeight(Reward.Weight) <= 0 || Reward.DefinitionId.IsNone()) continue;
        const bool bValid = Reward.Kind == ERewardKind::Unit
            ? Reward.Stars >= 1 && Reward.Stars <= 2 && !GetOfferDefinition(Reward.DefinitionId).UnitId.IsNone()
            : IsItemValid(Reward.DefinitionId);
        if (!bValid) continue;
        Candidates.Add(Reward);
        Weights.Add(Reward.Weight);
    }
    for (int32 Slot = 0; Slot < RewardSlots; ++Slot)
    {
        const int32 Index = DrawWeightedIndex(Weights);
        if (Index == INDEX_NONE) return false;
        NewRewards.Add(Candidates[Index]);
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

void AShopManager::CommitPreparation(int32 Round, int32 DeploymentLimit, TArray<FShopOffer>&& NextOffers)
{
    if (CurrentRound == 0) Gold = FMath::Max(0, StartingGold);
    CurrentRound = Round;
    CurrentDeploymentLimit = DeploymentLimit;
    Phase = EShopPhase::Preparation;
    bRoundGoldGranted = false;
    RewardedKills.Reset();
    Rewards.Reset();
    if (!bShopLocked)
    {
        NextOffers.SetNum(ShopSlots);
        Offers = MoveTemp(NextOffers);
        ++ShopRevision;
    }
}
