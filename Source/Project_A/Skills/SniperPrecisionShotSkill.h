#pragma once

#include "UnitSkill.h"
#include "SniperPrecisionShotSkill.generated.h"

UCLASS(Blueprintable, HideCategories=(Skill))
class PROJECT_A_API USniperPrecisionShotSkill : public UUnitPassiveSkill
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Precision Shot", meta=(ClampMin="1"))
    int32 ShotInterval = 3;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Precision Shot", meta=(ClampMin="0"))
    float AttackDamageRatio = 2.f;

    virtual FUnitSkillDefinition BuildSettings() const override;
    virtual bool ShouldActivate_Implementation(EUnitTrigger Event, const FUnitDamageRequest& Context,
        const FUnitDamageResult& Result) override { return false; }
    virtual void ModifyBasicAttackRelease(float AttackDamage, float CriticalMultiplier, FUnitDamageRequest& Damage) const override;
    virtual void OnBasicAttackReleased() override;
};
