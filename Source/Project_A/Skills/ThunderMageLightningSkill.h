#pragma once

#include "UnitSkill.h"
#include "ThunderMageLightningSkill.generated.h"

UCLASS(Blueprintable, HideCategories=(Skill))
class PROJECT_A_API UThunderMageLightningSkill : public UUnitActiveSkill
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Lightning|Damage", meta=(ClampMin="0"))
    float OneStarDamageAt100AP = 300.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Lightning|Damage", meta=(ClampMin="0"))
    float TwoStarDamageAt100AP = 540.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Lightning|Stun", meta=(ClampMin="0", Units="s"))
    float StunSeconds = 1.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Lightning|Cast", meta=(ClampMin="1"))
    int32 CastRange = 4;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Lightning|Cast", meta=(ClampMin="0.01", Units="s"))
    float CastSeconds = 0.01f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Lightning|Cast")
    TObjectPtr<UAnimMontage> CastMontage = nullptr;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Lightning|Cast")
    bool bUseAnimNotify = false;

    virtual FUnitSkillDefinition BuildSettings() const override;
    virtual void ApplyImpact_Implementation(const FUnitSkillDefinition& Config, ABattleUnitBase* Target,
        const TArray<float>& Values, const FUnitDamageRequest& Context) override;
};
