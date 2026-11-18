#include "ShopTransactionManager.h"
#include "BenchManager.h"
#include "RosterManager.h"
#include "ShopManager.h"
#include "../BattleUnitBase.h"
#include "../Skills/ElementalistSkill.h"

namespace
{
UElementalistSkill* FindElementalistSkill(UObject* Placement, FGuid UnitId)
{
    if (!IsValid(Placement)) return nullptr;
    ABattleUnitBase* Actor = IUnitPlacement::Execute_GetBattleUnit(Placement, UnitId);
    if (!IsValid(Actor)) return nullptr;
    for (UUnitSkillBase* Skill : Actor->GetEquippedSkills())
        if (auto* Elementalist = Cast<UElementalistSkill>(Skill)) return Elementalist;
    return nullptr;
}
}

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
    for (const FRosterUnitChange& Change : Changes)
    {
        if (Change.Edit == ERosterUnitEdit::Remove || Change.Unit.ElementalForm == EElementalForm::Unselected) continue;
        if (UElementalistSkill* Skill = FindElementalistSkill(Roster->Placement.GetObject(), Change.Unit.Id);
            Skill && IsValid(Skill->OwnerUnit) && Skill->OwnerUnit->UnitID == Change.Unit.UnitId
                && Skill->OwnerUnit->GetWorld() == GetWorld())
            Skill->SetForm(Change.Unit.ElementalForm);
    }
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
            if (!PlanMerge(UnitId, nullptr, {}, Changes, ItemState)) return false;

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
        if (!PlanMerge(Unit.UnitId, &Unit, Destination, Changes, ItemState)) return ETransactionResult::ChangeRejected;
    if (Changes.IsEmpty())
    {
        if (Destination.Location == EUnitLocation::None) return ETransactionResult::NoSpace;
        Changes.Add(Roster->MakeChange(ERosterUnitEdit::Add, Unit, Destination));
    }

    return ApplyChanges(Changes, MoveTemp(ItemState), Shop->GetPhase(), Shop->GetDeploymentLimit())
        ? ETransactionResult::Success : ETransactionResult::ChangeRejected;
}

bool AShopTransactionManager::PlanMerge(FName UnitId, const FRosterUnit* Incoming, FUnitPosition Destination,
    TArray<FRosterUnitChange>& Changes, AItemManager::FState& ItemState) const
{
    auto Materials = Roster->FindMergeMaterials(UnitId, Incoming ? 2 : 3, Changes);
    if (Incoming) Materials.Add(Incoming);
    if (Materials.Num() != 3) return true;

    // 남길 위치와 무관하게 재료 전부의 최신 실행 기록을 읽는다.
    TArray<FUnitSkillProgress> Progress;
    for (const FRosterUnit* Material : Materials)
    {
        FRosterUnit Snapshot;
        if (Material == Incoming) Snapshot = *Incoming;
        else if (!Roster->GetUnit(Material->Id, Snapshot)) return false;
        if (!UnitSkillProgress::Merge(Progress, Snapshot.SkillProgress, Progress)) return false;
    }

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
    Upgraded.SkillProgress = MoveTemp(Progress);
    TArray<FGuid> MaterialIds;
    for (const FRosterUnit* Material : Materials)
    {
        MaterialIds.Add(Material->Id);
        if (Material != Kept && Material != Incoming)
            Changes.Add(Roster->MakeChange(ERosterUnitEdit::Remove, *Material));
    }
    ItemState.InheritEquipment(MaterialIds, Kept->Id);
    Changes.Add(Roster->MakeChange(Kept == Incoming ? ERosterUnitEdit::Add : ERosterUnitEdit::Update, Upgraded, Destination));
    return true;
}

ETransactionResult AShopTransactionManager::RequestMoveToBench(FGuid UnitId, int32 Slot)
{
    return RequestMove(UnitId, UBenchManager::SlotPosition(Slot));
}

ETransactionResult AShopTransactionManager::RequestMove(FGuid UnitId, FUnitPosition Destination)
{
    return RequestMoveInternal(UnitId, Destination, nullptr, EElementalForm::Unselected);
}

bool AShopTransactionManager::TryMoveRegisteredElementalist(ABattleUnitBase* Unit, FUnitPosition Destination,
    ETransactionResult& Result)
{
    if (!IsConfigured() || !IsValid(Unit) || Unit->GetWorld() != GetWorld()) return false;
    const bool bElementalist = Unit->GetEquippedSkills().ContainsByPredicate(
        [](const UUnitSkillBase* Skill) { return Cast<UElementalistSkill>(Skill) != nullptr; });
    if (!bElementalist) return false;

    for (const FRosterUnit& Entry : Roster->Units)
    {
        if (Entry.UnitId != Unit->UnitID || IUnitPlacement::Execute_GetBattleUnit(Roster->Placement.GetObject(), Entry.Id) != Unit) continue;
        const FGuid Id = Entry.Id;
        Result = RequestMove(Id, Destination);
        return true;
    }
    return false;
}

bool AShopTransactionManager::IsElementalist(const FRosterUnit& Unit) const
{
    return Unit.ElementalForm != EElementalForm::Unselected || (!ElementalistUnitId.IsNone() && Unit.UnitId == ElementalistUnitId)
        || FindElementalistSkill(Roster->Placement.GetObject(), Unit.Id);
}

bool AShopTransactionManager::GetPendingElementalPlacement(FGuid& UnitId, FUnitPosition& Destination,
    EElementalForm& PreviousForm, int32& Revision) const
{
    if (!IsConfigured() || !PendingElementalPlacement.IsSet() || Shop->GetPhase() != EShopPhase::Preparation
        || Shop->GetRound() != PendingElementalPlacement->Round) return false;
    FRosterUnit Unit;
    if (!Roster->GetUnit(PendingElementalPlacement->UnitId, Unit)) return false;
    UnitId = PendingElementalPlacement->UnitId;
    Destination = PendingElementalPlacement->Destination;
    PreviousForm = Unit.ElementalForm;
    Revision = PendingElementalPlacement->Revision;
    return true;
}

ETransactionResult AShopTransactionManager::RequestElementalPlacement(EElementalForm Form, int32 Revision)
{
    if (!IsConfigured() || Shop->bChanging) return ETransactionResult::Unavailable;
    if (Shop->GetPhase() != EShopPhase::Preparation) return ETransactionResult::WrongPhase;
    if (!PendingElementalPlacement.IsSet() || PendingElementalPlacement->Round != Shop->GetRound()
        || PendingElementalPlacement->Revision != Revision
        || (Form != EElementalForm::Frost && Form != EElementalForm::Fire)) return ETransactionResult::InvalidSelection;

    const FPendingElementalPlacement Choice = PendingElementalPlacement.GetValue();
    PendingElementalPlacement.Reset();
    return RequestMoveInternal(Choice.UnitId, Choice.Destination, &Choice, Form);
}

void AShopTransactionManager::CancelElementalPlacement()
{
    if (IsValid(Shop) && !Shop->bChanging) PendingElementalPlacement.Reset();
}

ETransactionResult AShopTransactionManager::RequestMoveInternal(FGuid UnitId, FUnitPosition Destination,
    const FPendingElementalPlacement* Confirmed, EElementalForm Form)
{
    if (!IsConfigured() || Shop->bChanging) return ETransactionResult::Unavailable;
    if (!Shop->CanTrade() && !Shop->CanManageBoard()) return ETransactionResult::WrongPhase;
    if (!Roster->IsPositionValid(Destination)) return ETransactionResult::InvalidSelection;

    TGuardValue<bool> Guard(Shop->bChanging, true);
    FRosterUnit Unit;
    const FUnitPosition Source = Roster->GetPosition(UnitId);
    if (!Roster->GetUnit(UnitId, Unit) || !Roster->IsPositionValid(Source)) return ETransactionResult::InvalidSelection;
    const FGuid OccupantId = Roster->FindOccupant(Destination);
    const bool bElementalDeployment = Source.Location == EUnitLocation::Bench
        && Destination.Location == EUnitLocation::Board && IsElementalist(Unit);
    if (Confirmed)
    {
        if (!bElementalDeployment || Source.Column != Confirmed->Source.Column || OccupantId.IsValid())
            return ETransactionResult::ChangeRejected;
        Unit.ElementalForm = Form;
    }
    else if (bElementalDeployment && Shop->GetPhase() == EShopPhase::Preparation)
    {
        if (OccupantId.IsValid()) return ETransactionResult::NoSpace;
        PendingElementalPlacement = FPendingElementalPlacement{UnitId, Source, Destination, Shop->GetRound(), ++ElementalPlacementRevision};
        return ETransactionResult::FormChoiceRequired;
    }
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

    if (OccupantId.IsValid())
    {
        FRosterUnit Occupant;
        if (!Roster->GetUnit(OccupantId, Occupant)) return ETransactionResult::ChangeRejected;
        if (Shop->GetPhase() == EShopPhase::Preparation && Source.Location == EUnitLocation::Board
            && Destination.Location == EUnitLocation::Bench && IsElementalist(Occupant)) return ETransactionResult::ChangeRejected;
        AddMove(Occupant, Source);
    }
    AddMove(Unit, Destination);
    if (!ApplyChanges(Changes, MoveTemp(ItemState), Shop->GetPhase(), Shop->GetDeploymentLimit()))
        return ETransactionResult::ChangeRejected;

    Roster->NotifyChanged();
    if (PendingElementalPlacement.IsSet() && PendingElementalPlacement->UnitId == UnitId) PendingElementalPlacement.Reset();
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
    FRosterUnit Unit;
    if (!Roster->GetUnit(UnitId, Unit) || Roster->GetPosition(UnitId).Location != EUnitLocation::Board)
        return ETransactionResult::InvalidSelection;

    AItemManager::FState ItemState = Items->Snapshot();
    const auto Result = Items->PlanEquipmentChange(ItemState, UnitId, ItemId, UnequipSlot);
    if (Result != ETransactionResult::Success) return Result;

    TArray<FRosterUnitChange> Changes = { Roster->MakeChange(ERosterUnitEdit::Update, Unit) };
    if (!ApplyChanges(Changes, MoveTemp(ItemState), Shop->GetPhase(), Shop->GetDeploymentLimit()))
        return ETransactionResult::ChangeRejected;

    return ETransactionResult::Success;
}
