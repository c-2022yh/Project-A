#pragma once

#include "UnitSkill.h"
#include "PaladinGuardianOathSkill.generated.h"

UCLASS(Blueprintable, HideCategories=(Skill))
class PROJECT_A_API UPaladinGuardianOathSkill : public UUnitActiveSkill
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Guardian Oath|Shield", meta=(ClampMin="0"))
    float OneStarShieldAt100AP = 220.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Guardian Oath|Shield", meta=(ClampMin="0"))
    float TwoStarShieldAt100AP = 396.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Guardian Oath|Shield", meta=(ClampMin="0.01", Units="s"))
    float ShieldDuration = 4.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Guardian Oath|Targeting", meta=(ClampMin="1"))
    int32 AllyRange = 2;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Guardian Oath|Cast", meta=(ClampMin="0.01", Units="s"))
    float CastSeconds = 0.3f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Guardian Oath|Cast")
    TObjectPtr<UAnimMontage> CastMontage = nullptr;

    UFUNCTION(BlueprintImplementableEvent, Category="Guardian Oath|Visual")
    void OnProtectionApplied(ABattleUnitBase* Recipient);

    virtual FUnitSkillDefinition BuildSettings() const override;
    virtual void Execute_Implementation(ABattleUnitBase* Target, int32 ActionId) override;
};
