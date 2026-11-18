#pragma once

#include "UnitSkill.h"
#include "RaiderAmbushSkill.generated.h"

// 현재 체력이 가장 낮고 착지할 수 있는 적에게 급습해 물리 피해를 준다.
UCLASS(Blueprintable, HideCategories=(Skill))
class PROJECT_A_API URaiderAmbushSkill : public UUnitActiveSkill
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ambush|Damage", meta=(ClampMin="0"))
    float AttackDamageRatio = 2.5f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ambush|Cast", meta=(ClampMin="0.01", Units="s"))
    float CastSeconds = 0.01f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ambush|Cast")
    TObjectPtr<UAnimMontage> CastMontage = nullptr;

    virtual FUnitSkillDefinition BuildSettings() const override;
    virtual ABattleUnitBase* SelectTarget_Implementation() const override;
    virtual void Execute_Implementation(ABattleUnitBase* Target, int32 ActionId) override;
};
