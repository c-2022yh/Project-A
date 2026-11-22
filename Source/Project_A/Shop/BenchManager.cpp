#include "BenchManager.h"
#include "RosterManager.h"
#include "ShopMatchProgress.h"

UBenchManager::UBenchManager()
{
    Slots.SetNum(SlotCount);
}

bool UBenchManager::IsValidSlot(int32 Slot)
{
    return Slot >= 0 && Slot < SlotCount;
}

FUnitPosition UBenchManager::SlotPosition(int32 Slot)
{
    return { EUnitLocation::Bench, INDEX_NONE, Slot };
}

FGuid UBenchManager::GetOccupant(int32 Slot) const
{
    return IsValidSlot(Slot) ? Slots[Slot] : FGuid();
}

FUnitPosition UBenchManager::FindFreeSlot() const
{
    const int32 Slot = Slots.IndexOfByPredicate([](const FGuid& Id) { return !Id.IsValid(); });
    return Slot == INDEX_NONE ? FUnitPosition() : SlotPosition(Slot);
}

FUnitPosition UBenchManager::FindPosition(FGuid UnitId) const
{
    if (!UnitId.IsValid()) return {};
    const int32 Slot = Slots.IndexOfByKey(UnitId);
    return Slot == INDEX_NONE ? FUnitPosition() : SlotPosition(Slot);
}

bool UBenchManager::ApplyChanges(const TArray<FRosterUnitChange>& Changes, const FShopMatchState& Match)
{
    if (bApplying) return false;
    if (Changes.IsEmpty()) return true;
    const ARosterManager* Roster = Cast<ARosterManager>(GetOwner());
    if (!Roster) return false;

    TGuardValue<bool> Guard(bApplying, true);
    TArray<FGuid> Next = Slots;
    for (const FRosterUnitChange& Change : Changes)
    {
        if (Change.Edit != ERosterUnitEdit::Remove && Change.Edit != ERosterUnitEdit::Move) continue;
        const int32 Slot = Next.IndexOfByKey(Change.Unit.Id);
        if (Slot != INDEX_NONE) Next[Slot].Invalidate();
    }
    for (const FRosterUnitChange& Change : Changes)
    {
        if ((Change.Edit != ERosterUnitEdit::Add && Change.Edit != ERosterUnitEdit::Move)
            || Change.Destination.Location != EUnitLocation::Bench) continue;

        const int32 Slot = Change.Destination.Column;
        if (!Change.Unit.Id.IsValid() || !IsValidSlot(Slot) || Next[Slot].IsValid()
            || Next.Contains(Change.Unit.Id)) return false;
        Next[Slot] = Change.Unit.Id;
    }

    if (!Roster->Apply(Changes, Match.Phase, Match.DeploymentLimit)) return false;
    Slots = MoveTemp(Next);
    return true;
}

ETransactionResult UBenchManager::RequestMove(FGuid UnitId, int32 Slot)
{
    const FShopMatchState Match = ReadShopMatchState(GetWorld());
    if (!Match.CanTrade() && !Match.CanManageBoard()) return ETransactionResult::WrongPhase;
    const ARosterManager* Roster = Cast<ARosterManager>(GetOwner());
    if (!Roster || bApplying) return ETransactionResult::Unavailable;

    const FUnitPosition Source = FindPosition(UnitId);
    FRosterUnit Unit;
    if (Source.Location != EUnitLocation::Bench || !IsValidSlot(Slot) || !Roster->GetUnit(UnitId, Unit))
        return ETransactionResult::InvalidSelection;
    if (Source.Column == Slot) return ETransactionResult::Success;

    TArray<FRosterUnitChange> Changes = { Roster->MakeChange(ERosterUnitEdit::Move, Unit, SlotPosition(Slot)) };
    const FGuid Occupant = GetOccupant(Slot);
    if (Occupant.IsValid())
    {
        FRosterUnit Other;
        if (!Roster->GetUnit(Occupant, Other)) return ETransactionResult::ChangeRejected;
        Changes.Add(Roster->MakeChange(ERosterUnitEdit::Move, Other, Source));
    }
    return ApplyChanges(Changes, Match) ? ETransactionResult::Success : ETransactionResult::ChangeRejected;
}
