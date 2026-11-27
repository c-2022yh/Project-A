#include "RosterManager.h"
#include "BenchManager.h"
#include "../BattleUnitBase.h"

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
        if (!UnitSkillProgress::Validate(Unit.SkillProgress)) return false;
        if (IsValid(Placement.GetObject()))
        {
            ABattleUnitBase* Actor = IUnitPlacement::Execute_GetBattleUnit(Placement.GetObject(), Id);
            if (Actor)
            {
                if (!IsValid(Actor) || Actor->GetWorld() != GetWorld() || Actor->UnitID != Unit.UnitId
                    || Actor->TeamType != EBattleTeam::Player) return false;
                TArray<FUnitSkillProgress> Live;
                if (!Actor->CaptureSkillProgress(Live)
                    || !UnitSkillProgress::Merge(Unit.SkillProgress, Live, Unit.SkillProgress)) return false;
            }
        }
        return true;
    }
    return false;
}

bool ARosterManager::CacheUnitSkillProgress(FGuid Id)
{
    if (!IsValid(Placement.GetObject()) || !IsValid(IUnitPlacement::Execute_GetBattleUnit(Placement.GetObject(), Id))) return false;
    FRosterUnit Snapshot;
    if (!GetUnit(Id, Snapshot)) return false;
    FRosterUnit* Stored = Units.FindByPredicate([Id](const FRosterUnit& Unit) { return Unit.Id == Id; });
    if (!Stored) return false;
    Stored->SkillProgress = MoveTemp(Snapshot.SkillProgress);
    return true;
}

FUnitPosition ARosterManager::GetPosition(FGuid Id) const
{
    if (!FindUnit(Id)) return {};
    const FUnitPosition BenchPosition = Bench->FindPosition(Id);
    if (BenchPosition.Location == EUnitLocation::Bench) return BenchPosition;
    if (!IsValid(Placement.GetObject())) return {};
    const FUnitPosition Position = IUnitPlacement::Execute_GetUnitPosition(Placement.GetObject(), Id);
    return Position.Location == EUnitLocation::Board && IsPositionValid(Position) ? Position : FUnitPosition();
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

ETransactionResult ARosterManager::PlanAdmission(const FShopOffer& Offer, int32 Stars, FUnitPosition BenchSlot,
    EShopPhase Phase, TArray<FRosterUnitChange>& Changes, TArray<FMerge>& Merges) const
{
    if (Offer.UnitId.IsNone() || Offer.Cost <= 0 || Stars < 1 || Stars > 2)
        return ETransactionResult::InvalidSelection;

    const FRosterUnit Incoming { FGuid::NewGuid(), Offer.UnitId, Stars, Offer.Cost };
    FMerge Merge;
    const bool bBenchOnly = Phase == EShopPhase::Combat;
    if (Stars == 1 && (bBenchOnly || Phase == EShopPhase::Preparation)
        && PlanMerge(Offer.UnitId, &Incoming, bBenchOnly, Changes, Merge))
    {
        Merges.Add(MoveTemp(Merge));
        return ETransactionResult::Success;
    }
    if (BenchSlot.Location != EUnitLocation::Bench) return ETransactionResult::NoSpace;
    Changes.Add(MakeChange(ERosterUnitEdit::Add, Incoming, BenchSlot));
    return ETransactionResult::Success;
}

bool ARosterManager::PlanMerge(FName UnitId, const FRosterUnit* Incoming, bool bBenchOnly,
    TArray<FRosterUnitChange>& Changes, FMerge& Merge) const
{
    TArray<const FRosterUnit*, TInlineAllocator<3>> Materials;
    const int32 Required = Incoming ? 2 : 3;
    for (const FRosterUnit& Unit : Units)
    {
        if (Unit.UnitId != UnitId || Unit.Stars != 1) continue;
        const EUnitLocation Location = GetPosition(Unit.Id).Location;
        if (Location == EUnitLocation::None || (bBenchOnly && Location != EUnitLocation::Bench)) continue;
        if (Changes.ContainsByPredicate([&Unit](const FRosterUnitChange& Change) { return Change.Unit.Id == Unit.Id; }))
            continue;
        Materials.Add(&Unit);
        if (Materials.Num() == Required) break;
    }
    if (Materials.Num() != Required) return false;

    const FRosterUnit* Kept = Materials[0];
    for (const FRosterUnit* Material : Materials)
    {
        if (GetPosition(Material->Id).Location == EUnitLocation::Board)
        {
            Kept = Material;
            break;
        }
    }

    Merge.Kept = Kept->Id;
    for (const FRosterUnit* Material : Materials)
    {
        Merge.Materials.Add(Material->Id);
        if (Material != Kept) Changes.Add(MakeChange(ERosterUnitEdit::Remove, *Material));
    }

    // The incoming copy is consumed without spawning or occupying a slot.
    FRosterUnit Upgraded = *Kept;
    Upgraded.Stars = 2;
    Changes.Add(MakeChange(ERosterUnitEdit::Update, Upgraded));
    return true;
}

void ARosterManager::PlanPendingMerges(TArray<FRosterUnitChange>& Changes, TArray<FMerge>& Merges) const
{
    TArray<FName> UnitTypes;
    for (const FRosterUnit& Unit : Units)
        if (Unit.Stars == 1) UnitTypes.AddUnique(Unit.UnitId);

    for (FName UnitId : UnitTypes)
    {
        FMerge Merge;
        while (PlanMerge(UnitId, nullptr, false, Changes, Merge))
        {
            Merges.Add(MoveTemp(Merge));
            Merge = FMerge();
        }
    }
}

bool ARosterManager::GetUnitEquipment(FGuid Id, TArray<FName>& Equipment) const
{
    Equipment.Reset();
    const EUnitLocation Location = GetPosition(Id).Location;
    if (Location == EUnitLocation::Bench) return true;
    return Location == EUnitLocation::Board && IsValid(Placement.GetObject())
        && IUnitPlacement::Execute_GetUnitEquipment(Placement.GetObject(), Id, Equipment);
}
void ARosterManager::NotifyChanged()
{
    OnRosterChanged.Broadcast();
}
