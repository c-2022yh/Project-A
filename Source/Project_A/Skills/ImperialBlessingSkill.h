#pragma once

#include "UnitSkill.h"
#include "ImperialBlessingSkill.generated.h"

UCLASS(Blueprintable, HideCategories=(Skill))
class PROJECT_A_API UImperialBlessingSkill : public UUnitActiveSkill
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Blessing|Healing", meta=(ClampMin="0"))
    float OneStarHealingPerTickAt100AP = 25.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Blessing|Healing", meta=(ClampMin="0"))
    float TwoStarHealingPerTickAt100AP = 45.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Blessing|Buff", meta=(ClampMin="0"))
    float AttackDamageBonus = 0.1f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Blessing|Buff", meta=(ClampMin="0"))
    float AbilityPowerBonus = 0.1f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Blessing|Buff", meta=(ClampMin="1", ClampMax="60", Units="s"))
    int32 DurationSeconds = 4;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Blessing|Cast", meta=(ClampMin="0.01", Units="s"))
    float CastSeconds = 0.01f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Blessing|Cast")
    TObjectPtr<UAnimMontage> CastMontage = nullptr;

    virtual FUnitSkillDefinition BuildSettings() const override;
    virtual void Execute_Implementation(ABattleUnitBase* Target, int32 ActionId) override;
};
