#pragma once

#include "UnitSkill.h"
#include "ShamanWeakeningCurseSkill.generated.h"

// 시전마다 적 최대 두 명에게 공유 저주를 걸며, 기본 공격 대상은 유지한다.
UCLASS(Blueprintable, HideCategories=(Skill))
class PROJECT_A_API UShamanWeakeningCurseSkill : public UUnitActiveSkill
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Weakening Curse|Damage", meta=(ClampMin="0", ToolTip="AP 100 기준 1성 틱당 피해량."))
    float OneStarDamagePerTickAt100AP = 18.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Weakening Curse|Damage", meta=(ClampMin="0", ToolTip="AP 100 기준 2성 틱당 피해량."))
    float TwoStarDamagePerTickAt100AP = 32.4f;

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
    virtual bool RequiresLivingCastTarget() const override { return false; }
    virtual void Execute_Implementation(ABattleUnitBase* Target, int32 ActionId) override;

private:
    ABattleUnitBase* SelectCurseTarget(const ABattleUnitBase* Excluded = nullptr) const;
};
