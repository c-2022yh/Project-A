#pragma once

#include "UnitSkill.h"
#include "SpellbladeSkills.generated.h"

UCLASS(Blueprintable, HideCategories=(Skill))
class PROJECT_A_API USpellbladeDualWieldSkill : public UUnitPassiveSkill
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Dual Wield", meta=(ClampMin="0"))
    float PhysicalAttackDamageRatio = 0.5f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Dual Wield", meta=(ClampMin="0"))
    float MagicalAbilityPowerRatio = 0.35f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Dual Wield", meta=(ClampMin="0", ClampMax="1"))
    float FollowupTimeRatio = 0.8f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Dual Wield")
    bool bUseFollowupAnimNotify = false;

    virtual FUnitSkillDefinition BuildSettings() const override;
    virtual bool ShouldActivate_Implementation(EUnitTrigger Event, const FUnitDamageRequest& Context,
        const FUnitDamageResult& Result) override { return false; }
    virtual void ConfigureMeleeCombo(float AttackDamage, float AbilityPower, float& FirstRawDamage,
        float& FollowupRawDamage, float& OutFollowupTimeRatio, bool& bFollowupUsesNotify) const override;
};

UCLASS(Blueprintable, HideCategories=(Skill))
class PROJECT_A_API USpellbladeCrossSlashSkill : public UUnitActiveSkill
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Cross Slash", meta=(ClampMin="0"))
    float PhysicalAttackDamageRatio = 1.8f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Cross Slash", meta=(ClampMin="0"))
    float MagicalAbilityPowerRatio = 1.2f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Cross Slash|Cast", meta=(ClampMin="0.01", Units="s"))
    float CastSeconds = 0.01f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Cross Slash|Cast")
    TObjectPtr<UAnimMontage> CastMontage = nullptr;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Cross Slash|Cast")
    bool bUseAnimNotify = false;

    virtual FUnitSkillDefinition BuildSettings() const override;
};
