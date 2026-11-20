#pragma once

#include "UnitSkill.h"
#include "BerserkerRageSkill.generated.h"

// 광전사는 체력을 잃으면 공속이 오르고, 평타로 준 체력 피해의 일부를 회복한다.
UCLASS(Blueprintable, HideCategories=(Skill))
class PROJECT_A_API UBerserkerRageSkill : public UUnitPassiveSkill
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Berserker Rage", meta=(ClampMin="0", ToolTip="잃은 체력 비율에 곱할 공속 계수. 0.8이면 체력 1% 손실당 공속 +0.8%."))
    float AttackSpeedPerMissingHealth = 0.8f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Berserker Rage", meta=(ClampMin="0", ToolTip="이 패시브가 부여할 공속 상한. 0.64이면 최대 +64%."))
    float MaxAttackSpeedBonus = 0.64f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Berserker Rage", meta=(ClampMin="0", ClampMax="1", ToolTip="평타가 실제로 깎은 체력 중 회복할 비율. 0.1이면 10%."))
    float BasicAttackLifestealRatio = 0.1f;

    virtual FUnitSkillDefinition BuildSettings() const override;
    virtual float GetHealthBasedAttackSpeedBonus(float HealthRatio) const override;
    virtual void OnBasicAttackLanded(const FUnitDamageResult& Result) override;
    virtual bool ShouldActivate_Implementation(EUnitTrigger Event, const FUnitDamageRequest& Context, const FUnitDamageResult& Result) override;
};
