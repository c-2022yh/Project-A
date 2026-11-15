#include "ShopTransactionManager.h"
#include "BenchManager.h"
#include "RosterManager.h"
#include "ShopManager.h"

bool AShopTransactionManager::IsConfigured() const
{
    return IsValid(Shop) && IsValid(Roster) && IsValid(Items)
        && Shop->GetWorld() == GetWorld() && Roster->GetWorld() == GetWorld() && Items->GetWorld() == GetWorld()
        && IsValid(Roster->Placement.GetObject())
        && Roster->Placement.GetObject()->GetClass()->ImplementsInterface(UUnitPlacement::StaticClass());
}

bool AShopTransactionManager::ApplyChanges(TArray<FRosterUnitChange>& Changes, AItemManager::FState&& ItemState,
    EShopPhase Phase, int32 Limit)
{
    for (FRosterUnitChange& Change : Changes)
    {
        if (Change.Edit != ERosterUnitEdit::Remove)
            Change.ItemBonuses = Items->GetBonuses(ItemState, Change.Unit.Id);
    }

    if (!Roster->Apply(Changes, Phase, Limit)) return false;
    Roster->Commit(Changes);
    Items->Commit(MoveTemp(ItemState));
    return true;
}

bool AShopTransactionManager::EnterPreparation(int32 Round, int32 DeploymentLimit)
{
    if (!IsConfigured() || Shop->bChanging || Round != Shop->CurrentRound + 1 || DeploymentLimit < 1)
        return false;

    const EShopPhase Phase = Shop->GetPhase();
    if (Phase != EShopPhase::Inactive && Phase != EShopPhase::Settlement
        && (Phase != EShopPhase::RewardSelection || !Shop->bRewardReceived)) return false;
    if (Phase == EShopPhase::RewardSelection && Shop->GetDeploymentLimit() != DeploymentLimit) return false;

    TGuardValue<bool> Guard(Shop->bChanging, true);
    TArray<FShopOffer> NextOffers;
    if (!Shop->bShopLocked && !Shop->Catalog.IsEmpty() && !Shop->GenerateOffers(Round, NextOffers)) return false;

    TArray<FRosterUnitChange> Changes;
    AItemManager::FState ItemState = Items->Snapshot();
    TMap<FName, int32> Counts;
    TArray<FName> UnitTypes;
    for (const FRosterUnit& Unit : Roster->Units)
    {
        if (Unit.Stars != 1) continue;
        UnitTypes.AddUnique(Unit.UnitId);
        ++Counts.FindOrAdd(Unit.UnitId);
    }
    for (FName UnitId : UnitTypes)
        for (int32 Merge = 0; Merge < Counts.FindChecked(UnitId) / 3; ++Merge)
            PlanMerge(UnitId, nullptr, {}, Changes, ItemState);

    if (!ApplyChanges(Changes, MoveTemp(ItemState), EShopPhase::Preparation, DeploymentLimit)) return false;
    Shop->CommitPreparation(Round, DeploymentLimit, MoveTemp(NextOffers));
    Roster->NotifyChanged();
    Shop->OnShopChanged.Broadcast();
    return true;
}

ETransactionResult AShopTransactionManager::GrantStartingUnit(FName UnitId)
{
    if (!IsConfigured() || Shop->bChanging) return ETransactionResult::Unavailable;
    if (Shop->GetPhase() != EShopPhase::Inactive) return ETransactionResult::WrongPhase;

    TGuardValue<bool> Guard(Shop->bChanging, true);
    const ETransactionResult Result = AddUnit(Shop->GetOfferDefinition(UnitId), 1, false);
    if (Result == ETransactionResult::Success) Roster->NotifyChanged();
    return Result;
}

bool AShopTransactionManager::BeginRewardSelection(int32 NextDeploymentLimit)
{
    if (!IsConfigured() || Shop->bChanging || Shop->GetPhase() != EShopPhase::Settlement || NextDeploymentLimit < 1)
        return false;

    TGuardValue<bool> Guard(Shop->bChanging, true);
    TArray<FRewardOffer> Drawn;
    if (!Shop->GenerateRewards(Drawn, [this](FName Id)
    {
        FItemDefinition Item;
        return Items->GetItem(Id, Item);
    })) return false;

    Shop->CurrentDeploymentLimit = NextDeploymentLimit;
    Shop->Rewards = MoveTemp(Drawn);
    ++Shop->RewardRevision;
    Shop->bRewardReceived = false;
    Shop->Phase = EShopPhase::RewardSelection;
    Shop->OnShopChanged.Broadcast();
    return true;
}

bool AShopTransactionManager::GrantKillReward(FGuid DefeatedEnemyId)
{
    if (!IsConfigured() || Shop->bChanging) return false;

    TGuardValue<bool> Guard(Shop->bChanging, true);
    if (!Shop->CreditKill(DefeatedEnemyId)) return false;
    Items->TryGrantKillDrop();
    Shop->OnShopChanged.Broadcast();
    return true;
}

ETransactionResult AShopTransactionManager::RequestBuy(int32 Slot, int32 OfferRevision)
{
    if (!IsConfigured() || Shop->bChanging) return ETransactionResult::Unavailable;
    if (!Shop->CanTrade()) return ETransactionResult::WrongPhase;
    if (OfferRevision != Shop->GetShopRevision() || !Shop->Offers.IsValidIndex(Slot) || Shop->Offers[Slot].UnitId.IsNone())
        return ETransactionResult::InvalidSelection;

    const FShopOffer Offer = Shop->Offers[Slot];
    if (Shop->GetGold() < Offer.Cost) return ETransactionResult::InsufficientGold;

    TGuardValue<bool> Guard(Shop->bChanging, true);
    const ETransactionResult Result = AddUnit(Offer, 1, false);
    if (Result != ETransactionResult::Success) return Result;

    Shop->CommitPurchase(Slot, Offer.Cost);
    Roster->NotifyChanged();
    Shop->OnShopChanged.Broadcast();
    return Result;
}

ETransactionResult AShopTransactionManager::RequestSell(FGuid UnitId)
{
    if (!IsConfigured() || Shop->bChanging) return ETransactionResult::Unavailable;
    const EShopPhase Phase = Shop->GetPhase();
    if (!Shop->CanTrade() && Phase != EShopPhase::RewardSelection) return ETransactionResult::WrongPhase;

    TGuardValue<bool> Guard(Shop->bChanging, true);
    FRosterUnit Unit;
    if (!Roster->GetUnit(UnitId, Unit)) return ETransactionResult::InvalidSelection;
    const EUnitLocation Location = Roster->GetPosition(UnitId).Location;
    if (Location == EUnitLocation::None) return ETransactionResult::Unavailable;
    if (Phase == EShopPhase::Combat && Location != EUnitLocation::Bench) return ETransactionResult::WrongPhase;

    AItemManager::FState ItemState = Items->Snapshot();
    ItemState.ReturnEquipment(UnitId);
    TArray<FRosterUnitChange> Changes = { Roster->MakeChange(ERosterUnitEdit::Remove, Unit) };
    if (!ApplyChanges(Changes, MoveTemp(ItemState), Phase, Shop->GetDeploymentLimit()))
        return ETransactionResult::ChangeRejected;

    Shop->CreditSale(Unit);
    Roster->NotifyChanged();
    Shop->OnShopChanged.Broadcast();
    return ETransactionResult::Success;
}

ETransactionResult AShopTransactionManager::RequestReward(int32 Slot, int32 SelectionRevision)
{
    if (!IsConfigured() || Shop->bChanging) return ETransactionResult::Unavailable;
    if (Shop->GetPhase() != EShopPhase::RewardSelection) return ETransactionResult::WrongPhase;
    if (Shop->bRewardReceived || SelectionRevision != Shop->GetRewardRevision() || !Shop->Rewards.IsValidIndex(Slot))
        return ETransactionResult::InvalidSelection;

    {
        TGuardValue<bool> Guard(Shop->bChanging, true);
        const FRewardOffer Reward = Shop->Rewards[Slot];
        if (Reward.Kind == ERewardKind::Unit)
        {
            const auto Result = AddUnit(Shop->GetOfferDefinition(Reward.DefinitionId), Reward.Stars, true);
            if (Result != ETransactionResult::Success) return Result;
        }
        else if (!Items->GrantItem(Reward.DefinitionId))
        {
            return ETransactionResult::Unavailable;
        }

        Shop->bRewardReceived = true;
        Shop->Rewards.Reset();
        Roster->NotifyChanged();
        Shop->OnShopChanged.Broadcast();
    }

    // Progression can enter preparation after the reward has committed.
    OnRewardReceived.Broadcast();
    return ETransactionResult::Success;
}

ETransactionResult AShopTransactionManager::AddUnit(const FShopOffer& Offer, int32 Stars, bool bReward)
{
    if (Offer.UnitId.IsNone() || Offer.Cost <= 0 || Stars < 1 || Stars > 2) return ETransactionResult::InvalidSelection;

    FUnitPosition Destination;
    if (bReward)
    {
        Destination = IUnitPlacement::Execute_FindRewardTile(Roster->Placement.GetObject(), Shop->GetDeploymentLimit());
        if (Destination.Location != EUnitLocation::Board || Destination.Row < 0 || Destination.Column < 0
            || Roster->FindOccupant(Destination).IsValid()) Destination = {};
    }
    if (Destination.Location == EUnitLocation::None) Destination = Roster->GetBench()->FindFreeSlot();

    FRosterUnit Unit;
    Unit.Id = FGuid::NewGuid();
    Unit.UnitId = Offer.UnitId;
    Unit.Stars = Stars;
    Unit.BaseCost = Offer.Cost;

    TArray<FRosterUnitChange> Changes;
    AItemManager::FState ItemState = Items->Snapshot();
    if (Stars == 1 && Shop->GetPhase() == EShopPhase::Preparation)
        PlanMerge(Unit.UnitId, &Unit, Destination, Changes, ItemState);
    if (Changes.IsEmpty())
    {
        if (Destination.Location == EUnitLocation::None) return ETransactionResult::NoSpace;
        Changes.Add(Roster->MakeChange(ERosterUnitEdit::Add, Unit, Destination));
    }

    return ApplyChanges(Changes, MoveTemp(ItemState), Shop->GetPhase(), Shop->GetDeploymentLimit())
        ? ETransactionResult::Success : ETransactionResult::ChangeRejected;
}

void AShopTransactionManager::PlanMerge(FName UnitId, const FRosterUnit* Incoming, FUnitPosition Destination,
    TArray<FRosterUnitChange>& Changes, AItemManager::FState& ItemState) const
{
    auto Materials = Roster->FindMergeMaterials(UnitId, Incoming ? 2 : 3, Changes);
    if (Incoming) Materials.Add(Incoming);
    if (Materials.Num() != 3) return;

    const FRosterUnit* Kept = Materials[0];
    for (const FRosterUnit* Material : Materials)
    {
        const FUnitPosition Position = Material == Incoming ? Destination : Roster->GetPosition(Material->Id);
        if (Position.Location == EUnitLocation::Board)
        {
            Kept = Material;
            break;
        }
    }

    FRosterUnit Upgraded = *Kept;
    Upgraded.Stars = 2;
    TArray<FGuid> MaterialIds;
    for (const FRosterUnit* Material : Materials)
    {
        MaterialIds.Add(Material->Id);
        if (Material != Kept && Material != Incoming)
            Changes.Add(Roster->MakeChange(ERosterUnitEdit::Remove, *Material));
    }
    ItemState.InheritEquipment(MaterialIds, Kept->Id);
    Changes.Add(Roster->MakeChange(Kept == Incoming ? ERosterUnitEdit::Add : ERosterUnitEdit::Update, Upgraded, Destination));
}

ETransactionResult AShopTransactionManager::RequestMoveToBench(FGuid UnitId, int32 Slot)
{
    return RequestMove(UnitId, UBenchManager::SlotPosition(Slot));
}

ETransactionResult AShopTransactionManager::RequestMove(FGuid UnitId, FUnitPosition Destination)
{
    if (!IsConfigured() || Shop->bChanging) return ETransactionResult::Unavailable;
    if (!Shop->CanTrade() && !Shop->CanManageBoard()) return ETransactionResult::WrongPhase;
    if (!Roster->IsPositionValid(Destination)) return ETransactionResult::InvalidSelection;

    TGuardValue<bool> Guard(Shop->bChanging, true);
    const FRosterUnit* Unit = Roster->FindUnit(UnitId);
    const FUnitPosition Source = Roster->GetPosition(UnitId);
    if (!Unit || !Roster->IsPositionValid(Source)) return ETransactionResult::InvalidSelection;
    if (Shop->GetPhase() == EShopPhase::Combat
        && (Source.Location != EUnitLocation::Bench || Destination.Location != EUnitLocation::Bench))
        return ETransactionResult::WrongPhase;
    if (Source.Location == Destination.Location && Source.Column == Destination.Column
        && (Source.Location == EUnitLocation::Bench || Source.Row == Destination.Row))
        return ETransactionResult::Success;

    TArray<FRosterUnitChange> Changes;
    AItemManager::FState ItemState = Items->Snapshot();
    const auto AddMove = [&](const FRosterUnit& CurrentUnit, FUnitPosition Target)
    {
        if (Target.Location == EUnitLocation::Bench) ItemState.ReturnEquipment(CurrentUnit.Id);
        Changes.Add(Roster->MakeChange(ERosterUnitEdit::Move, CurrentUnit, Target));
    };

    const FGuid OccupantId = Roster->FindOccupant(Destination);
    if (OccupantId.IsValid())
    {
        const FRosterUnit* Occupant = Roster->FindUnit(OccupantId);
        if (!Occupant) return ETransactionResult::ChangeRejected;
        AddMove(*Occupant, Source);
    }
    AddMove(*Unit, Destination);
    if (!ApplyChanges(Changes, MoveTemp(ItemState), Shop->GetPhase(), Shop->GetDeploymentLimit()))
        return ETransactionResult::ChangeRejected;

    Roster->NotifyChanged();
    return ETransactionResult::Success;
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
    if (!IsConfigured() || Shop->bChanging) return ETransactionResult::Unavailable;
    if (!Shop->CanManageBoard()) return ETransactionResult::WrongPhase;

    TGuardValue<bool> Guard(Shop->bChanging, true);
    const FRosterUnit* Unit = Roster->FindUnit(UnitId);
    if (!Unit || Roster->GetPosition(UnitId).Location != EUnitLocation::Board)
        return ETransactionResult::InvalidSelection;

    AItemManager::FState ItemState = Items->Snapshot();
    const auto Result = Items->PlanEquipmentChange(ItemState, UnitId, ItemId, UnequipSlot);
    if (Result != ETransactionResult::Success) return Result;

    TArray<FRosterUnitChange> Changes = { Roster->MakeChange(ERosterUnitEdit::Update, *Unit) };
    if (!ApplyChanges(Changes, MoveTemp(ItemState), Shop->GetPhase(), Shop->GetDeploymentLimit()))
        return ETransactionResult::ChangeRejected;

    return ETransactionResult::Success;
}
