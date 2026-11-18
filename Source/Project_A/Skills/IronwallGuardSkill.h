#pragma once

#include "UnitSkill.h"
#include "IronwallGuardSkill.generated.h"

UCLASS(Blueprintable, HideCategories=(Skill))
class PROJECT_A_API UIronwallGuardSkill : public UUnitActiveSkill
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ironwall Guard|Defense", meta=(ClampMin="0", ClampMax="1"))
    float DamageReduction = 0.4f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ironwall Guard|Defense", meta=(ClampMin="0", Units="s"))
    float DurationSeconds = 4.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ironwall Guard|Cast", meta=(ClampMin="0.01", Units="s"))
    float CastSeconds = 0.01f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ironwall Guard|Cast")
    TObjectPtr<UAnimMontage> CastMontage = nullptr;

    virtual FUnitSkillDefinition BuildSettings() const override;
};
