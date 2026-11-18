#include "ThunderMageLightningSkill.h"
#include "../BattleUnitBase.h"

FUnitSkillDefinition UThunderMageLightningSkill::BuildSettings() const
{
    FUnitSkillDefinition Config;
    Config.SkillId = TEXT("ThunderMageLightning");
    Config.CastRange = FMath::Max(1, CastRange);
    Config.CastSeconds = FMath::IsFinite(CastSeconds) ? FMath::Max(0.01f, CastSeconds) : 0.01f;
    Config.CastMontage = CastMontage;
    Config.bUseAnimNotify = bUseAnimNotify;

    FUnitSkillEffect& Stun = Config.Effects.AddDefaulted_GetRef();
    Stun.Kind = EUnitEffectKind::Modifier;
    Stun.Duration = FMath::IsFinite(StunSeconds) ? FMath::Max(0.f, StunSeconds) : 0.f;
    Stun.Modifier.bStun = true;
    return Config;
}

void UThunderMageLightningSkill::ApplyImpact_Implementation(const FUnitSkillDefinition& Config, ABattleUnitBase* Target,
    const TArray<float>& Values, const FUnitDamageRequest& Context)
{
    if (!IsValid(OwnerUnit) || !OwnerUnit->bInCombat || !OwnerUnit->IsEnemyTarget(Target)) return;

    // 전달된 공통 피해 보정은 유지하고, 주문력에 비례하는 낙뢰 피해량만 별도로 계산한다.
    const float DamageAt100AP = OwnerUnit->StarLevel == 2 ? TwoStarDamageAt100AP : OneStarDamageAt100AP;
    const float Raw = DamageAt100AP * (OwnerUnit->FinalStats.AbilityPower / 100.f);
    FUnitDamageRequest Damage = Context;
    Damage.Victim = Target;
    Damage.RawDamage = FMath::IsFinite(Raw) ? FMath::Max(0.f, Raw) : 0.f;
    Damage.Type = EUnitDamageType::Magical;
    Damage.DamageId = FGuid::NewGuid();
    if (Damage.RawDamage <= 0.f || Target->ReceiveDamage(Damage).bApplied)
        Super::ApplyImpact_Implementation(Config, Target, Values, Context);
}
