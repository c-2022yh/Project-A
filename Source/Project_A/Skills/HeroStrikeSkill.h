#pragma once

#include "UnitSkill.h"
#include "HeroStrikeSkill.generated.h"

class USoundBase;

// 견습 용사 액티브: 다음 평타를 강화하고 그 타격의 실제 체력 피해 일부를 회복한다.
// 불필요한 범용 Settings 대신 이 스킬에 필요한 항목만 BP 기본값에 노출한다.
UCLASS(Blueprintable, HideCategories=(Skill))
class PROJECT_A_API UHeroStrikeSkill : public UUnitActiveSkill
{
    GENERATED_BODY()
public:
    UHeroStrikeSkill();

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hero Strike|Damage", meta=(ClampMin="0.01", ToolTip="강화 평타의 총 AD 계수. 2는 기본 평타를 포함한 AD 200%이며 별도로 AD 100%를 더하지 않는다."))
    float AttackDamageRatio = 2.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hero Strike|Damage", meta=(ClampMin="0.01"))
    float EvolvedAttackDamageRatio = 3.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hero Strike|Impact")
    TObjectPtr<USoundBase> EvolvedImpactSound = nullptr;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hero Strike|Healing", meta=(ClampMin="0", ClampMax="1", ToolTip="강화 평타가 적 체력에 실제로 준 피해 중 회복할 비율. 0.2는 20%. 보호막 피해와 초과 처치 피해는 제외한다."))
    float HealthDamageHealRatio = 0.2f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hero Strike|Cast", meta=(ClampMin="0.01", Units="s", ToolTip="마나 소모 후 평타 강화를 부여하기까지의 시전 시간. 기본 0.2초."))
    float CastSeconds = 0.2f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hero Strike|Cast", meta=(ToolTip="별도 시전 몽타주. 기본 None이며 타격에는 기존 평타 몽타주를 사용한다."))
    TObjectPtr<UAnimMontage> CastMontage = nullptr;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hero Strike|Cast", meta=(ToolTip="기본 false: 시전 시간 후 강화 부여. true면 시전 몽타주에서 CommitSkillEffect(ActionId)를 호출해야 한다."))
    bool bUseAnimNotify = false;

    virtual FUnitSkillDefinition BuildSettings() const override;
    virtual bool CanUse_Implementation(ABattleUnitBase* Target) const override;
    virtual void Execute_Implementation(ABattleUnitBase* Target, int32 ActionId) override;
    virtual void OnEmpoweredAttackHit_Implementation(const FUnitDamageRequest& Request, const FUnitDamageResult& Result) override;

    UFUNCTION(BlueprintImplementableEvent, Category="Hero Strike|Impact")
    void OnEvolvedImpact(ABattleUnitBase* Victim, const FUnitDamageResult& Result);

private:
    bool IsOwnerEvolved() const;
    // 강화 부여 시 확정한다. 다음 타격까지 설정/능력치가 바뀌어도 흡혈률은 유지한다.
    float ArmedHealRatio = 0.f;
    bool bArmedWhileEvolved = false;
};
