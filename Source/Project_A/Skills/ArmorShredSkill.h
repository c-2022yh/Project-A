#pragma once

#include "UnitSkill.h"
#include "ArmorShredSkill.generated.h"

// 파쇄병은 평타 피해가 끝난 뒤 실제 타격 대상의 방어력을 낮춘다.
UCLASS(Blueprintable, HideCategories=(Skill))
class PROJECT_A_API UArmorShredSkill : public UUnitPassiveSkill
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Armor Shred", meta=(ClampMin="0", ClampMax="1", ToolTip="방어력 감소 비율. 0.3이면 30% 감소."))
    float ArmorReduction = 0.3f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Armor Shred", meta=(ClampMin="0", Units="s"))
    float DurationSeconds = 4.f;

    virtual FUnitSkillDefinition BuildSettings() const override;
    virtual bool ShouldActivate_Implementation(EUnitTrigger Event, const FUnitDamageRequest& Context,
        const FUnitDamageResult& Result) override;
};
