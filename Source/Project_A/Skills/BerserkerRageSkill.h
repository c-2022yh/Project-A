#pragma once

#include "UnitSkill.h"
#include "BerserkerRageSkill.generated.h"

// 광전사는 현재 잃은 체력 비율을 공격속도 보너스로 사용한다.
UCLASS(Blueprintable, HideCategories=(Skill))
class PROJECT_A_API UBerserkerRageSkill : public UUnitPassiveSkill
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Berserker Rage", meta=(ClampMin="0", ToolTip="잃은 체력 비율에 곱할 공속 계수. 0.8이면 체력 1% 손실당 공속 +0.8%."))
    float AttackSpeedPerMissingHealth = 0.8f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Berserker Rage", meta=(ClampMin="0", ToolTip="이 패시브가 부여할 공속 상한. 0.64이면 최대 +64%."))
    float MaxAttackSpeedBonus = 0.64f;

    virtual FUnitSkillDefinition BuildSettings() const override;
    virtual float GetHealthBasedAttackSpeedBonus(float HealthRatio) const override;
    virtual bool ShouldActivate_Implementation(EUnitTrigger Event, const FUnitDamageRequest& Context, const FUnitDamageResult& Result) override;
};
