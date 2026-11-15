#include "RosterManager.h"
#include "BenchManager.h"

bool ARosterManager::IsPositionValid(const FUnitPosition& Position)
{
    return Position.Location == EUnitLocation::Bench
        ? UBenchManager::IsValidSlot(Position.Column)
        : Position.Location == EUnitLocation::Board && Position.Row >= 0 && Position.Column >= 0;
}

ARosterManager::ARosterManager()
{
    Bench = CreateDefaultSubobject<UBenchManager>(TEXT("Bench"));
}

const FRosterUnit* ARosterManager::FindUnit(FGuid Id) const
{
    return Units.FindByPredicate([Id](const FRosterUnit& Unit) { return Unit.Id == Id; });
}

bool ARosterManager::GetUnit(FGuid Id, FRosterUnit& Unit) const
{
    if (const FRosterUnit* Found = FindUnit(Id))
    {
        Unit = *Found;
        return true;
    }
    return false;
}

FUnitPosition ARosterManager::GetPosition(FGuid Id) const
{
    if (!IsValid(Placement.GetObject()) || !FindUnit(Id)) return {};
    return IUnitPlacement::Execute_GetUnitPosition(Placement.GetObject(), Id);
}

FGuid ARosterManager::FindOccupant(FUnitPosition Position) const
{
    if (Position.Location == EUnitLocation::Bench) return Bench->GetOccupant(Position.Column);
    if (!IsValid(Placement.GetObject()) || !IsPositionValid(Position)) return {};
    return IUnitPlacement::Execute_GetOccupant(Placement.GetObject(), Position);
}

bool ARosterManager::GetProfile(FGuid Id, FRosterUnitProfile& Profile) const
{
    return IsValid(Placement.GetObject()) && FindUnit(Id)
        && IUnitPlacement::Execute_GetUnitProfile(Placement.GetObject(), Id, Profile);
}

FGuid ARosterManager::GetHoveredUnit() const
{
    return IsValid(Placement.GetObject()) ? IUnitPlacement::Execute_GetHoveredUnit(Placement.GetObject()) : FGuid();
}

FRosterUnitChange ARosterManager::MakeChange(ERosterUnitEdit Edit, const FRosterUnit& Unit, FUnitPosition Destination) const
{
    FRosterUnitChange Change;
    Change.Edit = Edit;
    Change.Unit = Unit;
    Change.Destination = Destination;
    if (Unit.Stars == 2)
    {
        Change.StarStatMultiplier = FMath::Max(1.0f, TwoStarStatMultiplier);
        Change.StarVisualScale = FMath::Max(1.0f, TwoStarVisualScale);
    }
    return Change;
}

bool ARosterManager::Apply(const TArray<FRosterUnitChange>& Changes, EShopPhase Phase, int32 Limit) const
{
    return Changes.IsEmpty() || (IsValid(Placement.GetObject())
        && IUnitPlacement::Execute_ApplyUnitChanges(Placement.GetObject(), Changes, Phase, Limit));
}

void ARosterManager::Commit(const TArray<FRosterUnitChange>& Changes)
{
    for (const FRosterUnitChange& Change : Changes)
    {
        if (Change.Edit == ERosterUnitEdit::Add)
        {
            Units.Add(Change.Unit);
            continue;
        }
        const int32 Index = Units.IndexOfByPredicate([&Change](const FRosterUnit& Unit) { return Unit.Id == Change.Unit.Id; });
        check(Index != INDEX_NONE);
        if (Change.Edit == ERosterUnitEdit::Remove) Units.RemoveAt(Index);
        else Units[Index] = Change.Unit;
    }
}

TArray<const FRosterUnit*, TInlineAllocator<3>> ARosterManager::FindMergeMaterials(FName UnitId, int32 Count,
    const TArray<FRosterUnitChange>& PendingChanges) const
{
    TArray<const FRosterUnit*, TInlineAllocator<3>> Materials;
    for (const FRosterUnit& Unit : Units)
    {
        if (Unit.UnitId != UnitId || Unit.Stars != 1) continue;
        if (PendingChanges.ContainsByPredicate([&Unit](const FRosterUnitChange& Change) { return Change.Unit.Id == Unit.Id; }))
            continue;
        Materials.Add(&Unit);
        if (Materials.Num() == Count) break;
    }
    return Materials;
}
void ARosterManager::NotifyChanged()
{
    OnRosterChanged.Broadcast();
}
