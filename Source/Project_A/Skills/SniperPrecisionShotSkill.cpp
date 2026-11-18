#include "SniperPrecisionShotSkill.h"

FUnitSkillDefinition USniperPrecisionShotSkill::BuildSettings() const
{
    FUnitSkillDefinition Config;
    Config.SkillId = TEXT("SniperPrecisionShot");
    Config.Trigger = EUnitTrigger::BasicAttackHit;
    return Config;
}

void USniperPrecisionShotSkill::ModifyBasicAttackRelease(float AttackDamage, float CriticalMultiplier, FUnitDamageRequest& Damage) const
{
    if (Counter < FMath::Max(1, ShotInterval) - 1) return;
    Damage.RawDamage = AttackDamage * (FMath::IsFinite(AttackDamageRatio) ? FMath::Max(0.f, AttackDamageRatio) : 0.f);
    Damage.SkillId = Settings.SkillId;
    if (!Damage.bCritical)
    {
        Damage.bCritical = true;
        Damage.OffenseMultiplier *= CriticalMultiplier;
    }
}

void USniperPrecisionShotSkill::OnBasicAttackReleased()
{
    Counter = Counter >= FMath::Max(1, ShotInterval) - 1 ? 0 : Counter + 1;
}
