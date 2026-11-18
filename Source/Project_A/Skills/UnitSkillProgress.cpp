#include "UnitSkillProgress.h"
#include "UnitSkill.h"

bool UnitSkillProgress::Validate(const TArray<FUnitSkillProgress>& States)
{
    TSet<FName> Ids;
    for (const FUnitSkillProgress& State : States)
    {
        if (State.SkillId.IsNone() || !State.SkillClass || Ids.Contains(State.SkillId)
            || !State.SkillClass->GetDefaultObject<UUnitSkillBase>()->CanRestorePersistentProgress(State)) return false;
        Ids.Add(State.SkillId);
    }
    return true;
}
bool UnitSkillProgress::Merge(const TArray<FUnitSkillProgress>& Kept, const TArray<FUnitSkillProgress>& Materials, TArray<FUnitSkillProgress>& Out)
{
    if (!Validate(Kept) || !Validate(Materials)) return false;
    TArray<FUnitSkillProgress> Result = Kept;
    for (const FUnitSkillProgress& State : Materials)
    {
        FUnitSkillProgress* Existing = Result.FindByPredicate([&](const FUnitSkillProgress& S) { return S.SkillId == State.SkillId; });
        if (!Existing) { Result.Add(State); continue; }
        if (Existing->SkillClass != State.SkillClass) return false;
        FUnitSkillProgress Merged;
        if (!State.SkillClass->GetDefaultObject<UUnitSkillBase>()->MergePersistentProgress(*Existing, State, Merged)) return false;
        if (Merged.SkillId != State.SkillId || Merged.SkillClass != State.SkillClass || !Validate({Merged})) return false;
        *Existing = MoveTemp(Merged);
    }
    Out = MoveTemp(Result);
    return true;
}
