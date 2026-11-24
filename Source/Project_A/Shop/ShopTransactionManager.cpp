#include "ShopTransactionManager.h"
#include "BenchManager.h"
#include "Engine/World.h"
#include "ItemManager.h"
#include "ShopManager.h"
#include "ShopMatchProgress.h"

bool AShopTransactionManager::IsConfigured() const
{
    return IsValid(Shop) && IsValid(Roster) && IsValid(Items)
        && Shop->GetWorld() == GetWorld() && Roster->GetWorld() == GetWorld() && Items->GetWorld() == GetWorld()
        && IsValid(Roster->Placement.GetObject())
        && Roster->Placement.GetObject()->GetClass()->ImplementsInterface(UUnitPlacement::StaticClass());
}

FShopMatchState AShopTransactionManager::GetMatchState() const
{
    return ReadShopMatchState(GetWorld());
}

bool AShopTransactionManager::ReturnEquipmentFromBench(FGuid UnitId)
{
    if (!CanStartRequest() || !UnitId.IsValid()) return false;
    const FShopMatchState Match = GetMatchState();
    FRosterUnit Unit;
    if (!Match.CanManageBoard() || Roster->GetPosition(UnitId).Location != EUnitLocation::Bench
        || !Roster->GetUnit(UnitId, Unit)) return false;

    TArray<FName> Equipment;
    if (!IUnitPlacement::Execute_GetUnitEquipment(Roster->Placement.GetObject(), UnitId, Equipment)) return false;
    if (Equipment.IsEmpty()) return true;
    TGuardValue<bool> Guard(Shop->bChanging, true);
    TArray<FName> Inventory = Items->GetInventory();
    Items->PlanReturn(Inventory, Equipment);
    FRosterUnitChange Change = Roster->MakeChange(ERosterUnitEdit::Update, Unit);
    Change.bReplaceEquipment = true;
    if (!CommitChanges({ Change }, MoveTemp(Inventory), Match)) return false;
    Roster->NotifyChanged();
    return true;
}

bool AShopTransactionManager::CanStartRequest() const
{
    return IsConfigured() && !Shop->bChanging && !Roster->GetBench()->IsApplying();
}

bool AShopTransactionManager::CommitChanges(const TArray<FRosterUnitChange>& Changes, TArray<FName>&& Inventory,
    const FShopMatchState& Match)
{
    if (!Roster->GetBench()->ApplyChanges(Changes, Match)) return false;
    Roster->Commit(Changes);
    Items->Commit(MoveTemp(Inventory));
    return true;
}

bool AShopTransactionManager::ApplyMerges(TArray<FRosterUnitChange>& Changes,
    const TArray<ARosterManager::FMerge>& Merges, const FShopMatchState& Match)
{
    TArray<FName> Inventory = Items->GetInventory();
    for (const ARosterManager::FMerge& Merge : Merges)
    {
        TArray<FName> Inherited;
        TArray<FUnitSkillProgress> SkillProgress;
        for (FGuid Id : Merge.Materials)
        {
            TArray<FName> Equipped;
            if (!Roster->GetUnitEquipment(Id, Equipped)) return false;
            Inherited.Append(Equipped);

            // 재료를 제거하기 전에 현재 전투 기물의 성장 기록까지 합성 결과에 모은다.
            FRosterUnit Material;
            if (!Roster->GetUnit(Id, Material)
                || !UnitSkillProgress::Merge(SkillProgress, Material.SkillProgress, SkillProgress)) return false;
        }
        Items->LimitInheritedEquipment(Inherited, Inventory);
        FRosterUnitChange* Result = Changes.FindByPredicate([&Merge](const FRosterUnitChange& Change)
        {
            return Change.Unit.Id == Merge.Kept;
        });
        check(Result);
        Result->bReplaceEquipment = Roster->GetPosition(Merge.Kept).Location == EUnitLocation::Board;
        Result->Equipment = MoveTemp(Inherited);
        Result->Unit.SkillProgress = MoveTemp(SkillProgress);
    }
    return CommitChanges(Changes, MoveTemp(Inventory), Match);
}

bool AShopTransactionManager::EnterPreparation()
{
    if (!CanStartRequest()) return false;
    TGuardValue<bool> Guard(Shop->bChanging, true);
    const FShopMatchState Match = GetMatchState();
    if (Match.Phase != EShopPhase::Preparation || Match.Round <= Shop->LastPreparedRound
        || Match.DeploymentLimit < 1 || !Shop->Rewards.IsEmpty()) return false;

    TArray<FShopOffer> Offers;
    if (!Shop->IsShopLocked() && !Shop->Catalog.IsEmpty() && !Shop->GenerateOffers(Match.Round, Offers)) return false;
    if (Match.Round == 1 && Roster->bUseLiveBench && Roster->Units.IsEmpty() && !Offers.IsEmpty())
    {
        const auto IsAffordable = [this](const FShopOffer& Offer)
        {
            return !Offer.UnitId.IsNone() && Offer.Cost > 0 && Offer.Cost <= Shop->StartingGold;
        };
        if (!Offers.ContainsByPredicate(IsAffordable))
        {
            const FShopOffer* Starter = Shop->Catalog.FindByPredicate(IsAffordable);
            if (!Starter)
            {
                UE_LOG(LogTemp, Error, TEXT("The opening shop has no unit affordable with its starting gold."));
                return false;
            }
            Offers[0] = *Starter;
        }
    }
    TArray<FRosterUnitChange> Changes;
    TArray<ARosterManager::FMerge> Merges;
    Roster->PlanPendingMerges(Changes, Merges);
    if (!ApplyMerges(Changes, Merges, Match)) return false;

    Shop->CommitPreparation(Match.Round, MoveTemp(Offers));
    Roster->NotifyChanged();
    Shop->OnShopChanged.Broadcast();
    return true;
}

ETransactionResult AShopTransactionManager::GrantStartingUnit(FName UnitId)
{
    if (!CanStartRequest()) return ETransactionResult::Unavailable;
    TGuardValue<bool> Guard(Shop->bChanging, true);
    const FShopMatchState Match = GetMatchState();
    if (Match.Phase != EShopPhase::Inactive) return ETransactionResult::WrongPhase;

    const auto Result = AddUnit(Shop->GetOfferDefinition(UnitId), 1, Match);
    if (Result == ETransactionResult::Success) Roster->NotifyChanged();
    return Result;
}

bool AShopTransactionManager::OpenRewards()
{
    if (!CanStartRequest()) return false;
    TGuardValue<bool> Guard(Shop->bChanging, true);
    const FShopMatchState Match = GetMatchState();
    if (Match.Phase != EShopPhase::RewardSelection) return false;

    if (!Shop->OpenRewards(Match.Round, [this](FName Id)
    {
        FItemDefinition Item;
        return Items->GetItem(Id, Item);
    })) return false;

    Shop->OnShopChanged.Broadcast();
    return true;
}

bool AShopTransactionManager::GrantKillReward(FGuid DefeatedEnemyId)
{
    if (!CanStartRequest()) return false;
    TGuardValue<bool> Guard(Shop->bChanging, true);
    if (GetMatchState().Phase != EShopPhase::Combat || !Shop->CreditKill(DefeatedEnemyId)) return false;
    Items->TryGrantKillDrop();
    Shop->OnShopChanged.Broadcast();
    return true;
}

bool AShopTransactionManager::GrantRoundGold(int32 Amount)
{
    if (!CanStartRequest()) return false;
    TGuardValue<bool> Guard(Shop->bChanging, true);
    const FShopMatchState Match = GetMatchState();
    if (Match.Phase != EShopPhase::Settlement || !Shop->CreditRoundGold(Match.Round, Amount)) return false;
    Shop->OnShopChanged.Broadcast();
    return true;
}

ETransactionResult AShopTransactionManager::RequestReroll()
{
    if (!CanStartRequest()) return ETransactionResult::Unavailable;
    TGuardValue<bool> Guard(Shop->bChanging, true);
    const FShopMatchState Match = GetMatchState();
    if (!Match.CanTrade() || Match.Round != Shop->LastPreparedRound) return ETransactionResult::WrongPhase;

    const auto Result = Shop->Reroll(Match.Round);
    if (Result == ETransactionResult::Success) Shop->OnShopChanged.Broadcast();
    return Result;
}

ETransactionResult AShopTransactionManager::RequestShopLock(bool bLocked)
{
    if (!CanStartRequest()) return ETransactionResult::Unavailable;
    TGuardValue<bool> Guard(Shop->bChanging, true);
    const FShopMatchState Match = GetMatchState();
    if (!Match.CanTrade() || Match.Round != Shop->LastPreparedRound) return ETransactionResult::WrongPhase;

    Shop->SetShopLock(bLocked);
    Shop->OnShopChanged.Broadcast();
    return ETransactionResult::Success;
}

ETransactionResult AShopTransactionManager::RequestBuy(int32 Slot, int32 OfferRevision)
{
    if (!CanStartRequest()) return ETransactionResult::Unavailable;
    TGuardValue<bool> Guard(Shop->bChanging, true);
    const FShopMatchState Match = GetMatchState();
    if (!Match.CanTrade() || Match.Round != Shop->LastPreparedRound) return ETransactionResult::WrongPhase;

    FShopOffer Offer;
    const auto Validation = Shop->ValidatePurchase(Slot, OfferRevision, Offer);
    if (Validation != ETransactionResult::Success) return Validation;

    const auto Result = AddUnit(Offer, 1, Match);
    if (Result != ETransactionResult::Success) return Result;

    Shop->CommitPurchase(Slot, Offer.Cost);
    Roster->NotifyChanged();
    Shop->OnShopChanged.Broadcast();
    return Result;
}

ETransactionResult AShopTransactionManager::RequestSell(FGuid UnitId)
{
    if (!CanStartRequest()) return ETransactionResult::Unavailable;
    TGuardValue<bool> Guard(Shop->bChanging, true);
    const FShopMatchState Match = GetMatchState();
    if (!Match.CanTrade() && !Match.CanManageBoard()) return ETransactionResult::WrongPhase;
    if (Match.CanTrade() && Match.Round != Shop->LastPreparedRound) return ETransactionResult::WrongPhase;

    FRosterUnit Unit;
    if (!Roster->GetUnit(UnitId, Unit)) return ETransactionResult::InvalidSelection;
    const EUnitLocation Location = Roster->GetPosition(UnitId).Location;
    if (Location == EUnitLocation::None) return ETransactionResult::Unavailable;
    if (Match.Phase == EShopPhase::Combat && Location != EUnitLocation::Bench) return ETransactionResult::WrongPhase;

    TArray<FName> Equipped;
    if (!Roster->GetUnitEquipment(UnitId, Equipped)) return ETransactionResult::Unavailable;
    TArray<FName> Inventory = Items->GetInventory();
    Items->PlanReturn(Inventory, Equipped);
    const TArray<FRosterUnitChange> Changes = { Roster->MakeChange(ERosterUnitEdit::Remove, Unit) };
    if (!CommitChanges(Changes, MoveTemp(Inventory), Match)) return ETransactionResult::ChangeRejected;

    Shop->CreditSale(Unit);
    Roster->NotifyChanged();
    Shop->OnShopChanged.Broadcast();
    return ETransactionResult::Success;
}

ETransactionResult AShopTransactionManager::RequestReward(int32 Slot, int32 SelectionRevision)
{
    if (!CanStartRequest()) return ETransactionResult::Unavailable;
    {
        TGuardValue<bool> Guard(Shop->bChanging, true);
        const FShopMatchState Match = GetMatchState();
        if (Match.Phase != EShopPhase::RewardSelection) return ETransactionResult::WrongPhase;
        FRewardOffer Reward;
        if (!Shop->GetReward(Match.Round, Slot, SelectionRevision, Reward)) return ETransactionResult::InvalidSelection;

        if (Reward.Kind == ERewardKind::Unit)
        {
            const auto Result = AddUnit(Shop->GetOfferDefinition(Reward.DefinitionId), Reward.Stars, Match);
            if (Result != ETransactionResult::Success) return Result;
        }
        else if (Reward.Kind == ERewardKind::Gold)
        {
            if (Reward.GoldAmount <= 0) return ETransactionResult::InvalidSelection;
            Shop->AddGold(Reward.GoldAmount);
        }
        else if (!Items->GrantItem(Reward.DefinitionId))
        {
            return ETransactionResult::Unavailable;
        }

        Shop->CommitReward();
        Roster->NotifyChanged();
        Shop->OnShopChanged.Broadcast();
    }

    OnRewardReceived.Broadcast();
    return ETransactionResult::Success;
}

ETransactionResult AShopTransactionManager::AddUnit(const FShopOffer& Offer, int32 Stars, const FShopMatchState& Match)
{
    TArray<FRosterUnitChange> Changes;
    TArray<ARosterManager::FMerge> Merges;
    const auto Plan = Roster->PlanAdmission(Offer, Stars, Roster->GetBench()->FindFreeSlot(),
        Match.Phase == EShopPhase::Preparation, Changes, Merges);
    if (Plan != ETransactionResult::Success) return Plan;
    return ApplyMerges(Changes, Merges, Match) ? ETransactionResult::Success : ETransactionResult::ChangeRejected;
}

ETransactionResult AShopTransactionManager::RequestEquip(FGuid UnitId, FName ItemId)
{
    return ChangeEquipment(UnitId, ItemId, {});
}

ETransactionResult AShopTransactionManager::RequestUnequip(FGuid UnitId, int32 Slot)
{
    return ChangeEquipment(UnitId, NAME_None, Slot);
}

ETransactionResult AShopTransactionManager::ChangeEquipment(FGuid UnitId, FName ItemId, TOptional<int32> UnequipSlot)
{
    if (!CanStartRequest()) return ETransactionResult::Unavailable;
    TGuardValue<bool> Guard(Shop->bChanging, true);
    const FShopMatchState Match = GetMatchState();
    if (!Match.CanManageBoard()) return ETransactionResult::WrongPhase;

    FRosterUnit Unit;
    if (!Roster->GetUnit(UnitId, Unit) || Roster->GetPosition(UnitId).Location != EUnitLocation::Board)
        return ETransactionResult::InvalidSelection;

    auto Change = Roster->MakeChange(ERosterUnitEdit::Update, Unit);
    if (!Roster->GetUnitEquipment(UnitId, Change.Equipment)) return ETransactionResult::Unavailable;
    TArray<FName> Inventory = Items->GetInventory();
    const auto Result = Items->PlanEquipmentChange(Inventory, Change.Equipment, ItemId, UnequipSlot);
    if (Result != ETransactionResult::Success) return Result;

    Change.bReplaceEquipment = true;
    if (!CommitChanges({ Change }, MoveTemp(Inventory), Match)) return ETransactionResult::ChangeRejected;
    Roster->NotifyChanged();
    return ETransactionResult::Success;
}
