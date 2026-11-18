#include "PriestHealingLightSkill.h"
#include "../BattleUnitBase.h"

FUnitSkillDefinition UPriestHealingLightSkill::BuildSettings() const
{
    FUnitSkillDefinition Config;
    Config.SkillId = TEXT("PriestHealingLight");
    Config.Trigger = EUnitTrigger::ManaFull;
    Config.Target = EUnitSkillTarget::LowestHealthAlly;
    Config.CastRange = MAX_int32;
    Config.CastSeconds = FMath::IsFinite(CastSeconds) ? FMath::Max(0.01f, CastSeconds) : 0.01f;
    Config.CastMontage = CastMontage;

    return Config;
}

void UPriestHealingLightSkill::Execute_Implementation(ABattleUnitBase* Target, int32 ActionId)
{
    if (!IsValid(OwnerUnit) || !OwnerUnit->bInCombat || !OwnerUnit->IsAlive() || !IsValid(Target)
        || Target->TeamType != OwnerUnit->TeamType || Target->GetWorld() != OwnerUnit->GetWorld()) return;

    const float HealingAt100AP = OwnerUnit->StarLevel == 2 ? TwoStarHealingAt100AP : OneStarHealingAt100AP;
    if (!FMath::IsFinite(HealingAt100AP) || HealingAt100AP <= 0.f) return;

    // 성급별 기본 회복량 전체에 AP 비율을 곱한다. 최대 체력 제한은 공통 회복 함수에서 처리한다.
    const float Healing = HealingAt100AP * (OwnerUnit->FinalStats.AbilityPower / 100.f);
    Target->ReceiveHealing(Healing, OwnerUnit);
}
