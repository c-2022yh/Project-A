#include "IronwallGuardSkill.h"

FUnitSkillDefinition UIronwallGuardSkill::BuildSettings() const
{
    FUnitSkillDefinition Config;
    Config.SkillId = TEXT("IronwallGuard");
    Config.Target = EUnitSkillTarget::Self;
    Config.CastSeconds = FMath::IsFinite(CastSeconds) ? FMath::Max(0.01f, CastSeconds) : 0.01f;
    Config.CastMontage = CastMontage;

    FUnitSkillEffect& Effect = Config.Effects.AddDefaulted_GetRef();
    Effect.Kind = EUnitEffectKind::Modifier;
    Effect.bApplyToSelf = true;
    Effect.Duration = FMath::IsFinite(DurationSeconds) ? FMath::Max(0.f, DurationSeconds) : 0.f;
    Effect.Modifier.DamageReduction = FMath::IsFinite(DamageReduction) ? FMath::Clamp(DamageReduction, 0.f, 1.f) : 0.f;
    return Config;
}
