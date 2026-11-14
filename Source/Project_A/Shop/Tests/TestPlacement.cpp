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

FUnitPosition UTestPlacement::FindRewardTile_Implementation(int32 DeploymentLimit) const
{
    int32 Deployed = 0;
    for (const auto& Entry : Positions)
        if (Entry.Value.Location == EUnitLocation::Board) ++Deployed;
    if (Deployed >= DeploymentLimit) return {};
    for (int32 Slot = 0; Slot < AvailableBoardTiles; ++Slot)
    {
        const FUnitPosition Position { EUnitLocation::Board, 0, Slot };
        if (!GetOccupant_Implementation(Position).IsValid()) return Position;
    }
    return {};
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
        for (const auto& Entry : Next)
            if (SameSlot(Entry.Value, Change.Destination)) return false;
        Next.Add(Change.Unit.Id, Change.Destination);
    }
    int32 Deployed = 0;
    for (const auto& Entry : Next)
        if (Entry.Value.Location == EUnitLocation::Board) ++Deployed;
    if (Phase != EShopPhase::Inactive && Deployed > Limit) return false;
    Positions = MoveTemp(Next);
    return true;
}
