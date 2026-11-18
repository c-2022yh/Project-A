#include "SpellbladeSkills.h"

namespace
{
    float Nonnegative(float Value)
    {
        return FMath::IsFinite(Value) ? FMath::Max(0.f, Value) : 0.f;
    }
}

FUnitSkillDefinition USpellbladeDualWieldSkill::BuildSettings() const
{
    FUnitSkillDefinition Config;
    Config.SkillId = TEXT("SpellbladeDualWield");
    Config.Trigger = EUnitTrigger::BasicAttackHit;
    return Config;
}

void USpellbladeDualWieldSkill::ConfigureMeleeCombo(float AttackDamage, float AbilityPower, float& FirstRawDamage,
    float& FollowupRawDamage, float& OutFollowupTimeRatio, bool& bFollowupUsesNotify) const
{
    FirstRawDamage = Nonnegative(AttackDamage * PhysicalAttackDamageRatio);
    FollowupRawDamage = Nonnegative(AbilityPower * MagicalAbilityPowerRatio);
    OutFollowupTimeRatio = FMath::IsFinite(FollowupTimeRatio) ? FMath::Clamp(FollowupTimeRatio, 0.f, 1.f) : 0.8f;
    bFollowupUsesNotify = bUseFollowupAnimNotify;
}

FUnitSkillDefinition USpellbladeCrossSlashSkill::BuildSettings() const
{
    FUnitSkillDefinition Config;
    Config.SkillId = TEXT("SpellbladeCrossSlash");
    Config.CastRange = 1;
    Config.CastSeconds = FMath::IsFinite(CastSeconds) ? FMath::Max(0.01f, CastSeconds) : 0.01f;
    Config.CastMontage = CastMontage;
    Config.bUseAnimNotify = bUseAnimNotify;

    FUnitSkillEffect& Physical = Config.Effects.AddDefaulted_GetRef();
    Physical.Kind = EUnitEffectKind::Damage;
    Physical.DamageType = EUnitDamageType::Physical;
    Physical.AttackDamageRatio = Nonnegative(PhysicalAttackDamageRatio);
    FUnitSkillEffect& Magical = Config.Effects.AddDefaulted_GetRef();
    Magical.Kind = EUnitEffectKind::Damage;
    Magical.DamageType = EUnitDamageType::Magical;
    Magical.AbilityPowerRatio = Nonnegative(MagicalAbilityPowerRatio);
    return Config;
}
