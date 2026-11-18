#include "ImperialBlessingSkill.h"
#include "../BattleUnitBase.h"
#include "../BoardManager.h"

namespace
{
    const FName BlessingId(TEXT("ImperialBlessing"));
}

FUnitSkillDefinition UImperialBlessingSkill::BuildSettings() const
{
    FUnitSkillDefinition Config;
    Config.SkillId = BlessingId;
    Config.Target = EUnitSkillTarget::Self;
    Config.CastSeconds = FMath::IsFinite(CastSeconds) ? FMath::Max(0.01f, CastSeconds) : 0.01f;
    Config.CastMontage = CastMontage;
    return Config;
}

void UImperialBlessingSkill::Execute_Implementation(ABattleUnitBase* Target, int32 ActionId)
{
    if (!IsValid(OwnerUnit) || !OwnerUnit->IsAlive() || !OwnerUnit->bInCombat) return;
    ABattleUnitBase* Caster = OwnerUnit;
    const float BaseHealing = Caster->StarLevel == 2 ? TwoStarHealingPerTickAt100AP : OneStarHealingPerTickAt100AP;
    const float Healing = BaseHealing * (Caster->GetAbilityPowerWithoutPeriodicEffect(BlessingId) / 100.f);
    const int32 Duration = FMath::Clamp(DurationSeconds, 1, 60);
    TArray<TWeakObjectPtr<ABattleUnitBase>> Allies;
    const auto AddAlly = [&](ABattleUnitBase* Unit)
    {
        if (IsValid(Unit) && Unit->TeamType == Caster->TeamType && Unit->bInCombat && Unit->IsAlive()
            && Unit->Placement == EUnitPlacement::Board) Allies.AddUnique(Unit);
    };
    AddAlly(Caster);
    if (IsValid(Caster->BoardManager))
    {
        for (ABattleUnitBase* Unit : Caster->BoardManager->GetUnitsOnBoard()) AddAlly(Unit);
    }
    else
    {
        for (ABattleUnitBase* Unit : Caster->GetCombatRoster()) AddAlly(Unit);
    }
    for (const auto& Ally : Allies)
    {
        if (!IsValid(Caster) || Caster->ActiveActionId != ActionId) return;
        if (ABattleUnitBase* Unit = Ally.Get())
            Unit->ApplyPeriodicHealing(BlessingId, Caster, Healing, Duration, AttackDamageBonus, AbilityPowerBonus);
    }
}
