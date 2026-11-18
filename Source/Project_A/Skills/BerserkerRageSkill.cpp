#include "BerserkerRageSkill.h"

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
    // 공속 계산에서 상시 반영하므로 별도의 전투 트리거로 실행하지 않는다.
    return false;
}
