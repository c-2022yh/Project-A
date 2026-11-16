#include "TestPlacement.h"

namespace
{
bool SameSlot(const FUnitPosition& A, const FUnitPosition& B)
{
    return A.Location == B.Location && A.Column == B.Column && A.Row == B.Row;
}
}

FUnitPosition UTestPlacement::GetUnitPosition_Implementation(FGuid Id) const
{
    const FUnitPosition* Position = Positions.Find(Id);
    return Position ? *Position : FUnitPosition();
}

FGuid UTestPlacement::GetOccupant_Implementation(FUnitPosition Position) const
{
    for (const auto& Entry : Positions)
        if (SameSlot(Entry.Value, Position)) return Entry.Key;
    return {};
}

bool UTestPlacement::GetUnitEquipment_Implementation(FGuid Id, TArray<FName>& Equipment) const
{
    if (!bEquipmentApiAvailable || !Positions.Contains(Id)) return false;
    Equipment = UnitEquipment.FindRef(Id);
    return true;
}
bool UTestPlacement::ApplyUnitChanges_Implementation(const TArray<FRosterUnitChange>& Changes, EShopPhase Phase, int32 Limit)
{
    if (OnApply) OnApply();
    if (bReject) return false;
    TMap<FGuid, FUnitPosition> Next = Positions;
    for (const FRosterUnitChange& Change : Changes)
    {
        const FGuid Id = Change.Unit.Id;
        if (Phase == EShopPhase::Combat && GetUnitPosition_Implementation(Id).Location == EUnitLocation::Board) return false;
        if (Change.Edit == ERosterUnitEdit::Remove || Change.Edit == ERosterUnitEdit::Move) Next.Remove(Id);
    }
    for (const FRosterUnitChange& Change : Changes)
    {
        if (Change.Edit != ERosterUnitEdit::Add && Change.Edit != ERosterUnitEdit::Move) continue;
        if (Change.Destination.Location == EUnitLocation::Board && Change.Destination.Column >= AvailableBoardTiles) return false;
        for (const auto& Entry : Next)
            if (SameSlot(Entry.Value, Change.Destination)) return false;
        Next.Add(Change.Unit.Id, Change.Destination);
    }
    int32 Deployed = 0;
    for (const auto& Entry : Next)
        if (Entry.Value.Location == EUnitLocation::Board) ++Deployed;
    if (Phase != EShopPhase::Inactive && Deployed > Limit) return false;
    Positions = MoveTemp(Next);
    for (const auto& Change : Changes)
    {
        if (Change.Edit == ERosterUnitEdit::Remove) UnitEquipment.Remove(Change.Unit.Id);
        else if (Change.bReplaceEquipment) UnitEquipment.Add(Change.Unit.Id, Change.Equipment);
    }
    return true;
}
