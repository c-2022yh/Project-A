#pragma once

#include "ElementalForm.h"
#include "UnitSkill.h"
#include "ElementalistSkill.generated.h"

class ABoardTile;

UCLASS(Blueprintable, HideCategories=(Skill))
class PROJECT_A_API UElementalistSkill : public UUnitActiveSkill
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elementalist")
    EElementalForm DefaultForm = EElementalForm::Frost;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elementalist|Frost", meta=(ClampMin="1"))
    float FrostMaxMana = 45.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elementalist|Frost", meta=(ClampMin="0"))
    float FrostOneStarAPRatio = 3.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elementalist|Frost", meta=(ClampMin="0"))
    float FrostTwoStarAPRatio = 3.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elementalist|Frost", meta=(ClampMin="0", ClampMax="1"))
    float FrostAttackSpeedSlow = 0.25f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elementalist|Frost", meta=(ClampMin="0", Units="s"))
    float FrostSlowSeconds = 3.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elementalist|Frost", meta=(ClampMin="1"))
    int32 FrostCastRange = 4;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elementalist|Frost", meta=(ClampMin="0.01", Units="s"))
    float FrostCastSeconds = 0.01f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elementalist|Frost", meta=(ClampMin="1"))
    float FrostProjectileSpeed = 2000.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elementalist|Frost")
    FUnitProjectileVisualDefinition FrostProjectileVisual;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elementalist|Frost")
    TObjectPtr<UAnimMontage> FrostCastMontage = nullptr;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elementalist|Fire", meta=(ClampMin="1"))
    float FireMaxMana = 100.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elementalist|Fire", meta=(ClampMin="0"))
    float FireOneStarAPRatio = 4.5f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elementalist|Fire", meta=(ClampMin="0"))
    float FireTwoStarAPRatio = 4.5f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elementalist|Fire", meta=(ClampMin="0.01", Units="s"))
    float MeteorDelay = 1.5f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elementalist|Fire", meta=(ClampMin="0", ClampMax="1"))
    float MeteorInnerScale = 0.65f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elementalist|Fire", meta=(ClampMin="0", ClampMax="1"))
    float MeteorOuterScale = 0.4f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elementalist|Fire", meta=(ClampMin="0.01", Units="s"))
    float FireCastSeconds = 0.3f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elementalist|Fire")
    TObjectPtr<UAnimMontage> FireCastMontage = nullptr;

    UFUNCTION(BlueprintPure, Category="Elementalist")
    EElementalForm GetForm() const;
    UFUNCTION(BlueprintCallable, Category="Elementalist")
    bool SetForm(EElementalForm Form);
    UFUNCTION(BlueprintImplementableEvent, Category="Elementalist")
    void OnFormChanged(EElementalForm Form);
    UFUNCTION(BlueprintImplementableEvent, Category="Elementalist")
    void OnMeteorTelegraph(ABoardTile* Center, const TArray<ABoardTile*>& Tiles, float DelaySeconds);

    virtual FUnitSkillDefinition BuildSettings() const override;
    virtual void ModifyMaxMana(float& MaxMana) const override;
    virtual void CustomizeEffectValues(TArray<float>& Values) const override;
    virtual ABattleUnitBase* SelectTarget_Implementation() const override;
    virtual void PrepareCast(ABattleUnitBase* Target, int32 ActionId) override;
    virtual void BeginCast(int32 ActionId) override;
    virtual bool RequiresLivingCastTarget() const override;
    virtual void Execute_Implementation(ABattleUnitBase* Target, int32 ActionId) override;

private:
    EElementalForm SelectedForm = EElementalForm::Unselected;
    TWeakObjectPtr<ABoardTile> MeteorCenter;
};
