#pragma once

#include "UnitSkill.h"
#include "PriestHealingLightSkill.generated.h"

// 사제의 치유의 빛. 회복 대상과 마나 처리는 공통 시전 규칙을 사용한다.
UCLASS(Blueprintable, HideCategories=(Skill))
class PROJECT_A_API UPriestHealingLightSkill : public UUnitActiveSkill
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Healing Light|Healing", meta=(ClampMin="0", ToolTip="AP 100 기준 1성 회복량."))
    float OneStarHealingAt100AP = 300.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Healing Light|Healing", meta=(ClampMin="0", ToolTip="AP 100 기준 2성 회복량."))
    float TwoStarHealingAt100AP = 540.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Healing Light|Cast", meta=(ClampMin="0.01", Units="s", ToolTip="시전 시간 테스트 기본값은 0.01초다. 회복은 시전이 완료될 때 한 번 적용된다."))
    float CastSeconds = 0.01f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Healing Light|Cast")
    TObjectPtr<UAnimMontage> CastMontage = nullptr;

    virtual FUnitSkillDefinition BuildSettings() const override;
    virtual void Execute_Implementation(ABattleUnitBase* Target, int32 ActionId) override;
};
