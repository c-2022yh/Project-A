#pragma once

#include "UnitSkill.h"
#include "ShamanWeakeningCurseSkill.generated.h"

// 적에게 공유 저주를 걸고, 평타 대상과 별도로 다음 시전 대상을 고른다.
UCLASS(Blueprintable, HideCategories=(Skill))
class PROJECT_A_API UShamanWeakeningCurseSkill : public UUnitActiveSkill
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Weakening Curse|Damage", meta=(ClampMin="0", ToolTip="AP 100 기준 1성 틱당 피해량."))
    float OneStarDamagePerTickAt100AP = 20.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Weakening Curse|Damage", meta=(ClampMin="0", ToolTip="AP 100 기준 2성 틱당 피해량."))
    float TwoStarDamagePerTickAt100AP = 36.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Weakening Curse|Damage", meta=(ClampMin="0", ClampMax="1"))
    float MagicResistanceReduction = 0.3f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Weakening Curse|Cast", meta=(ClampMin="1"))
    int32 CastRange = 3;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Weakening Curse|Cast", meta=(ClampMin="0.01", Units="s"))
    float CastSeconds = 0.01f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Weakening Curse|Cast")
    TObjectPtr<UAnimMontage> CastMontage = nullptr;

    virtual FUnitSkillDefinition BuildSettings() const override;
    virtual ABattleUnitBase* SelectTarget_Implementation() const override;
    virtual void Execute_Implementation(ABattleUnitBase* Target, int32 ActionId) override;
};
