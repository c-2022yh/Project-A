#include "ShamanWeakeningCurseSkill.h"
#include "../BattleUnitBase.h"

namespace
{
    const FName CurseId(TEXT("ShamanWeakeningCurse"));
    constexpr float TickInterval = 1.f;
    constexpr int32 TickCount = 6;
}

FUnitSkillDefinition UShamanWeakeningCurseSkill::BuildSettings() const
{
    FUnitSkillDefinition Config;
    Config.SkillId = CurseId;
    Config.Trigger = EUnitTrigger::ManaFull;
    Config.CastRange = FMath::Max(1, CastRange);
    Config.CastSeconds = FMath::IsFinite(CastSeconds) ? FMath::Max(0.01f, CastSeconds) : 0.01f;
    Config.CastMontage = CastMontage;
    return Config;
}

ABattleUnitBase* UShamanWeakeningCurseSkill::SelectTarget_Implementation() const
{
    return SelectCurseTarget();
}

ABattleUnitBase* UShamanWeakeningCurseSkill::SelectCurseTarget(const ABattleUnitBase* Excluded) const
{
    if (!IsValid(OwnerUnit)) return nullptr;

    ABattleUnitBase* Best = nullptr;
    int32 BestDistance = MAX_int32;
    float BestRemaining = MAX_flt;
    ABattleUnitBase* Current = OwnerUnit->CurrentTarget;
    if (Current != Excluded && OwnerUnit->IsEnemyAttackTarget(Current))
    {
        const int32 Distance = ABattleUnitBase::HexDistance(OwnerUnit->CurrentTile, Current->CurrentTile);
        if (Distance <= Settings.CastRange && Current->GetPeriodicDamageRemaining(CurseId) < 0.f) return Current;
    }

    for (ABattleUnitBase* Enemy : OwnerUnit->GetCombatRoster())
    {
        if (Enemy == Excluded || !OwnerUnit->IsEnemyAttackTarget(Enemy)) continue;
        const int32 Distance = ABattleUnitBase::HexDistance(OwnerUnit->CurrentTile, Enemy->CurrentTile);
        if (Distance > Settings.CastRange) continue;

        // 저주가 없으면 -1이라 남은 시간이 있는 적보다 먼저 선택된다. 동률이면 가까운 적을 우선한다.
        const float Remaining = Enemy->GetPeriodicDamageRemaining(CurseId);
        if (Remaining < BestRemaining || (Remaining == BestRemaining && Distance < BestDistance))
        {
            Best = Enemy;
            BestRemaining = Remaining;
            BestDistance = Distance;
        }
    }
    return Best;
}

void UShamanWeakeningCurseSkill::Execute_Implementation(ABattleUnitBase*, int32 ActionId)
{
    // 시전 중 선행 대상이 죽거나 사거리에서 벗어날 수 있으므로 적용 시점에 두 대상을 다시 고른다.
    ABattleUnitBase* First = SelectCurseTarget();
    if (!First) return;
    ABattleUnitBase* Second = SelectCurseTarget(First);

    const float DamageAt100AP = OwnerUnit->StarLevel == 2 ? TwoStarDamagePerTickAt100AP : OneStarDamagePerTickAt100AP;
    const float TickDamage = DamageAt100AP * (OwnerUnit->FinalStats.AbilityPower / 100.f);
    First->ApplyPeriodicMagicDamage(CurseId, OwnerUnit, ActionId, TickDamage, TickInterval, TickCount, MagicResistanceReduction);
    if (IsValid(Second))
        Second->ApplyPeriodicMagicDamage(CurseId, OwnerUnit, ActionId, TickDamage, TickInterval, TickCount, MagicResistanceReduction);
}
