#pragma once

#include "UnitSkill.h"
#include "BlackfeatherArrowRainSkill.generated.h"

class ABoardTile;

UCLASS(Blueprintable, HideCategories=(Skill))
class PROJECT_A_API UBlackfeatherArrowRainSkill : public UUnitActiveSkill
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Arrow Rain|Damage", meta=(ClampMin="0"))
    float AttackDamageRatio = 1.8f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Arrow Rain|Damage", meta=(ClampMin="0", ClampMax="1"))
    float ArmorReduction = 0.3f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Arrow Rain|Damage", meta=(ClampMin="0", Units="s"))
    float ArmorReductionSeconds = 5.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Arrow Rain|Cast", meta=(ClampMin="0.01", Units="s"))
    float ImpactDelaySeconds = 0.5f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Arrow Rain|Cast", meta=(ClampMin="0.01", Units="s"))
    float CastSeconds = 0.01f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Arrow Rain|Cast")
    TObjectPtr<UAnimMontage> CastMontage = nullptr;

    UFUNCTION(BlueprintImplementableEvent, Category="Arrow Rain|Visual")
    void OnArrowRainLaunched(ABoardTile* Center, const TArray<ABoardTile*>& Tiles, float DelaySeconds);

    virtual bool RequiresLivingCastTarget() const override { return false; }
    virtual FUnitSkillDefinition BuildSettings() const override;
    virtual ABattleUnitBase* SelectTarget_Implementation() const override;
    virtual void Execute_Implementation(ABattleUnitBase* Target, int32 ActionId) override;
};
