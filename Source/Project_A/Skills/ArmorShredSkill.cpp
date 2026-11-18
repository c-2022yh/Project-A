#include "ArmorShredSkill.h"
#include "../BattleUnitBase.h"

FUnitSkillDefinition UArmorShredSkill::BuildSettings() const
{
    FUnitSkillDefinition Config;
    Config.SkillId = TEXT("ArmorShred");
    Config.Trigger = EUnitTrigger::BasicAttackHit;
    Config.Target = EUnitSkillTarget::CurrentEnemy;
    // 평타가 실제로 도착한 대상을 사용한다. 투사체 비행 중 두 기물의 거리가 바뀔 수 있다.
    Config.CastRange = MAX_int32;

    FUnitSkillEffect& Effect = Config.Effects.AddDefaulted_GetRef();
    Effect.EffectId = TEXT("ArmorShred");
    Effect.Kind = EUnitEffectKind::Modifier;
    Effect.Duration = FMath::IsFinite(DurationSeconds) ? FMath::Max(0.f, DurationSeconds) : 0.f;
    Effect.Modifier.ArmorReduction = FMath::IsFinite(ArmorReduction) ? FMath::Clamp(ArmorReduction, 0.f, 1.f) : 0.f;
    return Config;
}

bool UArmorShredSkill::ShouldActivate_Implementation(EUnitTrigger Event, const FUnitDamageRequest& Context,
    const FUnitDamageResult& Result)
{
    // 처치로 원래 대상이 사라졌을 때 평타 타깃 찾기로 다른 적에게 효과가 넘어가는 것을 막는다.
    if (Event != EUnitTrigger::BasicAttackHit || !Result.bApplied || Context.Source != EUnitDamageSource::BasicAttack
        || !IsValid(OwnerUnit) || Context.Attacker != OwnerUnit || !OwnerUnit->IsEnemyTarget(Context.Victim))
    {
        return false;
    }
    return Super::ShouldActivate_Implementation(Event, Context, Result);
}
