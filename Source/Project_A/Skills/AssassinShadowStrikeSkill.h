#pragma once

#include "RaiderAmbushSkill.h"
#include "AssassinShadowStrikeSkill.generated.h"

UCLASS(Blueprintable)
class PROJECT_A_API UAssassinShadowStrikeSkill : public URaiderAmbushSkill
{
    GENERATED_BODY()

public:
    UAssassinShadowStrikeSkill();

    virtual bool RequiresLivingCastTarget() const override { return false; }
    virtual FUnitSkillDefinition BuildSettings() const override;
    virtual ABattleUnitBase* SelectTarget_Implementation() const override;
    virtual void PrepareCast(ABattleUnitBase* Target, int32 ActionId) override;
    virtual void Execute_Implementation(ABattleUnitBase* Target, int32 ActionId) override;
};
