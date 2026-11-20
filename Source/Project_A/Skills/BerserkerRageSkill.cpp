#include "BerserkerRageSkill.h"
#include "../BattleUnitBase.h"

FUnitSkillDefinition UBerserkerRageSkill::BuildSettings() const
{
    FUnitSkillDefinition Config;
    Config.SkillId = TEXT("BerserkerRage");
    Config.Trigger = EUnitTrigger::CombatStart;
    Config.Target = EUnitSkillTarget::Self;
    return Config;
}

float UBerserkerRageSkill::GetHealthBasedAttackSpeedBonus(float HealthRatio) const
{
    if (!FMath::IsFinite(AttackSpeedPerMissingHealth) || !FMath::IsFinite(MaxAttackSpeedBonus)) return 0.f;
    const float MissingHealth = 1.f - FMath::Clamp(HealthRatio, 0.f, 1.f);
    return FMath::Min(MissingHealth * FMath::Max(0.f, AttackSpeedPerMissingHealth),
        FMath::Max(0.f, MaxAttackSpeedBonus));
}

bool UBerserkerRageSkill::ShouldActivate_Implementation(EUnitTrigger Event, const FUnitDamageRequest& Context, const FUnitDamageResult& Result)
{
    // 공속 계산과 평타 명중 콜백에서 처리한다.
    return false;
}

void UBerserkerRageSkill::OnBasicAttackLanded(const FUnitDamageResult& Result)
{
    if (!IsValid(OwnerUnit) || Result.HealthDamage <= 0.f || !FMath::IsFinite(BasicAttackLifestealRatio)) return;
    const float HealRatio = FMath::Clamp(BasicAttackLifestealRatio, 0.f, 1.f);
    OwnerUnit->ReceiveHealing(Result.HealthDamage * HealRatio, OwnerUnit);
}
