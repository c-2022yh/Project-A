#include "BenchManager.h"
#include "RosterManager.h"
#include "UnitPlacement.h"

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
    if (!Roster || !IsValidSlot(Slot)) return {};

    UObject* Placement = Roster->Placement.GetObject();
    return IsValid(Placement) ? IUnitPlacement::Execute_GetOccupant(Placement, SlotPosition(Slot)) : FGuid();
}

FUnitPosition UBenchManager::FindFreeSlot() const
{
    const ARosterManager* Roster = Cast<ARosterManager>(GetOwner());
    if (!Roster || !IsValid(Roster->Placement.GetObject())) return {};

    for (int32 Slot = 0; Slot < SlotCount; ++Slot)
        if (!GetOccupant(Slot).IsValid()) return SlotPosition(Slot);
    return {};
}
