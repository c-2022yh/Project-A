#pragma once

#include "UnitSkill.h"
#include "DirectionalMeleeSkills.generated.h"

class ABoardTile;

// 시전 시작에 앞칸과 추가 타일을 고정하고 적중 순간의 점유자를 공격한다.
UCLASS(Abstract, Blueprintable, HideCategories=(Skill))
class PROJECT_A_API UDirectionalMeleeSkill : public UUnitActiveSkill
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Directional Strike|Damage", meta=(ClampMin="0"))
    float PrimaryAttackDamageRatio = 0.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Directional Strike|Damage", meta=(ClampMin="0"))
    float SecondaryAttackDamageRatio = 0.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Directional Strike|Cast", meta=(ClampMin="0.01", Units="s"))
    float CastSeconds = 0.01f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Directional Strike|Cast")
    TObjectPtr<UAnimMontage> CastMontage = nullptr;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Directional Strike|Cast")
    bool bUseAnimNotify = false;

    virtual bool RequiresLivingCastTarget() const override { return false; }
    virtual FUnitSkillDefinition BuildSettings() const override;
    virtual bool CanUse_Implementation(ABattleUnitBase* Target) const override;
    virtual void PrepareCast(ABattleUnitBase* Target, int32 ActionId) override;
    virtual void ApplyImpact_Implementation(const FUnitSkillDefinition& Config, ABattleUnitBase* Target,
        const TArray<float>& Values, const FUnitDamageRequest& Context) override;

protected:
    virtual TArray<FIntPoint> GetSecondaryOffsets(FIntPoint Direction) const { return {}; }
    virtual void ApplyStrikeHit(ABattleUnitBase* Target, const FUnitDamageRequest& Damage);

private:
    TArray<TWeakObjectPtr<ABoardTile>> StrikeTiles;
    int32 StrikeActionId = 0;
};

UCLASS(Blueprintable)
class PROJECT_A_API UBarbarianGroundSlamSkill : public UDirectionalMeleeSkill
{
    GENERATED_BODY()

public:
    UBarbarianGroundSlamSkill();

protected:
    virtual TArray<FIntPoint> GetSecondaryOffsets(FIntPoint Direction) const override;
};

UCLASS(Blueprintable)
class PROJECT_A_API USpearmanPiercingThrustSkill : public UDirectionalMeleeSkill
{
    GENERATED_BODY()

public:
    USpearmanPiercingThrustSkill();

protected:
    virtual TArray<FIntPoint> GetSecondaryOffsets(FIntPoint Direction) const override;
};

UCLASS(Blueprintable)
class PROJECT_A_API UDarkKnightJudgmentSkill : public UBarbarianGroundSlamSkill
{
    GENERATED_BODY()

public:
    UDarkKnightJudgmentSkill();

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Judgment|Stun", meta=(ClampMin="0", Units="s"))
    float StunSeconds = 1.5f;

protected:
    virtual void ApplyStrikeHit(ABattleUnitBase* Target, const FUnitDamageRequest& Damage) override;
};
