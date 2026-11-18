#include "HeroStrikeSkill.h"
#include "../BattleUnitBase.h"
#include "HeroGrowthPassiveSkill.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

UHeroStrikeSkill::UHeroStrikeSkill()
{
    Settings.SkillId = TEXT("HeroStrike");
    Settings.Target = EUnitSkillTarget::Self;
    Settings.CastSeconds = 0.2f;
}

FUnitSkillDefinition UHeroStrikeSkill::BuildSettings() const
{
    FUnitSkillDefinition Config;
    Config.SkillId = TEXT("HeroStrike");
    Config.Trigger = EUnitTrigger::ManaFull;
    Config.Target = EUnitSkillTarget::Self;
    Config.CastSeconds = FMath::IsFinite(CastSeconds) ? FMath::Max(0.01f, CastSeconds) : 0.2f;
    Config.CastMontage = CastMontage;
    Config.bUseAnimNotify = bUseAnimNotify;
    return Config;
}

bool UHeroStrikeSkill::CanUse_Implementation(ABattleUnitBase* Target) const
{
    if (!Super::CanUse_Implementation(Target) || OwnerUnit->HasArmedBasicAttack(this)) return false;
    const float Ratio = IsOwnerEvolved() ? EvolvedAttackDamageRatio : AttackDamageRatio;
    return FMath::IsFinite(Ratio) && Ratio > 0.f;
}

bool UHeroStrikeSkill::IsOwnerEvolved() const
{
    if (!IsValid(OwnerUnit)) return false;
    for (const UUnitSkillBase* Skill : OwnerUnit->GetEquippedSkills())
        if (const auto* Growth = Cast<UHeroGrowthPassiveSkill>(Skill)) return Growth->bEvolved;
    return false;
}

void UHeroStrikeSkill::Execute_Implementation(ABattleUnitBase* Target, int32 ActionId)
{
    if (!IsValid(OwnerUnit)) return;
    // 마나 소모/시전은 공통 유닛이 담당한다. 이 스킬은 다음 평타에 효과만 장착한다.
    const bool bEvolved = IsOwnerEvolved();
    if (OwnerUnit->ArmNextBasicAttack(this, bEvolved ? EvolvedAttackDamageRatio : AttackDamageRatio))
    {
        ArmedHealRatio = FMath::IsFinite(HealthDamageHealRatio) ? FMath::Clamp(HealthDamageHealRatio, 0.f, 1.f) : 0.f;
        bArmedWhileEvolved = bEvolved;
    }
}

void UHeroStrikeSkill::OnEmpoweredAttackHit_Implementation(const FUnitDamageRequest& Request, const FUnitDamageResult& Result)
{
    if (!IsValid(OwnerUnit) || Request.Attacker != OwnerUnit || Request.Source != EUnitDamageSource::BasicAttack || !Result.bApplied) return;
    const bool bPlayEvolvedImpact = bArmedWhileEvolved;
    bArmedWhileEvolved = false;
    // HealthDamage는 저항/보호막/초과 처치 처리를 마친 결과다. 일반 평타나 다른 스킬에는 적용하지 않는다.
    OwnerUnit->ReceiveHealing(Result.HealthDamage * ArmedHealRatio, OwnerUnit);
    if (bPlayEvolvedImpact && IsValid(Request.Victim))
    {
        if (EvolvedImpactSound)
            UGameplayStatics::PlaySoundAtLocation(Request.Victim, EvolvedImpactSound, Request.Victim->GetActorLocation());
        OnEvolvedImpact(Request.Victim, Result);
    }
}
