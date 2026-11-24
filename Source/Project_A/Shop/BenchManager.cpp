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
    const ARosterManager* Roster = Cast<ARosterManager>(GetOwner());
    if (IsValidSlot(Slot) && Roster && Roster->bUseLiveBench && Roster->Placement.GetObject())
        return IUnitPlacement::Execute_GetOccupant(Roster->Placement.GetObject(), SlotPosition(Slot));
    return IsValidSlot(Slot) ? Slots[Slot] : FGuid();
}

FUnitPosition UBenchManager::FindFreeSlot() const
{
    for (int32 Slot = 0; Slot < SlotCount; ++Slot)
        if (!GetOccupant(Slot).IsValid()) return SlotPosition(Slot);
    return {};
}

FUnitPosition UBenchManager::FindPosition(FGuid UnitId) const
{
    if (!UnitId.IsValid()) return {};
    const ARosterManager* Roster = Cast<ARosterManager>(GetOwner());
    if (Roster && Roster->bUseLiveBench && Roster->Placement.GetObject())
    {
        const FUnitPosition Position = IUnitPlacement::Execute_GetUnitPosition(Roster->Placement.GetObject(), UnitId);
        return Position.Location == EUnitLocation::Bench ? Position : FUnitPosition();
    }
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
    // 메인맵에서는 보드에 생성된 실제 대기석이 점유 상태를 소유한다.
    if (Roster->bUseLiveBench) return Roster->Apply(Changes, Match.Phase, Match.DeploymentLimit);
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
